# QNX TC3 FVP BSP

Open-source Board Support Package (BSP) startup driver for booting QNX on the
[Arm Total Compute 3 (TC3)](https://developer.arm.com/Tools%20and%20Software/Fixed%20Virtual%20Platforms)
Fixed Virtual Platform (FVP).

## What This Is

A custom `startup-tc3` driver that initializes TC3 hardware for QNX:
- **GIC-700** (GICv3/v4) interrupt controller at TC3 addresses
- **PL011 UART** serial console
- **ARMv8 generic timer**
- **Memory map** for TC3 DRAM layout
- **PSCI** for reboot and SMP CPU bringup

This BSP enables QNX 8.0 to boot to an interactive shell on the FVP_TC3 simulator.

## Verified Boot Output

```
TC3 FVP startup
CurrentEL = EL2
CPU freq: 100000000 Hz, Timer freq: 100000000 Hz
ARM GIC-?00 r3p0, arch v4.0 detected
FP field=1 SIMD field=1
cpu->flags = 0xc0008c62
TC3 startup complete, launching procnto
Loading IFS...done
=== QNX 8.0 on FVP_TC3 ===
```

```
[QNX-FVP]# pidin
     pid tid name                         prio STATE
       1   1 /proc/boot/procnto-smp-instr   0f READY
       3   1 sbin/devc-serpl011            10r RECEIVE
       4   1 bin/slogger2                  10r RECEIVE
       5   1 bin/ksh                       10r SIGSUSPEND
```

## TC3 Hardware Map

| Device | Address | Notes |
|--------|---------|-------|
| GIC-700 GICD | `0x30000000` | GICv3 distributor |
| GIC-700 GICR | `0x30080000` | GICv3 redistributor |
| Board PL011 UART | `0x1c090000` | Startup console (SPI 37) |
| AP NS PL011 UART | `0x2A400000` | U-Boot/shell console (SPI 63) |
| DRAM | `0x80000000` | 2 GB |
| CPUs | 8 cores | 2x Cortex-A520 + 4x Cortex-A725 + 2x Cortex-X925 |

## Quick Start

```bash
# Prerequisites: QNX SDP 8.0, FVP_TC3, TC3 firmware stack (TC23.1),
#   QNX BSP tree with startup lib built (default: ~/qnx-fvp)
source ~/qnx800/qnxsdp-env.sh

# Build (syncs source into BSP tree, builds, installs to $QNX_TARGET)
./scripts/build-startup.sh
./scripts/build-ifs.sh

# Run
./scripts/run-fvp.sh
```

## Repository Structure

```
src/startup-tc3/
├── main.c                 # Hardware init: UART, GIC, RAM, timer, FPU
├── board_smp.c            # SMP CPU bringup via PSCI
├── init_asinfo.c          # Address space info
├── pinfo.mk               # Build metadata
├── Makefile
└── aarch64/
    ├── init_intrinfo.c    # GIC-700 interrupt controller init
    ├── _start.S           # Entry point: FPU enable, jump to cstart
    ├── Makefile
    └── le/Makefile

configs/
└── qnx-hv-host.build     # mkifs build file

scripts/
├── build-startup.sh       # Build startup-tc3 binary
├── build-ifs.sh           # Build QNX IFS image
├── run-fvp.sh             # Launch FVP and boot QNX
└── boot-qnx.exp           # expect script for U-Boot automation
```

## Boot Method

The TC3 firmware stack (RSE → SCP → TF-A → U-Boot) boots normally. The QNX IFS
ELF is preloaded into FVP DRAM via the `--data` parameter. An expect script
automates U-Boot to copy the IFS LOAD segment to its linked address and jump to
the entry point.

**Important**: Do not load the Linux fitImage alongside the QNX IFS. U-Boot will
auto-boot Linux instead of dropping to the prompt.

## Prerequisites

- **QNX SDP 8.0** with `startup-armv8_fm` BSP headers installed
- **FVP_TC3** v11.26.16 (Arm Fast Models)
- **TC3 firmware stack** (TC23.1) built with U-Boot as BL33
- **expect** for automated U-Boot interaction

See [docs/SETUP.md](docs/SETUP.md) for detailed environment setup.

## License

[Apache License 2.0](LICENSE)

All source code in this repository is original work or derived from
Apache 2.0 licensed components. No proprietary QNX SDP code is included.
The QNX SDP runtime libraries (`procnto`, `libc`, etc.) required at runtime
are not part of this repository and must be obtained separately under their
own license terms.
