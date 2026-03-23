#!/bin/bash
# Build the QNX IFS (Image Filesystem) for TC3 FVP
#
# Prerequisites:
#   - QNX SDP 8.0 (source qnxsdp-env.sh)
#   - startup-tc3 installed to $QNX_TARGET/aarch64le/boot/sys/
#
# Usage:
#   source ~/qnx800/qnxsdp-env.sh
#   ./build-ifs.sh [output-dir]
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
OUTPUT_DIR="${1:-$REPO_ROOT/output}"
BUILD_FILE="$REPO_ROOT/configs/qnx-hv-host.build"

if [ -z "$QNX_TARGET" ]; then
    echo "ERROR: QNX_TARGET not set. Run: source ~/qnx800/qnxsdp-env.sh"
    exit 1
fi

if [ ! -f "$QNX_TARGET/aarch64le/boot/sys/startup-tc3" ]; then
    echo "ERROR: startup-tc3 not installed. Run build-startup.sh first, then:"
    echo "  cp src/startup-tc3/aarch64/le/startup-tc3 \$QNX_TARGET/aarch64le/boot/sys/"
    exit 1
fi

mkdir -p "$OUTPUT_DIR"

# Copy guest binaries to a location mkifs can find
GUEST_DIR="$REPO_ROOT/guests"
if [ -f "$GUEST_DIR/hello-guest.bin" ]; then
    cp "$GUEST_DIR/hello-guest.bin" "$OUTPUT_DIR/"
fi

echo "Building IFS from $BUILD_FILE..."
MKIFS_PATH="$OUTPUT_DIR:$MKIFS_PATH" mkifs -v "$BUILD_FILE" "$OUTPUT_DIR/qnx-hv-host.ifs" 2>&1 | tail -5

echo "Creating raw binary for BL33..."
ntoaarch64-objcopy -O binary "$OUTPUT_DIR/qnx-hv-host.ifs" "$OUTPUT_DIR/qnx-hv-host.bin"

echo "Entry point:"
ntoaarch64-readelf -h "$OUTPUT_DIR/qnx-hv-host.ifs" | grep 'Entry point'

echo ""
echo "Output files:"
ls -lh "$OUTPUT_DIR/qnx-hv-host.ifs" "$OUTPUT_DIR/qnx-hv-host.bin"
