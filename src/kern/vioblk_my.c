//           vioblk.c - VirtIO serial port (console)
//          

#include "virtio.h"
#include "intr.h"
#include "halt.h"
#include "heap.h"
#include "io.h"
#include "device.h"
#include "error.h"
#include "string.h"
#include "thread.h"

//           COMPILE-TIME PARAMETERS
//          

#define VIOBLK_IRQ_PRIO 1

//           INTERNAL CONSTANT DEFINITIONS
//          

//           VirtIO block device feature bits (number, *not* mask)

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

//           INTERNAL TYPE DEFINITIONS
//          

//           All VirtIO block device requests consist of a request header, defined below,
//           followed by data, followed by a status byte. The header is device-read-only,
//           the data may be device-read-only or device-written (depending on request
//           type), and the status byte is device-written.

struct vioblk_request_header {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
};

//           Request type (for vioblk_request_header)

#define VIRTIO_BLK_T_IN             0
#define VIRTIO_BLK_T_OUT            1

//           Status byte values

#define VIRTIO_BLK_S_OK         0
#define VIRTIO_BLK_S_IOERR      1
#define VIRTIO_BLK_S_UNSUPP     2

//           Main device structure.
//          
//           FIXME You may modify this structure in any way you want. It is given as a
//           hint to help you, but you may have your own (better!) way of doing things.

struct vioblk_device {
    volatile struct virtio_mmio_regs * regs;
    struct io_intf io_intf;
    uint16_t instno;
    uint16_t irqno;
    int8_t opened;
    int8_t readonly;

    //           optimal block size
    uint32_t blksz;
    //           current position
    uint64_t pos;
    //           sizeo of device in bytes
    uint64_t size;
    //           size of device in blksz blocks
    uint64_t blkcnt;

    struct {
        //           signaled from ISR
        struct condition used_updated;

        //           We use a simple scheme of one transaction at a time.

        union {
            struct virtq_avail avail;
            char _avail_filler[VIRTQ_AVAIL_SIZE(1)];           //available size = 1
        };

        union {
            volatile struct virtq_used used;
            char _used_filler[VIRTQ_USED_SIZE(1)];
        };

        //           The first descriptor is an indirect descriptor and is the one used in
        //           the avail and used rings. The second descriptor points to the header,
        //           the third points to the data, and the fourth to the status byte.

        struct virtq_desc desc[4];
        //struct virtq_desc indirect[4];   //my 
        struct vioblk_request_header req_header;
        uint8_t req_status;
    } vq;

    //           Block currently in block buffer
    uint64_t bufblkno;
    //           Block buffer
    char * blkbuf;
};

//           INTERNAL FUNCTION DECLARATIONS
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

//           IOCTLs

static int vioblk_getlen(const struct vioblk_device * dev, uint64_t * lenptr);
static int vioblk_getpos(const struct vioblk_device * dev, uint64_t * posptr);
static int vioblk_setpos(struct vioblk_device * dev, const uint64_t * posptr);
static int vioblk_getblksz (
    const struct vioblk_device * dev, uint32_t * blkszptr);

//           EXPORTED FUNCTION DEFINITIONS
//          

//           Attaches a VirtIO block device. Declared and called directly from virtio.c.

void vioblk_attach(volatile struct virtio_mmio_regs * regs, int irqno) {
    //           FIXME add additional declarations here if needed
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
    char name[]  = "blk";
    //struct virtq_desc descriptor;
    //struct virtq_avail availRing;
    //struct virtq_used  usedRing;

    assert (regs->device_id == VIRTIO_ID_BLOCK);

    //           Signal device that we found a driver

    regs->status |= VIRTIO_STAT_DRIVER;
    //           fence o,io
    __sync_synchronize();

    //           Negotiate features. We need:
    //            - VIRTIO_F_RING_RESET and
    //            - VIRTIO_F_INDIRECT_DESC
    //           We want:
    //            - VIRTIO_BLK_F_BLK_SIZE and
    //            - VIRTIO_BLK_F_TOPOLOGY.

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

    //           If the device provides a block size, use it. Otherwise, use 512.

    if (virtio_featset_test(enabled_features, VIRTIO_BLK_F_BLK_SIZE))
        blksz = regs->config.blk.blk_size;
    else
        blksz = 512;

    debug("%p: virtio block device block size is %lu", regs, (long)blksz);

    //           Allocate initialize device struct

    dev = kmalloc(sizeof(struct vioblk_device) + blksz);
    memset(dev, 0, sizeof(struct vioblk_device));

    //           FIXME Finish initialization of vioblk device here
    //set up every queue
    dev->regs = regs;
    dev->opened = 0;
    dev->irqno = irqno;
    dev->blksz = blksz;
    dev->size = dev->regs->config.blk.capacity * blksz;
    dev->blkcnt = dev->regs->config.blk.capacity;
    // Initialize Descriptor 0: Indirect descriptor
    /*dev->vq.desc[0].addr = (uint64_t)&dev->vq.indirect[0]; // Address of the first descriptor in the indirect table
    dev->vq.desc[0].len = sizeof(dev->vq.indirect[0]) * 4; // Number of bytes to cover the next three descriptors
    dev->vq.desc[0].flags = VIRTQ_DESC_F_INDIRECT; // Set the INDIRECT flag
    dev->vq.desc[0].next = 0; // Not linking to another descriptor
    */
    //another mode 
    dev->vq.desc[0].addr = (uint64_t)&dev->vq.desc[0]; // Address of the first descriptor in the indirect table
    dev->vq.desc[0].len = sizeof(dev->vq.desc[0]) * 3; // Number of bytes to cover the next three descriptors
    dev->vq.desc[0].flags = VIRTQ_DESC_F_INDIRECT; // Set the INDIRECT flag
    dev->vq.desc[0].next = 0; // Not linking to another descriptor 

    // Initialize Descriptor 1: Request header
    dev->vq.desc[1].addr = (uint64_t)&dev->vq.req_header; // Address of the request header
    dev->vq.desc[1].len = sizeof(dev->vq.req_header); // Length of the request header
    dev->vq.desc[1].flags = VIRTQ_DESC_F_NEXT; // This descriptor points to the next one
    dev->vq.desc[1].next = 2; // Next descriptor is index 2

    // Initialize Descriptor 2: Data buffer
    dev->vq.desc[2].addr = (uint64_t)dev->blkbuf; // Address of the block buffer
    dev->vq.desc[2].len = dev->blksz; // Length of the block size (or adjusted data length)
    dev->vq.desc[2].flags = VIRTQ_DESC_F_NEXT; // This descriptor points to the next one
    dev->vq.desc[2].next = 3; // Next descriptor is index 3

    // Initialize Descriptor 3: Status byte
    dev->vq.desc[3].addr = (uint64_t)&dev->vq.req_status; // Address of the status byte
    dev->vq.desc[3].len = sizeof(dev->vq.req_status); // Length is 1 byte
    dev->vq.desc[3].flags = VIRTQ_DESC_F_WRITE; // This is a write descriptor for the status
    dev->vq.desc[3].next = 0; // No next descriptor, this is the last in the chain

    regs->queue_sel = 0;
    regs->queue_ready = 0;
    //           fence o,o
    __sync_synchronize();
    // regs->queue_desc = 1;
    // regs->queue_device = 2;
    // regs->queue_driver = 3;
    // regs->queue_num = 4;

    virtio_attach_virtq(regs, 0, 4, (uint64_t)&dev->vq.desc, (uint64_t)&dev->vq.used, (uint64_t)&dev->vq.avail);  //set the queue_num should be 4 at this point (4 descriptor for a single request) 


    intr_register_isr(irqno,  VIOBLK_IRQ_PRIO, &vioblk_isr, dev);    //register the isr        

    dev->instno =  device_register(name, &vioblk_open, dev);   //register the device in kern
    condition_init(&dev->vq.used_updated, "used");

    //initialize the io_intf


    dev->io_intf.ops = &vioblk_ops;

    regs->status |= VIRTIO_STAT_DRIVER_OK;    
    //           fence o,oi
    __sync_synchronize();
}

int vioblk_open(struct io_intf ** ioptr, void * aux) {
    //           FIXME your code here
    struct vioblk_device *dev = (struct vioblk_device *)aux;
    if (dev->opened) return -EBUSY;
    virtio_enable_virtq(dev->regs, 0);
    intr_enable_irq(dev->irqno);                    //enable intrrupt

    *ioptr = &dev->io_intf;                         //set the interface in the deive struct

    dev->opened = 1;                                 //set the device to open state

    return 0;                        //return 0 on success
}

//           Must be called with interrupts enabled to ensure there are no pending
//           interrupts (ISR will not execute after closing).

void vioblk_close(struct io_intf * io) {
    //           FIXME your code here
    struct vioblk_device * dev = (void*)io -
        offsetof(struct vioblk_device, io_intf);
    if (!dev->opened) return;                        //test if it is close
    virtio_reset_virtq(dev->regs, 0);
    intr_disable_irq(dev->irqno);                  //disable intrrupt
    dev->opened = 0;                            //set open state to 0 -> the device is not open
}

long vioblk_read(struct io_intf *restrict io, void *restrict buf, unsigned long bufsz) {
    struct vioblk_device *dev = (void *)io - offsetof(struct vioblk_device, io_intf);
    unsigned long bytes_read = 0;


    while (bytes_read < bufsz) {
        if (dev->pos >= dev->size) {
            break; // End of device
        }
        uint16_t desc_idx = 1;
        // Calculate the size to read
        unsigned long copy_size = (bufsz - bytes_read < dev->blksz) ? (bufsz - bytes_read) : dev->blksz;

        // Prepare the request header
        dev->vq.req_header.type = VIRTIO_BLK_T_IN;
        dev->vq.req_header.reserved = 0;
        dev->vq.req_header.sector = dev->pos / dev->blksz;

        // Set up descriptors
        // Descriptor 0: Request header
        dev->vq.desc[desc_idx].addr = (uint64_t)&dev->vq.req_header;
        dev->vq.desc[desc_idx].len = sizeof(dev->vq.req_header);
        dev->vq.desc[desc_idx].flags = VIRTQ_DESC_F_NEXT;
        dev->vq.desc[desc_idx].next = desc_idx + 1;

        // Descriptor 1: Data buffer
        dev->vq.desc[desc_idx + 1].addr = (uint64_t)dev->blkbuf;
        dev->vq.desc[desc_idx + 1].len = copy_size;
        dev->vq.desc[desc_idx + 1].flags = VIRTQ_DESC_F_NEXT | VIRTQ_DESC_F_WRITE;
        dev->vq.desc[desc_idx + 1].next = desc_idx + 2;

        // Descriptor 2: Status byte
        dev->vq.desc[desc_idx + 2].addr = (uint64_t)&dev->vq.req_status;
        dev->vq.desc[desc_idx + 2].len = sizeof(dev->vq.req_status);
        dev->vq.desc[desc_idx + 2].flags = VIRTQ_DESC_F_WRITE;
        dev->vq.desc[desc_idx + 2].next = 0;

        // Add to available ring
        uint16_t avail_idx = 0;          //only the first position 
        dev->vq.avail.ring[avail_idx] = desc_idx;
        __sync_synchronize();
        dev->vq.avail.idx++;

        // Notify the device
        virtio_notify_avail(dev->regs, 0);

        // Wait for the device to process the request
        condition_wait(&dev->vq.used_updated);

        // Check the status
        if (dev->vq.req_status != VIRTIO_BLK_S_OK) {
            return -EIO;
        }

        // Copy data to user buffer
        memcpy((uint8_t *)buf + bytes_read, &dev->blkbuf, copy_size);

        // Update position and bytes read
        dev->pos += copy_size;
        bytes_read += copy_size;

        // Move to the next set of descriptors
    }

    return bytes_read;
}
/*
long vioblk_read(
    struct io_intf *restrict io,
    void *restrict buf,
    unsigned long bufsz)
{
    struct vioblk_device *dev = (void *)io - offsetof(struct vioblk_device, io_intf);
    unsigned long bytes_read = 0;
    unsigned long block_size_read = dev->blksz;               //at this time just set it to one block size

    //at this point I let the device could read more than 1 block but this mp can only read one block at one time
    while (bytes_read < bufsz) {
        // Ensure position is within device size
        if (dev->pos >= dev->size) {
            break; // Out of range, stop reading
        }
        unsigned long copy_size = (bufsz - bytes_read < block_size_read) ? (bufsz - bytes_read) : block_size_read;
        // Prepare the request header for a read operation
        dev->vq.req_header.type = VIRTIO_BLK_T_IN; // Read operation
        dev->vq.req_header.sector = dev->pos / dev->blksz; // Starting sector based on position
        dev->vq.desc[2].len = copy_size;


        // Set up the descriptor chain
        //dev->vq.indirect[0] = dev->vq.desc[1];
        //dev->vq.indirect[2] = dev->vq.desc[2];
        //dev->vq.indirect[3] = dev->vq.desc[3];

        // Add the descriptor index to the available ring
        dev->vq.avail.idx = 0;
        __sync_synchronize();
        dev->vq.avail.ring[dev->vq.avail.idx] = 1; // Index of the first descriptor,  the available size is 1

        dev->vq.req_status = 3;

        // Notify the device
        virtio_notify_avail(dev->regs, 0); // Notify queue 0

        // Wait for the read operation to complete
        //if (dev->vq.req_status == 3) condition_wait(&dev->vq.used_updated);

        //while (dev->vq.req_status == 3) continue;
        // Check the status byte to ensure the operation was successful
        if (dev->vq.req_status != VIRTIO_BLK_S_OK) {
            // Handle read error
            return -EIO; // Input/output error
        }

        // Copy the block data from the block buffer to the user buffer
        
        memcpy((uint8_t *)buf + bytes_read, &dev->blkbuf, copy_size);

        // Update the position and the total bytes read
        dev->pos += copy_size;
        bytes_read += copy_size;
    }

    return bytes_read; // Return the total number of bytes read
}
*/

long vioblk_write(
    struct io_intf *restrict io,
    const void *restrict buf,
    unsigned long n)
{
    struct vioblk_device *dev = (void *)io - offsetof(struct vioblk_device, io_intf);
    unsigned long bytes_written = 0;
    unsigned long block_size_write = dev->blksz; // Set to one block size
    uint8_t status_byte = 0;

    if (dev->readonly){
        return -2;
    }
    while (bytes_written < n) {
        // Ensure position is within device size
        if (dev->pos >= dev->size) {
            break; // Out of range, stop writing
        }

        // Prepare the request header for a write operation
        dev->vq.req_header.type = VIRTIO_BLK_T_OUT; // Write operation
        dev->vq.req_header.sector = dev->pos / dev->blksz; // Starting sector based on position

        // Copy data from the user buffer to the device block buffer
        unsigned long copy_size = (n - bytes_written < block_size_write) ? (n - bytes_written) : block_size_write;
        memcpy(dev->blkbuf, (const uint8_t *)buf + bytes_written, copy_size);

        // Set up the descriptor chain
        /*
        dev->vq.indirect[0] = dev->vq.desc[1]; // Points to the request header
        dev->vq.indirect[1] = dev->vq.desc[2]; // Points to the data buffer
        dev->vq.indirect[2] = dev->vq.desc[3]; // Points to the status byte
        */

        // Add the descriptor index to the available ring
        dev->vq.avail.ring[dev->vq.avail.idx] = 0; // Index of the first descriptor
        __sync_synchronize();
        dev->vq.avail.idx = 0;      //only one element in this ring

        // Notify the device
        virtio_notify_avail(dev->regs, 0);  // Notify queue 0

        // Wait for the write operation to complete
        condition_wait(&dev->vq.used_updated);

        // Check the status byte to ensure the operation was successful
        if (status_byte != VIRTIO_BLK_S_OK) {
            // Handle write error
            return -EIO; // Input/output error
        }

        // Update the position and the total bytes written
        dev->pos += copy_size;
        bytes_written += copy_size;
    }

    return bytes_written; // Return the total number of bytes written
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
    //           FIXME your code here
    struct vioblk_device *dev = (struct vioblk_device *)aux;

    //Check if the interrupt is for this device
    if (irqno != dev->irqno) {
        return; // Not the correct IRQ, return early
    }

    // Read the interrupt status register to determine the cause of the interrupt
    uint32_t interrupt_status = dev->regs->interrupt_status;

    // Check if the interrupt status indicates used ring update
    if (interrupt_status == 0) {
        //if there are a ring buffer update interrupt broadcast this queue
        condition_broadcast(&dev->vq.used_updated);
    }

    // Step 5: Acknowledge the interrupt
    dev->regs->interrupt_ack = interrupt_status; 

}

int vioblk_getlen(const struct vioblk_device * dev, uint64_t * lenptr) {
    //           FIXME your code here
    *lenptr = dev->size;  
    return 0;                //?
    //return dev->size;                     //I do not know what is the use of this lenptr

}

int vioblk_getpos(const struct vioblk_device * dev, uint64_t * posptr) {
    //           FIXME your code here
    *posptr = dev->pos;
    return 0;
    //return dev->pos;

}

int vioblk_setpos(struct vioblk_device * dev, const uint64_t * posptr) {
    //           FIXME your code here
    dev->pos = *posptr;
    return 0;
    //return ori_value;                     //I do not know what the return value is, so I just it to the origin posi

}

int vioblk_getblksz (
    const struct vioblk_device * dev, uint32_t * blkszptr)
{
    //           FIXME your code here
    *blkszptr = dev->blksz;
    return 0;
    //return dev->blksz;
}
