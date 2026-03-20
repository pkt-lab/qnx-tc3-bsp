/*
 * Copyright 2026 Peikan Tsai <peikantsai@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/*
 * Arm Total Compute 3 (TC3) FVP startup
 * GIC-700 (GICv3): GICD=0x30000000, GICR=0x30080000
 * PL011 UART: 0x2A400000 (AP NS), 0x1c090000 (board)
 * DRAM: 0x80000000, 8 CPUs (2xA520+4xA725+2xX925)
 */

#include <startup.h>
#include <aarch64/psci.h>

extern void init_intrinfo_tc3(void);

#define TC3_DRAM_BASE       0x80000000ULL
#define TC3_DRAM_SIZE_DEFAULT (2ULL * 1024 * 1024 * 1024)
#define DEFAULT_CPU_FREQ    100000000

#if defined(__aarch64__)
    #define FDT_REG     0
#else
    #define FDT_REG     2
#endif

void tweak_cmdline(struct bootargs_entry *bap, const char *name) {
    if (boot_regs[FDT_REG])
        fdt_tweak_cmdline(bap, name, boot_regs[FDT_REG]);
}

int main(int argc, char **argv, char **envv)
{
    static const struct debug_device debug_devices[] = {
        {
            .name = "pl011",
            .defaults = {
                [0] = "0x1c090000^0.115200.24000000",
                [1] = "0x2A400000^0.115200.24000000"
            },
            .init = init_pl011,
            .put = put_pl011,
            .callouts = {
                [DEBUG_DISPLAY_CHAR] = &display_char_pl011,
                [DEBUG_POLL_KEY]     = &poll_key_pl011,
                [DEBUG_BREAK_DETECT] = &break_detect_pl011,
            }
        },
    };

    int      opt;
    paddr_t  memsize = TC3_DRAM_SIZE_DEFAULT;

    if (boot_regs[FDT_REG]) {
        fdt_init(boot_regs[FDT_REG]);
        fdt_psci_configure();
    }

    while ((opt = getopt(argc, argv, COMMON_OPTIONS_STRING "m:")) != -1) {
        switch (opt) {
        case 'm':
            memsize = getsize(optarg, NULL);
            break;
        default:
            handle_common_option(opt);
            break;
        }
    }

    select_debug(debug_devices, sizeof(debug_devices));

    kprintf("TC3 FVP startup\n");

    /* Show what EL we are at */
    {
        unsigned long el_val;
        __asm__ __volatile__("mrs %0, CurrentEL" : "=r"(el_val));
        el_val = (el_val >> 2) & 3;
        kprintf("CurrentEL = EL%d\n", (int)el_val);
    }

    /* PSCI reboot */
    psci_call = &psci_smc;
    add_callout(offsetof(struct callout_entry, reboot), &reboot_psci_smc);

    if (cpu_freq == 0) {
        if (fdt_size != 0)
            cpu_freq = fdt_get_cpu_freq();
        if (cpu_freq == 0)
            cpu_freq = DEFAULT_CPU_FREQ;
    }

#if defined(__aarch64__)
    if (timer_freq == 0) {
        timer_freq = aa64_sr_rd32(cntfrq_el0);
        if (timer_freq == 0)
            timer_freq = DEFAULT_CPU_FREQ;
    }
#endif

    kprintf("CPU freq: %u Hz, Timer freq: %u Hz\n", cpu_freq, timer_freq);

    if (fdt_size != 0) {
        init_raminfo_fdt();
        fdt_asinfo();
    } else {
        kprintf("No FDT, adding RAM at 0x%x size 0x%x\n",
                (unsigned)(TC3_DRAM_BASE), (unsigned)(memsize));
        add_ram(TC3_DRAM_BASE, memsize);
    }

    alloc_ram(shdr->ram_paddr, shdr->ram_size, 1);

    hypervisor_init(0);
    init_smp();

    if (shdr->flags1 & STARTUP_HDR_FLAGS1_VIRTUAL)
        init_mmu();

    kprintf("Calling init_intrinfo_tc3...\n");
    init_intrinfo_tc3();
    kprintf("GIC init done\n");

    init_qtime();
    kprintf("qtime done\n");

    init_cacheattr();
    kprintf("cacheattr done\n");

    init_cpuinfo();
    kprintf("cpuinfo done\n");

    /* Show FPU detection results */
    {
        unsigned long pfr0;
        __asm__ __volatile__("mrs %0, id_aa64pfr0_el1" : "=r"(pfr0));
        kprintf("id_aa64pfr0_el1 = 0x%x %x\n",
                (unsigned)(pfr0 >> 32), (unsigned)pfr0);
        kprintf("FP field=%d SIMD field=%d\n",
                (int)((pfr0 >> 16) & 0xf),
                (int)((pfr0 >> 20) & 0xf));
        kprintf("cpu->flags = 0x%x\n", lsp.cpuinfo.p->flags);
    }

    init_hwinfo();
    add_typed_string(_CS_MACHINE, "Arm TC3 FVP");

    kprintf("TC3 startup complete, launching procnto\n");

    init_system_private();
    print_syspage();

    return 0;
}
