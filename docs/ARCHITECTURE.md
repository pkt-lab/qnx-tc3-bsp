# Architecture

## Overview

This project boots QNX 8.0 on the Arm Total Compute 3 (TC3) Fixed Virtual Platform (FVP). Since no QNX BSP exists for TC3, we created a custom `startup-tc3` driver and use a hybrid boot approach: the TC3 firmware stack (RSE→SCP→TF-A→U-Boot) initializes the hardware, then U-Boot loads the QNX IFS into memory and jumps to it.

## TC3 FVP Hardware Map

| Component | Address | Notes |
|-----------|---------|-------|
| DRAM | 0x80000000 | 2GB (configurable up to 64GB) |
| GIC-700 GICD | 0x30000000 | GICv3 distributor |
| GIC-700 GICR | 0x30080000 | GICv3 redistributor |
| AP NS UART (PL011) | 0x2A400000 | SPI 63 (IRQ 95), used by U-Boot/Linux |
| AP Secure UART | 0x2A410000 | Used by TF-A |
| Board UART (PL011) | 0x1c090000 | SPI 37 (IRQ 69), used by QNX startup minidriver |
| CPUs | 8 cores | 2xA520 + 4xA725 + 2xX925 in 3 subclusters |
| Timer | ARMv8 generic | PPIs 13, 14, 11, 10 |

## Boot Flow

```
FVP_TC3 Power On
    │
    ├── RSE (Cortex-M55) — Root of Trust, provisions keys, loads SCP+AP images
    ├── SCP (Cortex-M85) — Power/clock management, releases AP cores
    │
    ├── TF-A BL1 → BL2 → BL31 (EL3)
    ├── Hafnium (S-EL2) — Secure Partition Manager
    ├── OP-TEE + Trusted Services (S-EL1/S-EL0)
    │
    ├── U-Boot (BL33, NS-EL2→EL1) — Normal world bootloader
    │       │
    │       ├── U-Boot autoboot: tries fitImage at 0xa0000000
    │       │   (falls through since QNX IFS isn't a fitImage)
    │       │
    │       └── [via expect script]
    │           ├── cp.b 0xb0000200 0x80000200 0x4c5000  (copy QNX LOAD segment)
    │           └── go 0x80000800                          (jump to QNX entry)
    │
    └── QNX startup-tc3 (NS-EL1)
            ├── select_debug() → PL011 at 0x1c090000 (board UART)
            ├── add_ram(0x80000000, 2GB)
            ├── init_mmu()
            ├── init_intrinfo_tc3() → GIC-700 at 0x30000000
            ├── init_qtime(), init_cpuinfo()
            └── procnto-smp-instr → QNX shell
```

## Key Design Decisions

### Why use U-Boot as intermediate loader?

The TC3 firmware stack uses RSE to cryptographically verify all boot images (BL1, BL2, BL31, BL33). Replacing U-Boot as BL33 requires rebuilding RSE with new image hashes — a full rebuild of TF-A + RSE + flash-image. Using U-Boot as the loader avoids this: the firmware chain boots normally with Linux images, then we load QNX via U-Boot's memory commands.

### Why not use bootelf?

U-Boot's `bootelf` command on aarch64 silently fails for QNX ELF files. The `cp.b` + `go` approach manually copies the ELF LOAD segment to its linked address and jumps to the entry point. This works reliably.

### Why does QNX run at EL1 (not EL2)?

U-Boot's `go` command jumps at the current exception level. On TC3 with Hafnium as S-EL2 SPM, the normal world runs at EL1 (not EL2). QNX startup detects this and reports "Hypervisor support disabled". This means `qvm` (QNX hypervisor) cannot run. To enable EL2, we would need to either disable Hafnium or configure it to allow NS-EL2 access.

### Why the custom startup-tc3 BSP?

The stock `startup-armv8_fm` hardcodes GIC addresses for the standard Foundation Model (`GICD=0x2f000000`, `GICR=0x2f100000`). TC3's GIC-700 is at different addresses (`0x30000000`, `0x30080000`). Accessing the wrong GIC address crashes the FVP model. Our `startup-tc3` uses the correct TC3 addresses.

## Current Status (2026-03-19)

| Feature | Status | Notes |
|---------|--------|-------|
| TC3 firmware boot (RSE→SCP→TF-A→U-Boot) | **Working** | Full chain boots |
| QNX startup-tc3 on FVP | **Partial** | Boots to MMU init, GIC init, IFS load |
| Serial output (board UART) | **Working** | 0x1c090000, startup minidriver output visible |
| SMP (secondary CPUs) | **Not working** | PSCI from U-Boot's `go` doesn't work |
| QNX shell (procnto + ksh) | **Not yet** | Blocked by GIC/SMP issues |
| qvm (hypervisor) | **Not yet** | Requires EL2 (currently EL1) |

## Known Issues

1. **SMP startup fails**: `board_smp_start()` uses PSCI but U-Boot's `go` doesn't maintain PSCI context properly. Need to either use single-CPU mode or find a way to invoke PSCI from QNX.

2. **EL1 only**: U-Boot drops to EL1 when using `go`. QNX hypervisor (qvm) requires EL2. Solution: replace U-Boot as BL33 with proper RSE rebuild, or disable Hafnium.

3. **No FDT passthrough**: The FDT from TF-A is at 0x80000000 but gets overwritten when we copy the QNX IFS. Startup falls back to hardcoded RAM/device info.
