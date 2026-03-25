# QNX TC3 FVP BSP

Board Support Package for booting QNX 8.0 on the
[Arm Total Compute 3 (TC3)](https://developer.arm.com/Tools%20and%20Software/Fixed%20Virtual%20Platforms)
Fixed Virtual Platform (FVP).

## Features

- **8-core SMP** — all 3 subclusters (2×A520 + 4×A725 + 2×X925) via PSCI
- **EL2 VHE hypervisor** — QVM with Linux guest (buildroot boots to shell)
- **VirtIO block storage** — custom MMIO driver for FVP (workaround for FVP queue bug)
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
ssh -p 8022 root@<host-ip>
```

## Linux Guest (QVM)

Boot a Linux guest under QNX QVM hypervisor using the TC3 buildroot kernel and rootfs.

### Build the TC3 Linux stack

Follow the [TC3 User Guide](https://totalcompute.docs.arm.com/en/totalcompute/totalcompute/tc3/user-guide.html):

```bash
# Download TC3 source (one-time setup)
export PLATFORM=tc3 FILESYSTEM=buildroot TC_TARGET_FLAVOR=fvp
mkdir tc3-workspace && cd tc3-workspace
repo init -u https://gitlab.arm.com/arm-reference-solutions/arm-reference-solutions-manifest \
    -m tc3_a14.xml -b refs/tags/TC23.1 -g bsp
repo sync -j6
cd build-scripts && ./setup.sh

# Build kernel + buildroot rootfs
./run_docker.sh ./build-linux.sh build
./run_docker.sh ./build-buildroot.sh build
```

Output files:
- `output/tc3/buildroot/fvp/tmp_build/linux/arch/arm64/boot/Image` — Linux kernel
- `output/tc3/buildroot/fvp/tmp_build/buildroot/images/rootfs.cpio.gz` — Buildroot rootfs

### Build host disk image and launch guest

```bash
# Create host-disk.img with guest kernel + rootfs (no QNX SDP needed)
LINUX_IMAGE=<tc3-workspace>/output/.../Image \
ROOTFS_CPIO=<tc3-workspace>/output/.../rootfs.cpio.gz \
./scripts/build-host-disk.sh

# Boot QNX + launch Linux guest
./scripts/run-fvp.sh
ssh -p 8022 root@<host-ip>
qvm @/guests/linux/linux-guest.qvmconf &
```

## Repository Structure

```
src/        # QNX drivers (startup-tc3, devs-smc, devb-virtio-fvp)
configs/    # IFS build file (qnx-hv-host.build)
dts/        # TF-A device tree patches for QNX
guests/     # QVM guest configs and binaries (hello-guest, linux)
scripts/    # Build and run scripts
tools/      # Diagnostic utilities (virtio-probe)
docs/       # Architecture documentation
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
