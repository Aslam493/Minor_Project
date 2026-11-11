#!/bin/bash
# F24 flood test, one run. usage: f24_flood_run.sh <pi-ip> <N flooders> <seconds> <log file>
# Needs on the Pi's /tmp (copied by the caller): mon_client, pta_latency, kf (prototype K_frame, hex). Adds load only; kills the flooders by PID at the end.
PI="$1"; N="$2"; SECS="$3"; LOG="$4"
S=(ssh -o BatchMode=yes -o ConnectTimeout=10 root@"$PI")
{
echo "# F24 run: N=$N flooders, soak ${SECS}s, $(date -Is)"
echo "## before"; "${S[@]}" '/tmp/mon_client status 0 | sed -n 1,7p; uptime'
CPUS=(0 2 3)
"${S[@]}" "rm -f /tmp/fl.pids; i=0; while [ \$i -lt $N ]; do c=\$(echo '0 2 3' | cut -d' ' -f\$(( (i % 3) + 1 ))); /tmp/pta_latency \$c 200000 >/dev/null 2>&1 & echo \$! >> /tmp/fl.pids; i=\$((i+1)); done; sleep 3; a=0; for p in \$(cat /tmp/fl.pids 2>/dev/null); do [ -d /proc/\$p ] && a=\$((a+1)); done; echo flooders started: $N, alive after 3 s: \$a; uptime"
echo "## soak (heartbeat responder + software frame sender, period 100 ms)"
"${S[@]}" "/tmp/mon_client --retries 20 soak $SECS --key-file /tmp/kf; echo soak_exit=\$?"
"${S[@]}" 'a=0; for p in $(cat /tmp/fl.pids 2>/dev/null); do [ -d /proc/$p ] && a=$((a+1)); done; echo flooders still alive at end of soak: $a; if [ -s /tmp/fl.pids ]; then kill $(cat /tmp/fl.pids) 2>/dev/null; sleep 2; kill -9 $(cat /tmp/fl.pids) 2>/dev/null; fi; rm -f /tmp/fl.pids; echo flooders stopped; uptime'
echo "## after"; "${S[@]}" '/tmp/mon_client status 0 | sed -n 1,7p; /tmp/mon_client events --max 5 | head -8; dmesg | grep -i -E "panic|oops|BUG|optee.*(err|fail)" | tail -3'
} 2>&1 | tee "$LOG"
