#!/bin/bash
# Step 3: raw frames sender -> receiver (10 classic + 10 FD x 48 bytes), then one frame receiver -> sender.
R="ssh -o BatchMode=yes root@10.237.27.187"; S="ssh -o BatchMode=yes sender@10.237.27.170"
cnt_r() { $R 'for f in rx_packets rx_errors rx_dropped rx_over_errors rx_crc_errors tx_packets tx_errors tx_dropped; do printf "%s=%s " $f $(cat /sys/class/net/can0/statistics/$f); done; echo; /tmp/canmode can0 | grep -E "controller state|error counters"'; }
cnt_s() { $S 'for f in rx_packets rx_errors rx_dropped tx_packets tx_errors tx_dropped; do printf "%s=%s " $f $(cat /sys/class/net/can0/statistics/$f); done; echo; ip -details -statistics link show can0 | grep -E "can <|state"; ip -details -statistics link show can0 | grep -A1 "re-started"'; }
echo "==== BEFORE"; echo "-- receiver"; cnt_r; echo "-- sender"; cnt_s
$R '/tmp/candump -td -x can0 > /tmp/step3_rx.log 2>&1 < /dev/null & echo $! > /tmp/candump.pid' ; sleep 1
echo "==== SEND 10 classic (123#DEADBEEF)"
$S 'for i in $(seq 1 10); do cansend can0 123#DEADBEEF || echo FAIL $i; sleep 0.05; done'
echo "==== SEND 10 FD, 48 bytes each, BRS (cansend 123##1<96 hex>)"
$S 'for i in $(seq 1 10); do p=$(printf "%02X" $i); for j in $(seq 1 47); do p="$p$(printf "%02X" $(( (i*16 + j) & 255 )))"; done; echo "$p" >> /tmp/step3_fd_sent.txt; cansend can0 123##1$p || echo FAIL $i; sleep 0.05; done'
sleep 1
$R 'kill $(cat /tmp/candump.pid); sleep 0.5'
echo "==== AFTER"; echo "-- receiver"; cnt_r; echo "-- sender"; cnt_s
echo "==== receiver candump log"; $R 'cat /tmp/step3_rx.log'
echo "==== sender FD payloads sent (hex)"; $S 'cat /tmp/step3_fd_sent.txt; rm -f /tmp/step3_fd_sent.txt'
