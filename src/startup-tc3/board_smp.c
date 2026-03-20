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
#include <startup.h>
#if defined(__aarch64__)
#include <aarch64/psci.h>
#else
#include <arm/psci.h>
#endif

/*
 * TC3 FVP CPU MPIDR values (from tc-base.dtsi):
 *   subcluster0: 2x Cortex-A520 (cpu@0, cpu@100)
 *   subcluster1: 4x Cortex-A725 (cpu@200..cpu@500)
 *   subcluster2: 2x Cortex-X925 (cpu@600, cpu@700)
 */
static const unsigned long tc3_mpidr[] = {
    0x0000, 0x0100,             /* subcluster0: A520 */
    0x0200, 0x0300, 0x0400, 0x0500,  /* subcluster1: A725 */
    0x0600, 0x0700,             /* subcluster2: X925 */
};
#define TC3_NUM_CPUS (sizeof(tc3_mpidr) / sizeof(tc3_mpidr[0]))

unsigned
board_smp_num_cpu()
{
    unsigned num = fdt_num_cpu();
    if (num == 0) {
        if (max_cpus != ~0u) {
            num = (max_cpus <= TC3_NUM_CPUS) ? max_cpus : TC3_NUM_CPUS;
        } else {
            num = TC3_NUM_CPUS;
        }
    }
    kprintf("SMP: num_cpus=%u\n", num);
    return num;
}

void
board_smp_init(struct smp_entry *smp, unsigned num_cpus)
{
    /* TC3 uses GIC-700 (GICv3); IPI callout set by init_intrinfo_tc3() */
}

int
board_smp_start(unsigned cpu, void (*start)(void))
{
    unsigned long mpidr;

    if (cpu < TC3_NUM_CPUS) {
        mpidr = tc3_mpidr[cpu];
    } else {
        mpidr = psci_cpu_id(cpu);
    }

    kprintf("SMP: starting CPU %u MPIDR=0x%lx entry=%p\n", cpu, mpidr, start);

    /* Ensure PSCI is using SMC and the correct CPU_ON function ID */
    psci_call = &psci_smc;
    long rc = (*psci_call)(PSCI_CPU_ON, mpidr, (uintptr_t)start, 0);
    kprintf("SMP: PSCI CPU_ON returned %ld\n", rc);

    if (rc != PSCI_SUCCESS) {
        kprintf("SMP: CPU %u failed (PSCI rc=%ld)\n", cpu, rc);
        return 0;
    }
    return 1;
}

unsigned
board_smp_adjust_num(unsigned cpu)
{
    return cpu;
}
