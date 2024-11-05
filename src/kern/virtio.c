//           virtio.h - MMIO-based VirtIO
//          

#include "virtio.h"
#include "halt.h"
#include "heap.h"
#include "string.h"
#include "intr.h"
#include "error.h"

#include <stddef.h>

#define VIRTIO_MAGIC 0x74726976

//           EXPORTED FUNCTION DEFINITIONS
//          

void virtio_attach(void * mmio_base, int irqno) {
    volatile struct virtio_mmio_regs * regs = (struct virtio_mmio_regs*) mmio_base;
    extern void viocons_attach (
        //           viocons.c
        volatile struct virtio_mmio_regs * regs, int irqno);
    
    extern void vioblk_attach (
        //           vioblk.c
        volatile struct virtio_mmio_regs * regs, int irqno);

    if (regs->magic_value != VIRTIO_MAGIC) {
        kprintf("%p: No virtio magic number found\n", mmio_base);
        return;
    }

    if (regs->version != 2) {
        kprintf("%p: Unexpected virtio version (found %u, expected %u)\n",
            mmio_base, (unsigned int)regs->version, 2);
        return;
    }

    if (regs->device_id == VIRTIO_ID_NONE)
        return;

    //           reset
    regs->status = 0;
    regs->status = VIRTIO_STAT_ACKNOWLEDGE;

    switch (regs->device_id) {
    case VIRTIO_ID_CONSOLE:
        debug("%p: Found virtio console device", regs);
        viocons_attach(regs, irqno);
        break;
    case VIRTIO_ID_BLOCK:
        debug("%p: Found virtio block device", regs);
        vioblk_attach(regs, irqno);
        break;
    default:
        kprintf("%p: Unknown virtio device type %u ignored\n",
            mmio_base, (unsigned int) regs->device_id);
        return;
    }
}

int virtio_negotiate_features (
    volatile struct virtio_mmio_regs * regs,
    virtio_featset_t enabled,
    const virtio_featset_t wanted,
    const virtio_featset_t needed)
{
    uint_fast32_t i;
    
    //           Check if all needed features are offered

    for (i = 0; i < VIRTIO_FEATLEN; i++) {
        if (needed[i] != 0) {
            regs->device_features_sel = i;
            //           fence o,i
            __sync_synchronize();
            if ((regs->device_features & needed[i]) != needed[i])
                return -ENOTSUP;                                       //the driver can support the device
        }
    }

    //           All required features are available. Now request the desired ones (which
    //           should be a superset of the required ones).

    for (i = 0; i < VIRTIO_FEATLEN; i++) {
        if (wanted[i] != 0) {
            regs->device_features_sel = i;
            regs->driver_features_sel = i;
            //           fence o,i
            __sync_synchronize();
            enabled[i] = regs->device_features & wanted[i];
            regs->driver_features = enabled[i];          //the driver features only contain the wanted features which are available in device
            //           fence o,o
            __sync_synchronize();
        }
    }

    regs->status |= VIRTIO_STAT_FEATURES_OK;
    assert (regs->status & VIRTIO_STAT_FEATURES_OK);

    return 0;
}

void virtio_attach_virtq (
    volatile struct virtio_mmio_regs * regs, int qid, uint_fast16_t len,
    uint64_t desc_addr, uint64_t used_addr, uint64_t avail_addr)
{
    regs->queue_sel = qid;
    regs->queue_ready = 0;
    //           fence o,o
    __sync_synchronize();
    regs->queue_desc = desc_addr;
    regs->queue_device = used_addr;
    regs->queue_driver = avail_addr;
    regs->queue_num = len;
    //           fence o,o
    __sync_synchronize();
}

void __attribute__ ((weak)) viocons_attach (
    volatile struct virtio_mmio_regs * regs, int irqno)
{
    panic("viocons not included!");
}
