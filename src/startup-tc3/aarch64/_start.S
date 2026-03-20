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
 * _start.S for TC3 FVP
 *
 * Entered from TF-A BL31 at EL2 (via U-Boot go at EL1).
 * Enable FPU access, then jump to cstart.
 */

    .text
    .align 2

    .extern cstart
    .global _start

_start:
    /*
     * X0-X3 may contain boot info. Do NOT modify before cstart.
     *
     * Enable FPU/NEON access at EL1:
     * CPACR_EL1.FPEN [21:20] = 0b11 (no trap FP/NEON at EL0/EL1)
     */
    mrs     x9, CurrentEL
    lsr     x9, x9, #2

    cmp     x9, #1                 // EL1?
    bne     1f
    mrs     x10, cpacr_el1
    orr     x10, x10, #(3 << 20)  // FPEN = 0b11
    msr     cpacr_el1, x10
    isb
    b       2f

1:  // EL2
    mrs     x10, cptr_el2
    bic     x10, x10, #(1 << 10)  // Clear TFP bit (don't trap FP)
    msr     cptr_el2, x10
    isb

2:
    b       cstart
