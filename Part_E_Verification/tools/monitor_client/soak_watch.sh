#!/bin/bash
# soak_watch.sh <pi-ip> <interval-seconds> <samples> <out.csv>
# Laptop side: every <interval> s ask the monitor for status pages 0 and 1 (read-only commands 20) over SSH and append one CSV line.
# Needs /tmp/mon_client on the Pi. If the Pi cannot be reached the line says UNREACHABLE (that is evidence too). Never writes to the Pi.
PI="$1"; IV="$2"; N="$3"; OUT="$4"
[ -n "$PI" ] && [ -n "$OUT" ] || { echo "usage: $0 <pi> <interval> <samples> <out.csv>"; exit 2; }
[ -s "$OUT" ] || echo "utc,uptime_s,boot_id,flags,last_counter,hb_seq,hb_state,hb_miss,ev_seq,ev_lost,skip_total,pta_lock_timeouts,poll_age_us,starved,v_frame,v_hb,n_TIMEOUT,n_LATE_ANSWER,n_AUTH_FAIL,n_REPLAY,n_COUNTER_GAP,n_COUNTER_RESYNC,n_TRAFFIC_RECOVERED,n_HEARTBEAT_RECOVERED" > "$OUT"
for ((i = 0; i < N; i++)); do
  t=$(date -u +%FT%TZ)
  raw=$(ssh -o BatchMode=yes -o ConnectTimeout=10 -o ServerAliveInterval=5 -o ServerAliveCountMax=3 root@"$PI" 'cut -d" " -f1 /proc/uptime; /tmp/mon_client status 0; /tmp/mon_client status 1' 2>/dev/null)
  if [ -z "$raw" ]; then echo "$t,UNREACHABLE" >> "$OUT"; else
    echo "$raw" | awk -v t="$t" '
      NR==1 {up=$1}
      /^page 0:/ {for(k=1;k<=NF;k++){ if($k=="boot_id") bid=$(k+1); if($k=="flags") fl=$(k+1)}}
      /core1_poll_age_ticks/ {for(k=1;k<=NF;k++) if($k=="core1_poll_age_ticks") pa=$(k+1)}
      /skip_total/ && /pta_lock_timeouts/ {for(k=1;k<=NF;k++){ if($k=="skip_total") st=$(k+1); if($k=="pta_lock_timeouts") lt=$(k+1)}}
      /last_counter/ && /hb_seq/ {for(k=1;k<=NF;k++){ if($k=="last_counter") lc=$(k+1); if($k=="hb_seq") hs=$(k+1); if($k=="hb_state") hst=$(k+1); if($k=="hb_miss_count") hm=$(k+1)}}
      /ev_seq/ && /ev_lost/ {for(k=1;k<=NF;k++){ if($k=="ev_seq") es=$(k+1); if($k=="ev_lost") el=$(k+1)}}
      /health:/ {for(k=1;k<=NF;k++) if($k ~ /^starved=/){split($k,a,"="); sv=a[2]}}
      /^page 1:/ {for(k=1;k<=NF;k++){ if($k=="frame") vf=$(k+1); if($k=="heartbeat") vh=$(k+1)}}
      $1=="TIMEOUT" {n1=$2} $1=="LATE_ANSWER" {n2=$2} $1=="AUTH_FAIL" {n3=$2} $1=="REPLAY" {n4=$2} $1=="COUNTER_GAP" {n5=$2} $1=="COUNTER_RESYNC" {n6=$2} $1=="TRAFFIC_RECOVERED" {n7=$2} $1=="HEARTBEAT_RECOVERED" {n8=$2}
      END {printf "%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%.0f,%s,%s,%s,%d,%d,%d,%d,%d,%d,%d,%d\n", t,up,bid,fl,lc,hs,hst,hm,es,el,st,lt,pa/19.2,sv,vf,vh,n1,n2,n3,n4,n5,n6,n7,n8}' >> "$OUT"
  fi
  [ $((i + 1)) -lt "$N" ] && sleep "$IV"
done
