#!/bin/bash
# Build the minimal bare-metal guest binary for qvm
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
GUEST_DIR="$REPO_ROOT/guests"

# Use QNX cross-toolchain if available, else system aarch64 gcc
if command -v ntoaarch64-gcc >/dev/null 2>&1; then
    CC=ntoaarch64-gcc
    OBJCOPY=ntoaarch64-objcopy
elif command -v aarch64-none-elf-gcc >/dev/null 2>&1; then
    CC=aarch64-none-elf-gcc
    OBJCOPY=aarch64-none-elf-objcopy
else
    echo "ERROR: No aarch64 cross-compiler found"
    exit 1
fi

echo "Building hello-guest with $CC..."
# Build as position-independent, linked at a low address
# qvm's 'load' places it at the ram base from .qvmconf
$CC -nostdlib -nostartfiles -Wl,--section-start=.text=0x1000 -Wl,--no-dynamic-linker \
    -o "$GUEST_DIR/hello-guest.elf" "$GUEST_DIR/hello-guest.S"
$OBJCOPY -O binary "$GUEST_DIR/hello-guest.elf" "$GUEST_DIR/hello-guest.bin"

echo "SUCCESS: $GUEST_DIR/hello-guest.bin"
ls -l "$GUEST_DIR/hello-guest.bin"
