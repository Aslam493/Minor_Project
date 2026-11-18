#!/bin/bash
# direct_bus_board_test.sh <pi-ip> - Pi side of the DIRECT sender test: can0 is put into normal CAN-FD mode (no loopback), the forwarder runs for 40 s and
# prints what the controller received and what Secure World judged. Meanwhile the LAPTOP sends with laptop_can_send.py (see monitor/DIRECT_SENDER_SETUP.md).
# Changes only can0's mode (restored at the end) and RAM files in /tmp. Reboot the Pi afterwards for a clean monitor log.
PI="${1:?usage: direct_bus_board_test.sh <pi-ip>}"; HERE="$(cd "$(dirname "$0")" && pwd)"; PV="$(cd "$HERE/.." && pwd)"; ROOT="$(cd "$PV/.." && pwd)"
MC="$PV/tools/monitor_client"; CM="$ROOT/optee_full/toolchains/aarch64/bin/aarch64-none-linux-gnu-gcc"; T=$(mktemp -d)
$CM -std=gnu11 -Wall -Wextra -Werror -O2 -I"$PV/tools/canmode/include" -o "$T/canfdcfg" "$PV/tools/canmode/canfdcfg.c" || exit 1
$CM -std=gnu11 -O2 -I"$PV/tools/canmode/include" -o "$T/canmode" "$PV/tools/canmode/canmode.c" || exit 1
scp -q -o BatchMode=yes "$T/canfdcfg" "$T/canmode" "$MC/mon_client" "$MC/can_forwarder" root@"$PI":/tmp/ || exit 1; rm -rf "$T"
echo ">>> The Pi will listen for 40 s. START THE LAPTOP SENDER NOW (laptop_can_send.py), within 40 s after the line 'forwarder running'."
ssh -o BatchMode=yes root@"$PI" 'sh -s' <<'REMOTE'
chmod +x /tmp/canfdcfg /tmp/canmode /tmp/mon_client /tmp/can_forwarder
M="/tmp/mon_client --retries 20"
echo "== no key file on the Pi: $(ls /tmp | grep -c '^kf$')"; $M status 0 | sed -n 1,2p
/tmp/canfdcfg can0 bus || { echo "canfdcfg FAILED: restoring"; /tmp/canfdcfg can0 normal; exit 1; }
sleep 1; /tmp/canmode can0 | grep -E "mtu|nominal|data bit|FD mode|loopback|listen-only|controller state"
echo "== forwarder running (40 s)"; /tmp/can_forwarder run 40 --verbose 2>&1 | grep -v "ACCEPTED (detail 0)"
echo "== monitor events"; $M events --all | grep -v returned
echo "== can0 controller state after the run"; /tmp/canmode can0 | grep -E "controller state|error counters"
/tmp/canfdcfg can0 normal; sleep 1; /tmp/canmode can0 | grep -E "mtu|FD mode|loopback"
rm -f /tmp/canfdcfg /tmp/canmode /tmp/mon_client /tmp/can_forwarder; ls /tmp
REMOTE
