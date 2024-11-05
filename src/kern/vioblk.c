//           vioblk.c - VirtIO serial port (console)
//          
//1. Do we need to set all the struct in attach? 
//2. How to use descriptor? Do we need to add one queue of indirect descriptor?
//Or do the 1st need to be linked with 234?
//3. How to process requests? setting which part of header/descriptor?
//4. How to overwrite? Just set vq.avail.ring[0]?
//5. How to test?
#include "virtio.h"
#include "intr.h"
#include "halt.h"
#include "heap.h"
#include "io.h"
#include "device.h"
#include "error.h"
#include "string.h"
#include "thread.h"
//           COMPILE-TIME PARAMETERS
//          

#define VIOBLK_IRQ_PRIO 1

//           INTERNAL CONSTANT DEFINITIONS
//          

//           VirtIO block device feature bits (number, *not* mask)

#define VIRTIO_BLK_F_SIZE_MAX       1
#define VIRTIO_BLK_F_SEG_MAX        2
#define VIRTIO_BLK_F_GEOMETRY       4
#define VIRTIO_BLK_F_RO             5
#define VIRTIO_BLK_F_BLK_SIZE       6
#define VIRTIO_BLK_F_FLUSH          9
#define VIRTIO_BLK_F_TOPOLOGY       10
#define VIRTIO_BLK_F_CONFIG_WCE     11
#define VIRTIO_BLK_F_MQ             12
#define VIRTIO_BLK_F_DISCARD        13
#define VIRTIO_BLK_F_WRITE_ZEROES   14

//           INTERNAL TYPE DEFINITIONS
//          

//           All VirtIO block device requests consist of a request header, defined below,
//           followed by data, followed by a status byte. The header is device-read-only,
//           the data may be device-read-only or device-written (depending on request
//           type), and the status byte is device-written.

struct vioblk_request_header {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
};

//           Request type (for vioblk_request_header)

#define VIRTIO_BLK_T_IN             0
#define VIRTIO_BLK_T_OUT            1

//           Status byte values

#define VIRTIO_BLK_S_OK         0
#define VIRTIO_BLK_S_IOERR      1
#define VIRTIO_BLK_S_UNSUPP     2

//           Main device structure.
//          
//           FIXME You may modify this structure in any way you want. It is given as a
//           hint to help you, but you may have your own (better!) way of doing things.
#define VIRTIO_BLK_QID 0
struct vioblk_device {
    volatile struct virtio_mmio_regs * regs;
    struct io_intf io_intf;
    uint16_t instno;
    uint16_t irqno;
    int8_t opened;
    int8_t readonly;

    //           optimal block size
    uint32_t blksz;
    //           current position
    uint64_t pos;
    //           sizeo of device in bytes
    uint64_t size;
    //           size of device in blksz blocks
    uint64_t blkcnt;

    struct {
        //           signaled from ISR
        struct condition used_updated;

        //           We use a simple scheme of one transaction at a time.

        union {
            struct virtq_avail avail;
            char _avail_filler[VIRTQ_AVAIL_SIZE(1)];
        };

        union {
            volatile struct virtq_used used;
            char _used_filler[VIRTQ_USED_SIZE(1)];
        };

        //           The first descriptor is an indirect descriptor and is the one used in
        //           the avail and used rings. The second descriptor points to the header,
        //           the third points to the data, and the fourth to the status byte.

        struct virtq_desc desc[4];
        struct vioblk_request_header req_header;
        uint8_t req_status;
    } vq;

    //           Block currently in block buffer
    uint64_t bufblkno;
    //           Block buffer
    char * blkbuf;
};

//           INTERNAL FUNCTION DECLARATIONS
//          

static int vioblk_open(struct io_intf ** ioptr, void * aux);

static void vioblk_close(struct io_intf * io);

static long vioblk_read (
    struct io_intf * restrict io,
    void * restrict buf,
    unsigned long bufsz);

static long vioblk_write (
    struct io_intf * restrict io,
    const void * restrict buf,
    unsigned long n);

static int vioblk_ioctl (
    struct io_intf * restrict io, int cmd, void * restrict arg);

static void vioblk_isr(int irqno, void * aux);

//           IOCTLs

static int vioblk_getlen(const struct vioblk_device * dev, uint64_t * lenptr);
static int vioblk_getpos(const struct vioblk_device * dev, uint64_t * posptr);
static int vioblk_setpos(struct vioblk_device * dev, const uint64_t * posptr);
static int vioblk_getblksz (
    const struct vioblk_device * dev, uint32_t * blkszptr);

//           EXPORTED FUNCTION DEFINITIONS
//          

//           Attaches a VirtIO block device. Declared and called directly from virtio.c.
//
void vioblk_attach(volatile struct virtio_mmio_regs * regs, int irqno) {
    //           FIXME add additional declarations here if needed
    static const struct io_ops vioblk_ops={
        .close=vioblk_close,
        .read=vioblk_read,
        .write=vioblk_write,
        .ctl=vioblk_ioctl
    };

    virtio_featset_t enabled_features, wanted_features, needed_features;
    struct vioblk_device * dev;
    uint_fast32_t blksz;
    int result;

    assert (regs->device_id == VIRTIO_ID_BLOCK);

    //           Signal device that we found a driver

    regs->status |= VIRTIO_STAT_DRIVER;
    //           fence o,io
    __sync_synchronize();

    //           Negotiate features. We need:
    //            - VIRTIO_F_RING_RESET and
    //            - VIRTIO_F_INDIRECT_DESC
    //           We want:
    //            - VIRTIO_BLK_F_BLK_SIZE and
    //            - VIRTIO_BLK_F_TOPOLOGY.

    virtio_featset_init(needed_features);
    virtio_featset_add(needed_features, VIRTIO_F_RING_RESET);
    virtio_featset_add(needed_features, VIRTIO_F_INDIRECT_DESC);
    virtio_featset_init(wanted_features);
    virtio_featset_add(wanted_features, VIRTIO_BLK_F_BLK_SIZE);
    virtio_featset_add(wanted_features, VIRTIO_BLK_F_TOPOLOGY);
    result = virtio_negotiate_features(regs,
        enabled_features, wanted_features, needed_features);
    if (result != 0) {
        kprintf("%p: virtio feature negotiation failed\n", regs);
        return;
    }

    //           If the device provides a block size, use it. Otherwise, use 512.

    if (virtio_featset_test(enabled_features, VIRTIO_BLK_F_BLK_SIZE))
        blksz = regs->config.blk.blk_size;
    else
        blksz = 512;

    debug("%p: virtio block device block size is %lu", regs, (long)blksz);

    //           Allocate initialize device struct

    dev = kmalloc(sizeof(struct vioblk_device) + blksz);
    memset(dev, 0, sizeof(struct vioblk_device));

    //           FIXME Finish initialization of vioblk device here
    dev->regs=regs;
    //Do not need to use instno and readonly
    dev->io_intf.ops=&vioblk_ops;
    dev->irqno=irqno;
    dev->opened=0;
    dev->blksz=blksz;
    dev->pos=0;
    //initialize size
    dev->size=dev->regs->config.blk.capacity * blksz;
    dev->blkcnt=dev->regs->config.blk.capacity;
    condition_init(&dev->vq.used_updated,"usedupdated");
    dev->bufblkno=UINT64_MAX;
    //Fill in descriptors
    uint64_t descaddr=(uintptr_t)&dev->vq.desc[0];
    uint64_t usedaddr=(uintptr_t)&dev->vq.used;
    uint64_t availaddr=(uintptr_t)&dev->vq.avail;
    virtio_attach_virtq(regs,VIRTIO_BLK_QID,4,descaddr,usedaddr,availaddr);
    
    dev->vq.desc[0].len=1;
    dev->vq.desc[0].flags=VIRTIO_F_INDIRECT_DESC;
    dev->vq.desc[0].next=0;

    dev->vq.desc[1].addr=(uintptr_t)&dev->vq.req_header;
    dev->vq.desc[1].flags=VIRTQ_DESC_F_NEXT;
    dev->vq.desc[1].len=sizeof(struct vioblk_request_header);
    dev->vq.desc[1].next=2;
    
    dev->vq.desc[2].addr=(uintptr_t)dev->blkbuf;
    dev->vq.desc[2].flags=VIRTQ_DESC_F_NEXT;
    dev->vq.desc[2].len=dev->blksz;
    dev->vq.desc[2].next=3;

    dev->vq.desc[3].addr=(uintptr_t)&dev->vq.req_status;
    dev->vq.desc[3].flags=VIRTQ_DESC_F_NEXT;
    dev->vq.desc[3].len=8;
    dev->vq.desc[3].next=0;

    intr_register_isr(irqno,VIOBLK_IRQ_PRIO,vioblk_isr,dev);
    dev->instno=device_register("blk",&vioblk_open,dev);
    dev->vq.avail.idx=0;
    dev->vq.used.idx=0;
    regs->status |= VIRTIO_STAT_DRIVER_OK;
    //           fence o,oi
    __sync_synchronize();
}

int vioblk_open(struct io_intf ** ioptr, void * aux) {
    //           FIXME your code here
    struct vioblk_device* dev=aux;
    if(dev->opened)return -EBUSY;
    virtio_enable_virtq(dev->regs,VIRTIO_BLK_QID);

    intr_enable_irq(dev->irqno);
    *ioptr=&dev->io_intf;
    dev->opened=1;
    return 0;
}

//           Must be called with interrupts enabled to ensure there are no pending
//           interrupts (ISR will not execute after closing).

void vioblk_close(struct io_intf * io) {
    // FIXME your code here
    struct vioblk_device* const dev =(void*)io - offsetof(struct vioblk_device, io_intf);
    if(dev->opened==0)return;
    virtio_reset_virtq(dev->regs,VIRTIO_BLK_QID);
    intr_disable_irq(dev->irqno);
    dev->opened=0;
}

long vioblk_read (
    struct io_intf * restrict io,
    void * restrict buf,
    unsigned long bufsz)
{
    //           FIXME your code here
    struct vioblk_device *dev=(struct vioblk_device *)((char *)io - offsetof(struct vioblk_device, io_intf));
    unsigned long n=0;
    unsigned long blksec=0;
    unsigned long offset=0;
    dev->vq.avail.idx=0;
    while(n<bufsz){
        long bytes_copy=bufsz-n>dev->blksz? dev->blksz : bufsz-n;
        dev->vq.req_header.type=VIRTIO_BLK_T_IN;
        dev->vq.req_header.sector=blksec;
        virtio_notify_avail(dev->regs,VIRTIO_BLK_QID);
        dev->vq.avail.ring[dev->vq.avail.idx]=0;
        condition_wait(&dev->vq.used_updated);
        if(dev->vq.req_status!=VIRTIO_BLK_S_OK)return -EIO;
        memcpy(buf+offset,dev->blkbuf,bytes_copy);
        offset+=dev->blksz;
        blksec+=dev->blksz / 512;
    }
    return bufsz;
}

long vioblk_write (
    struct io_intf * restrict io,
    const void * restrict buf,
    unsigned long n)
{
    //           FIXME your code here
    struct vioblk_device *dev=(struct vioblk_device *)((char *)io - offsetof(struct vioblk_device, io_intf));
    unsigned long bw=0;
    unsigned long blksec=0;
    unsigned long offset=0;
    dev->vq.avail.idx=0;
    while(bw<n){
        long bytes_copy=n-bw>dev->blksz? dev->blksz : n-bw;
        dev->vq.req_header.type=VIRTIO_BLK_T_OUT;
        dev->vq.req_header.sector=blksec;
        virtio_notify_avail(dev->regs,VIRTIO_BLK_QID);
        dev->vq.avail.ring[dev->vq.avail.idx]=0;
        condition_wait(&dev->vq.used_updated);
        if(dev->vq.req_status!=VIRTIO_BLK_S_OK)return -EIO;
        memcpy(dev->blkbuf,buf+offset,bytes_copy);
        offset+=dev->blksz;
        blksec+=dev->blksz / 512;
    }
    return n;
}

int vioblk_ioctl(struct io_intf * restrict io, int cmd, void * restrict arg) {
    struct vioblk_device * const dev = (void*)io -
        offsetof(struct vioblk_device, io_intf);
    
    trace("%s(cmd=%d,arg=%p)", __func__, cmd, arg);
    
    switch (cmd) {
    case IOCTL_GETLEN:
        return vioblk_getlen(dev, arg);
    case IOCTL_GETPOS:
        return vioblk_getpos(dev, arg);
    case IOCTL_SETPOS:
        return vioblk_setpos(dev, arg);
    case IOCTL_GETBLKSZ:
        return vioblk_getblksz(dev, arg);
    default:
        return -ENOTSUP;
    }
}

void vioblk_isr(int irqno, void * aux) {
    //           FIXME your code here
    struct vioblk_device* const dev=aux;
    if(irqno!=dev->irqno)return;
    const uint32_t status=dev->regs->interrupt_status;
    if(status==0){
        condition_broadcast(&dev->vq.used_updated);
        dev->regs->interrupt_ack=0;
    }
}

int vioblk_getlen(const struct vioblk_device * dev, uint64_t * lenptr) {
    //           FIXME your code here
    *lenptr=dev->regs->queue_num;
    return 0;
}

int vioblk_getpos(const struct vioblk_device * dev, uint64_t * posptr) {
    //           FIXME your code here
    *posptr=dev->pos;
    return 0;
}

int vioblk_setpos(struct vioblk_device * dev, const uint64_t * posptr) {
    //           FIXME your code here
    dev->pos=*posptr;
    return 0;
}

int vioblk_getblksz (
    const struct vioblk_device * dev, uint32_t * blkszptr)
{
    //           FIXME your code here
    *blkszptr=dev->blksz;
    return 0;
}
