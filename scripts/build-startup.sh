#!/bin/bash
# Build the startup-tc3 BSP driver for QNX on Arm TC3 FVP
#
# Prerequisites:
#   - QNX SDP 8.0 installed (source qnxsdp-env.sh)
#   - BSP_hyp-guest-arm unpacked (for startup lib headers)
#
# Usage:
#   source ~/qnx800/qnxsdp-env.sh
#   ./build-startup.sh
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
BSP_DIR="$REPO_ROOT/src/startup-tc3"

if [ -z "$QNX_TARGET" ]; then
    echo "ERROR: QNX_TARGET not set. Run: source ~/qnx800/qnxsdp-env.sh"
    exit 1
fi

# Check if startup lib headers are installed
if [ ! -f "$QNX_TARGET/usr/include/startup.h" ]; then
    echo "ERROR: startup.h not found in QNX_TARGET."
    echo "You need to build the startup library from the BSP first:"
    echo "  cd ~/qnx-fvp/src/hardware/startup/lib && make install"
    exit 1
fi

echo "Building startup-tc3..."
cd "$BSP_DIR/aarch64/le"
make clean 2>/dev/null || true
make

BINARY="$BSP_DIR/aarch64/le/startup-tc3"
if [ -f "$BINARY" ]; then
    echo "SUCCESS: Built $BINARY"
    echo "Install with: cp $BINARY \$QNX_TARGET/aarch64le/boot/sys/startup-tc3"
    file "$BINARY"
else
    echo "FAILED: startup-tc3 not produced"
    exit 1
fi
