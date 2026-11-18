#!/bin/bash
# modeP_board_test.sh <pi-ip> - Mode P: the packets (made on the laptop before Linux) are replayed through the CAN controller; Linux HOLDS them;
# Secure World asks (packet request), the forwarder replies, Secure World judges. Needs the Mode P image on the Pi. The key is NOT copied to the Pi.
# Changes only can0's mode (restored) and RAM files in /tmp. Reboot the Pi afterwards for a clean monitor log.
PI="${1:?usage: modeP_board_test.sh <pi-ip>}"; HERE="$(cd "$(dirname "$0")" && pwd)"; PV="$(cd "$HERE/.." && pwd)"; ROOT="$(cd "$PV/.." && pwd)"
MC="$PV/tools/monitor_client"; CM="$ROOT/optee_full/toolchains/aarch64/bin/aarch64-none-linux-gnu-gcc"; T=$(mktemp -d)
python3 "$MC/packet_gen.py" demo "$T/packets.txt" || exit 1
head -6 "$T/packets.txt" > "$T/few.txt"
$CM -std=gnu11 -Wall -Wextra -Werror -O2 -I"$PV/tools/canmode/include" -o "$T/canfdcfg" "$PV/tools/canmode/canfdcfg.c" || exit 1
$CM -std=gnu11 -O2 -I"$PV/tools/canmode/include" -o "$T/canmode" "$PV/tools/canmode/canmode.c" || exit 1
scp -q -o BatchMode=yes "$T/packets.txt" "$T/few.txt" "$T/canfdcfg" "$T/canmode" "$MC/mon_client" "$MC/can_forwarder" "$MC/can_replay" root@"$PI":/tmp/ || exit 1
rm -rf "$T"
ssh -o BatchMode=yes root@"$PI" 'sh -s' <<'REMOTE'
chmod +x /tmp/canfdcfg /tmp/canmode /tmp/mon_client /tmp/can_forwarder /tmp/can_replay
say() { echo; echo "=================================================================="; echo "$1"; echo "=================================================================="; }
M="/tmp/mon_client --retries 20"
say "0. The Pi and the monitor (no key file on the Pi: $(ls /tmp | grep -c '^kf$') key files)"; $M status 0 | sed -n 1,2p; $M status 0 | grep health
if ! $M status 0 | grep -q "mode 2"; then echo "NOT the Mode P image (mode is not 2): stopping"; rm -f /tmp/canfdcfg /tmp/canmode /tmp/mon_client /tmp/can_forwarder /tmp/can_replay /tmp/packets.txt /tmp/few.txt; exit 1; fi
say "1. can0 into CAN-FD + chip loopback"
/tmp/canfdcfg can0 test || { echo "canfdcfg FAILED: restoring"; /tmp/canfdcfg can0 normal; exit 1; }
sleep 1; /tmp/canmode can0 | grep -E "mtu|data bit|FD mode|loopback"
say "2. PUSH is refused in Mode P: the old forwarder (command 17) replays 5 packets; Secure World must judge none"
/tmp/can_forwarder run 6 > /tmp/push.out 2>&1 &
FP=$!; sleep 2; /tmp/can_replay /tmp/few.txt; wait $FP; grep -E "frames submitted|dropped|can0 rx_packets" /tmp/push.out
$M status 1 | grep -E "^page 1|frame "
evc() { $M status 1 | awk -v k="$1" '$1==k {print $2; f=1} END {if (!f) print 0}'; }
B_AF=$(evc AUTH_FAIL); B_RP=$(evc REPLAY); B_GP=$(evc COUNTER_GAP); B_ML=$(evc MALFORMED); B_TR=$(evc TRAFFIC_RECOVERED); B_LA=$(evc LATE_ANSWER)
say "3. PULL: forwarder holds the packets and answers Secure World's requests; can_replay sends the ready-made packets through the CAN controller"
/tmp/can_forwarder run 25 --verbose --pull > /tmp/fwd.out 2>&1 &
FP=$!; sleep 3; /tmp/can_replay /tmp/packets.txt; sleep 1; wait $FP
say "4. forwarder output (requests answered, what was sent in each reply, what Secure World judged)"; cat /tmp/fwd.out
say "4b. EXPECTED vs ACTUAL (packets reached Secure World only through command 31; the old forwarder in step 2 judged none)"
ok=0; bad=0
chk() { if [ "$2" = "$3" ]; then echo "  PASS  $1: $3"; ok=$((ok+1)); else echo "  FAIL  $1: expected $2, got $3"; bad=$((bad+1)); fi; }
chk "altered packet (one tag bit flipped) -> AUTH_FAIL events" 1 $(( $(evc AUTH_FAIL) - B_AF ))
# REPLAY detail 1 = older counter, detail 2 = same counter again (DUPLICATE); the demo file has one of each. (First board run expected 1 by mistake; corrected 2026-11-18, see MODE_P_RECORD.md.)
chk "old packet (detail 1) + repeated packet (detail 2) -> REPLAY events" 2 $(( $(evc REPLAY) - B_RP ))
chk "counter jump of 10 -> COUNTER_GAP events" 1 $(( $(evc COUNTER_GAP) - B_GP ))
chk "wrong CAN id + extended id -> MALFORMED events" 2 $(( $(evc MALFORMED) - B_ML ))
# the demo pauses 1000 ms, 400 ms and 2500 ms are all longer than the 300 ms traffic deadline, so there are 3 recoveries (first run expected 1 by mistake)
chk "three pauses longer than 300 ms, each followed by a valid packet -> TRAFFIC_RECOVERED events" 3 $(( $(evc TRAFFIC_RECOVERED) - B_TR ))
echo "  LATE_ANSWER events during this functional run (informational, not a pass condition): $(( $(evc LATE_ANSWER) - B_LA ))"
echo "  RESULT: $ok PASS, $bad FAIL"
say "5. monitor event log (Secure World)"; $M events --all | grep -v returned
say "6. restore can0 to the boot mode"
/tmp/canfdcfg can0 normal; sleep 1; /tmp/canmode can0 | grep -E "mtu|FD mode|loopback"
rm -f /tmp/canfdcfg /tmp/canmode /tmp/mon_client /tmp/can_forwarder /tmp/can_replay /tmp/packets.txt /tmp/few.txt /tmp/fwd.out /tmp/push.out; echo; ls /tmp
REMOTE
