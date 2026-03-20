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

unsigned
board_smp_num_cpu()
{
    unsigned num = fdt_num_cpu();
    if (num == 0) {
        if (max_cpus != ~0u) {
            num = max_cpus;
        } else {
            /* TC3 default: 8 cores */
            num = 8;
        }
    }
    return num;
}

void
board_smp_init(struct smp_entry *smp, unsigned num_cpus)
{
    /* TC3 uses GICv3, IPI via GIC redistributor */
    smp->send_ipi = (void *)&sendipi_gic_v2;
}

int
board_smp_start(unsigned cpu, void (*start)(void))
{
    /* Always use PSCI (TF-A provides it) */
    return psci_smp_start(cpu, start);
}

unsigned
board_smp_adjust_num(unsigned cpu)
{
    return cpu;
}
