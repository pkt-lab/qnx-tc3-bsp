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
 * TC3 FVP GIC-700 (GICv3) interrupt controller initialization
 * GICD: 0x30000000, GICR: 0x30080000
 */

#include <startup.h>
#include <aarch64/gic.h>

/* TC3 FVP GIC-700 addresses (from tc-base.dtsi) */
#define TC3_GICD_PADDR    0x30000000
#define TC3_GICR_PADDR    0x30080000
#define TC3_GICT_PADDR    0x00000000   /* no ITS for now */
#define TC3_GICC_PADDR    0x2c000000   /* CPU interface (for MMIO fallback) */

void
init_intrinfo_tc3(void)
{
    kprintf("TC3 GIC init: GICD=0x%x GICR=0x%x\n", TC3_GICD_PADDR, TC3_GICR_PADDR);

    gic_v3_set_paddr(TC3_GICD_PADDR, TC3_GICR_PADDR, TC3_GICT_PADDR);
    gic_v3_use_mm_reg_callouts(TC3_GICC_PADDR, 0);
    gic_v3_initialize();

    struct smp_entry *const smp = lsp.smp.p;
    if (smp != NULL) {
        smp->send_ipi = (void *)gic_sendipi;
    }
}
