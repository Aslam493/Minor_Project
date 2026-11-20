#!/bin/bash
S=/tmp/claude-1000/-home-aslam-Minor-Project-Part-E-Verification/588f201f-270c-43fa-bbae-fc64f750a545/scratchpad
R="ssh -o BatchMode=yes root@10.237.27.187"; SN="ssh -o BatchMode=yes sender@10.237.27.170"
scp -q -o BatchMode=yes $S/step6_packets.txt sender@10.237.27.170:/tmp/
echo "==== BEFORE: monitor"; $R '/tmp/mon_client --retries 20 status 0 | sed -n 1,5p'
$R '/tmp/can_forwarder run 25 --pull --verbose > /tmp/step6_fwd.log 2>&1 < /dev/null & echo $! > /tmp/fwd.pid'; sleep 4
echo "==== SENDER: attack file"; $SN 'python3 /tmp/laptop_can_send.py --iface can0 /tmp/step6_packets.txt' 2>&1 | tail -2
sleep 22
$R 'cat /tmp/step6_fwd.log' > $1.fwd.log
echo "==== forwarder summary"; tail -9 $1.fwd.log | cut -c1-250
echo "==== EVENTS from seq 8"; $R '/tmp/mon_client --retries 20 events --from 8 --all'
echo "==== status page 0 / 1"; $R '/tmp/mon_client --retries 20 status 0 | sed -n 1,8p; /tmp/mon_client --retries 20 status 1'
