#!/bin/bash
# reboot_sample.sh <pi-ip> <reboots> <mon_client binary> <out.csv>
# T18b: reboot the Pi N times and record the monitor's boot_id of every boot (the plan wants 20+ distinct values).
# Laptop side. The Pi's /tmp is RAM, so mon_client is copied again after every boot; nothing persistent is written on the Pi.
# Stops (does NOT power-cycle) if the Pi is not back within 240 s.
PI="$1"; N="$2"; BIN="$3"; OUT="$4"
[ -n "$PI" ] && [ -n "$N" ] && [ -x "$BIN" ] && [ -n "$OUT" ] || { echo "usage: $0 <pi> <reboots> <mon_client> <out.csv>"; exit 2; }
SSH="ssh -o BatchMode=yes -o ConnectTimeout=5 root@$PI"
[ -s "$OUT" ] || echo "n,utc,uptime_s,boot_id,flags,init_ok,starved,note" > "$OUT"
sample() {  # $1 = n
  scp -q -o BatchMode=yes "$BIN" root@"$PI":/tmp/mon_client 2>/dev/null && $SSH 'chmod +x /tmp/mon_client' || { echo "$1,$(date -u +%FT%TZ),,,,,,SCP_FAILED" >> "$OUT"; return 1; }
  raw=$($SSH 'cut -d" " -f1 /proc/uptime; /tmp/mon_client status 0' 2>/dev/null)
  echo "$raw" | awk -v n="$1" -v t="$(date -u +%FT%TZ)" '
    NR==1 {up=$1}
    /^page 0:/ {for(k=1;k<=NF;k++){ if($k=="boot_id") b=$(k+1); if($k=="flags") f=$(k+1)}}
    /health:/ {for(k=1;k<=NF;k++){ if($k ~ /^init_ok=/){split($k,a,"="); io=a[2]} if($k ~ /^starved=/){split($k,a,"="); sv=a[2]}}}
    END {printf "%s,%s,%s,%s,%s,%s,%s,\n", n, t, up, b, f, io, sv}' >> "$OUT"
}
wait_up() {  # wait until ssh answers with uptime < 200 s
  for ((s = 0; s < 240; s += 5)); do
    u=$($SSH 'cut -d" " -f1 /proc/uptime' 2>/dev/null | cut -d. -f1)
    [ -n "$u" ] && [ "$u" -lt 200 ] && return 0
    sleep 5
  done
  return 1
}
wait_up || { echo "Pi not reachable"; exit 1; }
sleep 20; sample 0     # the boot we are on now
for ((i = 1; i <= N; i++)); do
  $SSH 'sync; reboot' >/dev/null 2>&1
  sleep 25                       # let it go down
  wait_up || { echo "$i,$(date -u +%FT%TZ),,,,,,NOT_BACK_IN_240S" >> "$OUT"; echo "STOP: Pi not back after reboot $i"; exit 1; }
  sleep 20                       # let the monitor and tee-supplicant settle
  sample "$i"
  echo "reboot $i/$N: $(tail -1 "$OUT")"
done
echo "unique boot_ids: $(cut -d, -f4 "$OUT" | tail -n +2 | grep -c . ) samples, $(cut -d, -f4 "$OUT" | tail -n +2 | grep . | sort -u | wc -l) distinct"
