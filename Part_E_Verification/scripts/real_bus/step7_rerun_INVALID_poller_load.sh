#!/bin/bash
CM=/tmp/ssh_cm_rx; R="ssh -o BatchMode=yes -o ControlMaster=auto -o ControlPath=$CM -o ControlPersist=120 root@10.237.27.187"; SN="ssh -o BatchMode=yes sender@10.237.27.170"
MC='/tmp/mon_client --retries 20'
page1() { $R "$MC status 1 | grep -E 'TIMEOUT|TRAFFIC_RECOVERED|HEARTBEAT_RECOVERED|COUNTER_GAP' | tr -s ' ' | tr '\n' ';'"; echo; }
$R true
echo "################ 7b (rerun): forwarder frozen 2 s, status polled every ~0.3 s during the run"
M=$($R "$MC status 0 | grep -o 'ev_seq [0-9]*' | cut -d' ' -f2"); echo "ev_seq=$M"; echo -n "before: "; page1
$R '/tmp/can_forwarder run 22 --pull --verbose > /tmp/fwd7b2.log 2>&1 < /dev/null & echo $! > /tmp/fwd.pid; /tmp/poll.sh > /tmp/poll7b.log 2>&1 < /dev/null & echo $! > /tmp/poll.pid'; sleep 3
$SN 'python3 /tmp/laptop_can_send.py --iface can0 /tmp/step7b2_packets.txt' > /tmp/sender7b.out 2>&1 &
SP=$!; sleep 4
$R 'cat /proc/uptime | cut -d" " -f1; kill -STOP $(cat /tmp/fwd.pid)' | sed 's/^/STOP at uptime /'; sleep 2
$R 'cat /proc/uptime | cut -d" " -f1; kill -CONT $(cat /tmp/fwd.pid)' | sed 's/^/CONT at uptime /'
wait $SP; sleep 4; cat /tmp/sender7b.out | tail -1
$R 'kill $(cat /tmp/fwd.pid) $(cat /tmp/poll.pid) 2>/dev/null'; sleep 1
echo -n "after:  "; page1
$R 'cat /tmp/poll7b.log' > /tmp/poll7b_local.log
echo "-- events from seq $((M+1))"; $R "$MC events --from $((M+1)) --all"
sleep 5
echo; echo "################ 7c (rerun): heartbeat responder OFF (~6 s) then ON, one-command swap; status polled"
M=$($R "$MC status 0 | grep -o 'ev_seq [0-9]*' | cut -d' ' -f2"); echo "ev_seq=$M"; echo -n "before: "; page1
$R '/tmp/can_forwarder run 40 --pull --verbose --no-heartbeat > /tmp/fwd7c2a.log 2>&1 < /dev/null & echo $! > /tmp/fwd.pid; /tmp/poll.sh > /tmp/poll7c.log 2>&1 < /dev/null & echo $! > /tmp/poll.pid'; sleep 2
$SN 'python3 /tmp/laptop_can_send.py --iface can0 /tmp/step7c2_packets.txt' > /tmp/sender7c.out 2>&1 &
SP=$!; sleep 6
$R 'cat /proc/uptime | cut -d" " -f1; kill $(cat /tmp/fwd.pid); /tmp/can_forwarder run 40 --pull --verbose > /tmp/fwd7c2b.log 2>&1 < /dev/null & echo $! > /tmp/fwd.pid' | sed 's/^/SWAP (heartbeat back ON) at uptime /'
wait $SP; sleep 4; tail -1 /tmp/sender7c.out
$R 'kill $(cat /tmp/fwd.pid) $(cat /tmp/poll.pid) 2>/dev/null'; sleep 1
echo -n "after:  "; page1
$R 'cat /tmp/poll7c.log' > /tmp/poll7c_local.log
$R 'grep -E "PULL:|end state" /tmp/fwd7c2a.log /tmp/fwd7c2b.log | cut -c1-200'
echo "-- events from seq $((M+1))"; $R "$MC events --from $((M+1)) --all"
