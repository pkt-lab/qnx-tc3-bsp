# QNX TC3 FVP BSP

Board Support Package for booting QNX 8.0 on the
[Arm Total Compute 3 (TC3)](https://developer.arm.com/Tools%20and%20Software/Fixed%20Virtual%20Platforms)
Fixed Virtual Platform (FVP).

## Features

- **8-core SMP** — all 3 subclusters (2×A520 + 4×A725 + 2×X925) via PSCI
- **EL2 VHE hypervisor** — qvm guest support
- **Networking** — SMSC LAN91C111 driver (ported from FreeBSD), ping + SSH working
- **SSH access** — `ssh -p 8022 root@<host>` (empty password)
- **FDT passthrough** — device tree from TF-A/U-Boot
- **Raw binary boot** — no `cp.b` needed, just `go <entry>`

## Quick Start

```bash
# Prerequisites: QNX SDP 8.0, FVP_TC3, TC3 firmware stack (TC23.1),
#   QNX BSP tree with startup lib built (default: ~/qnx-fvp)
source ~/qnx800/qnxsdp-env.sh

# Build
./scripts/build-startup.sh    # Compile startup-tc3
./scripts/build-ifs.sh        # Build QNX IFS image

# Run (boots to shell with networking + SSH)
./scripts/run-fvp.sh

# Connect via SSH
ssh -p 8022 root@<amd-host-ip>
```

## What Boots

```
QNX localhost 8.0.0 Arm_TC3_FVP aarch64le
CPU:AARCH64 Release:8.0.0  FreeMem:7657MB/7824MB
Processor1-8: 2×A520 + 4×A725 + 2×X925 (100MHz FPU)
smc0: 172.20.51.1 (SMSC LAN91C111, 100baseTX)
sshd: port 22 (forwarded to host:8022)
```

## Repository Structure

```
src/
├── startup-tc3/          # QNX startup driver (GIC-700, SMP, FDT, VHE)
└── devs-smc/             # SMSC LAN91C111 network driver (FreeBSD port)

configs/
└── qnx-hv-host.build    # IFS build file (startup script, binaries, config)

scripts/
├── build-startup.sh      # Build startup-tc3 via QNX BSP tree
├── build-ifs.sh          # Build QNX IFS + raw binary
├── build-guest.sh        # Build qvm guest binary
├── run-fvp.sh            # Launch FVP and boot QNX
└── boot-qnx.exp          # U-Boot automation (expect script)

guests/
├── hello-guest.S          # Bare-metal qvm guest (assembly)
└── hello-guest.qvmconf    # qvm guest configuration

docs/
└── ARCHITECTURE.md        # Design decisions and hardware map
```

## Boot Method

The TC3 firmware stack (RSE → SCP → TF-A → U-Boot) boots normally. The QNX
raw binary is preloaded into FVP DRAM at its linked address. An expect script
waits for the U-Boot prompt and issues `go 0x82000800`.

## Prerequisites

- **QNX SDP 8.0** with BSP startup library built (`~/qnx-fvp`)
- **FVP_TC3** v11.26.16 (Arm Fast Models)
- **TC3 firmware stack** (TC23.1) with virtio_net DTS patch and `simple-bus` root
- **expect** for U-Boot automation

## Network Driver

The SMSC LAN91C111 driver (`devs-smc.so`) is ported from FreeBSD `sys/dev/smc/`
(BSD-2-Clause). It uses the QNX io-sock FreeBSD driver API with minimal changes:
- Added `ofwbus` registration for root-level FDT nodes
- Added `iosock_module_version` symbol
- Uses `mods-phy.so` for PHY detection (smcphy)
- FDT device tree loaded at 0x88000000 via `-u arg` (standard QNX boot)

## License

[Apache License 2.0](LICENSE)

Source code is original work or derived from Apache 2.0 / BSD-2-Clause licensed
components. No proprietary QNX SDP code is included.
