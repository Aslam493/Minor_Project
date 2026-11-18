#!/bin/bash
# canbridge_board_test.sh <pi-ip> - the packet comes from the CAN controller path: can_sender -> can0 (FD + chip loopback) -> can_forwarder -> monitor.
# Changes ONLY can0's mode (restored at the end) and RAM files in /tmp. Reboot the Pi afterwards for a clean monitor log.
PI="${1:?usage: canbridge_board_test.sh <pi-ip>}"; HERE="$(cd "$(dirname "$0")" && pwd)"; PV="$(cd "$HERE/.." && pwd)"; ROOT="$(cd "$PV/.." && pwd)"
MC="$PV/tools/monitor_client"; CM="$ROOT/optee_full/toolchains/aarch64/bin/aarch64-none-linux-gnu-gcc"
KEYS="$ROOT/optee_full/optee_os/core/arch/arm/plat-rpi3/rpi3_can_monitor_keys.h"
$CM -std=gnu11 -Wall -Wextra -Werror -O2 -I"$PV/tools/canmode/include" -o /tmp/canfdcfg_arm "$PV/tools/canmode/canfdcfg.c" || exit 1
$CM -std=gnu11 -O2 -I"$PV/tools/canmode/include" -o /tmp/canmode_arm "$PV/tools/canmode/canmode.c" || exit 1
KF=$(mktemp); sed -n '/KEY_FRAME_INIT/,/^}/p' "$KEYS" | grep -o '0x[0-9a-fA-F][0-9a-fA-F]' | sed 's/0x//' | tr -d '\n' > "$KF"
scp -q -o BatchMode=yes "$MC/mon_client" "$MC/can_sender" "$MC/can_forwarder" /tmp/canfdcfg_arm root@"$PI":/tmp/ || exit 1
scp -q -o BatchMode=yes /tmp/canmode_arm root@"$PI":/tmp/canmode && scp -q -o BatchMode=yes "$KF" root@"$PI":/tmp/kf; rm -f "$KF" /tmp/canfdcfg_arm /tmp/canmode_arm
ssh -o BatchMode=yes root@"$PI" 'sh -s' <<'REMOTE'
mv /tmp/canfdcfg_arm /tmp/canfdcfg; chmod +x /tmp/mon_client /tmp/can_sender /tmp/can_forwarder /tmp/canfdcfg /tmp/canmode; chmod 600 /tmp/kf
say() { echo; echo "=== $1"; }
M="/tmp/mon_client --retries 20"; S="/tmp/can_sender --key-file /tmp/kf"
say "0. Pi state: uptime $(cut -d. -f1 /proc/uptime) s; monitor"; $M status 0 | sed -n 1,2p; $M status 0 | grep health
say "1. can0 BEFORE (boot mode)"; /tmp/canmode can0 | grep -E "mtu|nominal|FD mode|loopback|ctrlmode SUPPORTED"
say "2. put can0 into CAN-FD + chip loopback (canfdcfg can0 test)"
/tmp/canfdcfg can0 test || { echo "canfdcfg FAILED: restoring and stopping"; /tmp/canfdcfg can0 normal; exit 1; }
sleep 1; /tmp/canmode can0 | grep -E "mtu|operstate|nominal|data bit|FD mode|loopback|controller state"
say "3. forwarder starts (reads can0, submits to the monitor, answers the heartbeat)"
/tmp/can_forwarder run 45 --verbose > /tmp/fwd.out 2>&1 &
FP=$!; sleep 3
say "4. HEALTHY: sender sends 5 s of valid frames (100 ms period) THROUGH the CAN controller"
OUT=$($S run 5 100 1000); echo "$OUT"; L=$(echo "$OUT" | sed -n 's/.*counters [0-9]*\.\.\([0-9]*\).*/\1/p'); echo "last counter sent: $L"
sleep 1
say "5. one frame each (valid, tampered, duplicate, replay, gap, wrong id, extended id)"
N=$((L + 1)); $S once 0x123 $N $N
sleep 0.4; $S once 0x123 $((N + 1)) $((N + 1)) --bad-tag
sleep 0.2; $S once 0x123 $N $N
sleep 0.2; $S once 0x123 $((N - 5)) $((N - 5))
sleep 0.2; $S once 0x123 $((N + 10)) $((N + 10))
sleep 0.2; $S once 0x124 $((N + 11)) $((N + 11))
sleep 0.2; $S once 0x123 $((N + 11)) $((N + 11)) --eff
say "6. SILENCE for 2.5 s (no frames): the monitor must raise a traffic TIMEOUT"
sleep 2.5
say "7. traffic resumes: next frame must be ACCEPTED and raise TRAFFIC_RECOVERED"
$S once 0x123 $((N + 11)) $((N + 11)); sleep 1
wait $FP
say "8. forwarder output (each frame it read from can0 and the verdict the monitor gave)"
cat /tmp/fwd.out
say "9. monitor event log (Secure World)"; $M events --all | grep -v returned
say "10. restore can0 to boot mode"
/tmp/canfdcfg can0 normal; sleep 1; /tmp/canmode can0 | grep -E "mtu|operstate|nominal|FD mode|loopback"
rm -f /tmp/mon_client /tmp/can_sender /tmp/can_forwarder /tmp/canfdcfg /tmp/canmode /tmp/kf /tmp/fwd.out; ls /tmp
REMOTE
