#!/bin/bash
# demo_live.sh <pi-ip>  -  live demonstration of the Secure CAN Monitor on the Pi (software-injected frames, NOT CAN-bus traffic).
# Copies mon_client and the PROTOTYPE frame key to the Pi's /tmp (RAM), runs the steps below with narration, removes them again.
# Leaves events in the monitor's RAM log: reboot the Pi afterwards for a clean state.
PI="${1:?usage: demo_live.sh <pi-ip>}"; HERE="$(cd "$(dirname "$0")" && pwd)"; ROOT="$(cd "$HERE/../.." && pwd)"
BIN="$ROOT/Part_E_Verification/tools/monitor_client/mon_client"
KEYS="$ROOT/optee_full/optee_os/core/arch/arm/plat-rpi3/rpi3_can_monitor_keys.h"
KF=$(mktemp); sed -n '/KEY_FRAME_INIT/,/^}/p' "$KEYS" | grep -o '0x[0-9a-f][0-9a-f]' | sed 's/0x//' | tr -d '\n' > "$KF"
scp -q -o BatchMode=yes "$BIN" root@"$PI":/tmp/mon_client && scp -q -o BatchMode=yes "$KF" root@"$PI":/tmp/kf || exit 1
rm -f "$KF"
ssh -o BatchMode=yes root@"$PI" 'sh -s' <<'REMOTE'
chmod +x /tmp/mon_client; chmod 600 /tmp/kf
M="/tmp/mon_client --retries 20"
say() { echo; echo "=================================================================="; echo "$1"; echo "=================================================================="; }
say "1. The monitor is alive in Secure World (OP-TEE), Core 1 is polling it"
$M status 0 | sed -n 1,2p; $M status 0 | grep health
NS=$($M events --all | grep -c '^seq'); echo "(events already in the ring before the demo: $NS)"
say "2. HEALTHY TRAFFIC for 8 s: a software sender (100 ms period) + heartbeat responder, valid frames only"
$M soak 8 --key-file /tmp/kf
L=$($M status 0 | sed -n 's/.*last_counter \([0-9]*\).*/\1/p' | head -1); echo "last accepted counter now: $L"
echo "--- now the sender is stopped; we inject faults ourselves, one frame at a time ---"
say "3. A valid frame (counter L+1) is accepted"
$M frame 0x123 $((L+1)) $((L+1)) --key-file /tmp/kf
say "4. TAMPERED frame (one bit of the authentication tag flipped) -> must be rejected"
$M frame 0x123 $((L+2)) $((L+2)) --key-file /tmp/kf --bad-tag
say "5. The same frame again, a repeat of an old counter -> DUPLICATE"
$M frame 0x123 $((L+1)) $((L+1)) --key-file /tmp/kf
say "6. An older counter (replay) -> REPLAY"
$M frame 0x123 $((L-5)) $((L-5)) --key-file /tmp/kf
say "7. A skipped counter (jump of 20) -> GAP"
$M frame 0x123 $((L+22)) $((L+22)) --key-file /tmp/kf
say "8. Wrong CAN id (0x124) -> MALFORMED"
$M frame 0x124 $((L+23)) $((L+23)) --key-file /tmp/kf
say "9. Heartbeat: Secure World asks, Linux must answer the fresh challenge in time"
$M answer
say "10. SILENCE: nobody sends frames and nobody answers the heartbeat for 5 s"
sleep 5
$M status 0 | sed -n 3,5p
say "11. What the monitor recorded (the events; severity and cause are in the event name and detail)"
$M events --from $((NS+1)) --all | grep -v returned | grep -v health
echo; echo "(events 1..$NS were there before the demo)"
rm -f /tmp/mon_client /tmp/kf
REMOTE
