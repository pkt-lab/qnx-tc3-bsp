#!/bin/sh
# Read CNTVCT_EL0 (virtual counter) via /proc/timer_list
# Usage: sh /check-cntvct.sh [label]
LABEL="${1:-guest}"
echo "=== $LABEL: Timer check ==="
echo "Uptime: $(cat /proc/uptime)"

# Read raw counter from timer_list
if [ -f /proc/timer_list ]; then
    echo "Timer list (first clock):"
    head -20 /proc/timer_list | grep -E "now|offset|clock"
fi

# Read multiple times to show counter progression
for i in 1 2 3; do
    TS=$(cat /proc/uptime | cut -d' ' -f1)
    echo "$LABEL sample $i: uptime=$TS"
    sleep 1
done
echo "=== $LABEL: done ==="
