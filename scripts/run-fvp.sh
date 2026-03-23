#!/bin/bash
# Launch FVP_TC3 with QNX IFS preloaded in DRAM and boot via U-Boot
#
# The raw binary (.bin) is loaded directly at its linked address in FVP
# DRAM. U-Boot boots, fails to find Linux, drops to prompt. The expect
# script then issues 'go <entry>' to jump to QNX startup.
#
# IMPORTANT: Do NOT load the Linux fitImage (tc-fitImage.bin) — U-Boot
# will auto-boot Linux instead of dropping to the prompt.
#
# Prerequisites:
#   - FVP_TC3 installed at ~/FVP_TC3/
#   - TC3 firmware stack built at ~/tc3-workspace/ (Linux buildroot)
#   - QNX IFS built (qnx-hv-host.ifs + qnx-hv-host.bin)
#
# Usage:
#   ./run-fvp.sh [path-to-qnx-ifs]
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

QNX_IFS="${1:-$REPO_ROOT/output/qnx-hv-host.ifs}"
QNX_BIN="${QNX_IFS%.ifs}.bin"
FVP_BIN="${FVP_BIN:-$HOME/FVP_TC3/models/Linux64_GCC-9.3/FVP_TC3}"
DEPLOY="${DEPLOY:-$HOME/tc3-workspace/output/tc3/buildroot/fvp/deploy}"
LOG_DIR="/tmp/qnx-fvp-logs"
DRAM_BASE=0x80000000
# DTB loaded at 0x88000000 (128MB offset, same as TI BSPs)
# Must not overlap with IFS at 0x82000000 or sysram at 0x80000000
FDT_DRAM_OFFSET=0x08000000
# Use full TC3 DTB (from TF-A build) for boot + io-sock
# Falls back to minimal DTB if TF-A DTB not available
TFA_DTB="${DEPLOY}/../tmp_build/tfa/build/tc/debug/fdts/tc3.dtb"
if [ -f "$TFA_DTB" ]; then
    QNX_DTB="$TFA_DTB"
else
    QNX_DTB="$REPO_ROOT/output/minimal-smc.dtb"
    echo "WARNING: TF-A DTB not found, using minimal DTB (no CPU/RAM info)"
fi

if [ ! -f "$QNX_BIN" ]; then
    echo "ERROR: QNX binary not found: $QNX_BIN"
    echo "Build it first: ./scripts/build-ifs.sh"
    exit 1
fi

if [ ! -f "$FVP_BIN" ]; then
    echo "ERROR: FVP_TC3 not found: $FVP_BIN"
    exit 1
fi

if [ ! -d "$DEPLOY" ]; then
    echo "ERROR: TC3 firmware deploy dir not found: $DEPLOY"
    echo "Build the TC3 Linux stack first"
    exit 1
fi

# Extract entry point and LOAD vaddr from ELF to compute DRAM offset
READELF="${READELF:-$(command -v ntoaarch64-readelf 2>/dev/null || command -v readelf)}"
if [ -n "$READELF" ] && [ -f "$QNX_IFS" ]; then
    LOAD_VADDR=$("$READELF" -l "$QNX_IFS" 2>/dev/null | grep -A0 '^ *LOAD' | awk '{print $3}')
    ENTRY=$("$READELF" -h "$QNX_IFS" 2>/dev/null | awk '/Entry point/{print $NF}')
fi

LOAD_VADDR="${LOAD_VADDR:-0x82000200}"
ENTRY="${ENTRY:-0x82000800}"

# FVP --data board.dram=<file>@<offset> loads relative to DRAM base (0x80000000)
# Raw binary starts at LOAD_VADDR, so offset = LOAD_VADDR - DRAM_BASE
DRAM_OFFSET=$(printf "0x%x" $(( LOAD_VADDR - DRAM_BASE )))

echo "QNX bin: $QNX_BIN"
echo "Load at: DRAM+$DRAM_OFFSET (vaddr $LOAD_VADDR)"
echo "Entry:   $ENTRY"

# Kill any existing FVP
pkill -9 FVP_TC3 2>/dev/null || true
sleep 1

# Setup
source "$HOME/FVP_TC3/scripts/runtime.sh" 2>/dev/null || true
mkdir -p "$LOG_DIR"
rm -f "$LOG_DIR"/*.log

echo ""
echo "=== Launching FVP_TC3 ==="
echo "FVP: $FVP_BIN"
echo "Firmware: $DEPLOY"
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
  --data board.dram="${QNX_BIN}@${DRAM_OFFSET}" \
  --data board.dram="${QNX_DTB}@${FDT_DRAM_OFFSET}" \
  -C board.smsc_91c111.enabled=1 \
  -C board.hostbridge.userNetworking=1 \
  -C board.hostbridge.userNetPorts="8022=22" \
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
echo "Waiting for U-Boot prompt (timeout 120s)..."
ELAPSED=0
while [ $ELAPSED -lt 120 ]; do
    if grep -q "TOTAL_COMPUTE#" "$LOG_DIR/uart-ap.log" 2>/dev/null; then
        echo "U-Boot prompt detected after ${ELAPSED}s"
        sleep 2  # let U-Boot settle
        break
    fi
    sleep 3
    ELAPSED=$((ELAPSED + 3))
done
if [ $ELAPSED -ge 120 ]; then
    echo "ERROR: U-Boot prompt not detected after 120s"
    echo ""
    echo "=== FVP log (tail) ==="
    tail -20 "$LOG_DIR/fvp-run.log" 2>/dev/null
    echo ""
    echo "=== AP UART ==="
    cat "$LOG_DIR/uart-ap.log" 2>/dev/null || echo "(empty)"
    kill "$FVP_PID" 2>/dev/null
    exit 1
fi

echo "Booting QNX (go $ENTRY)..."
expect "$SCRIPT_DIR/boot-qnx.exp" "$ENTRY" 2>&1 | tail -15

echo ""
echo "Waiting for QNX shell (timeout 60s)..."
ELAPSED=0
while [ $ELAPSED -lt 60 ]; do
    if grep -q "Shell ready" "$LOG_DIR/uart-ap.log" 2>/dev/null; then
        echo "QNX shell ready after ${ELAPSED}s"
        break
    fi
    if ! kill -0 "$FVP_PID" 2>/dev/null; then
        echo "ERROR: FVP exited unexpectedly"
        tail -20 "$LOG_DIR/fvp-run.log" 2>/dev/null
        exit 1
    fi
    sleep 3
    ELAPSED=$((ELAPSED + 3))
done
if [ $ELAPSED -ge 60 ]; then
    echo "ERROR: QNX shell not detected after 60s"
    echo ""
    echo "=== Board UART ==="
    cat "$LOG_DIR/uart-board.log" 2>/dev/null || echo "(empty)"
    echo ""
    echo "=== AP UART ==="
    cat "$LOG_DIR/uart-ap.log" 2>/dev/null || echo "(empty)"
    kill "$FVP_PID" 2>/dev/null
    exit 1
fi

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
