# QNX TC3 FVP BSP

Board Support Package for booting QNX 8.0 on the
[Arm Total Compute 3 (TC3)](https://developer.arm.com/Tools%20and%20Software/Fixed%20Virtual%20Platforms)
Fixed Virtual Platform (FVP).

## Features

- **8-core SMP** — all 3 subclusters (2×A520 + 4×A725 + 2×X925) via PSCI
- **EL2 VHE hypervisor** — qvm guest support
- **Networking** — SMSC LAN91C111 driver (ported from FreeBSD), DHCP + SSH
- **SSH access** — `ssh -p 8022 root@<host>` (empty password)
- **FDT passthrough** — device tree via standard `-u arg` boot
- **Raw binary boot** — `go <entry> <fdt_addr>` (same as TI J784S4 BSP)

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

## Repository Structure

```
src/
├── startup-tc3/          # QNX startup driver (GIC-700, SMP, FDT, VHE)
└── devs-smc/             # SMSC LAN91C111 network driver (FreeBSD port)

configs/
└── qnx-hv-host.build    # IFS build file (startup script, binaries, config)

dts/
├── tc3-fdt-qnx.patch    # TF-A DTS patches (simple-bus, virtio_net)
└── README.md             # How to apply and rebuild firmware

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
raw binary and DTB are preloaded into FVP DRAM. An expect script waits for
the U-Boot prompt and issues `go 0x82000800 0x88000000` (entry + FDT address).

The DTB is loaded at 0x88000000 (128MB offset from DRAM base), following the
same pattern as TI J784S4 and other QNX BSPs. Startup uses `-u arg` to parse
the FDT address from U-Boot's `go` arguments.

## Prerequisites

- **QNX SDP 8.0** with BSP startup library built (`~/qnx-fvp`)
- **FVP_TC3** v11.26.16 (Arm Fast Models)
- **TC3 firmware stack** (TC23.1) with DTS patches applied (see [dts/README.md](dts/README.md))
- **expect** for U-Boot automation

## Network Driver

The SMSC LAN91C111 driver (`devs-smc.so`) is ported from FreeBSD `sys/dev/smc/`
(BSD-2-Clause). It uses the QNX io-sock FreeBSD driver API with minimal changes:
- Added `ofwbus` registration for root-level FDT nodes
- Added `iosock_module_version` symbol (required by io-sock module loader)
- Uses `mods-phy.so` for PHY detection (smcphy)

## License

[Apache License 2.0](LICENSE)

Source code is original work or derived from Apache 2.0 / BSD-2-Clause licensed
components. No proprietary QNX SDP code is included.
