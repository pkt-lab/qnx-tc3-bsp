#!/bin/bash
# Build host-disk.img containing Linux guest files for QVM
#
# Creates an ext2 disk image with:
#   linux/Image             - Linux kernel (aarch64)
#   linux/rootfs.cpio.gz    - Initramfs rootfs (buildroot)
#   linux/linux-guest.dtb   - Guest device tree
#   linux/linux-guest.qvmconf - QVM configuration
#
# Prerequisites:
#   Build the TC3 Linux stack (kernel + buildroot rootfs):
#     https://totalcompute.docs.arm.com/en/totalcompute/totalcompute/tc3/user-guide.html
#
#   Quick steps:
#     export PLATFORM=tc3 FILESYSTEM=buildroot TC_TARGET_FLAVOR=fvp
#     cd tc3-workspace/build-scripts
#     ./run_docker.sh ./build-linux.sh build
#     ./run_docker.sh ./build-buildroot.sh build
#
#   Output files used by this script:
#     <TC3_WORKSPACE>/output/tc3/buildroot/fvp/tmp_build/linux/arch/arm64/boot/Image
#     <TC3_WORKSPACE>/output/tc3/buildroot/fvp/tmp_build/buildroot/images/rootfs.cpio.gz
#
# Usage:
#   ./scripts/build-host-disk.sh [output-dir]
#
#   Override source files with env vars:
#     LINUX_IMAGE=/path/to/Image ROOTFS_CPIO=/path/to/rootfs.cpio.gz ./scripts/build-host-disk.sh
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
OUTPUT_DIR="${1:-$REPO_ROOT/output}"
GUESTS_DIR="$REPO_ROOT/guests/linux"
TC3_WS="${TC3_WORKSPACE:-$HOME/tc3-workspace}"

# Source files (override with LINUX_IMAGE= and ROOTFS_CPIO= env vars)
LINUX_IMAGE="${LINUX_IMAGE:-$TC3_WS/output/tc3/buildroot/fvp/tmp_build/linux/arch/arm64/boot/Image}"
ROOTFS_CPIO="${ROOTFS_CPIO:-$TC3_WS/output/tc3/buildroot/fvp/tmp_build/buildroot/images/rootfs.cpio.gz}"
GUEST_DTS="$GUESTS_DIR/linux-guest.dts"
GUEST_QVMCONF="$GUESTS_DIR/linux-guest.qvmconf"

# Check prerequisites
for f in "$LINUX_IMAGE" "$ROOTFS_CPIO" "$GUEST_DTS" "$GUEST_QVMCONF"; do
    if [ ! -f "$f" ]; then
        echo "ERROR: Required file not found: $f"
        exit 1
    fi
done

if ! command -v dtc >/dev/null 2>&1; then
    echo "ERROR: dtc (device tree compiler) not found"
    exit 1
fi

mkdir -p "$OUTPUT_DIR"

# Compile guest DTB
echo "Compiling guest DTB..."
dtc -I dts -O dtb -o "$OUTPUT_DIR/linux-guest.dtb" "$GUEST_DTS"

# Calculate image size: kernel + rootfs + dtb + qvmconf + overhead
KERNEL_SIZE=$(stat -c%s "$LINUX_IMAGE")
ROOTFS_SIZE=$(stat -c%s "$ROOTFS_CPIO")
DTB_SIZE=$(stat -c%s "$OUTPUT_DIR/linux-guest.dtb")
TOTAL_SIZE=$(( (KERNEL_SIZE + ROOTFS_SIZE + DTB_SIZE + 20*1024*1024) ))  # +20MB overhead for ext2
# Round up to 4MB boundary
IMG_SIZE_MB=$(( (TOTAL_SIZE + 4*1024*1024 - 1) / (1024*1024) ))
IMG_SIZE_MB=$(( (IMG_SIZE_MB + 3) / 4 * 4 ))  # round to 4MB

echo "Image size: ${IMG_SIZE_MB}MB (kernel=${KERNEL_SIZE}, rootfs=${ROOTFS_SIZE})"

HOST_DISK="$OUTPUT_DIR/host-disk.img"
echo "Creating host-disk.img (${IMG_SIZE_MB}MB)..."
truncate -s "${IMG_SIZE_MB}M" "$HOST_DISK"
mkfs.ext2 -qF "$HOST_DISK"

# Mount, copy files, and ensure cleanup on error
MOUNT_DIR=$(mktemp -d)
cleanup() { sudo umount "$MOUNT_DIR" 2>/dev/null; rmdir "$MOUNT_DIR" 2>/dev/null; }
trap cleanup EXIT
sudo mount -o loop "$HOST_DISK" "$MOUNT_DIR"

sudo mkdir -p "$MOUNT_DIR/linux"
sudo cp "$LINUX_IMAGE" "$MOUNT_DIR/linux/Image"
sudo cp "$ROOTFS_CPIO" "$MOUNT_DIR/linux/$(basename "$ROOTFS_CPIO")"
sudo cp "$OUTPUT_DIR/linux-guest.dtb" "$MOUNT_DIR/linux/linux-guest.dtb"
sudo cp "$GUEST_QVMCONF" "$MOUNT_DIR/linux/linux-guest.qvmconf"
# Copy additional guest configs and tools if they exist
for f in "$GUESTS_DIR"/linux-guest2.qvmconf "$GUESTS_DIR"/check-cntvct.sh; do
    [ -f "$f" ] && sudo cp "$f" "$MOUNT_DIR/linux/"
done

echo "Contents:"
ls -lh "$MOUNT_DIR/linux/"

sudo umount "$MOUNT_DIR"
rmdir "$MOUNT_DIR"
trap - EXIT

echo ""
echo "Output: $HOST_DISK (${IMG_SIZE_MB}MB)"
echo ""
echo "To use with FVP, update run-fvp.sh:"
echo "  -C board.virtioblockdevice.image_path=$HOST_DISK"
