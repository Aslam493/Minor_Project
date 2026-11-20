#!/bin/sh
# board_full_check.sh - run ON the Raspberry Pi: checks every angle of the Secure CAN Monitor and gives one verdict.
# Plain POSIX sh (BusyBox). No laptop and no ssh needed while it runs.
#
#   usage: sh board_full_check.sh [--dir DIR] [--soak SECS] [--quick] [--c6-image]
#
#   --dir DIR     folder holding mon_client, c5_board_tests, c6_negative_raw and kf (default /tmp)
#   --soak SECS   length of the soak in section G (default 60, 0 = skip)
#   --quick       skip the long sections F (full regression) and G (soak): about 30 s in total
#   --c6-image    the flashed image has the C6 IDLE_EXPIRED change, so T21d must PASS (default: frozen image, T21d is KNOWN)
#
# Needs in DIR (copy them once from the laptop, see the end of this header):
#   mon_client, c5_board_tests, c6_negative_raw   (ARM64 builds from Part_E_Verification/tools/monitor_client)
#   kf                                            (prototype K_frame as 64 hex characters; only needed for frame tests)
#
# Sections
#   B  Boot and hardware      kernel, OP-TEE driver, tee devices, supplicant, CPU mask, Core 1 heartbeat, PTA, FIQ unarmed, can0, MCP2518FD, SPI CRC
#   C  Isolation settings     CPU1 never runs Linux, secure DRAM and shared-memory pool reserved (N-04), /dev/tee0 root-only (F24)
#   D  Monitor health         init_ok, starved, lock timeouts, Core 1 polling on its own, past the boot grace
#   E  Detection              valid, tampered, duplicate, conflict, replay, malformed, counter untouched by rejects, gap, traffic TIMEOUT and
#                             recovery, fresh challenges, ON_TIME, no answer without a challenge, heartbeat TIMEOUT and recovery, event log
#   F  Regression             c5_board_tests --no-t6, legacy-command refusals (T25), idle-lease caps
#   G  Soak                   sender 10 frames/s + heartbeat responder: no false alarm, no BUSY, no starvation
#   H  After                  monitor still healthy, Core 1 still rising
#
# SAFETY: changes only the monitor's RAM counters and event log (like demo_live.sh). Never flashes, reboots, arms the FIQ or writes files
# outside /tmp. Reboot afterwards for a clean event log. Frames are software-injected through the PTA, NOT CAN-bus traffic.
# Exit status: 0 = no FAIL, 1 = at least one FAIL, 2 = setup problem.
#
# One-time copy from the laptop (in ~/Minor_Project):
#   MC=Part_E_Verification/tools/monitor_client
#   sed -n '/KEY_FRAME_INIT/,/^}/p' optee_full/optee_os/core/arch/arm/plat-rpi3/rpi3_can_monitor_keys.h | grep -o '0x[0-9a-fA-F][0-9a-fA-F]' | sed 's/0x//' | tr -d '\n' > /tmp/kf
#   scp $MC/mon_client $MC/c5_board_tests $MC/c6_negative_raw /tmp/kf Part_E_Verification/scripts/board_full_check.sh root@10.237.27.187:/tmp/
#   rm /tmp/kf
# Then on the Pi:  sh /tmp/board_full_check.sh
#
# KNOWN LIMIT (2026-11-16): E11 is timing-sensitive. Each mon_client call takes 0.2-0.4 s, longer than the 300 ms traffic deadline, so a
# traffic TIMEOUT can fire between two of the script's frames, and the monitor writes at most 1 event per second per class after the first 4
# of that class (the counter stays exact). Run it on a freshly rebooted Pi (empty ring): E11 passes there. On a second run without a reboot E11
# can FAIL although the monitor behaved correctly (checked in the event log: TIMEOUT 300.4 ms after the last frame).

DIR=/tmp; SOAK=60; QUICK=0; C6IMG=0; MODEP=0
while [ $# -gt 0 ]; do
  case "$1" in
    --dir) DIR="$2"; shift ;;
    --soak) SOAK="$2"; shift ;;
    --quick) QUICK=1 ;;
    --c6-image) C6IMG=1 ;;
    --mode-p) MODEP=1 ;;      # the flashed image is the Mode P image (command 17 refused): tests that need command 17 are SKIPPED with that reason, not failed
    -h|--help) sed -n 2,36p "$0"; exit 0 ;;
    *) echo "unknown option $1"; exit 2 ;;
  esac; shift
done
TS=$(date -u +%Y%m%dT%H%M%SZ); LOG="/tmp/board_check_$TS.txt"; RC="/tmp/board_check_rc.$$"

main() {
NP=0; NF=0; NS=0; NK=0
pass(){ echo "  PASS   $1"; NP=$((NP+1)); }
fail(){ echo "  FAIL   $1"; NF=$((NF+1)); }
skip(){ echo "  SKIP   $1"; NS=$((NS+1)); }
known(){ echo "  KNOWN  $1"; NK=$((NK+1)); }
note(){ echo "  note   $1"; }
chk(){ if [ "$2" = 1 ]; then pass "$1"; else fail "$1"; fi; }
yes_(){ if "$@"; then echo 1; else echo 0; fi; }

M="$DIR/mon_client --retries 20"; K="--key-file $DIR/kf"
S0=""; st(){ S0=$($M status 0 2>&1); }
f(){ echo "$S0" | tr ' ' '\n' | awk -v k="$1" 'p==k{print; exit} {p=$0}'; }      # value after "key" in status
hv(){ echo "$S0" | tr ' ,' '\n\n' | sed -n "s/^$1=//p" | head -1; }            # value of key=value in the health line
v17(){ $M frame 0x123 "$1" "$2" $K $3 2>&1 | sed -n 's/^verdict \([A-Z_]*\) (detail \([0-9]*\)).*/\1 \2/p'; }
vid(){ $M frame "$1" "$2" "$3" $K 2>&1 | sed -n 's/^verdict \([A-Z_]*\) (detail \([0-9]*\)).*/\1 \2/p'; }
nonce(){ $M nonce 2>&1 | sed -n 's/^hb_seq \([0-9]*\) state \([A-Z_]*\) nonce \(0x[0-9a-f]*\).*/\1 \2 \3/p'; }
evsince(){ $M events --from $(( $1 + 1 )) --all 2>&1 | grep '^seq'; }
fresh_issue(){ q0=$(nonce | cut -d' ' -f1); i=0; while [ $i -lt 600 ]; do q=$(nonce | cut -d' ' -f1); [ -n "$q" ] && [ "$q" != "$q0" ] && return 0; i=$((i+1)); done; return 1; }
hbeat(){ spi_poc_test --core1-heartbeat 2>/dev/null | sed -n 's/.*heartbeat: //p'; }

echo "board_full_check  $(date -u '+%Y-%m-%d %H:%M:%S UTC')  host $(hostname)  kernel $(uname -r)  uptime $(cut -d. -f1 /proc/uptime) s"
echo "tools: $DIR   log: $LOG"

# ---------------------------------------------------------------- setup
[ "$(id -u)" = 0 ] || { echo "  FAIL   run as root (the TEE device is root-only)"; echo 2 > "$RC"; return; }
for b in mon_client c5_board_tests c6_negative_raw; do
  [ -x "$DIR/$b" ] || chmod +x "$DIR/$b" 2>/dev/null
  [ -x "$DIR/$b" ] || { echo "  FAIL   $DIR/$b missing: copy it from the laptop (see the header of this script)"; echo 2 > "$RC"; return; }
done
HAVEKEY=0; [ -f "$DIR/kf" ] && [ "$(tr -d '\n ' < "$DIR/kf" | wc -c)" -eq 64 ] && HAVEKEY=1 && chmod 600 "$DIR/kf"
WHY="no key file"
[ $MODEP = 1 ] && { HAVEKEY=0; WHY="Mode P image: needs command 17, which is refused there (packets are tested by modeP_board_test.sh through command 31)"; note "--mode-p: frame tests E1-E12, F1, F2 and G need command 17 and are skipped (not failed); E18 expects only the heartbeat events"; }
[ $HAVEKEY = 1 ] || [ $MODEP = 1 ] || note "no 64-hex-character key in $DIR/kf: frame tests (E1-E12, F1, G) will be skipped"

# ---------------------------------------------------------------- B
echo; echo "== B  Boot and hardware"
dm=$(dmesg)
chk "B1 kernel is 6.7.0-rc6-v8 ($(uname -r))" $([ "$(uname -r)" = 6.7.0-rc6-v8 ] && echo 1 || echo 0)
rev=$(echo "$dm" | grep -m1 'optee: revision')
chk "B2 OP-TEE reports its revision: ${rev#*] }" $([ -n "$rev" ] && echo 1 || echo 0)
chk "B3 OP-TEE driver initialised" $(echo "$dm" | grep -q 'optee: initialized driver' && echo 1 || echo 0)
chk "B4 no OP-TEE error in the kernel log" $(echo "$dm" | grep -iqE 'optee.*(fail|error|panic)' && echo 0 || echo 1)
chk "B5 /dev/tee0 and /dev/teepriv0 exist" $(yes_ test -e /dev/tee0 -a -e /dev/teepriv0)
chk "B6 tee-supplicant is running" $(ps | grep -v grep | grep -q tee-supplicant && echo 1 || echo 0)
h1=$(hbeat); sleep 2; h2=$(hbeat)
chk "B7 legacy Core 1 heartbeat rising: $h1 -> $h2" $([ -n "$h1" ] && [ -n "$h2" ] && [ "$h2" -gt "$h1" ] 2>/dev/null && echo 1 || echo 0)
chk "B8 PTA answers (FIQ count read)" $(spi_poc_test --fiq-count 2>/dev/null | grep -q 'FIQ count' && echo 1 || echo 0)
fc=$(spi_poc_test --fiq-control 2>/dev/null | sed -n 's/.*(live value): //p')
chk "B9 FIQ not armed (FIQ_CONTROL ${fc:-?})" $([ "$fc" = 0x00000000 ] && echo 1 || echo 0)
chk "B10 can0 exists and is up" $([ "$(cat /sys/class/net/can0/operstate 2>/dev/null)" = up ] && echo 1 || echo 0)
chk "B11 mcp251xfd initialised the MCP2518FD on can0" $(echo "$dm" | grep -q 'mcp251xfd spi0.0 can0: MCP2518FD.*successfully initialized' && echo 1 || echo 0)
chk "B12 no SPI CRC errors from the CAN driver" $(echo "$dm" | grep -iqE 'CRC (read|write) error|mcp251xfd.*CRC error' && echo 0 || echo 1)

# ---------------------------------------------------------------- C
echo; echo "== C  Isolation settings (Linux side)"
on=$(cat /sys/devices/system/cpu/online)
chk "C1 Linux CPUs online = 0,2-3, Core 1 never runs Linux ('$on')" $([ "$on" = 0,2-3 ] && echo 1 || echo 0)
chk "C2 kernel log: CPU1 failed to come online (held by the TF-A divert)" $(echo "$dm" | grep -q 'CPU1: failed to come online' && echo 1 || echo 0)
# (2026-11-16: these three checks were rewritten after the first run reported false FAILs: the DT node is named optee_secure@10000000
#  and the shared-memory pool is reserved by the /memreserve/ table, so neither has a node named after 0x10100000 or 0x08000000;
#  the Pi's BusyBox has no stat. They now read /proc/iomem and the ls -l mode string.)
io=$(cat /proc/iomem 2>/dev/null)
chk "C3 secure DRAM 0x10000000-0x10ffffff (TF-A + OP-TEE, N-04) is reserved in /proc/iomem" $(echo "$io" | grep -q '^10000000-10ffffff : reserved' && echo 1 || echo 0)
chk "C4 shared-memory pool 0x08000000-0x09ffffff is reserved in /proc/iomem" $(echo "$io" | grep -q '^ *08000000-09ffffff : reserved' && echo 1 || echo 0)
md=$(ls -l /dev/tee0 2>/dev/null | awk '{print $1, $3, $4}'); mode=${md%% *}
chk "C5 /dev/tee0 has no world access ($md)" $([ -n "$mode" ] && [ "$(echo "$mode" | cut -c8-10)" = --- ] && echo 1 || echo 0)
mem=$(grep '^teeclnt:' /etc/group 2>/dev/null | cut -d: -f4)
[ -z "$mem" ] && pass "C6 group teeclnt has no members (F24: only root can call the TEE)" || note "C6 teeclnt members: $mem (F24 mitigation not in place)"

# ---------------------------------------------------------------- D
echo; echo "== D  Monitor health"
up=$(cut -d. -f1 /proc/uptime); st
echo "$S0" | grep -q '^page 0' || { fail "D0 monitor status (command 20) did not answer: $(echo "$S0" | head -1)"; }
chk "D1 monitor initialised: init_ok=$(hv init_ok)" $([ "$(hv init_ok)" = 1 ] && echo 1 || echo 0)
chk "D2 Core 1 not starved: starved=$(hv starved)" $([ "$(hv starved)" = 0 ] && echo 1 || echo 0)
chk "D3 no PTA lock timeouts: $(f pta_lock_timeouts)" $([ "$(f pta_lock_timeouts)" = 0 ] && echo 1 || echo 0)
pa=$(echo "$S0" | sed -n 's/.*core1 last poll \([0-9]*\) ms ago.*/\1/p')
chk "D4 Core 1 last poll ${pa:-?} ms ago (limit 10 ms)" $([ -n "$pa" ] && [ "$pa" -le 10 ] && echo 1 || echo 0)
p1=$(f poll_count); sleep 1; st; p2=$(f poll_count)
chk "D5 Core 1 polls on its own: poll_count $p1 -> $p2 in 1 s, no Linux help" $([ -n "$p2" ] && [ "$p2" -gt "$p1" ] 2>/dev/null && echo 1 || echo 0)
chk "D6 past the 60 s boot grace (uptime $up s), timeouts are live" $([ "$up" -ge 70 ] && echo 1 || echo 0)
[ "$up" -lt 70 ] && note "wait until the Pi has been up 70 s, otherwise E16 cannot see a heartbeat TIMEOUT"
E0=$(f ev_seq); note "event log before section E: ev_seq $E0, ev_lost $(f ev_lost)"

# ---------------------------------------------------------------- E
echo; echo "== E  Detection, step by step (software-injected frames, ID 0x123)"
fresh_issue && $M answer >/dev/null 2>&1        # heartbeat healthy before the frame steps
if [ $HAVEKEY = 1 ]; then
  st; L=$(f last_counter); FL=$(f flags); armed=$(( FL & 4 ))
  c=$((L + 1)); r=$(v17 $c $c)
  if [ $armed != 0 ]; then chk "E1 valid next frame ($c) -> ACCEPTED: $r" $([ "${r% *}" = ACCEPTED ] && echo 1 || echo 0)
  else chk "E1 first frame since boot ($c) -> RESYNC: $r" $([ "${r% *}" = RESYNC ] && echo 1 || echo 0); fi
  L=$c; c=$((L + 1)); r=$(v17 $c $c); chk "E2 valid next frame ($c) -> ACCEPTED: $r" $([ "${r% *}" = ACCEPTED ] && echo 1 || echo 0); L=$c
  r=$(v17 $((L + 1)) $((L + 1)) --bad-tag); st
  chk "E3 one tag bit flipped -> AUTH_FAIL ($r), last_counter stays $L ($(f last_counter))" $([ "${r% *}" = AUTH_FAIL ] && [ "$(f last_counter)" = $L ] && echo 1 || echo 0)
  r=$(v17 $L $L);          chk "E4 same frame again ($L) -> DUPLICATE: $r" $([ "${r% *}" = DUPLICATE ] && echo 1 || echo 0)
  r=$(v17 $L $((L + 7)));  chk "E5 same counter, different payload -> CONFLICT: $r" $([ "${r% *}" = CONFLICT ] && echo 1 || echo 0)
  if [ $L -ge 4 ]; then r=$(v17 $((L - 3)) $((L - 3))); chk "E6 older counter ($((L - 3))) -> REPLAY: $r" $([ "${r% *}" = REPLAY ] && echo 1 || echo 0)
  else skip "E6 replay needs last_counter >= 4"; fi
  r=$(vid 0x124 $((L + 1)) $((L + 1))); chk "E7 wrong CAN id 0x124 -> MALFORMED: $r" $([ "${r% *}" = MALFORMED ] && echo 1 || echo 0)
  st; chk "E8 rejected frames never moved the counter: last_counter $(f last_counter) = $L" $([ "$(f last_counter)" = $L ] && echo 1 || echo 0)
  c=$((L + 6)); r=$(v17 $c $c); chk "E9 counter jumps $L -> $c -> GAP detail 5 (5 frames missing): $r" $([ "$r" = "GAP 5" ] && echo 1 || echo 0); L=$c
  c=$((L + 1)); r=$(v17 $c $c); chk "E10 after the gap the next frame ($c) is ACCEPTED: $r" $([ "${r% *}" = ACCEPTED ] && echo 1 || echo 0); L=$c
  st; E1=$(f ev_seq); sleep 1; st
  ta=$(f traffic_age_ticks); tev=$(evsince $E1 | grep -E 'TIMEOUT +counter [0-9]+ detail 1 ')
  chk "E11 1 s of silence -> traffic TIMEOUT (detail 1) from Core 1; traffic age $((ta / 19200)) ms (deadline 300 ms)" $([ -n "$tev" ] && [ "$ta" -ge 5760000 ] && echo 1 || echo 0)
  c=$((L + 1)); r=$(v17 $c $c); L=$c; sleep 1; rev=$(evsince $E1 | grep TRAFFIC_RECOVERED)
  chk "E12 traffic resumes -> ACCEPTED ($r) and TRAFFIC_RECOVERED event" $([ "${r% *}" = ACCEPTED ] && [ -n "$rev" ] && echo 1 || echo 0)
else
  skip "E1-E12 frame tests ($WHY)"
fi
a=$(nonce); sleep 2; b=$(nonce); sa=${a%% *}; sb=${b%% *}; na=${a##* }; nb=${b##* }
chk "E13 Secure World issues fresh challenges: hb_seq $sa -> $sb, nonce $na -> $nb" $([ -n "$sb" ] && [ "$sb" -gt "$sa" ] 2>/dev/null && [ "$na" != "$nb" ] && echo 1 || echo 0)
if fresh_issue; then r=$($M answer 2>&1 | head -1); chk "E14 Normal World answers a fresh challenge -> ON_TIME: $r" $(echo "$r" | grep -q ': ON_TIME' && echo 1 || echo 0)
else fail "E14 no new challenge seen within the polling window"; fi
r=$($M answer 2>&1 | head -1); chk "E15 answer again without a new challenge -> nothing to answer: $r" $(echo "$r" | grep -q 'no open challenge' && echo 1 || echo 0)
st; E2=$(f ev_seq); sleep 5; st
hm=$(f hb_miss_count); hs=$(f hb_state); hev=$(evsince $E2 | grep -E 'TIMEOUT +counter [0-9]+ detail 2 ')
chk "E16 responder silent 5 s -> hb_state $hs, misses $hm (>=3), heartbeat TIMEOUT (detail 2) from Core 1" $([ -n "$hev" ] && [ "${hm:-0}" -ge 3 ] && echo 1 || echo 0)
fresh_issue; r=$($M answer 2>&1 | head -1); sleep 1; st; rec=$(evsince $E2 | grep HEARTBEAT_RECOVERED)
chk "E17 responder back -> $r, HEARTBEAT_RECOVERED, miss count $(f hb_miss_count)" $([ -n "$rec" ] && [ "$(f hb_miss_count)" = 0 ] && echo 1 || echo 0)
ev=$(evsince $E0); miss=""; want="TIMEOUT HEARTBEAT_RECOVERED"
[ $HAVEKEY = 1 ] && want="AUTH_FAIL REPLAY MALFORMED COUNTER_GAP TRAFFIC_RECOVERED $want"
for t in $want; do echo "$ev" | grep -q " $t " || miss="$miss $t"; done
chk "E18 every detection is in the Secure World event log ($want)${miss:+, missing:$miss}" $([ -z "$miss" ] && echo 1 || echo 0)
echo "$ev" | sed 's/^/         /'

# ---------------------------------------------------------------- F
echo; echo "== F  Full regression and negative tests"
if [ $QUICK = 1 ]; then skip "F1-F3 (--quick)"
else
  if [ $HAVEKEY = 1 ]; then
    out=$("$DIR/c5_board_tests" --key-file "$DIR/kf" --no-t6 2>&1)
    echo "$out" | grep -E '^  (FAIL|SKIP)' | sed 's/^/     /'
    sum=$(echo "$out" | grep '^SUMMARY' | tail -1); nf=$(echo "$sum" | sed -n 's/.* \([0-9]*\) FAIL.*/\1/p')
    fl=$(echo "$out" | grep -E '^  FAIL' | awk '{print $2}' | tr '\n' ' ' | sed 's/ $//')
    if [ "${nf:-x}" = 0 ]; then pass "F1 c5_board_tests: $sum"
    elif [ $C6IMG = 0 ] && [ "$nf" = 1 ] && [ "$fl" = T21d ]; then
      known "F1 c5_board_tests: $sum. Only T21d failed: IDLE_EXPIRED is not flashed on the frozen image (use --c6-image after flashing C6)"
    else fail "F1 c5_board_tests: ${sum:-no SUMMARY line}; failed: $fl"; fi
  else skip "F1 c5_board_tests ($WHY)"; fi
  if [ $MODEP = 1 ]; then
    # 2026-11-21: c6_negative_raw --mode-p states the Mode P expectations (with the reason next to each, see its header): R3 ids 30 and 31 exist in
    # Mode P (BAD_PARAMETERS for wrong types, not NOT_SUPPORTED); R4 command 17 is refused in Mode P (NOT_SUPPORTED before its parameters are checked).
    # R4b (no state changed) is the same in both modes. No failure is tolerated any more: the old "R4/R4b = KNOWN" exception is gone.
    out=$("$DIR/c6_negative_raw" --mode-p 2>&1); sum=$(echo "$out" | grep '^SUMMARY' | tail -1); nfl=$(echo "$out" | grep -E '^  FAIL' | awk '{print $2}' | tr '\n' ' ' | sed 's/ $//')
    echo "$out" | grep -E '^  FAIL' | sed 's/^/     /'
    if [ -n "$sum" ] && [ -z "$nfl" ]; then pass "F2 legacy/debug commands refused, bad parameters rejected (T25, Mode P expectations): $sum"
    else fail "F2 c6_negative_raw --mode-p: ${sum:-no SUMMARY line}; failed: $nfl"; fi
  else
  out=$("$DIR/c6_negative_raw" 2>&1); rc=$?; sum=$(echo "$out" | grep '^SUMMARY' | tail -1)
  echo "$out" | grep -E '^  FAIL' | sed 's/^/     /'
  chk "F2 legacy/debug commands refused, bad parameters rejected (T25): $sum" $([ $rc = 0 ] && echo 1 || echo 0)
  fi
  if [ $MODEP = 1 ]; then skip "F3 idle-lease caps (C6 negative): one step injects a frame with command 17, refused in Mode P; the lease commands themselves are covered by F1 on the Mode F image"
  else
  out=$("$DIR/c5_board_tests" --key-file "$DIR/kf" --c6-negative 2>&1); rc=$?
  np=$(echo "$out" | grep -c '^  PASS'); nfl=$(echo "$out" | grep -c '^  FAIL')
  echo "$out" | grep -E '^  FAIL' | sed 's/^/     /'
  chk "F3 idle-lease caps (C6 negative): $np PASS, $nfl FAIL" $([ $rc = 0 ] && [ "$nfl" = 0 ] && echo 1 || echo 0)
  fi
fi

# ---------------------------------------------------------------- G
echo; echo "== G  Soak (sender 10 frames/s + heartbeat responder)"
if [ $QUICK = 1 ] || [ "$SOAK" = 0 ] || [ $HAVEKEY = 0 ]; then skip "G soak (--quick, --soak 0 or $WHY)"
else
  out=$($M soak "$SOAK" $K 2>&1); echo "$out" | sed 's/^/     /'
  g(){ v=$(echo "$1" | tr ' ' '\n' | awk -v k="$2" 'p==k{print; exit} {p=$0}'); echo "${v:-0}"; }
  fv=$(echo "$out" | grep '^frame verdicts'); hb=$(echo "$out" | grep '^heartbeat answers'); es=$(echo "$out" | grep '^end state')
  [ -z "$fv" ] && fail "G0 soak printed no verdict line"
  acc=$(g "$fv" ACCEPTED); res=$(g "$fv" RESYNC)
  badf=$(( $(g "$fv" AUTH_FAIL) + $(g "$fv" REPLAY) + $(g "$fv" MALFORMED) + $(g "$fv" DUPLICATE) + $(g "$fv" CONFLICT) + $(g "$fv" GAP) ))
  busy=$(g "$fv" BUSY); err=$(g "$fv" errors); on=$(g "$hb" ON_TIME); late=$(g "$hb" LATE); oth=$(g "$hb" other)
  lt=$(echo "$es" | sed -n 's/.*pta_lock_timeouts +\([0-9]*\).*/\1/p'); sv=$(echo "$es" | sed -n 's/.*starved \([0-9]*\).*/\1/p')
  chk "G1 frames: ACCEPTED $acc (+RESYNC $res) of ~$((SOAK * 10)), wrong verdicts $badf, BUSY $busy, errors $err" \
      $([ $((acc + res)) -ge $((SOAK * 10 * 95 / 100)) ] && [ "$badf" = 0 ] && [ "$busy" = 0 ] && [ "$err" = 0 ] && echo 1 || echo 0)
  chk "G2 heartbeat: ON_TIME $on, LATE $late (first answer may be late), other $oth" \
      $([ "$on" -ge $((SOAK * 90 / 100)) ] && [ "$late" -le 2 ] && [ "$oth" = 0 ] && echo 1 || echo 0)
  chk "G3 no lock timeouts (+${lt:-?}) and no starvation (${sv:-?}) under load" $([ "$lt" = 0 ] && [ "$sv" = 0 ] && echo 1 || echo 0)
fi

# ---------------------------------------------------------------- H
echo; echo "== H  After the run"
st; chk "H1 monitor still healthy: init_ok=$(hv init_ok) starved=$(hv starved) lock timeouts $(f pta_lock_timeouts) ev_lost $(f ev_lost)" \
        $([ "$(hv init_ok)" = 1 ] && [ "$(hv starved)" = 0 ] && [ "$(f pta_lock_timeouts)" = 0 ] && echo 1 || echo 0)
h1=$(hbeat); sleep 1; h2=$(hbeat)
chk "H2 Core 1 still running: heartbeat $h1 -> $h2, FIQ still unarmed" \
    $([ -n "$h2" ] && [ "$h2" -gt "$h1" ] 2>/dev/null && [ "$(spi_poc_test --fiq-control 2>/dev/null | sed -n 's/.*(live value): //p')" = 0x00000000 ] && echo 1 || echo 0)

echo; echo "== SUMMARY"
echo "$NP PASS, $NF FAIL, $NK KNOWN, $NS SKIP"
echo "Scope: software-injected frames through the PTA (not CAN-bus traffic); prototype keys; detection only; Pi 3 has no TZASC."
echo "Test events are now in the monitor's RAM log: reboot for a clean state. Remove the key when done: rm $DIR/kf"
echo "Log: $LOG"
[ $NF = 0 ] && echo 0 > "$RC" || echo 1 > "$RC"
}

main 2>&1 | tee "$LOG"
rc=$(cat "$RC" 2>/dev/null || echo 2); rm -f "$RC"; exit "$rc"
