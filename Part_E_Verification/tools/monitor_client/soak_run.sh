#!/bin/bash
# soak_run.sh <pi-ip> <seconds-first> <seconds-second>
# Copies mon_client and the PROTOTYPE K_frame key file to the Pi's /tmp and starts two soaks back to back, detached (nohup + setsid), on the Pi.
# The soak is a software sender (frames injected through the PTA, NOT CAN-bus traffic) plus the heartbeat responder, period 100 ms.
PI="$1"; S1="$2"; S2="$3"; HERE="$(cd "$(dirname "$0")" && pwd)"; ROOT="$(cd "$HERE/../../.." && pwd)"
KEYS="$ROOT/optee_full/optee_os/core/arch/arm/plat-rpi3/rpi3_can_monitor_keys.h"
KF=$(mktemp); sed -n '/KEY_FRAME_INIT/,/^}/p' "$KEYS" | grep -o '0x[0-9a-f][0-9a-f]' | sed 's/0x//' | tr -d '\n' > "$KF"
scp -q -o BatchMode=yes "$ROOT/Part_E_Verification/build_scratch/monitor_client_aarch64/mon_client" "$KF" root@"$PI":/tmp/ 2>&1 || exit 1
ssh -o BatchMode=yes root@"$PI" "mv /tmp/$(basename "$KF") /tmp/kf; chmod 600 /tmp/kf; chmod +x /tmp/mon_client
cat > /tmp/soak_chain.sh <<'C'
#!/bin/sh
date -u > /tmp/soak_1.start
/tmp/mon_client --retries 20 soak $S1 --key-file /tmp/kf > /tmp/soak_1.out 2>&1; echo \"EXIT \$?\" >> /tmp/soak_1.out; date -u > /tmp/soak_1.end
sleep 2
date -u > /tmp/soak_2.start
/tmp/mon_client --retries 20 soak $S2 --key-file /tmp/kf > /tmp/soak_2.out 2>&1; echo \"EXIT \$?\" >> /tmp/soak_2.out; date -u > /tmp/soak_2.end
C
chmod +x /tmp/soak_chain.sh; nohup setsid /tmp/soak_chain.sh >/dev/null 2>&1 < /dev/null & sleep 1; ls /tmp | tr '\n' ' '"
rm -f "$KF"
