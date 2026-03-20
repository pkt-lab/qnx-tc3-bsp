#!/bin/bash
# Build the startup-tc3 BSP driver for QNX on Arm TC3 FVP
#
# This script syncs source files into the QNX BSP tree (which provides
# the startup library, headers, and build system), builds there, and
# copies the binary back.
#
# Prerequisites:
#   - QNX SDP 8.0 installed (source qnxsdp-env.sh)
#   - QNX BSP tree with startup library built (BSP_ROOT)
#
# Usage:
#   source ~/qnx800/qnxsdp-env.sh
#   ./build-startup.sh
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
SRC_DIR="$REPO_ROOT/src/startup-tc3"

# BSP tree containing startup library and build system
BSP_ROOT="${BSP_ROOT:-$HOME/qnx-fvp}"
BSP_TC3="$BSP_ROOT/src/hardware/startup/boards/tc3"

if [ -z "$QNX_TARGET" ]; then
    echo "ERROR: QNX_TARGET not set. Run: source ~/qnx800/qnxsdp-env.sh"
    exit 1
fi

if [ ! -d "$BSP_TC3" ]; then
    echo "ERROR: BSP board directory not found: $BSP_TC3"
    echo "Set BSP_ROOT to the QNX BSP tree (default: ~/qnx-fvp)"
    exit 1
fi

if [ ! -f "$BSP_ROOT/install/usr/include/startup.h" ]; then
    echo "ERROR: startup.h not found in BSP install dir."
    echo "Build the startup library first:"
    echo "  cd $BSP_ROOT/src/hardware/startup/lib && make install"
    exit 1
fi

# Sync source files into BSP tree
echo "Syncing source to BSP tree..."
cp "$SRC_DIR"/main.c "$SRC_DIR"/board_smp.c "$SRC_DIR"/init_asinfo.c "$SRC_DIR"/pinfo.mk "$BSP_TC3/"
cp "$SRC_DIR"/aarch64/init_intrinfo.c "$BSP_TC3/aarch64/"
cp "$SRC_DIR"/aarch64/_start.S "$BSP_TC3/aarch64/"

# Build
echo "Building startup-tc3..."
cd "$BSP_TC3/aarch64/le"
make clean 2>/dev/null || true
make

BINARY="$BSP_TC3/aarch64/le/startup-tc3"
if [ ! -f "$BINARY" ]; then
    echo "FAILED: startup-tc3 not produced"
    exit 1
fi

# Copy binary back to repo and install to QNX_TARGET
mkdir -p "$REPO_ROOT/output"
cp "$BINARY" "$REPO_ROOT/output/startup-tc3"
cp "$BINARY" "$QNX_TARGET/aarch64le/boot/sys/startup-tc3"

echo "SUCCESS: startup-tc3 built and installed"
echo "  Output: $REPO_ROOT/output/startup-tc3"
echo "  Installed: \$QNX_TARGET/aarch64le/boot/sys/startup-tc3"
file "$BINARY"
