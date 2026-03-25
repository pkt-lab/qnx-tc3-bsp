/*
 * VirtIO MMIO block device driver for Arm TC3 FVP
 *
 * QNX's stock devb-virtio hangs on the TC3 FVP because the FVP's
 * VirtIO MMIO block device returns QueueNumMax=256 for ALL queue
 * indices (should return 0 for non-existent queues per VirtIO spec).
 * This causes devb-virtio's viod_find_mmio() queue scan to loop
 * forever.
 *
 * This driver works around the bug by configuring only queue 0
 * (skipping the queue scan entirely) and exposes /dev/vblk0 as a
 * QNX resource manager for raw block I/O.
 *
 * Usage: devb-virtio-fvp [addr] [irq] [devname]
 *   defaults: addr=0x1c130000 irq=236 devname=/dev/vblk0
 *
 * Copyright 2026 Peikan Tsai <peikantsai@gmail.com>
 * Licensed under the Apache License, Version 2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/neutrino.h>
#include <sys/iofunc.h>
#include <sys/dispatch.h>
#include <sys/resmgr.h>
#include <sys/types.h>
#include <sys/procmgr.h>

#include <sys/cam_device.h>
#include <sys/dcmd_cam.h>

#include "virtio-mmio.h"

#define DEFAULT_ADDR    0x1c130000
#define DEFAULT_IRQ     236     /* GIC SPI 204 + 32 */
#define SECTOR_SIZE     512
#define MAX_XFER_SECTORS 128    /* 64KB max per request */
#define QUEUE_NUM       256
#ifndef PAGE_SIZE
#define PAGE_SIZE       4096
#endif
#define VIRTIO_DEVICE_ID_BLOCK  2

/* Global state */
static volatile uint32_t *regs;
static struct virtqueue vq;
static uint64_t capacity_sectors;   /* device capacity in 512-byte sectors */
static int irq_id;
static sem_t io_done;
static pthread_mutex_t io_lock = PTHREAD_MUTEX_INITIALIZER;

/* DMA buffers (physically contiguous) */
static struct virtio_blk_req *req_hdr;
static uint64_t req_hdr_phys;
static uint8_t *data_buf;
static uint64_t data_buf_phys;
static uint8_t *status_buf;
static uint64_t status_buf_phys;

/* Resource manager state */
static iofunc_attr_t resmgr_attr;
static resmgr_connect_funcs_t connect_funcs;
static resmgr_io_funcs_t io_funcs;
static dispatch_t *dpp;

/*
 * Get physical address of a mapped region
 */
static uint64_t get_phys(void *vaddr)
{
    off64_t phys = 0;
    if (mem_offset64(vaddr, NOFD, 1, &phys, NULL) == -1)
        return 0;
    return (uint64_t)phys;
}

/*
 * Allocate physically contiguous uncached memory
 */
static void *alloc_phys(size_t size, uint64_t *phys_out)
{
    void *p = mmap(NULL, size,
                   PROT_READ | PROT_WRITE | PROT_NOCACHE,
                   MAP_PHYS | MAP_ANON | MAP_PRIVATE, NOFD, 0);
    if (p == MAP_FAILED)
        return NULL;
    memset(p, 0, size);
    *phys_out = get_phys(p);
    if (*phys_out == 0) {
        munmap(p, size);
        return NULL;
    }
    return p;
}

/*
 * Calculate virtqueue memory layout (legacy)
 */
static size_t vring_size(uint32_t num)
{
    size_t desc  = num * sizeof(struct vring_desc);
    size_t avail = sizeof(uint16_t) * (3 + num);
    size_t part1 = (desc + avail + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    size_t used  = sizeof(uint16_t) * 3 + sizeof(struct vring_used_elem) * num;
    return part1 + used;
}

/*
 * Initialize VirtIO MMIO device — only queue 0 (bypass broken queue scan)
 */
static int virtio_init(uintptr_t base_addr)
{
    regs = mmap_device_memory(NULL, 0x200,
                              PROT_READ | PROT_WRITE | PROT_NOCACHE,
                              0, base_addr);
    if (regs == MAP_FAILED) {
        fprintf(stderr, "virtio: mmap_device_memory failed\n");
        return -1;
    }

    /* Verify magic and device ID */
    if (virtio_read32(regs, VIRTIO_MMIO_MAGIC) != VIRTIO_MMIO_MAGIC_VALUE) {
        fprintf(stderr, "virtio: bad magic at 0x%lx\n", (unsigned long)base_addr);
        return -1;
    }
    if (virtio_read32(regs, VIRTIO_MMIO_DEVICE_ID) != VIRTIO_DEVICE_ID_BLOCK) {
        fprintf(stderr, "virtio: not a block device (id=%u)\n",
                virtio_read32(regs, VIRTIO_MMIO_DEVICE_ID));
        return -1;
    }

    /* Clear stale interrupt from U-Boot and reset device */
    {
        uint32_t isr = virtio_read32(regs, VIRTIO_MMIO_INTERRUPT_STATUS);
        if (isr)
            virtio_write32(regs, VIRTIO_MMIO_INTERRUPT_ACK, isr);
    }
    virtio_write32(regs, VIRTIO_MMIO_STATUS, 0);

    /* Acknowledge + Driver */
    virtio_write32(regs, VIRTIO_MMIO_STATUS, VIRTIO_STATUS_ACKNOWLEDGE);
    virtio_write32(regs, VIRTIO_MMIO_STATUS,
                   VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

    /* Read and accept features (accept none for simplicity) */
    virtio_write32(regs, VIRTIO_MMIO_HOST_FEATURES_SEL, 0);
    /* uint32_t features = */ virtio_read32(regs, VIRTIO_MMIO_HOST_FEATURES);
    virtio_write32(regs, VIRTIO_MMIO_GUEST_FEATURES_SEL, 0);
    virtio_write32(regs, VIRTIO_MMIO_GUEST_FEATURES, 0);

    /* Setup queue 0 ONLY (skip queue scan — FVP bug workaround) */
    virtio_write32(regs, VIRTIO_MMIO_QUEUE_SEL, 0);
    uint32_t qmax = virtio_read32(regs, VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (qmax == 0) {
        fprintf(stderr, "virtio: queue 0 not available\n");
        return -1;
    }

    vq.num = (qmax < QUEUE_NUM) ? qmax : QUEUE_NUM;
    vq.last_used_idx = 0;
    vq.vq_size = vring_size(vq.num);
    /* Round up to page alignment */
    vq.vq_size = (vq.vq_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    vq.vq_mem = alloc_phys(vq.vq_size, &vq.vq_phys);
    if (!vq.vq_mem) {
        fprintf(stderr, "virtio: queue alloc failed\n");
        return -1;
    }

    /* Set up virtqueue pointers */
    vq.desc = (struct vring_desc *)vq.vq_mem;
    size_t avail_off = vq.num * sizeof(struct vring_desc);
    vq.avail = (struct vring_avail *)((uint8_t *)vq.vq_mem + avail_off);
    size_t used_off = (avail_off + sizeof(uint16_t) * (3 + vq.num)
                       + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    vq.used = (struct vring_used *)((uint8_t *)vq.vq_mem + used_off);

    /* Tell device about the queue */
    virtio_write32(regs, VIRTIO_MMIO_QUEUE_NUM, vq.num);
    virtio_write32(regs, VIRTIO_MMIO_QUEUE_ALIGN, PAGE_SIZE);
    virtio_write32(regs, VIRTIO_MMIO_QUEUE_PFN,
                   (uint32_t)(vq.vq_phys / PAGE_SIZE));

    /* Driver OK */
    virtio_write32(regs, VIRTIO_MMIO_STATUS,
                   VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
                   VIRTIO_STATUS_DRIVER_OK);

    /* Read device capacity from config space at offset 0x100 */
    /* VirtIO block config: uint64_t capacity (in 512-byte sectors) */
    /* Note: access config via 8-bit reads — some FVP models don't support
       32-bit access to config space in legacy MMIO mode */
    {
        volatile uint8_t *cfg = (volatile uint8_t *)regs + VIRTIO_MMIO_CONFIG;
        uint64_t cap = 0;
        for (int i = 0; i < 8; i++)
            cap |= (uint64_t)cfg[i] << (i * 8);
        capacity_sectors = cap;
    }
    /* Fallback: if capacity is 0 or unreasonable, use 4MB default.
       FVP config space may return garbage in legacy MMIO mode. */
    if (capacity_sectors == 0 || capacity_sectors > 0x100000000ULL) {
        fprintf(stderr, "virtio-mmio: WARNING: config capacity=%llu invalid, using 4MB default\n",
                (unsigned long long)capacity_sectors);
        capacity_sectors = 8192;  /* 4MB */
    }

    fprintf(stderr, "virtio-mmio: block device ready, capacity=%llu sectors (%llu MB)\n",
            (unsigned long long)capacity_sectors,
            (unsigned long long)(capacity_sectors * SECTOR_SIZE / (1024 * 1024)));

    return 0;
}

/*
 * Perform a single block I/O request (synchronous, under io_lock)
 * type: VIRTIO_BLK_T_IN (read) or VIRTIO_BLK_T_OUT (write)
 */
static int virtio_blk_io(int type, uint64_t sector, size_t nsectors)
{
    if (sector + nsectors > capacity_sectors)
        return EIO;
    if (nsectors > MAX_XFER_SECTORS)
        return EIO;

    /* Fill request header */
    req_hdr->type = type;
    req_hdr->reserved = 0;
    req_hdr->sector = sector;
    *status_buf = 0xff;  /* sentinel */

    /* Descriptor 0: request header (device-readable) */
    vq.desc[0].addr = req_hdr_phys;
    vq.desc[0].len = sizeof(struct virtio_blk_req);
    vq.desc[0].flags = VRING_DESC_F_NEXT;
    vq.desc[0].next = 1;

    /* Descriptor 1: data buffer */
    vq.desc[1].addr = data_buf_phys;
    vq.desc[1].len = nsectors * SECTOR_SIZE;
    vq.desc[1].flags = VRING_DESC_F_NEXT;
    if (type == VIRTIO_BLK_T_IN)
        vq.desc[1].flags |= VRING_DESC_F_WRITE;
    vq.desc[1].next = 2;

    /* Descriptor 2: status byte (device-writable) */
    vq.desc[2].addr = status_buf_phys;
    vq.desc[2].len = 1;
    vq.desc[2].flags = VRING_DESC_F_WRITE;
    vq.desc[2].next = 0;

    /* Add to available ring */
    uint16_t avail_idx = vq.avail->idx;
    vq.avail->ring[avail_idx % vq.num] = 0; /* desc chain head = 0 */
    __asm__ __volatile__("dmb sy" ::: "memory");
    vq.avail->idx = avail_idx + 1;
    __asm__ __volatile__("dmb sy" ::: "memory");

    /* Notify device */
    virtio_write32(regs, VIRTIO_MMIO_QUEUE_NOTIFY, 0);

    /* Wait for completion via interrupt */
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 5;  /* 5 second timeout */
    if (sem_timedwait(&io_done, &ts) == -1) {
        fprintf(stderr, "virtio: I/O timeout (sector %llu)\n",
                (unsigned long long)sector);
        return EIO;
    }

    /* Check status */
    if (*status_buf != VIRTIO_BLK_S_OK) {
        fprintf(stderr, "virtio: I/O error status=%u\n", *status_buf);
        return EIO;
    }

    return EOK;
}

/*
 * Interrupt handler thread
 */
static void *isr_thread(void *arg)
{
    int irq_num = (int)(intptr_t)arg;
    struct sigevent event;
    SIGEV_INTR_INIT(&event);

    irq_id = InterruptAttachEvent(irq_num, &event, _NTO_INTR_FLAGS_TRK_MSK);
    if (irq_id == -1) {
        fprintf(stderr, "virtio: InterruptAttachEvent(%d) failed: %s\n",
                irq_num, strerror(errno));
        return NULL;
    }

    fprintf(stderr, "virtio-mmio: interrupt handler ready (irq=%d)\n", irq_num);

    for (;;) {
        InterruptWait(0, NULL);

        /* Acknowledge VirtIO interrupt */
        uint32_t isr = virtio_read32(regs, VIRTIO_MMIO_INTERRUPT_STATUS);
        virtio_write32(regs, VIRTIO_MMIO_INTERRUPT_ACK, isr);

        /* Memory barrier before reading used ring (VirtIO spec requirement) */
        __asm__ __volatile__("dmb ld" ::: "memory");

        /* Check for completed requests */
        while (vq.last_used_idx != vq.used->idx) {
            vq.last_used_idx++;
            sem_post(&io_done);
        }

        InterruptUnmask(irq_num, irq_id);
    }
    return NULL;
}

/*
 * Resource manager: read handler
 */
static int virtio_io_read(resmgr_context_t *ctp, io_read_t *msg,
                           iofunc_ocb_t *ocb)
{
    int status = iofunc_read_verify(ctp, msg, ocb, NULL);
    if (status != EOK)
        return status;

    size_t nbytes = msg->i.nbytes;
    off64_t offset = ocb->offset;

    if (offset >= (off64_t)(capacity_sectors * SECTOR_SIZE)) {
        _IO_SET_READ_NBYTES(ctp, 0);
        return EOK;
    }

    /* Clamp to device size */
    if (offset + nbytes > capacity_sectors * SECTOR_SIZE)
        nbytes = capacity_sectors * SECTOR_SIZE - offset;

    /* Align to sector boundaries */
    uint64_t sector = offset / SECTOR_SIZE;
    size_t sec_offset = offset % SECTOR_SIZE;
    size_t total_bytes = nbytes + sec_offset;
    size_t nsectors = (total_bytes + SECTOR_SIZE - 1) / SECTOR_SIZE;
    if (nsectors > MAX_XFER_SECTORS)
        nsectors = MAX_XFER_SECTORS;

    pthread_mutex_lock(&io_lock);
    int err = virtio_blk_io(VIRTIO_BLK_T_IN, sector, nsectors);
    if (err) {
        fprintf(stderr, "virtio: blk_io failed sector=%llu nsectors=%zu err=%d\n",
                (unsigned long long)sector, nsectors, err);
        pthread_mutex_unlock(&io_lock);
        return err;
    }

    /* Copy to client */
    size_t actual = nsectors * SECTOR_SIZE - sec_offset;
    if (actual > nbytes)
        actual = nbytes;
    resmgr_msgwrite(ctp, data_buf + sec_offset, actual, 0);
    pthread_mutex_unlock(&io_lock);

    _IO_SET_READ_NBYTES(ctp, actual);
    ocb->offset += actual;
    if (msg->i.nbytes > 0)
        ocb->flags |= IOFUNC_ATTR_ATIME;
    return EOK;
    ocb->offset += actual;
    if (msg->i.nbytes > 0)
        ocb->flags |= IOFUNC_ATTR_ATIME;
    return EOK;
}

/*
 * Resource manager: write handler
 */
static int virtio_io_write(resmgr_context_t *ctp, io_write_t *msg,
                            iofunc_ocb_t *ocb)
{
    int status = iofunc_write_verify(ctp, msg, ocb, NULL);
    if (status != EOK)
        return status;

    size_t nbytes = msg->i.nbytes;
    off64_t offset = ocb->offset;

    if (offset >= (off64_t)(capacity_sectors * SECTOR_SIZE))
        return ENOSPC;

    /* Clamp to device size */
    if (offset + nbytes > capacity_sectors * SECTOR_SIZE)
        nbytes = capacity_sectors * SECTOR_SIZE - offset;

    uint64_t sector = offset / SECTOR_SIZE;
    size_t sec_offset = offset % SECTOR_SIZE;
    size_t total_bytes = nbytes + sec_offset;
    size_t nsectors = (total_bytes + SECTOR_SIZE - 1) / SECTOR_SIZE;
    if (nsectors > MAX_XFER_SECTORS)
        nsectors = MAX_XFER_SECTORS;

    pthread_mutex_lock(&io_lock);

    /* For unaligned writes, read first to preserve partial sectors */
    if (sec_offset != 0 || (nbytes % SECTOR_SIZE) != 0) {
        int err = virtio_blk_io(VIRTIO_BLK_T_IN, sector, nsectors);
        if (err) {
            pthread_mutex_unlock(&io_lock);
            return err;
        }
    }

    /* Copy client data into DMA buffer */
    size_t actual = nsectors * SECTOR_SIZE - sec_offset;
    if (actual > nbytes)
        actual = nbytes;
    resmgr_msgread(ctp, data_buf + sec_offset, actual, sizeof(msg->i));

    /* Write sectors */
    int err = virtio_blk_io(VIRTIO_BLK_T_OUT, sector, nsectors);
    pthread_mutex_unlock(&io_lock);

    if (err)
        return err;

    _IO_SET_WRITE_NBYTES(ctp, actual);
    ocb->offset += actual;
    if (actual > 0)
        ocb->flags |= IOFUNC_ATTR_MTIME | IOFUNC_ATTR_CTIME;
    return EOK;
}

/*
 * Resource manager: devctl handler (log + default)
 */
static int virtio_io_devctl(resmgr_context_t *ctp, io_devctl_t *msg,
                             iofunc_ocb_t *ocb)
{
    if (msg->i.dcmd == DCMD_CAM_DEVINFO) {
        cam_devinfo_t info;
        memset(&info, 0, sizeof(info));
        info.num_sctrs = (uint32_t)capacity_sectors;
        info.num_sctrs64 = capacity_sectors;
        info.sctr_size = SECTOR_SIZE;
        info.cylinders = 1;
        info.heads = 1;
        info.tracks = (uint32_t)capacity_sectors;
        info.type = 0; /* direct access */
        info.xfer_len_max = MAX_XFER_SECTORS;
        info.xfer_len_optimal = MAX_XFER_SECTORS;
        info.xfer_len_granularity = 1;

        memset(&msg->o, 0, sizeof(msg->o));
        msg->o.ret_val = EOK;
        msg->o.nbytes = sizeof(info);
        SETIOV(&ctp->iov[0], &msg->o, sizeof(msg->o));
        SETIOV(&ctp->iov[1], &info, sizeof(info));
        return _RESMGR_NPARTS(2);
    }

    return iofunc_devctl_default(ctp, msg, ocb);
}

int main(int argc, char *argv[])
{
    uintptr_t base_addr = DEFAULT_ADDR;
    int irq_num = DEFAULT_IRQ;
    const char *devname = "/dev/vblk0";

    if (argc > 1) base_addr = strtoul(argv[1], NULL, 0);
    if (argc > 2) irq_num = atoi(argv[2]);
    if (argc > 3) devname = argv[3];

    /* Need I/O privilege */
    if (ThreadCtl(_NTO_TCTL_IO, 0) == -1) {
        perror("ThreadCtl");
        return 1;
    }

    sem_init(&io_done, 0, 0);

    /* Initialize VirtIO device (includes stale interrupt clear + reset) */
    if (virtio_init(base_addr) != 0) {
        fprintf(stderr, "virtio-mmio: init failed\n");
        return 1;
    }

    /* Allocate DMA buffers */
    req_hdr = alloc_phys(sizeof(struct virtio_blk_req), &req_hdr_phys);
    data_buf = alloc_phys(MAX_XFER_SECTORS * SECTOR_SIZE, &data_buf_phys);
    status_buf = alloc_phys(PAGE_SIZE, &status_buf_phys);
    if (!req_hdr || !data_buf || !status_buf) {
        fprintf(stderr, "virtio-mmio: DMA buffer alloc failed\n");
        return 1;
    }

    /* Start interrupt handler thread */
    pthread_t isr_tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    struct sched_param param = { .sched_priority = 21 };
    pthread_attr_setschedparam(&attr, &param);
    pthread_create(&isr_tid, &attr, isr_thread, (void *)(intptr_t)irq_num);
    pthread_attr_destroy(&attr);

    /* Set up resource manager */
    dpp = dispatch_create();
    if (!dpp) {
        perror("dispatch_create");
        return 1;
    }

    iofunc_func_init(_RESMGR_CONNECT_NFUNCS, &connect_funcs,
                     _RESMGR_IO_NFUNCS, &io_funcs);
    io_funcs.read = virtio_io_read;
    io_funcs.write = virtio_io_write;
    io_funcs.devctl = virtio_io_devctl;

    iofunc_attr_init(&resmgr_attr, S_IFBLK | 0660, NULL, NULL);
    resmgr_attr.nbytes = capacity_sectors * SECTOR_SIZE;

    resmgr_attr_t rattr;
    memset(&rattr, 0, sizeof(rattr));
    rattr.nparts_max = 1;
    rattr.msg_max_size = sizeof(io_write_t) + MAX_XFER_SECTORS * SECTOR_SIZE;

    int id = resmgr_attach(dpp, &rattr, devname, _FTYPE_ANY,
                           _RESMGR_FLAG_SELF, &connect_funcs, &io_funcs,
                           &resmgr_attr);
    if (id == -1) {
        perror("resmgr_attach");
        return 1;
    }

    fprintf(stderr, "virtio-mmio: %s ready (%llu MB)\n", devname,
            (unsigned long long)(capacity_sectors * SECTOR_SIZE / (1024 * 1024)));

    /* Daemonize */
    procmgr_daemon(0, PROCMGR_DAEMON_NOCLOSE | PROCMGR_DAEMON_NODEVNULL);

    /* Dispatch loop */
    dispatch_context_t *ctp = dispatch_context_alloc(dpp);
    for (;;) {
        ctp = dispatch_block(ctp);
        if (ctp)
            dispatch_handler(ctp);
    }

    return 0;
}
