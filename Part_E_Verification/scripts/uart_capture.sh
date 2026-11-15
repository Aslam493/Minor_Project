#!/bin/bash
# uart_capture.sh [device] [out-file]  - read the Pi console (115200 8N1) and save it with a timestamp per line.
# Needs no extra package. Stop with Ctrl-C. The adapter must be 3.3 V; wire only GND, TX, RX.
DEV=${1:-/dev/ttyUSB0}; OUT=${2:-baseline/uart_boot_log_$(date +%Y%m%d_%H%M%S).txt}
[ -c "$DEV" ] || { echo "$DEV not found (is the adapter plugged in?)"; exit 1; }
stty -F "$DEV" 115200 cs8 -cstopb -parenb raw -echo -crtscts || exit 1
echo "reading $DEV -> $OUT  (Ctrl-C to stop)"
while IFS= read -r line; do printf '%s %s\n' "$(date -u +%T.%3N)" "$line"; done < "$DEV" | tee "$OUT"
