#!/bin/bash
# modeP_timing_test.sh <pi-ip> <idle|load> [seconds=600] [spin]
# Mode P reply-time measurement, one phase per run. REBOOT THE PI BEFORE EACH PHASE (Secure World keeps its maximum since boot).
#   idle: only the forwarder (can_forwarder --pull --stats --cpus 0,2,3).
#   load: the same, with 3 busy processes (tools/monitor_client/busyloop), one pinned to each of the cores 0, 2, 3, for the whole run.
#   spin: the forwarder polls without sleeping (tight polling); its CPU cost is printed.
# Reports: Linux side (request fetched -> reply returned, gap between fetches; p50/p99/max in us), Secure World side (request issued -> answer received,
# from command 20 page 2, 2 ms buckets), LATE_ANSWER count, request misses and TIMEOUT events. No packets are sent: replies are empty (valid).
# Changes nothing on the Pi except RAM files in /tmp and the monitor's RAM counters. Needs the Mode P image.
PI="${1:?usage: modeP_timing_test.sh <pi-ip> <idle|load> [seconds] [spin]}"; PH="${2:?idle|load}"; SECS="${3:-600}"; SP="${4:-}"
HERE="$(cd "$(dirname "$0")" && pwd)"; MC="$(cd "$HERE/../tools/monitor_client" && pwd)"; LOG="$HERE/../baseline/modeP_timing_${PH}${SP:+_spin}_$(date +%Y%m%d_%H%M%S).txt"
case "$PH" in idle|load) ;; *) echo "phase must be idle or load"; exit 2;; esac
CC="$HERE/../../optee_full/toolchains/aarch64/bin/aarch64-linux-gnu-gcc"; T=$(mktemp -d)
$CC -std=gnu11 -Wall -Wextra -Werror -O2 -o "$T/busyloop" "$MC/busyloop.c" || exit 1
scp -q -o BatchMode=yes "$MC/can_forwarder" "$MC/mon_client" "$T/busyloop" root@"$PI":/tmp/ || exit 1; rm -rf "$T"
{ echo "# Mode P timing, phase $PH, $SECS s${SP:+, TIGHT polling}, $(date -Is), laptop $(hostname -s)"
ssh -o BatchMode=yes root@"$PI" "sh -s $PH $SECS $SP" <<'REMOTE'
PH=$1; SECS=$2; SP=$3; M="/tmp/mon_client --retries 20"; chmod +x /tmp/can_forwarder /tmp/mon_client /tmp/busyloop
echo "uptime $(cut -d. -f1 /proc/uptime) s, kernel $(uname -r), online cpus $(cat /sys/devices/system/cpu/online)"
$M status 0 | sed -n 1p
$M status 0 | grep -q "mode 2" || { echo "NOT the Mode P image (mode is not 2): stopping"; rm -f /tmp/can_forwarder /tmp/mon_client /tmp/busyloop; exit 1; }
$M status 2 | sed -n 1p | grep -q "page 2" || { echo "status page 2 missing: this is not the image with the latency page: stopping"; rm -f /tmp/can_forwarder /tmp/mon_client /tmp/busyloop; exit 1; }
up=$(cut -d. -f1 /proc/uptime); [ "$up" -lt 70 ] && { echo "waiting for the 60 s boot grace"; sleep $((70 - up)); }
echo "--- before: events"; $M status 1 | grep -E "LATE_ANSWER|TIMEOUT|STALE|DUP|UNISSUED|HEARTBEAT|HB_" ; $M status 2 | sed -n 1p
if [ "$PH" = load ]; then for c in 0 2 3; do /tmp/busyloop $c $((SECS + 30)) & done; sleep 2; echo "load: 3 busy processes on cores 0, 2, 3 started"; fi
echo "--- forwarder run"
/tmp/can_forwarder --cpus 0,2,3 run "$SECS" --pull --stats ${SP:+--spin} 2>&1
echo "--- after"
$M status 2
echo "--- events (no packets were sent; any event below is about the request/reply or heartbeat itself)"; $M events --all | grep -v returned | tail -40
$M status 1 | grep -E "LATE_ANSWER|TIMEOUT|STALE|DUP|UNISSUED|HEARTBEAT|HB_"
echo "--- load average: $(cut -d' ' -f1-3 /proc/loadavg)"
rm -f /tmp/can_forwarder /tmp/mon_client /tmp/busyloop; ls /tmp
REMOTE
} 2>&1 | tee "$LOG"; echo "log: $LOG"
