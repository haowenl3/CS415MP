#include "console.h"
#include "halt.h"
#include "vioblk.c"
#include "timer.h"
extern char _kimg_end[];

#define RAM_SIZE (8*1024*1024)
#define RAM_START 0x80000000UL
#define KERN_START RAM_START
#define USER_START 0x80100000UL

#define UART0_IOBASE 0x10000000
#define UART1_IOBASE 0x10000100
#define UART0_IRQNO 10

#define VIRT0_IOBASE 0x10001000
#define VIRT1_IOBASE 0x10002000
#define VIRT0_IRQNO 1
void test_vioblk(){
    console_init();
    intr_init();
    devmgr_init();
    timer_init();
    heap_init(_kimg_end, (void*)USER_START);
    struct virtio_mmio_regs* regs;
    regs=kmalloc(sizeof(struct virtio_mmio_regs));
    regs->device_id=VIRTIO_ID_BLOCK;
    virtio_featset_t needed_features;
    virtio_featset_init(needed_features);
    virtio_featset_add(needed_features, VIRTIO_F_RING_RESET);
    virtio_featset_add(needed_features, VIRTIO_F_INDIRECT_DESC);
    for (uint_fast32_t i = 0; i < 2; i++) {
        if (needed_features[i] != 0) {
            regs->device_features_sel = i;
            regs->device_features |= needed_features[i];//=needed_features[i];
        }
    }
    vioblk_attach(regs,0);
    struct io_intf io;
    struct io_intf* ioptr=&io;
    
    int result=vioblk_open(&ioptr, regs);
    assert(result==0);
    
    char test='T';
    char* buf=&test;
    char test2='F';
    char* testbuf=&test2;
    result=vioblk_write(ioptr,buf,1);
    assert(result==1);
    result=vioblk_read(ioptr,testbuf,1);
    assert(result==1);
    assert(*testbuf=='T');
    vioblk_close(ioptr);
}

int main(){
    test_vioblk();
    return 0;
}