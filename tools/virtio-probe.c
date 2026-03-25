/*
 * Read VirtIO MMIO registers to diagnose devb-virtio detection failure.
 * Usage: virtio-probe [address]   (default: 0x1c130000)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/neutrino.h>

int main(int argc, char *argv[])
{
    uintptr_t base = 0x1c130000;
    if (argc > 1)
        base = strtoul(argv[1], NULL, 0);

    /* Need I/O privilege for mmap_device_memory */
    if (ThreadCtl(_NTO_TCTL_IO, 0) == -1) {
        perror("ThreadCtl IO");
        return 1;
    }

    volatile uint32_t *regs = mmap_device_memory(NULL, 0x200,
        PROT_READ | PROT_WRITE | PROT_NOCACHE,
        0, base);
    if (regs == MAP_FAILED) {
        perror("mmap_device_memory");
        return 1;
    }

    printf("VirtIO MMIO registers at 0x%lx:\n", (unsigned long)base);
    printf("  MagicValue  [0x000]: 0x%08x", regs[0x000/4]);
    if (regs[0x000/4] == 0x74726976)
        printf(" (\"virt\" - OK)");
    else
        printf(" (INVALID - expected 0x74726976)");
    printf("\n");
    printf("  Version     [0x004]: %u", regs[0x004/4]);
    if (regs[0x004/4] == 1)
        printf(" (legacy)");
    else if (regs[0x004/4] == 2)
        printf(" (modern/v1.0+)");
    printf("\n");
    printf("  DeviceID    [0x008]: %u", regs[0x008/4]);
    if (regs[0x008/4] == 2)
        printf(" (block device)");
    else if (regs[0x008/4] == 0)
        printf(" (no device / not configured)");
    printf("\n");
    printf("  VendorID    [0x00c]: 0x%08x\n", regs[0x00c/4]);
    printf("  Status      [0x070]: 0x%08x", regs[0x070/4]);
    uint32_t status = regs[0x070/4];
    if (status & 0x01) printf(" ACKNOWLEDGE");
    if (status & 0x02) printf(" DRIVER");
    if (status & 0x04) printf(" DRIVER_OK");
    if (status & 0x08) printf(" FEATURES_OK");
    if (status & 0x40) printf(" DEVICE_NEEDS_RESET");
    if (status & 0x80) printf(" FAILED");
    if (status == 0) printf(" (reset/idle)");
    printf("\n");
    printf("  DevFeatures [0x010]: 0x%08x\n", regs[0x010/4]);
    printf("  IntStatus   [0x060]: 0x%08x\n", regs[0x060/4]);

    /* Reset device if "reset" argument given */
    if (argc > 2 && strcmp(argv[2], "reset") == 0) {
        printf("\nResetting device...\n");
        /* Acknowledge any pending VirtIO interrupt first */
        uint32_t isr = regs[0x060/4];
        if (isr) {
            printf("  Clearing VirtIO InterruptStatus=0x%x\n", isr);
            regs[0x064/4] = isr; /* InterruptACK */
        }
        /* Reset the device */
        regs[0x070/4] = 0;
        printf("  Status after reset: 0x%08x\n", regs[0x070/4]);
        printf("  IntStatus after reset: 0x%08x\n", regs[0x060/4]);
        /* Clear GIC pending for SPI 204 (GICD_ICPENDR[7], bit 12) */
        /* SPI 204 = INTID 236 = ICPENDR[236/32] bit [236%32] = ICPENDR[7] bit 12 */
        volatile uint32_t *gicd = mmap_device_memory(NULL, 0x10000,
            PROT_READ | PROT_WRITE | PROT_NOCACHE, 0, 0x30000000);
        if (gicd != MAP_FAILED) {
            uint32_t pending = gicd[0x280/4 + 7]; /* ICPENDR[7] */
            printf("  GIC ISPENDR[7] before clear: 0x%08x (bit12=%d)\n",
                gicd[0x200/4 + 7], (gicd[0x200/4 + 7] >> 12) & 1);
            gicd[0x280/4 + 7] = (1u << 12); /* Clear SPI 204 pending */
            printf("  GIC ISPENDR[7] after clear:  0x%08x (bit12=%d)\n",
                gicd[0x200/4 + 7], (gicd[0x200/4 + 7] >> 12) & 1);
            munmap_device_memory((void *)gicd, 0x10000);
        }
        printf("  DeviceID: %u\n", regs[0x008/4]);
    }

    /* Step through VirtIO init if "init" argument given */
    if (argc > 2 && strcmp(argv[2], "init") == 0) {
        printf("\n=== VirtIO init sequence ===\n");

        printf("1. Reset... ");
        regs[0x070/4] = 0;
        printf("Status=0x%x\n", regs[0x070/4]);

        printf("2. ACKNOWLEDGE... ");
        regs[0x070/4] = 1;
        printf("Status=0x%x\n", regs[0x070/4]);

        printf("3. DRIVER... ");
        regs[0x070/4] = 3; /* ACKNOWLEDGE | DRIVER */
        printf("Status=0x%x\n", regs[0x070/4]);

        printf("4. Read HostFeatures (sel=0)... ");
        regs[0x014/4] = 0; /* HostFeaturesSel */
        printf("Features[0]=0x%08x\n", regs[0x010/4]);

        printf("   Read HostFeatures (sel=1)... ");
        regs[0x014/4] = 1;
        printf("Features[1]=0x%08x\n", regs[0x010/4]);

        printf("5. Write GuestFeatures... ");
        regs[0x024/4] = 0; /* GuestFeaturesSel */
        regs[0x020/4] = 0; /* GuestFeatures = none */
        printf("OK\n");

        printf("6. Read QueueNumMax (queue 0)... ");
        regs[0x030/4] = 0; /* QueueSel */
        printf("QueueNumMax=%u\n", regs[0x034/4]);

        printf("7. QueueNum, QueueAlign, QueuePFN... ");
        uint32_t qmax = regs[0x034/4];
        if (qmax == 0) {
            printf("SKIP (no queue)\n");
        } else {
            /* Allocate contiguous physical memory for virtqueue */
            size_t qsz = qmax * 16 + 4096; /* rough estimate */
            void *qmem = mmap(NULL, qsz,
                PROT_READ | PROT_WRITE | PROT_NOCACHE,
                MAP_PHYS | MAP_ANON | MAP_PRIVATE, NOFD, 0);
            if (qmem == MAP_FAILED) {
                perror("queue mmap FAILED");
            } else {
                off64_t phys = 0;
                mem_offset64(qmem, NOFD, 1, &phys, NULL);
                printf("qmem=%p phys=0x%llx\n", qmem, (unsigned long long)phys);
                printf("   Writing QueueNum=%u... ", qmax);
                fflush(stdout);
                regs[0x038/4] = qmax;
                printf("OK\n");
                printf("   Writing QueueAlign=4096... ");
                fflush(stdout);
                regs[0x03c/4] = 4096;
                printf("OK\n");
                printf("   Writing QueuePFN=0x%x... ",
                    (uint32_t)(phys / 4096));
                fflush(stdout);
                regs[0x040/4] = (uint32_t)(phys / 4096);
                printf("OK\n");
                printf("   Writing DRIVER_OK... ");
                fflush(stdout);
                regs[0x070/4] = 0x07; /* ACK|DRIVER|DRIVER_OK */
                printf("Status=0x%x\n", regs[0x070/4]);
                munmap(qmem, qsz);
            }
        }

        printf("8. Final Status=0x%x\n", regs[0x070/4]);
        printf("=== Init probe complete ===\n");
    }

    /* Test interrupt attachment if "irqtest" argument given */
    if (argc > 2 && strcmp(argv[2], "irqtest") == 0) {
        int irq_num = (argc > 3) ? atoi(argv[3]) : 236;
        printf("\nTesting InterruptAttach(%d)... ", irq_num);
        fflush(stdout);

        struct sigevent event;
        SIGEV_INTR_INIT(&event);
        int id = InterruptAttachEvent(irq_num, &event, _NTO_INTR_FLAGS_TRK_MSK);
        if (id == -1) {
            perror("InterruptAttachEvent FAILED");
        } else {
            printf("OK (id=%d)\n", id);
            InterruptDetach(id);
            printf("Detached.\n");
        }
    }

    /* Scan all queue indices to check QueueNumMax */
    if (argc > 2 && strcmp(argv[2], "queues") == 0) {
        printf("\n=== Queue scan (writing QueueSel, reading QueueNumMax) ===\n");
        regs[0x070/4] = 0; /* reset */
        regs[0x070/4] = 1; /* ACKNOWLEDGE */
        regs[0x070/4] = 3; /* DRIVER */
        for (int q = 0; q < 10; q++) {
            regs[0x030/4] = q; /* QueueSel */
            uint32_t qmax = regs[0x034/4]; /* QueueNumMax */
            printf("  Queue %d: QueueNumMax=%u\n", q, qmax);
            if (qmax == 0) {
                printf("  (queue %d does not exist — loop should exit here)\n", q);
                break;
            }
        }
        regs[0x070/4] = 0; /* reset */
    }

    /* Scan GIC for pending interrupts after triggering VirtIO */
    if (argc > 2 && strcmp(argv[2], "irqscan") == 0) {
        /* GIC Distributor at 0x30000000 */
        volatile uint32_t *gicd = mmap_device_memory(NULL, 0x10000,
            PROT_READ | PROT_WRITE | PROT_NOCACHE, 0, 0x30000000);
        if (gicd == MAP_FAILED) {
            perror("mmap GICD");
        } else {
            /* First, do a full VirtIO init with a queue */
            printf("\n=== IRQ Scan: init device + trigger ===\n");
            regs[0x070/4] = 0; /* reset */
            regs[0x070/4] = 1; /* ACKNOWLEDGE */
            regs[0x070/4] = 3; /* DRIVER */
            regs[0x024/4] = 0;
            regs[0x020/4] = 0; /* guest features = 0 */
            regs[0x030/4] = 0; /* QueueSel = 0 */
            uint32_t qmax = regs[0x034/4];
            printf("QueueNumMax=%u\n", qmax);

            /* Allocate queue memory */
            size_t qsz = 16384;
            void *qmem = mmap(NULL, qsz,
                PROT_READ | PROT_WRITE | PROT_NOCACHE,
                MAP_PHYS | MAP_ANON | MAP_PRIVATE, NOFD, 0);
            if (qmem != MAP_FAILED) {
                memset(qmem, 0, qsz);
                off64_t phys = 0;
                mem_offset64(qmem, NOFD, 1, &phys, NULL);
                regs[0x038/4] = qmax; /* QueueNum */
                regs[0x03c/4] = 4096; /* QueueAlign */
                regs[0x040/4] = (uint32_t)(phys / 4096); /* QueuePFN */
                regs[0x070/4] = 7; /* DRIVER_OK */
                printf("Device ready, Status=0x%x\n", regs[0x070/4]);

                /* Read GICD ISPENDR before trigger (SPIs start at bit 0 of ISPENDR1) */
                printf("\nGICD ISPENDR before trigger:\n");
                for (int i = 1; i <= 31; i++) {
                    uint32_t val = gicd[0x200/4 + i]; /* ISPENDR[i] */
                    if (val != 0) {
                        for (int b = 0; b < 32; b++) {
                            if (val & (1u << b))
                                printf("  SPI %d (vector %d) pending\n",
                                    i*32 + b - 32, i*32 + b);
                        }
                    }
                }

                /* Trigger: write to QueueNotify */
                printf("\nTriggering QueueNotify...\n");
                regs[0x050/4] = 0; /* notify queue 0 */

                /* Small delay for interrupt to propagate */
                for (volatile int d = 0; d < 100000; d++);

                /* Check VirtIO InterruptStatus */
                printf("VirtIO InterruptStatus: 0x%x\n", regs[0x060/4]);

                /* Scan GICD ISPENDR after trigger */
                printf("\nGICD ISPENDR after trigger:\n");
                int found = 0;
                for (int i = 1; i <= 31; i++) {
                    uint32_t val = gicd[0x200/4 + i];
                    if (val != 0) {
                        for (int b = 0; b < 32; b++) {
                            if (val & (1u << b)) {
                                printf("  SPI %d (vector %d) pending\n",
                                    i*32 + b - 32, i*32 + b);
                                found++;
                            }
                        }
                    }
                }
                if (!found)
                    printf("  (none)\n");

                /* Reset device */
                regs[0x070/4] = 0;
                munmap(qmem, qsz);
            }
            munmap_device_memory((void *)gicd, 0x10000);
        }
    }

    munmap_device_memory((void *)regs, 0x200);
    return 0;
}
