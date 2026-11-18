#!/bin/bash
# collect_evidence.sh <pi-ip> <out.txt>  - fresh evidence for every component, from the laptop and from the live Pi.
# Reboots the Pi first (empty event ring), copies the tools to /tmp, runs the checks, removes the tools and the key, reboots again.
# Software-injected frames only (NOT CAN-bus traffic). Output = plain text, sections [C1]..[C8].
PI="${1:?usage: collect_evidence.sh <pi-ip> <out.txt>}"; OUT="${2:?out file}"
HERE="$(cd "$(dirname "$0")" && pwd)"; PV="$(cd "$HERE/.." && pwd)"; ROOT="$(cd "$PV/.." && pwd)"
MC="$PV/tools/monitor_client"; ST="$PV/monitor/source_tests"; SSH="ssh -o BatchMode=yes -o ConnectTimeout=6 root@$PI"
KEYS="$ROOT/optee_full/optee_os/core/arch/arm/plat-rpi3/rpi3_can_monitor_keys.h"
: > "$OUT"
sec(){ { echo; echo "################################################################"; echo "## $1"; echo "################################################################"; } >> "$OUT"; }
run(){ { echo; echo "\$ $1"; } >> "$OUT"; shift; "$@" >> "$OUT" 2>&1; echo "[exit $?]" >> "$OUT"; }
rsh(){ { echo; echo "pi# $1"; } >> "$OUT"; $SSH "$1" >> "$OUT" 2>&1; echo "[exit $?]" >> "$OUT"; }
waitup(){ for i in $(seq 1 40); do u=$($SSH 'cut -d" " -f1 /proc/uptime' 2>/dev/null | cut -d. -f1); [ -n "$u" ] && [ "$u" -lt 300 ] && return 0; sleep 5; done; return 1; }

sec "[H] Header"
{ echo "evidence collected: $(date '+%Y-%m-%d %H:%M:%S %Z') on $(hostname)"; echo "repo: branch $(git -C "$PV" rev-parse --abbrev-ref HEAD) commit $(git -C "$PV" rev-parse --short HEAD)"; echo "optee_os commit $(git -C "$ROOT/optee_full/optee_os" rev-parse --short HEAD), trusted-firmware-a $(git -C "$ROOT/optee_full/trusted-firmware-a" rev-parse --short HEAD)"; echo "Scope: software-injected frames through the PTA (not CAN-bus traffic); prototype keys; detection only; Pi 3 has no TZASC."; } >> "$OUT"

# fresh(): reboot the Pi (empty event ring, idle-lease use counter back to 0), wait, copy the tools again.
# The idle lease allows 16 uses per boot, and the on-Pi check, the C5 test and the C6 test each use some, so each gets its own boot.
fresh() {
  $SSH 'sync; reboot' >/dev/null 2>&1; sleep 30; waitup || { echo "Pi did not come back" >> "$OUT"; exit 1; }; sleep 75
  KF=$(mktemp); sed -n '/KEY_FRAME_INIT/,/^}/p' "$KEYS" | grep -o '0x[0-9a-fA-F][0-9a-fA-F]' | sed 's/0x//' | tr -d '\n' > "$KF"
  scp -q -o BatchMode=yes "$MC/mon_client" "$MC/c5_board_tests" "$MC/c6_negative_raw" "$PV/scripts/board_full_check.sh" root@"$PI":/tmp/ && scp -q -o BatchMode=yes "$KF" root@"$PI":/tmp/kf; rm -f "$KF"
  rsh 'chmod +x /tmp/mon_client /tmp/c5_board_tests /tmp/c6_negative_raw; chmod 600 /tmp/kf; echo "Pi fresh boot, uptime $(cut -d. -f1 /proc/uptime) s, boot_id $(/tmp/mon_client status 0 | sed -n "s/.*boot_id \(0x[0-9a-f]*\).*/\1/p"), tools copied to /tmp (RAM)"'
}
fresh

sec "[C1] Baseline and hardware readiness"
run "sha256sum -c SD_Card_Backups/SHA256SUMS   (rollback images are intact)" bash -c "cd '$ROOT/SD_Card_Backups' && sha256sum -c SHA256SUMS"
rsh 'uname -r; dmesg | grep -m1 "optee: revision"; dmesg | grep -m1 "optee: initialized driver"; cat /sys/devices/system/cpu/online; dmesg | grep "CPU1: failed"; dmesg | grep "mcp251xfd spi0.0 can0"; cat /sys/class/net/can0/operstate; ls -l /dev/tee0 /dev/teepriv0; grep -E "^ *(08000000-09ffffff|10000000-10ffffff)" /proc/iomem; dmesg | grep "OF: reserved mem" | grep optee'
run "verify_boot.sh $PI   (16 boot/hardware checks)" bash "$PV/tools/verify_boot/verify_boot.sh" "$PI"

sec "[C2] Monitor foundation (host reference tests on the real sources + live monitor state)"
for t in test test_ts test_uart; do run "make $t | tail -1  (ASan + UBSan)" bash -c "cd '$ST' && make $t 2>&1 | tail -1"; done
run "static_checks.sh | tail -1" bash -c "cd '$ST' && bash static_checks.sh 2>&1 | tail -1"
rsh '/tmp/mon_client status 0; /tmp/mon_client status 1 | head -12'

sec "[C3] Core 1 integration (reserved core runs the monitor loop)"
rsh 'echo "Linux cores online: $(cat /sys/devices/system/cpu/online) (Core 1 reserved for the Secure World)"; /tmp/mon_client status 0 | grep poll_count; sleep 2; /tmp/mon_client status 0 | grep poll_count'
run "stack and call-graph record (documented analysis)" bash -c "grep -m6 -E 'worst|640|2000|4096|688' '$PV/monitor/COMPONENT_3_CORE1_CALL_LIST_AND_SYMBOL_DIFF.md' | cut -c1-200"

sec "[C4] Timing and measurement (10 s healthy run)"
rsh '/tmp/mon_client soak 10 --key-file /tmp/kf'
run "recorded loop model (documented measurement)" bash -c "grep -m5 -E '26\\.6|12\\.6|448|7\\.24' '$PV/monitor/COMPONENT_4_TIMING_AND_MEASUREMENT.md' | cut -c1-200"

sec "[C5] Heartbeat and traffic deadline (full on-Pi check E1-E18, regression, 60 s soak)"
rsh 'sh /tmp/board_full_check.sh'
sec "[C5b] The 27-check C5 board test, full output (own fresh boot)"
fresh
rsh '/tmp/c5_board_tests --key-file /tmp/kf --no-t6'

sec "[C6] Hardening (legacy commands refused, bad parameters rejected, idle-lease caps) (own fresh boot)"
fresh
rsh '/tmp/c6_negative_raw'
rsh '/tmp/c5_board_tests --key-file /tmp/kf --c6-negative'

sec "[C7] Independent CAN (chip B)"
run "NOT BUILT: designed only" bash -c "head -12 '$PV/monitor/COMPONENT_7_DESIGNED_NOT_BUILT.md' | cut -c1-220"

sec "[C8] Soaks, reboots, mutation, final check"
run "analyze_soak.py on the 7.27 h soak CSV" python3 "$MC/analyze_soak.py" "$PV/baseline/soak_watch_20261112.csv" --handoff 2026-11-12T13:47
run "21 boots, boot IDs (T18b)" cat "$PV/baseline/t18b_boot_ids_20261116.csv"
run "mutation testing (run 2026-11-18, real sources, 144 mutants)" bash -c "tail -1 '$PV/monitor/source_tests/mutation_20261118.txt' | cut -c1-260"
run "cited paths in the documents" python3 "$PV/scripts/check_cited_paths.py"

sec "[END] Cleanup"
rsh 'rm -f /tmp/mon_client /tmp/c5_board_tests /tmp/c6_negative_raw /tmp/kf /tmp/board_full_check.sh /tmp/board_check_*; ls /tmp; sync; reboot'
sleep 30; waitup && sleep 20 && run "verify_boot.sh after cleanup and reboot" bash "$PV/tools/verify_boot/verify_boot.sh" "$PI"
echo "DONE" >> "$OUT"
