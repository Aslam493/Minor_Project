#!/bin/bash
# precreated_packets_board_test.sh <pi-ip> - the packets are made BEFORE Linux is involved (laptop, packet_gen.py); the Pi only replays them
# through the CAN controller (can0, FD + chip loopback). The key is NOT copied to the Pi. Changes only can0's mode (restored) and RAM files.
PI="${1:?usage: precreated_packets_board_test.sh <pi-ip>}"; HERE="$(cd "$(dirname "$0")" && pwd)"; PV="$(cd "$HERE/.." && pwd)"; ROOT="$(cd "$PV/.." && pwd)"
MC="$PV/tools/monitor_client"; CM="$ROOT/optee_full/toolchains/aarch64/bin/aarch64-none-linux-gnu-gcc"; T=$(mktemp -d)
echo "laptop: making the packets (key read from the Secure World source on the laptop only)"
python3 "$MC/packet_gen.py" demo "$T/packets.txt" || exit 1
echo "laptop: $(grep -c -v '^#' "$T/packets.txt") packets in the file; the file contains messages and tags, not the key"
$CM -std=gnu11 -Wall -Wextra -Werror -O2 -I"$PV/tools/canmode/include" -o "$T/canfdcfg" "$PV/tools/canmode/canfdcfg.c" || exit 1
$CM -std=gnu11 -O2 -I"$PV/tools/canmode/include" -o "$T/canmode" "$PV/tools/canmode/canmode.c" || exit 1
scp -q -o BatchMode=yes "$T/packets.txt" "$T/canfdcfg" "$T/canmode" "$MC/mon_client" "$MC/can_forwarder" "$MC/can_replay" root@"$PI":/tmp/ || exit 1
rm -rf "$T"
ssh -o BatchMode=yes root@"$PI" 'sh -s' <<'REMOTE'
chmod +x /tmp/canfdcfg /tmp/canmode /tmp/mon_client /tmp/can_forwarder /tmp/can_replay
say() { echo; echo "=== $1"; }
M="/tmp/mon_client --retries 20"
say "0. What is on the Pi (no key file anywhere in /tmp; can_replay and can_forwarder contain no key and no HMAC code)"; ls -la /tmp | grep -v "^total"; echo "key files named kf in /tmp: $(ls /tmp | grep -c '^kf$')"
echo "first packet in the file:"; sed -n 2p /tmp/packets.txt | cut -c1-110
say "1. Pi state: uptime $(cut -d. -f1 /proc/uptime) s; monitor"; $M status 0 | sed -n 1,2p; $M status 0 | grep health
say "2. can0 into CAN-FD + chip loopback"
/tmp/canfdcfg can0 test || { echo "canfdcfg FAILED: restoring"; /tmp/canfdcfg can0 normal; exit 1; }
sleep 1; /tmp/canmode can0 | grep -E "mtu|operstate|data bit|FD mode|loopback"
say "3. forwarder starts (reads can0, submits to the monitor, answers the heartbeat)"
/tmp/can_forwarder run 25 --verbose > /tmp/fwd.out 2>&1 &
FP=$!; sleep 3
say "4. can_replay sends the ready-made packets through the CAN controller"
/tmp/can_replay /tmp/packets.txt
sleep 1; wait $FP
say "5. forwarder output: every frame read from can0 and the verdict from Secure World"; cat /tmp/fwd.out
say "6. monitor event log (Secure World)"; $M events --all | grep -v returned
say "7. restore can0 to the boot mode"
/tmp/canfdcfg can0 normal; sleep 1; /tmp/canmode can0 | grep -E "mtu|operstate|FD mode|loopback"
rm -f /tmp/canfdcfg /tmp/canmode /tmp/mon_client /tmp/can_forwarder /tmp/can_replay /tmp/packets.txt /tmp/fwd.out; echo; ls /tmp
REMOTE
