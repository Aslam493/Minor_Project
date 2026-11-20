#!/bin/bash
S=/tmp/claude-1000/-home-aslam-Minor-Project-Part-E-Verification/588f201f-270c-43fa-bbae-fc64f750a545/scratchpad
MC=/home/aslam/Minor_Project/Part_E_Verification/tools/monitor_client
R="ssh -o BatchMode=yes root@10.237.27.187"; SN="ssh -o BatchMode=yes sender@10.237.27.170"
scp -q -o BatchMode=yes $S/step5_packets.txt $MC/laptop_can_send.py sender@10.237.27.170:/tmp/
echo "==== image: $($R '/tmp/mon_client --retries 20 status 0 | head -1')"
echo "==== BEFORE: receiver can0"; $R 'for f in rx_packets rx_errors rx_dropped tx_packets; do printf "%s=%s " $f $(cat /sys/class/net/can0/statistics/$f); done; echo; /tmp/canmode can0 | grep -E "FD mode|controller state|error counters"'
$R '/tmp/can_forwarder run 75 --pull --verbose --stats > /tmp/step5_fwd.log 2>&1 < /dev/null & echo $! > /tmp/fwd.pid'; sleep 4
echo "==== SENDER starts: 600 packets, 1 per 100 ms (10/s, 60 s)"
$SN 'python3 /tmp/laptop_can_send.py --iface can0 /tmp/step5_packets.txt' 2>&1 | tail -3
sleep 14   # let the forwarder run out its 75 s
echo "==== forwarder output (verdict counts and tail)"
$R 'cat /tmp/step5_fwd.log' > $1.fwd.log
grep -c . $1.fwd.log; tail -25 $1.fwd.log
echo "==== AFTER: receiver can0"; $R 'for f in rx_packets rx_errors rx_dropped tx_packets; do printf "%s=%s " $f $(cat /sys/class/net/can0/statistics/$f); done; echo; /tmp/canmode can0 | grep -E "controller state|error counters"'
echo "==== monitor status"; $R '/tmp/mon_client --retries 20 status 0'
echo "==== monitor events"; $R '/tmp/mon_client --retries 20 events --all'
