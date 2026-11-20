#!/bin/bash
R="ssh -o BatchMode=yes root@10.237.27.187"; S="ssh -o BatchMode=yes sender@10.237.27.170"
st_r() { $R 'for f in rx_packets rx_errors rx_dropped rx_over_errors rx_fifo_errors tx_packets tx_errors; do printf "%s=%s " $f $(cat /sys/class/net/can0/statistics/$f); done; echo; /tmp/canmode can0 | grep -E "controller state|error counters"'; }
st_s() { $S 'for f in rx_packets rx_errors tx_packets tx_errors tx_dropped; do printf "%s=%s " $f $(cat /sys/class/net/can0/statistics/$f); done; echo; ip -details -statistics link show can0 | grep -E "can <" ; ip -details -statistics link show can0 | grep -A1 "re-started" | tail -1 | cut -c1-70'; }
echo "==== BEFORE"; echo "-- receiver"; st_r; echo "-- sender"; st_s
$R '/tmp/candump -td -x can0 > /tmp/step4_rx.log 2>&1 < /dev/null & echo $! > /tmp/candump.pid'; sleep 1
echo "==== SEND"; $S 'python3 /tmp/load_sender.py 1000 100'
sleep 2; $R 'kill $(cat /tmp/candump.pid); sleep 0.5'
echo "==== AFTER"; echo "-- receiver"; st_r; echo "-- sender"; st_s
$R 'cat /tmp/step4_rx.log' > "$1"
echo "receiver log lines: $(wc -l < "$1")"
