# Architecture

## Overview

This project boots QNX 8.0 on the Arm Total Compute 3 (TC3) Fixed Virtual Platform (FVP) with all 8 heterogeneous cores, EL2 VHE hypervisor support, and FDT-based hardware discovery. The TC3 firmware stack (RSE→SCP→TF-A→U-Boot) initializes the hardware, then U-Boot loads the QNX raw binary at its linked address and jumps to it.

## TC3 FVP Hardware Map

| Component | Address | Notes |
|-----------|---------|-------|
| DRAM | 0x80000000 | 2GB (configurable up to 64GB) |
| FDT | 0x80000000 | Placed by TF-A, modified by U-Boot |
| QNX IFS | 0x82000000 | Relocated to preserve FDT |
| GIC-700 GICD | 0x30000000 | GICv3 distributor |
| GIC-700 GICR | 0x30080000 | GICv3 redistributor |
| AP NS UART (PL011) | 0x2A400000 | SPI 63 (IRQ 95), QNX shell console |
| Board UART (PL011) | 0x1c090000 | SPI 37 (IRQ 69), startup boot messages |
| VirtIO block (MMIO) | 0x1c130000 | SPI 204 (IRQ 236), board.virtioblockdevice |
| SMSC LAN91C111 | 0x18000000 | SPI 109 (IRQ 141), board.smsc_91c111 |
| Timer | ARMv8 generic | 100 MHz |

## CPU Topology

| CPU | MPIDR | Subcluster | Core | MIDR |
|-----|-------|------------|------|------|
| 0-1 | 0x000, 0x100 | 0 | Cortex-A520 | 410fd801 |
| 2-5 | 0x200-0x500 | 1 | Cortex-A725 | 410fd870 |
| 6-7 | 0x600, 0x700 | 2 | Cortex-X925 | 410fd850 |

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
    ├── U-Boot (BL33, NS-EL2) — Normal world bootloader
    │       │
    │       ├── U-Boot autoboot: tries fitImage at 0xa0000000
    │       │   (falls through since no fitImage loaded)
    │       │
    │       └── [via expect script] go 0x82000800
    │
    └── QNX startup-tc3 (NS-EL2)
            ├── FDT init from 0x80000000
            ├── VHE hypervisor enable
            ├── SMP: 8 cores via PSCI CPU_ON
            ├── GIC-700 init
            ├── MMU, timer, cpuinfo
            └── procnto-smp-instr
                ├── devb-virtio-fvp → /dev/vblk0 (VirtIO MMIO block)
                │   └── dd → shmem → devb-loopback → ext2 mount /guests
                ├── io-sock (smc0 + vdevpeer-net)
                ├── dhcpcd (DHCP via writable /var/run)
                ├── sshd (port 22, forwarded as 8022)
                └── qvm @/guests/linux/linux-guest.qvmconf
                    └── Linux 6.1 guest (2 vCPU, 256MB, buildroot)
                        ├── GICv3, PL011 UART, arch timer
                        ├── virtio-net (vdevpeer to host)
                        └── virtio-blk (optional, host disk passthrough)
```

## Key Design Decisions

### Raw binary boot via U-Boot `go`

The QNX `.bin` (raw binary from `objcopy`) is loaded directly at its linked address (`0x82000200`) in FVP DRAM via the `--data` parameter. U-Boot just does `go 0x82000800` — no `cp.b`, no ELF parsing. This is the standard QNX U-Boot boot method used by all modern BSPs (NXP i.MX8MP, TI AM62x, etc.).

`bootelf` silently fails on aarch64 for QNX ELF files (known U-Boot bug — no cache cleanup, no EL handling in the 64-bit ELF loader).

### FDT address detection

U-Boot's `go` passes `(argc, argv)` in x0/x1 (C calling convention), not an FDT pointer. `boot_regs[0]` contains `1` (argc), not a valid address. Startup checks if x0 is within DRAM range; if not, uses the known TC3 FDT address `0x80000000` where TF-A places the device tree.

### Hardcoded MPIDR table for SMP

QNX's `psci_cpu_id()` returns the CPU index as-is (0, 1, 2...) which are not valid MPIDRs on TC3. The TC3 MPIDR values (0x0, 0x100, 0x200..0x700) are hardcoded in `board_smp.c` from `tc-base.dtsi`. PSCI CPU_ON is called directly via `psci_smc`.

Note: `fdt_num_cpu()` correctly returns 8 from the FDT, but `fdt_psci_configure()` doesn't match `"arm,psci-1.0"` (only matches `"arm,psci"`). This is harmless — `psci_cpu_on_cmd` stays at `-1` and the default `PSCI_CPU_ON` function ID is used.

### IFS at 0x82000000

The IFS was relocated from `0x80000000` to `0x82000000` to preserve the FDT placed by TF-A at DRAM base. The 32MB gap is sufficient for any FDT.

### Custom startup-tc3 BSP

The stock `startup-armv8_fm` hardcodes GIC addresses for the standard Foundation Model (`GICD=0x2f000000`). TC3's GIC-700 is at `0x30000000`/`0x30080000`. Wrong GIC addresses crash the FVP.

## VirtIO Block Driver (FVP Workaround)

QNX's stock `devb-virtio` hangs on TC3 FVP because the FVP returns `QueueNumMax=256` for ALL queue indices (VirtIO spec requires 0 for non-existent queues). This causes `viod_find_mmio()` to loop forever scanning queues.

`devb-virtio-fvp` works around this by configuring only queue 0 (skipping the scan). Root cause found via `pidin backtrace` → `ntoaarch64-objdump -d` disassembly of the queue scan loop.

## QVM Linux Guest

Linux 6.1.75 (TC3 buildroot) boots under QVM with:
- 2 vCPUs (SMP confirmed), 256MB RAM
- Initramfs loaded at explicit address 0x48000000 (QVM `initrd load` auto-placement has invalid magic bug)
- Console logged to `/dev/shmem/linux-guest.log` via vdev pl011 tee redirect
- vdev virtio-net with vdevpeer for host↔guest networking
- vdev virtio-blk ready for persistent guest rootfs passthrough

## Remaining Work

| Feature | Status | Notes |
|---------|--------|-------|
| Guest networking (vdevpeer) | **Config ready** | vp0 interface needs manual creation after guest boot |
| Guest persistent rootfs | **Config ready** | vdev virtio-blk commented, needs guest-fs.img |
| MPAM | **Not started** | Memory Partitioning and Monitoring (TC3 feature) |
