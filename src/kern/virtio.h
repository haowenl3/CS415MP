//           virtio.h - MMIO-based VirtIO
//          

#ifndef _VIRTIO_H_
#define _VIRTIO_H_

#include <stdint.h>

//           EXPORTED CONSTANT DEFINITIONS
//          

#define VIRTIO_STAT_ACKNOWLEDGE         (1 << 0)
#define VIRTIO_STAT_DRIVER              (1 << 1)
#define VIRTIO_STAT_FEATURES_OK         (1 << 3)
#define VIRTIO_STAT_DRIVER_OK           (1 << 2)
#define VIRTIO_STAT_DEVICE_NEEDS_RESET  (1 << 6)
#define VIRTIO_STAT_FAILED              (1 << 7)

//           Feature bits (number, *not* mask)

#define VIRTIO_F_INDIRECT_DESC		28
#define VIRTIO_F_EVENT_IDX			29
#define VIRTIO_F_ANY_LAYOUT			27
#define VIRTIO_F_RING_RESET         40

#define VIRTQ_LEN_MAX 32768

#define VIRTQ_USED_F_NO_NOTIFY		1
#define VIRTQ_AVAIL_F_NO_INTERRUPT	1

#define VIRTQ_DESC_F_NEXT       	(1 << 0)
#define VIRTQ_DESC_F_WRITE      	(1 << 1)
#define VIRTQ_DESC_F_INDIRECT		(1 << 2)

//           length of feature vector
#define VIRTIO_FEATLEN 4

//           EXPORTED TYPE DEFINITIONS
//          

typedef uint32_t virtio_featset_t[VIRTIO_FEATLEN];
struct virtio_mmio_regs {
    uint32_t magic_value;
    uint32_t version;
    uint32_t device_id;
    uint32_t vendor_id;
    uint32_t device_features;
    uint32_t device_features_sel;
    uint32_t _reserved_0x18[2];
    uint32_t driver_features;
    uint32_t driver_features_sel;
    uint32_t _reserved_0x28[2];
    uint32_t queue_sel;
    uint32_t queue_num_max;
    uint32_t queue_num;
    uint32_t _reserved_0x3c[2];
    uint32_t queue_ready;
    uint32_t _reserved_0x48[2];
    uint32_t queue_notify;
    uint32_t _reserved_0x54[3];
    uint32_t interrupt_status;
    uint32_t interrupt_ack;
    uint32_t _reserved_0x68[2];
    uint32_t status;
    uint32_t _reserved_0x74[3];
    uint64_t queue_desc;
    uint32_t _reserved_0x8c[2];
    uint64_t queue_driver;
    uint32_t _reserved_0x9c[2];
    uint64_t queue_device;
    uint32_t shm_sel;
    uint64_t shm_len;
    uint64_t shm_base;
    uint32_t queue_reset;
    uint32_t _reserved_0xc4[14];
    
    union {
        //           Block device config
        struct {
            uint64_t capacity;
            uint32_t size_max;
            uint32_t seg_max;
            struct {
                uint16_t cylinders;
                uint8_t heads;
                uint8_t sectors;
            } geometry;
            uint32_t blk_size;
            struct {
                uint8_t physical_block_exp;
                uint8_t alignment_offset;
                uint16_t min_io_size;
                uint32_t opt_io_size;
            } topology;
            uint8_t writeback;
            char unused0;
            uint16_t num_queues;
            uint32_t max_discard_sectors;
            uint32_t max_discard_seg;
            uint32_t discard_sector_alignment;
            uint32_t max_write_zeroes_sectors;
            uint32_t max_write_zeroes_seg;
            uint8_t write_zeroes_may_unmap;
            char unused1[3];
            uint32_t max_secure_erase_sectors;
            uint32_t max_secure_erase_seg;
            uint32_t secure_erase_sector_alignment;
        } blk;
        uint8_t raw[0];
    } config;
};

struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    int16_t next;
};

struct virtq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];
};

//           VIRTQ_AVAIL_SIZE(n)
//           Evaluates to a compile-time constant giving the size of a virtq avail ring
//           sized for /n/ elements.

#define VIRTQ_AVAIL_SIZE(n) \
    (sizeof(struct virtq_avail)+(n)*sizeof(uint16_t))

struct virtq_used_elem {
    uint32_t id;
    uint32_t len;
};

struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    struct virtq_used_elem ring[];
};

//           VIRTQ_USED_SIZE(n)
//           Evaluates to a compile-time constant giving the size of a virtq used ring
//           sized for /n/ elements.

#define VIRTQ_USED_SIZE(n) \
    (sizeof(struct virtq_used)+(n)*sizeof(struct virtq_used_elem))


//           EXPORTED FUNCTION DEFINITIONS
//          

extern void virtio_attach(void * mmio_base, int irqno);

static inline int virtio_check_feature (
    volatile struct virtio_mmio_regs * regs, uint_fast16_t k);

extern int virtio_negotiate_features (
    volatile struct virtio_mmio_regs * regs,
    virtio_featset_t enabled_features,
    const virtio_featset_t wanted_features,
    const virtio_featset_t needed_features);

//           Notifies the device of new elements in the avail ring. This function is
//           safe to call from an ISR.

static inline void virtio_notify_avail (
    volatile struct virtio_mmio_regs * regs, int qid);

extern void virtio_attach_virtq (
    volatile struct virtio_mmio_regs * regs, int qid, uint_fast16_t len,
    uint64_t desc_addr, uint64_t used_addr, uint64_t avail_addr);

static inline void virtio_enable_virtq (
    volatile struct virtio_mmio_regs * regs, int qid);

static inline void virtio_reset_virtq (
    volatile struct virtio_mmio_regs * regs, int qid);

//           Zero-initializes a VirtIO feature set bitmap.

static inline void virtio_featset_init(virtio_featset_t fts);

//           Sets a feature in a VirtIO feature set bitmap.

static inline void virtio_featset_add(virtio_featset_t fts, uint_fast16_t k);

//           Returns 1 if feature bit is set, and 0 otherwise.

static inline int virtio_featset_test(virtio_featset_t fts, uint_fast16_t k);



//           VIRTIO DEVICE ID LIST
//          

#define VIRTIO_ID_NONE                  0 /* no device */

//           VirtIO Device IDs from Linux (virtio_ids.h). See end of file for that header
//           and license.

#define VIRTIO_ID_NET                   1 /* virtio net */
#define VIRTIO_ID_BLOCK                 2 /* virtio block */
#define VIRTIO_ID_CONSOLE               3 /* virtio console */
#define VIRTIO_ID_RNG                   4 /* virtio rng */
#define VIRTIO_ID_BALLOON               5 /* virtio balloon */
#define VIRTIO_ID_IOMEM                 6 /* virtio ioMemory */
#define VIRTIO_ID_RPMSG                 7 /* virtio remote processor messaging */
#define VIRTIO_ID_SCSI                  8 /* virtio scsi */
#define VIRTIO_ID_9P                    9 /* 9p virtio console */
#define VIRTIO_ID_MAC80211_WLAN         10 /* virtio WLAN MAC */
#define VIRTIO_ID_RPROC_SERIAL          11 /* virtio remoteproc serial link */
#define VIRTIO_ID_CAIF                  12 /* Virtio caif */
#define VIRTIO_ID_MEMORY_BALLOON        13 /* virtio memory balloon */
#define VIRTIO_ID_GPU                   16 /* virtio GPU */
#define VIRTIO_ID_CLOCK                 17 /* virtio clock/timer */
#define VIRTIO_ID_INPUT                 18 /* virtio input */
#define VIRTIO_ID_VSOCK                 19 /* virtio vsock transport */
#define VIRTIO_ID_CRYPTO                20 /* virtio crypto */
#define VIRTIO_ID_SIGNAL_DIST           21 /* virtio signal distribution device */
#define VIRTIO_ID_PSTORE                22 /* virtio pstore device */
#define VIRTIO_ID_IOMMU                 23 /* virtio IOMMU */
#define VIRTIO_ID_MEM                   24 /* virtio mem */
#define VIRTIO_ID_SOUND                 25 /* virtio sound */
#define VIRTIO_ID_FS                    26 /* virtio filesystem */
#define VIRTIO_ID_PMEM                  27 /* virtio pmem */
#define VIRTIO_ID_RPMB                  28 /* virtio rpmb */
#define VIRTIO_ID_MAC80211_HWSIM        29 /* virtio mac80211-hwsim */
#define VIRTIO_ID_VIDEO_ENCODER         30 /* virtio video encoder */
#define VIRTIO_ID_VIDEO_DECODER         31 /* virtio video decoder */
#define VIRTIO_ID_SCMI                  32 /* virtio SCMI */
#define VIRTIO_ID_NITRO_SEC_MOD         33 /* virtio nitro secure module*/
#define VIRTIO_ID_I2C_ADAPTER           34 /* virtio i2c adapter */
#define VIRTIO_ID_WATCHDOG              35 /* virtio watchdog */
#define VIRTIO_ID_CAN                   36 /* virtio can */
#define VIRTIO_ID_DMABUF                37 /* virtio dmabuf */
#define VIRTIO_ID_PARAM_SERV            38 /* virtio parameter server */
#define VIRTIO_ID_AUDIO_POLICY          39 /* virtio audio policy */
#define VIRTIO_ID_BT                    40 /* virtio bluetooth */
#define VIRTIO_ID_GPIO                  41 /* virtio gpio */


//           INLINE FUNCTION DEFINITIONS
//          

static inline int virtio_check_feature (
    volatile struct virtio_mmio_regs * regs, uint_fast16_t k)
{
    regs->device_features_sel = k/32;
    //           fence o,i
    __sync_synchronize();
    return ((regs->device_features >> (k%32)) & 1);
}

static void virtio_notify_avail (
    volatile struct virtio_mmio_regs * regs, int qid)
{
    //           fence w,o
    __sync_synchronize();
    regs->queue_notify = qid;
}

static inline void virtio_enable_virtq (
    volatile struct virtio_mmio_regs * regs, int qid)
{
    regs->queue_sel = qid;
    //           fence o,o
    __sync_synchronize();
    regs->queue_ready = 1;
}

static inline void virtio_reset_virtq (
    volatile struct virtio_mmio_regs * regs, int qid)
{
    regs->queue_sel = qid;
    //           fence o,o
    __sync_synchronize();
    regs->queue_reset = 1;
}

static inline void virtio_featset_init(virtio_featset_t fts) {
    uint_fast8_t i;

    for (i = 0; i < VIRTIO_FEATLEN; i++)
        fts[i] = 0;
}

static inline void virtio_featset_add(virtio_featset_t fts, uint_fast16_t k) {
    fts[k/32] |= UINT32_C(1) << (k%32);              //just find the specific bit 
}

static inline int virtio_featset_test(virtio_featset_t fts, uint_fast16_t k) {
    return ((fts[k/32] >> (k%32)) & 1);              //test if this bit is 1
}

//           _VIRTIO_H_
#endif

/* An interface for efficient virtio implementation.
 *
 * This header is BSD licensed so anyone can use the definitions
 * to implement compatible drivers/servers.
 *
 * Copyright 2007, 2009, IBM Corporation
 * Copyright 2011, Red Hat, Inc
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of IBM nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL IBM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Virtio IDs
 *
 * This header is BSD licensed so anyone can use the definitions to implement
 * compatible drivers/servers.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of IBM nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL IBM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE. */