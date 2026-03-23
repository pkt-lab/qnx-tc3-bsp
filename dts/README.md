# TC3 Device Tree Patches

The TC3 firmware's device tree (DTS) needs two patches for QNX:

1. **`simple-bus` + `ranges`** on root node — required by QNX `mods-fdt.so`
2. **`virtio_net` node** — adds virtio-net device (optional, not used by current driver)

## Getting the TC3 Firmware Source

Follow the [Total Compute User Guide](https://totalcompute.docs.arm.com/en/totalcompute/totalcompute/tc3/user-guide.html#download-the-source-code-and-build):

```bash
# Prerequisites
export PLATFORM=tc3
export FILESYSTEM=buildroot
export TC_TARGET_FLAVOR=fvp
export TC_BRANCH=refs/tags/TC23.1

# Download source (requires repo tool)
mkdir tc3-workspace && cd tc3-workspace
repo init -u https://gitlab.arm.com/arm-reference-solutions/arm-reference-solutions-manifest \
    -m tc3_a14.xml -b ${TC_BRANCH} -g bsp
repo sync -j6

# Setup (downloads toolchains, creates Docker image)
cd build-scripts
./setup.sh
```

## Applying DTS Patches

```bash
cd tc3-workspace/src/trusted-firmware-a
git apply /path/to/qnx-tc3-bsp/dts/tc3-fdt-qnx.patch
```

## Building Firmware

Build all firmware components (uses Docker):

```bash
cd tc3-workspace/build-scripts
./run_docker.sh ./build-all.sh build
```

Or build individually on the host (if Docker has glibc mismatch):

```bash
cd tc3-workspace
source tc-venv/bin/activate
export PATH=$HOME/tc3-workspace/tools/arm-gnu-toolchain-13.2.Rel1-x86_64-aarch64-none-elf/bin:\
$HOME/tc3-workspace/tools/arm-gnu-toolchain-13.2.Rel1-x86_64-arm-none-eabi/bin:\
$HOME/tc3-workspace/tools/clang+llvm-15.0.6-x86_64-linux-gnu-ubuntu-18.04/bin:\
$HOME/tc3-workspace/tools/cmake-3.22.4-linux-x86_64/bin:$PATH

# Build (order matters)
./build-scripts/build-scp.sh -f buildroot -p tc3 -t fvp -g swr build
./build-scripts/build-hafnium.sh -f buildroot -p tc3 -t fvp -g swr build
./build-scripts/build-trusted-services.sh -f buildroot -p tc3 -t fvp -g swr build
./build-scripts/build-optee-os.sh -f buildroot -p tc3 -t fvp -g swr build
./build-scripts/build-u-boot.sh -f buildroot -p tc3 -t fvp -g swr build
./build-scripts/build-tfa.sh -f buildroot -p tc3 -t fvp -g swr build
./build-scripts/build-rse.sh -f buildroot -p tc3 -t fvp -g swr build

# Deploy (order matters — RSE signs SCP using TF-A BL1)
mkdir -p output/tc3/buildroot/fvp/deploy
./build-scripts/build-scp.sh -f buildroot -p tc3 -t fvp -g swr deploy
./build-scripts/build-tfa.sh -f buildroot -p tc3 -t fvp -g swr deploy
./build-scripts/build-rse.sh -f buildroot -p tc3 -t fvp -g swr deploy
./build-scripts/build-flash-image.sh -f buildroot -p tc3 -t fvp -g swr deploy
```

Output firmware images are in `tc3-workspace/output/tc3/buildroot/fvp/deploy/`.

## What the Patches Change

**`tc-base.dtsi`** — root node:
```diff
-  compatible = "arm,tc";
+  compatible = "arm,tc", "simple-bus";
+  ranges;
```

**`tc3.dts`** — add virtio-net defines:
```diff
+  #define VIRTIO_NET_ADDR  1c150000
+  #define VIRTIO_NET_INT   205
```

**`tc-fvp.dtsi`** — add virtio-net node:
```diff
+  virtio_net@VIRTIO_NET_ADDR {
+    compatible = "virtio,mmio";
+    reg = <0x0 ADDRESSIFY(VIRTIO_NET_ADDR) 0x0 0x200>;
+    interrupts = <GIC_SPI VIRTIO_NET_INT IRQ_TYPE_LEVEL_HIGH 0>;
+  };
```
