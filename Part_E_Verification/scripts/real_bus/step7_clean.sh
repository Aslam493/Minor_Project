#!/bin/bash
CM=/tmp/ssh_cm_rx2; R="ssh -o BatchMode=yes -o ControlMaster=auto -o ControlPath=$CM -o ControlPersist=120 root@10.237.27.187"; SN="ssh -o BatchMode=yes sender@10.237.27.170"
MC='/tmp/mon_client --retries 20'
c1() { $R "$MC status 1 | grep -E 'TIMEOUT|TRAFFIC_RECOVERED|HEARTBEAT_RECOVERED|COUNTER_GAP|LATE|STALE' | tr -s ' ' | tr '\n' ';'"; echo; }
mid() { $R "$MC status 0 | grep -E 'traffic_age|hb_state|last_counter' | tr -s ' ' ; $MC status 1 | grep -E ' TIMEOUT' | tr -s ' '"; }
$R true
echo "################ 7b (clean rerun): forwarder frozen for 2 s; ONE status read in the middle of the freeze"
M=$($R "$MC status 0 | grep -o 'ev_seq [0-9]*' | cut -d' ' -f2"); echo "ev_seq=$M"; echo -n "before: "; c1
$R '/tmp/can_forwarder run 22 --pull --verbose > /tmp/fwd7b3.log 2>&1 < /dev/null & echo $! > /tmp/fwd.pid'; sleep 3
$SN 'python3 /tmp/laptop_can_send.py --iface can0 /tmp/step7b3_packets.txt' > /tmp/sender7b3.out 2>&1 &
SP=$!; sleep 4
$R 'kill -STOP $(cat /tmp/fwd.pid)'; echo "forwarder STOPPED"; sleep 1
echo "-- one read in the middle of the freeze:"; mid
sleep 1; $R 'kill -CONT $(cat /tmp/fwd.pid)'; echo "forwarder RESUMED"
wait $SP; sleep 4; tail -1 /tmp/sender7b3.out
$R 'kill $(cat /tmp/fwd.pid) 2>/dev/null'; sleep 1; echo -n "after:  "; c1
echo "-- events from seq $((M+1))"; $R "$MC events --from $((M+1)) --all"
sleep 6
echo; echo "################ 7c (clean rerun): heartbeat responder OFF ~6 s, then ON (one-command swap); ONE status read while it is off"
M=$($R "$MC status 0 | grep -o 'ev_seq [0-9]*' | cut -d' ' -f2"); echo "ev_seq=$M"; echo -n "before: "; c1
$R '/tmp/can_forwarder run 40 --pull --verbose --no-heartbeat > /tmp/fwd7c3a.log 2>&1 < /dev/null & echo $! > /tmp/fwd.pid'; sleep 2
$SN 'python3 /tmp/laptop_can_send.py --iface can0 /tmp/step7c3_packets.txt' > /tmp/sender7c3.out 2>&1 &
SP=$!; sleep 4
echo "-- one read while the heartbeat responder is off (about 6 s after it was stopped):"; mid
sleep 2
$R 'kill $(cat /tmp/fwd.pid); /tmp/can_forwarder run 40 --pull --verbose > /tmp/fwd7c3b.log 2>&1 < /dev/null & echo $! > /tmp/fwd.pid'; echo "SWAP: heartbeat responder back ON"
wait $SP; sleep 4; tail -1 /tmp/sender7c3.out
$R 'kill $(cat /tmp/fwd.pid) 2>/dev/null'; sleep 1; echo -n "after:  "; c1
$R 'grep -E "PULL:|end state" /tmp/fwd7c3a.log /tmp/fwd7c3b.log | cut -c1-200'
echo "-- events from seq $((M+1))"; $R "$MC events --from $((M+1)) --all"
