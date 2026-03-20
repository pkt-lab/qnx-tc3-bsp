#!/bin/bash
# Launch FVP_TC3 with QNX IFS preloaded in DRAM
# U-Boot boots, fails to find Linux fitImage, drops to prompt
# expect script then copies QNX IFS to its linked address and jumps to it
# IMPORTANT: Do NOT load the Linux fitImage (tc-fitImage.bin) — U-Boot
# will auto-boot Linux instead of dropping to the prompt
#
# Prerequisites:
#   - FVP_TC3 installed at ~/FVP_TC3/
#   - TC3 firmware stack built at ~/tc3-workspace/ (Linux buildroot)
#   - QNX IFS ELF built (qnx-hv-host.ifs)
#
# Usage:
#   ./run-fvp.sh [path-to-qnx-ifs]
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

QNX_IFS="${1:-$REPO_ROOT/output/qnx-hv-host.ifs}"
FVP_BIN="${FVP_BIN:-$HOME/FVP_TC3/models/Linux64_GCC-9.3/FVP_TC3}"
DEPLOY="${DEPLOY:-$HOME/tc3-workspace/output/tc3/buildroot/fvp/deploy}"
LOG_DIR="/tmp/qnx-fvp-logs"

if [ ! -f "$QNX_IFS" ]; then
    echo "ERROR: QNX IFS not found: $QNX_IFS"
    echo "Build it first: ./build-ifs.sh"
    exit 1
fi

if [ ! -f "$FVP_BIN" ]; then
    echo "ERROR: FVP_TC3 not found: $FVP_BIN"
    exit 1
fi

if [ ! -d "$DEPLOY" ]; then
    echo "ERROR: TC3 firmware deploy dir not found: $DEPLOY"
    echo "Build the TC3 Linux stack first (see docs/SETUP.md)"
    exit 1
fi

# Kill any existing FVP
pkill -9 FVP_TC3 2>/dev/null || true
sleep 1

# Setup
source "$HOME/FVP_TC3/scripts/runtime.sh" 2>/dev/null || true
mkdir -p "$LOG_DIR"
rm -f "$LOG_DIR"/*.log

echo "=== Launching FVP_TC3 ==="
echo "FVP: $FVP_BIN"
echo "Firmware: $DEPLOY"
echo "QNX IFS: $QNX_IFS"
echo "Logs: $LOG_DIR/"

nohup "$FVP_BIN" \
  -C board.flashloader0.fname="${DEPLOY}/fip_gpt-tc.bin" \
  -C css.sms.rse.rom.raw_image="${DEPLOY}/rse_rom.bin" \
  -C css.sms.rse.VMADDRWIDTH=16 \
  -C css.sms.rse.intchecker.ICBC_RESET_VALUE=0x0000011B \
  -C css.sms.rse.sic.SIC_AUTH_ENABLE=1 \
  -C css.sms.rse.sic.SIC_DECRYPT_ENABLE=1 \
  --data css.sms.rse.sram0="${DEPLOY}/rse_encrypted_cm_provisioning_bundle_0.bin@0x400" \
  --data css.sms.rse.sram1="${DEPLOY}/rse_encrypted_dm_provisioning_bundle_0.bin@0x0" \
  --data board.dram="${QNX_IFS}@0x30000000" \
  -C css.cluster0.subcluster0.has_ete=1 \
  -C css.cluster0.subcluster1.has_ete=1 \
  -C css.cluster0.subcluster2.has_ete=1 \
  -C disable_visualisation=1 \
  -C soc.terminal_s0.start_telnet=0 \
  -C soc.terminal_s1.start_telnet=0 \
  -C css.terminal_uart_ap.start_telnet=0 \
  -C css.terminal_uart1_ap.start_telnet=0 \
  -C board.terminal_0.start_telnet=0 \
  -C css.pl011_uart_ap.out_file="$LOG_DIR/uart-ap.log" \
  -C css.pl011_uart_ap.unbuffered_output=1 \
  -C board.pl011_uart2.out_file="$LOG_DIR/uart-board.log" \
  -C board.pl011_uart2.unbuffered_output=1 \
  -C css.pl011_uart1_ap.out_file="$LOG_DIR/uart-secure.log" \
  -C css.pl011_uart1_ap.unbuffered_output=1 \
  > "$LOG_DIR/fvp-run.log" 2>&1 &
FVP_PID=$!
echo "FVP PID: $FVP_PID"

echo ""
echo "Waiting ~65s for U-Boot prompt..."
sleep 65

echo "Loading QNX via U-Boot (cp.b + go)..."
expect "$SCRIPT_DIR/boot-qnx.exp" 2>&1 | tail -15

echo ""
echo "Waiting 30s for QNX output..."
sleep 30

echo ""
echo "=== QNX Output (board UART) ==="
cat "$LOG_DIR/uart-board.log" 2>/dev/null || echo "(empty)"
echo ""
echo "=== AP UART (tail) ==="
tail -5 "$LOG_DIR/uart-ap.log" 2>/dev/null || echo "(empty)"
echo ""

if kill -0 "$FVP_PID" 2>/dev/null; then
    echo "FVP still running (PID $FVP_PID)"
    echo "  Board UART: telnet localhost 5006"
    echo "  AP UART:    telnet localhost 5002"
    echo "  Kill:       kill $FVP_PID"
else
    echo "FVP exited (check $LOG_DIR/fvp-run.log)"
fi
