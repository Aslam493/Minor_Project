#!/bin/bash
CM=/tmp/ssh_cm_rx3; R="ssh -o BatchMode=yes -o ControlMaster=auto -o ControlPath=$CM -o ControlPersist=120 root@10.237.27.187"; SN="ssh -o BatchMode=yes sender@10.237.27.170"
MC='/tmp/mon_client --retries 20'
c1() { $R "$MC status 1 | grep -E 'TIMEOUT|TRAFFIC_RECOVERED|HEARTBEAT_RECOVERED|COUNTER_GAP' | tr -s ' ' | tr '\n' ';'"; echo; }
mid() { $R "$MC status 0 | grep -E 'traffic_age|hb_state|last_counter' | tr -s ' '; $MC status 1 | grep -E ' TIMEOUT' | tr -s ' '"; }
$R true
echo "################ 7c (corrected): start with a HEALTHY heartbeat, then responder OFF ~6 s, then ON again"
$R '/tmp/can_forwarder run 60 --pull --verbose > /tmp/fwd7c4a.log 2>&1 < /dev/null & echo $! > /tmp/fwd.pid'; sleep 4
echo -n "state with heartbeat answered: "; mid | tr '\n' ' '; echo
M=$($R "$MC status 0 | grep -o 'ev_seq [0-9]*' | cut -d' ' -f2"); echo "ev_seq=$M (events after this are the test's)"; echo -n "before: "; c1
$SN 'python3 /tmp/laptop_can_send.py --iface can0 /tmp/step7c4_packets.txt' > /tmp/sender7c4.out 2>&1 &
SP=$!; sleep 3
$R 'kill $(cat /tmp/fwd.pid); /tmp/can_forwarder run 60 --pull --verbose --no-heartbeat > /tmp/fwd7c4b.log 2>&1 < /dev/null & echo $! > /tmp/fwd.pid'; echo "SWAP 1: heartbeat responder OFF (traffic still forwarded)"
sleep 5
echo "-- one read about 5 s after the heartbeat responder stopped:"; mid
sleep 1.5
$R 'kill $(cat /tmp/fwd.pid); /tmp/can_forwarder run 60 --pull --verbose > /tmp/fwd7c4c.log 2>&1 < /dev/null & echo $! > /tmp/fwd.pid'; echo "SWAP 2: heartbeat responder back ON"
wait $SP; sleep 4; tail -1 /tmp/sender7c4.out
echo "-- one read at the end of the stream, heartbeat answered again:"; mid
$R 'kill $(cat /tmp/fwd.pid) 2>/dev/null'; sleep 1; echo -n "after:  "; c1
echo "-- events from seq $((M+1))"; $R "$MC events --from $((M+1)) --all"
