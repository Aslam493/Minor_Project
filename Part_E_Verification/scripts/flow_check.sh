#!/bin/bash
# flow_check.sh <pi-ip> - one end-to-end check of the whole Mode P flow, with expected numbers computed BEFORE the run and compared AFTER it.
#   laptop: packet file is generated, then re-verified by an independent program (verify_packets.py)
#   Pi:     the packets go through the CAN controller (can0, chip loopback: no adapter exists for real wires), Linux holds them, Secure World asks,
#           Linux replies, Secure World judges; then the request/reply protocol is attacked with deliberate mistakes (pq_protocol_test).
# Needs the Mode P image (status mode 2). No key is copied to the Pi. Changes only can0's mode (restored) and RAM files in /tmp. Works on a booted Pi
# without a reboot: packet counters start above the monitor's last_counter.
PI="${1:?usage: flow_check.sh <pi-ip>}"; HERE="$(cd "$(dirname "$0")" && pwd)"; PV="$(cd "$HERE/.." && pwd)"; MC="$PV/tools/monitor_client"
CC="$PV/../optee_full/toolchains/aarch64/bin/aarch64-linux-gnu-gcc"; T=$(mktemp -d); LOG="$PV/baseline/flow_check_$(date +%Y%m%d_%H%M%S).txt"
ok=0; bad=0; chk() { if [ "$2" = "$3" ]; then echo "  PASS  $1: $3"; ok=$((ok+1)); else echo "  FAIL  $1: expected $2, got $3"; bad=$((bad+1)); fi; }
{
echo "== L1 laptop: host self-tests"
(cd "$MC" && python3 packet_gen.py --selftest | tail -1; python3 laptop_can_send.py --selftest | tail -1; ./canbridge_test | tail -1)
ssh -o BatchMode=yes -o ConnectTimeout=8 root@"$PI" true 2>/dev/null || { echo "Pi $PI is not reachable from this laptop (laptop addresses: $(hostname -I)): stopping"; exit 2; }
M="/tmp/mon_client --retries 20"
scp -q -o BatchMode=yes "$MC/mon_client" "$MC/can_forwarder" "$MC/can_replay" "$MC/pq_protocol_test" "$PV/tools/canmode/canfdcfg" root@"$PI":/tmp/ 2>/dev/null || { $CC -std=gnu11 -O2 -I"$PV/tools/canmode/include" -o "$T/canfdcfg" "$PV/tools/canmode/canfdcfg.c" && scp -q -o BatchMode=yes "$MC/mon_client" "$MC/can_forwarder" "$MC/can_replay" "$MC/pq_protocol_test" "$T/canfdcfg" root@"$PI":/tmp/; } || exit 2
echo; echo "== L2 Pi: state before"
ssh -o BatchMode=yes root@"$PI" "chmod +x /tmp/mon_client /tmp/can_forwarder /tmp/can_replay /tmp/pq_protocol_test /tmp/canfdcfg; echo uptime \$(cut -d. -f1 /proc/uptime) s; ls /tmp | grep -c '^kf\$' | sed 's/^/key files on the Pi: /'; /tmp/mon_client status 0 | sed -n 1,1p; /tmp/mon_client status 0 | grep health" || exit 2
LC=$(ssh -o BatchMode=yes root@"$PI" "$M status 0" | sed -n 's/.*last_counter \([0-9]*\).*/\1/p' | head -1); MODE=$(ssh -o BatchMode=yes root@"$PI" "$M status 0" | sed -n '1s/.*mode \([0-9]*\).*/\1/p')
[ "$MODE" = 2 ] || { echo "NOT the Mode P image (mode '$MODE'): stopping"; exit 2; }
START=$((LC + 1)); N=300
echo; echo "== L3 laptop: make the packets (counters start at $START, above the monitor's last_counter $LC)"
python3 - "$MC" "$START" "$N" "$T/flow.txt" <<'PY'
import sys; sys.path.insert(0, sys.argv[1]); import packet_gen as g
start, n, out = int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]; key = g.key_from_source(); L = ["# flow_check packets made before Linux by packet_gen.py\n"]
for i in range(n): c = start + i; L.append(g.line(100, 0x123, g.packet(key, 0x123, c, 0xC0DE0000 | (c & 0xFFFF)), "valid %d" % c))
L.append(g.line(100, 0x123, g.packet(key, 0x123, start + n, start + n, True), "one tag bit flipped -> AUTH_FAIL"))
L.append(g.line(100, 0x123, g.packet(key, 0x123, start + n - 5, start + n - 5), "older counter -> REPLAY"))
L.append(g.line(100, 0x123, g.packet(key, 0x123, start + n, start + n), "valid %d (after the altered one)" % (start + n)))
open(out, "w").write("".join(L))
PY
(cd "$MC" && python3 verify_packets.py "$T/flow.txt")
TOTAL=$((N + 3)); echo "expected before the run: $TOTAL packets sent, $((N + 1)) ACCEPTED, AUTH_FAIL +1, REPLAY +1 (detail 1), 0 LATE replies, 0 dropped"
scp -q -o BatchMode=yes "$T/flow.txt" root@"$PI":/tmp/flow.txt
echo; echo "== L4 Pi: run (can0 in chip loopback; forwarder holds, Secure World asks, Linux replies)"
ssh -o BatchMode=yes root@"$PI" 'sh -s' > "$T/pi.txt" 2>&1 <<REMOTE
M="/tmp/mon_client --retries 20"; evc() { \$M status 1 | awk -v k="\$1" '\$1==k {print \$2; f=1} END {if (!f) print 0}'; }
/tmp/canfdcfg can0 test >/dev/null 2>&1 || { echo "canfdcfg FAILED"; /tmp/canfdcfg can0 normal; exit 1; }; sleep 1
echo "B_AF=\$(evc AUTH_FAIL) B_RP=\$(evc REPLAY) B_LA=\$(evc LATE_ANSWER) B_TO=\$(evc TIMEOUT) B_FT=\$(\$M status 1 | sed -n 's/.*frame \([0-9]*\).*/\1/p' | head -1)"
\$M status 2 | sed -n 1p | sed 's/^/BEFORE /'
/tmp/can_forwarder --cpus 0,2,3 run 50 --pull --stats --verbose > /tmp/fwd.out 2>&1 &
FP=\$!; sleep 3; /tmp/can_replay /tmp/flow.txt; wait \$FP
echo "A_AF=\$(evc AUTH_FAIL) A_RP=\$(evc REPLAY) A_LA=\$(evc LATE_ANSWER) A_TO=\$(evc TIMEOUT) A_FT=\$(\$M status 1 | sed -n 's/.*frame \([0-9]*\).*/\1/p' | head -1)"
echo ---FWD---; cat /tmp/fwd.out; echo ---P2---; \$M status 2
/tmp/canfdcfg can0 normal >/dev/null 2>&1; rm -f /tmp/fwd.out /tmp/flow.txt
REMOTE
cat "$T/pi.txt" | sed -n '/---FWD---/,$p' | grep -vE "^  request [0-9]+: sent" | cut -c1-230
echo; echo "== L5 expected vs actual"
g() { grep -o "$1=[0-9]*" "$T/pi.txt" | head -1 | cut -d= -f2; }
F=$(sed -n '/---FWD---/,/---P2---/p' "$T/pi.txt")
RECV=$(echo "$F" | sed -n 's/.*queued: \([0-9]*\) |.*/\1/p'); LATE=$(echo "$F" | sed -n 's/.*LATE \([0-9]*\),.*/\1/p'); JUD=$(echo "$F" | sed -n 's/.*judged: \([0-9]*\) |.*/\1/p'); QL=$(echo "$F" | sed -n 's/.*still queued: \([0-9]*\) |.*/\1/p'); DR=$(echo "$F" | sed -n 's/.*dropped by the queue: \([0-9]*\).*/\1/p')
RX=$(echo "$F" | sed -n 's/.*(+\([0-9]*\)).*/\1/p' | head -1); REQ=$(echo "$F" | sed -n 's/.*requests answered: \([0-9]*\) (ON_TIME \([0-9]*\).*/\1 \2/p')
chk "frames the CAN controller received (rx_packets +)" $TOTAL "$RX"
chk "frames Linux queued (held)" $TOTAL "$RECV"
chk "packets Secure World judged (frame total +)" $TOTAL "$(( $(g A_FT) - $(g B_FT) ))"
chk "packets sent in replies and judged" $TOTAL "$JUD"
chk "packets still queued at the end" 0 "$QL"; chk "packets dropped by the queue" 0 "$DR"
chk "altered packet -> AUTH_FAIL events +" 1 $(( $(g A_AF) - $(g B_AF) )); chk "older packet -> REPLAY events +" 1 $(( $(g A_RP) - $(g B_RP) ))
chk "requests answered LATE (forwarder replies)" 0 "$LATE"; chk "LATE_ANSWER events +" 0 $(( $(g A_LA) - $(g B_LA) ))
chk "requests: all answered ON_TIME (answered ON_TIME)" "$(echo $REQ | cut -d' ' -f1)" "$(echo $REQ | cut -d' ' -f2)"
SEQS=$(echo "$F" | sed -n 's/^  request \([0-9]*\): sent.*/\1/p'); UNIQ=$(echo "$SEQS" | sort -nu | wc -l); CNT=$(echo "$SEQS" | grep -c .)
chk "request numbers that carried packets are all different" "$CNT" "$UNIQ"; chk "request numbers that carried packets rise in order" "$(echo "$SEQS" | sort -n | tr '\n' ' ')" "$(echo "$SEQS" | tr '\n' ' ')"
echo "  info  TIMEOUT events + during the run: $(( $(g A_TO) - $(g B_TO) )) (the run ends with the forwarder stopping, so 1 request TIMEOUT after it is normal)"
echo; echo "== L6 Pi: request/reply protocol with deliberate mistakes"
ssh -o BatchMode=yes root@"$PI" "/tmp/pq_protocol_test" 2>&1 | tee "$T/pq.txt"
chk "protocol test failures" 0 "$(grep -c '^  FAIL' "$T/pq.txt")"
ssh -o BatchMode=yes root@"$PI" "rm -f /tmp/mon_client /tmp/can_forwarder /tmp/can_replay /tmp/pq_protocol_test /tmp/canfdcfg; ls /tmp; cat /sys/class/net/can0/mtu"
echo; echo "== RESULT: $ok PASS, $bad FAIL (flow checks). Frames travel through the controller in chip loopback; real CAN wires need an adapter (untested)."
} 2>&1 | tee "$LOG"; rm -rf "$T"; echo "log: $LOG"
