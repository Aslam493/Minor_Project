#!/bin/bash
R="ssh -o BatchMode=yes root@10.237.27.187"; SN="ssh -o BatchMode=yes sender@10.237.27.170"
MC='/tmp/mon_client --retries 20'
page1() { $R "$MC status 1 | grep -E 'verdict totals|COUNTER_GAP|TIMEOUT|TRAFFIC_RECOVERED|HEARTBEAT_RECOVERED|AUTH_FAIL|REPLAY|MALFORMED'"; }
startf() { $R "$1 > /tmp/$2.log 2>&1 < /dev/null & echo \$! > /tmp/fwd.pid"; }
killf()  { $R 'kill $(cat /tmp/fwd.pid) 2>/dev/null; sleep 1'; }
send()   { $SN "python3 /tmp/laptop_can_send.py --iface can0 /tmp/$1" 2>&1 | tail -1; }

echo "################ 7a: Linux drops every 10th frame for ~10 s (test forwarder, FWD_TEST_DROP_EVERY=10)"
echo "-- before"; page1; M=$($R "$MC status 0 | grep -o 'ev_seq [0-9]*' | cut -d' ' -f2"); echo "ev_seq=$M"
startf "FWD_TEST_DROP_EVERY=10 /tmp/can_forwarder_test run 16 --pull --verbose" fwd7a; sleep 3
send step7a_packets.txt; sleep 4; killf
echo "-- forwarder log (TEST lines)"; $R 'grep -c "TEST: dropped" /tmp/fwd7a.log; grep -E "PULL:|end state" /tmp/fwd7a.log | cut -c1-230'
echo "-- events from seq $((M+1))"; $R "$MC events --from $((M+1)) --all"; echo "-- after"; page1
sleep 6

echo; echo "################ 7b: forwarder frozen for 2 s (SIGSTOP/SIGCONT) while the sender keeps sending"
echo "-- before"; page1; M=$($R "$MC status 0 | grep -o 'ev_seq [0-9]*' | cut -d' ' -f2"); echo "ev_seq=$M"
startf "/tmp/can_forwarder run 20 --pull --verbose" fwd7b; sleep 3
( send step7b_packets.txt ) & SP=$!
sleep 4; $R 'kill -STOP $(cat /tmp/fwd.pid)'; echo "forwarder STOPPED"; sleep 2; $R 'kill -CONT $(cat /tmp/fwd.pid)'; echo "forwarder RESUMED (2 s later)"
wait $SP; sleep 3; killf
$R 'grep -E "PULL:|end state" /tmp/fwd7b.log | cut -c1-230'
echo "-- events from seq $((M+1))"; $R "$MC events --from $((M+1)) --all"; echo "-- after"; page1
sleep 6

echo; echo "################ 7c: heartbeat responder OFF for ~5 s (forwarder --no-heartbeat), then back on"
echo "-- before"; page1; M=$($R "$MC status 0 | grep -o 'ev_seq [0-9]*' | cut -d' ' -f2"); echo "ev_seq=$M"
startf "/tmp/can_forwarder run 8 --pull --verbose --no-heartbeat" fwd7c1; sleep 2
( send step7c_packets.txt ) & SP=$!
sleep 6; killf; startf "/tmp/can_forwarder run 8 --pull --verbose" fwd7c2; echo "heartbeat responder RESTARTED"
wait $SP; sleep 3; killf
$R 'grep -E "PULL:|heartbeat answers|end state" /tmp/fwd7c1.log /tmp/fwd7c2.log | cut -c1-230'
echo "-- events from seq $((M+1))"; $R "$MC events --from $((M+1)) --all"; echo "-- after"; page1
