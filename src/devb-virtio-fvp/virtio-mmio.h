/*
 * VirtIO MMIO legacy (v1) register definitions and virtqueue structures.
 * Based on VirtIO 1.0 spec, section 4.2.2 (MMIO Device Register Layout)
 */
#ifndef VIRTIO_MMIO_H
#define VIRTIO_MMIO_H

#include <stdint.h>

/* MMIO register offsets */
#define VIRTIO_MMIO_MAGIC           0x000
#define VIRTIO_MMIO_VERSION         0x004
#define VIRTIO_MMIO_DEVICE_ID       0x008
#define VIRTIO_MMIO_VENDOR_ID       0x00c
#define VIRTIO_MMIO_HOST_FEATURES   0x010
#define VIRTIO_MMIO_HOST_FEATURES_SEL 0x014
#define VIRTIO_MMIO_GUEST_FEATURES  0x020
#define VIRTIO_MMIO_GUEST_FEATURES_SEL 0x024
#define VIRTIO_MMIO_QUEUE_SEL       0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX   0x034
#define VIRTIO_MMIO_QUEUE_NUM       0x038
#define VIRTIO_MMIO_QUEUE_ALIGN     0x03c
#define VIRTIO_MMIO_QUEUE_PFN       0x040
#define VIRTIO_MMIO_QUEUE_NOTIFY    0x050
#define VIRTIO_MMIO_INTERRUPT_STATUS 0x060
#define VIRTIO_MMIO_INTERRUPT_ACK   0x064
#define VIRTIO_MMIO_STATUS          0x070
/* Device-specific config starts at 0x100 */
#define VIRTIO_MMIO_CONFIG          0x100

#define VIRTIO_MMIO_MAGIC_VALUE     0x74726976  /* "virt" */

/* Status bits */
#define VIRTIO_STATUS_ACKNOWLEDGE   0x01
#define VIRTIO_STATUS_DRIVER        0x02
#define VIRTIO_STATUS_DRIVER_OK     0x04
#define VIRTIO_STATUS_FEATURES_OK   0x08
#define VIRTIO_STATUS_FAILED        0x80

/* Block device types */
#define VIRTIO_BLK_T_IN             0   /* read */
#define VIRTIO_BLK_T_OUT            1   /* write */

/* Block device status */
#define VIRTIO_BLK_S_OK             0
#define VIRTIO_BLK_S_IOERR          1
#define VIRTIO_BLK_S_UNSUPP         2

/* Descriptor flags */
#define VRING_DESC_F_NEXT           1
#define VRING_DESC_F_WRITE          2

/* Block request header */
struct virtio_blk_req {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
};

/* Virtqueue descriptor (16 bytes) */
struct vring_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
};

/* Available ring */
struct vring_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];
    /* uint16_t used_event; after ring[num] */
};

/* Used ring element */
struct vring_used_elem {
    uint32_t id;
    uint32_t len;
};

/* Used ring */
struct vring_used {
    uint16_t flags;
    uint16_t idx;
    struct vring_used_elem ring[];
    /* uint16_t avail_event; after ring[num] */
};

/* Virtqueue state */
struct virtqueue {
    uint32_t num;           /* queue size */
    uint32_t last_used_idx;
    struct vring_desc  *desc;
    struct vring_avail *avail;
    struct vring_used  *used;
    void    *vq_mem;        /* mmap'd memory for free */
    size_t   vq_size;
    uint64_t vq_phys;       /* physical address of vq_mem */
};

/* Register access helpers */
static inline uint32_t virtio_read32(volatile uint32_t *regs, int offset) {
    return regs[offset / 4];
}
static inline void virtio_write32(volatile uint32_t *regs, int offset, uint32_t val) {
    regs[offset / 4] = val;
}

#endif /* VIRTIO_MMIO_H */
