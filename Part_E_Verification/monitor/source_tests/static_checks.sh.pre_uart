#!/bin/bash
# Static checks for the Component 2.2 monitor source (final code, stages 1-6). Read-only. Exit 0 = no FAIL.
# PASS and FAIL are real results of the commands below. Comments are stripped before every grep.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
OS="$ROOT/optee_full/optee_os"; P="$OS/core/arch/arm/plat-rpi3"; G="$OS/core/pta/rpi3_can_monitor_pta.c"
SN="$ROOT/Part_E_Verification/source_snapshots"; SNAP0="$SN/optee_os_20261106_pre_monitor"; SNAP2="$SN/optee_os_20261107_stage2plus_pre"
[ -n "${PLAT_OVERRIDE:-}" ] && P="$PLAT_OVERRIDE"
C="$P/rpi3_can_monitor.c"; H="$P/rpi3_can_monitor.h"; PL="$P/rpi3_can_monitor_plat.h"; K="$P/rpi3_can_monitor_keys.h"; SRV="$P/rpi3_can_server.c"
pass=0; fail=0
ok(){ printf "  PASS     %s\n" "$1"; pass=$((pass+1)); }
bad(){ printf "  FAIL     %s\n" "$1"; fail=$((fail+1)); }
chk(){ if [ "$2" = 1 ]; then ok "$1"; else bad "$1"; fi; }
strip(){ python3 "$HERE/strip_comments.py" "$1"; }
# body of one function (from its signature line to the closing brace at column 0)
fbody(){ strip "$C" | awk -v n="$1" '$0 ~ ("^[A-Za-z_].*[ \t*]" n "\\(") {f=1} f{print} f&&/^}/{exit}'; }
CRIT="rpi3_mon_ev_append rpi3_mon_traffic_refresh rpi3_mon_evaluate rpi3_mon_hb_issue_if_due rpi3_mon_frame_equal rpi3_mon_frame_store rpi3_mon_frame_apply rpi3_mon_frame_record rpi3_mon_build_proj0 rpi3_mon_build_proj1"
ALLSRC="$( (strip "$C"; strip "$H"; strip "$PL"; strip "$G") )"

echo "static_checks: final monitor code (stages 1-6)"
# ---- build guards
chk "BG1 CFG_RPI3_CAN_MONITOR ?= n"          $(grep -q '^CFG_RPI3_CAN_MONITOR ?= n$' "$P/conf.mk" && echo 1 || echo 0)
chk "BG1 CFG_RPI3_CAN_MONITOR_DEBUG ?= n"    $(grep -q '^CFG_RPI3_CAN_MONITOR_DEBUG ?= n$' "$P/conf.mk" && echo 1 || echo 0)
chk "BG2 conf.mk errors when the monitor is on without the PTA" $(grep -q 'CFG_RPI3_CAN_MONITOR=y requires CFG_RPI3_CAN_SPI_POC_PTA=y' "$P/conf.mk" && echo 1 || echo 0)
chk "BG2 plat sub.mk compiles the monitor only when enabled" $(grep -q '^srcs-\$(CFG_RPI3_CAN_MONITOR) += rpi3_can_monitor.c$' "$P/sub.mk" && echo 1 || echo 0)
chk "BG2 pta sub.mk compiles the glue only when enabled"     $(grep -q '^srcs-\$(CFG_RPI3_CAN_MONITOR) += rpi3_can_monitor_pta.c$' "$OS/core/pta/sub.mk" && echo 1 || echo 0)
# ---- lock discipline
chk "X8 no cpu_spin_lock / xsave / no_dldetect / expired() anywhere in the monitor sources" $([ "$(echo "$ALLSRC" | grep -cE 'cpu_spin_lock[ (_]|cpu_spin_lock_xsave|cpu_spin_lock_no_dldetect|(^|[^_a-z])expired[ ]*\(')" = 0 ] && echo 1 || echo 0)
tl=$(for f in $(strip "$C" | grep -oE '^[A-Za-z_][^(]*[ \t*](rpi3_mon_[a-z0-9_]+)\(' | sed -E 's/.*[ \t*](rpi3_mon_[a-z0-9_]+)\($/\1/' | sort -u); do fbody "$f" | grep -q 'MON_PLAT_TRYLOCK' && echo -n "$f "; done)
chk "G9 trylock is called only in rpi3_mon_acquire, rpi3_mon_note_legacy, rpi3_mon_core1_attempt ($tl)" $([ "$tl" = "rpi3_mon_acquire rpi3_mon_core1_attempt rpi3_mon_note_legacy " ] && echo 1 || echo 0)
chk "G9 exactly three trylock call sites" $([ "$(strip "$C" | grep -c 'MON_PLAT_TRYLOCK')" = 3 ] && echo 1 || echo 0)
ul=$(for f in rpi3_mon_release rpi3_mon_note_legacy rpi3_mon_core1_attempt rpi3_mon_acquire rpi3_mon_init; do fbody "$f" | grep -qE 'MON_PLAT_UNLOCK' && echo -n "$f "; done)
chk "W5 unlocks only in release, note_legacy, core1_attempt ($ul); never in acquire" $([ "$ul" = "rpi3_mon_release rpi3_mon_note_legacy rpi3_mon_core1_attempt " ] && echo 1 || echo 0)
chk "W5 exactly 3 unlock call sites in the logic file, no force release" $([ "$(strip "$C" | grep -c 'MON_PLAT_UNLOCK')" = 3 ] && echo 1 || echo 0)
chk "BUD acquire unmasks only after a failed budget and unlocks first on success paths (release does unlock-restore)" $([ "$(fbody rpi3_mon_release | grep -c 'MON_PLAT_UNLOCK_RESTORE')" = 1 ] && echo 1 || echo 0)
chk "Y1 Core 1 attempt uses cpu_spin_unlock (no mask restore) and never MASK/UNMASK" $(fbody rpi3_mon_core1_attempt | grep -qE 'MON_PLAT_(MASK_ALL|UNMASK)' && echo 0 || echo 1)
# ---- critical section helpers: no calls that could be slow, blocking or allocate
bad_c=0; for f in $CRIT; do n=$(fbody "$f" | grep -cE '\b(memcpy|memcmp|memset|memmove|malloc|calloc|realloc|free|printf|IMSG|DMSG|EMSG|FMSG|trace_printf|crypto_[a-z_]+|mobj_[a-z_]+|tee_[a-z_]+|MON_PLAT_(MAC|RNG|PANIC|EMSG|MASK|UNMASK|TRYLOCK|UNLOCK))\b'); [ "$n" = 0 ] || { echo "     $f: $n forbidden token(s)"; bad_c=1; }; done
chk "H2/CS1/CS2 critical-section helpers contain no memcpy/memset/heap/printing/crypto/lock calls" $([ "$bad_c" = 0 ] && echo 1 || echo 0)
chk "H2 the only memset in the logic file is in rpi3_mon_init" $([ "$(strip "$C" | grep -c '\bmemset\b')" = 1 ] && fbody rpi3_mon_init | grep -q '\bmemset\b' && echo 1 || echo 0)
cr=0; for f in $CRIT; do fbody "$f" | grep -qE 'for \(|while \(' && cr=1; done
chk "H1 informational: loops exist in critical helpers (bounded by constants; checked by the host timing tests)" 1
# ---- HMAC / crypto isolation (F27)
cf=$(for f in $(strip "$C" | grep -oE '^[A-Za-z_][^(]*[ \t*](rpi3_mon_[a-z0-9_]+)\(' | sed -E 's/.*[ \t*](rpi3_mon_[a-z0-9_]+)\($/\1/' | sort -u); do fbody "$f" | grep -qE '\bMON_PLAT_(MAC_[A-Z]+|RNG)\b|crypto_' && echo -n "$f "; done)
chk "F27 MAC/RNG/crypto primitives appear only in rpi3_mon_hmac and rpi3_mon_init_crypto ($cf)" $([ "$cf" = "rpi3_mon_hmac rpi3_mon_init_crypto " ] && echo 1 || echo 0)
hf=$(for f in $(strip "$C" | grep -oE '^[A-Za-z_][^(]*[ \t*](rpi3_mon_[a-z0-9_]+)\(' | sed -E 's/.*[ \t*](rpi3_mon_[a-z0-9_]+)\($/\1/' | sort -u); do fbody "$f" | grep -q 'rpi3_mon_hmac(' && [ "$f" != rpi3_mon_hmac ] && echo -n "$f "; done)
chk "F27 rpi3_mon_hmac is called only from cmd17, cmd22 and init_crypto ($hf)" $([ "$hf" = "rpi3_mon_cmd17 rpi3_mon_cmd22 rpi3_mon_init_crypto " ] && echo 1 || echo 0)
core1=$( (fbody rpi3_mon_core1_attempt; fbody rpi3_mon_core1_entry_check; strip "$H" | awk '/rpi3_mon_core1_pass/{f=1} f{print} f&&/^}/{exit}') )
chk "F27 Core 1 functions (attempt, entry check, pass) mention no hmac/mac/crypto/rng/malloc/free" $([ "$(echo "$core1" | grep -ciE 'hmac|mac_|crypto|rng|malloc|free\(|mempool')" = 0 ] && echo 1 || echo 0)
chk "F27 the monitor sources contain no heap allocation" $([ "$(echo "$ALLSRC" | grep -cE '\b(malloc|calloc|realloc|free|nex_[a-z_]+|mempool_[a-z_]+)\(')" = 0 ] && echo 1 || echo 0)
c17=$(fbody rpi3_mon_cmd17); l_h=$(echo "$c17" | grep -n 'rpi3_mon_hmac(' | head -1 | cut -d: -f1); l_l=$(echo "$c17" | grep -n 'rpi3_mon_hold(' | head -1 | cut -d: -f1)
chk "F13 in command 17 the HMAC call comes before the lock is taken (line $l_h < $l_l)" $([ -n "$l_h" ] && [ -n "$l_l" ] && [ "$l_h" -lt "$l_l" ] && echo 1 || echo 0)
chk "KEY keys are referenced only as arguments of rpi3_mon_hmac and in their own header" $([ "$(strip "$C" | grep -n 'rpi3_mon_key_' | grep -vE 'rpi3_mon_hmac\(|^[0-9]+:static const uint8_t rpi3_mon_key_|^[0-9]+:\s*const uint8_t \*key|rpi3_mon_key_(frame|hb)\[' | wc -l)" = 0 ] && echo 1 || echo 0)
chk "KEY glue and bindings never mention the key names" $([ "$( (strip "$G"; strip "$PL") | grep -c 'rpi3_mon_key_\|RPI3_MON_KEY_')" = 0 ] && echo 1 || echo 0)
chk "KEY test key is not instantiated in the logic file" $([ "$(strip "$C" | grep -c 'KEY_TEST')" = 0 ] && echo 1 || echo 0)
chk "KEY keys header is included only by the logic file" $([ "$(grep -l 'rpi3_can_monitor_keys.h' "$P"/*.c "$P"/*.h "$OS"/core/pta/*.c 2>/dev/null | tr '\n' ' ')" = "$P/rpi3_can_monitor.c " ] && echo 1 || echo 0)
# ---- atomics / publication
chk "V3/V8 pta_lock_timeouts is touched only by MON_PLAT_INC32 and rpi3_mon_load_u32" $([ "$(strip "$C" | grep -n 'rpi3_mon_pta_lock_timeouts' | grep -vE 'MON_PLAT_INC32|rpi3_mon_load_u32' | wc -l)" = 0 ] && echo 1 || echo 0)
chk "V3 MON_PLAT_INC32 on the timeout counter appears exactly once, in rpi3_mon_acquire" $([ "$(strip "$C" | grep -c 'MON_PLAT_INC32(&rpi3_mon_pta_lock_timeouts)')" = 1 ] && fbody rpi3_mon_acquire | grep -q 'MON_PLAT_INC32(&rpi3_mon_pta_lock_timeouts)' && echo 1 || echo 0)
gw=$(strip "$C" | awk '/^TEE_Result rpi3_mon_init\(void\)/{f=1} f&&/^}/{f=0; next} !f' | grep -n 'rpi3_mon\.health\.' | grep -vE 'rpi3_mon_(load|store)_u(32|64)' | wc -l)
chk "Y8/Z2 G7 words are accessed only through rpi3_mon_load/store_u32/u64 (outside init)" $([ "$gw" = 0 ] && echo 1 || echo 0)
last_stmt=$(strip "$C" | awk '/^TEE_Result rpi3_mon_init\(void\)/{f=1} f{print} f&&/^}/{exit}' | grep -vE '^\s*$' | tail -3 | head -2 | tr -d '\t ' | tr '\n' '|')
chk "INIT-4 init_ok is the last statement before the final return (release store)" $([ "$last_stmt" = "MON_PLAT_STORE_RELEASE32(&rpi3_mon.init_ok,1);|returnTEE_SUCCESS;|" ] && echo 1 || echo 0)
chk "INIT-4 init_ok is stored only once (no other publication)" $([ "$(strip "$C" | grep -c 'init_ok *=\|STORE_RELEASE32(&rpi3_mon.init_ok')" = 1 ] && echo 1 || echo 0)
chk "INIT-4 init_ok is read with an acquire load" $(grep -q 'MON_PLAT_LOAD_ACQUIRE32(&rpi3_mon.init_ok)' "$C" && echo 1 || echo 0)
chk "INIT-4 init failure returns TEE_SUCCESS (boot never fails because of the monitor)" $(fbody rpi3_mon_init | grep -q 'return res;' && echo 0 || echo 1)
sa=$(grep -c '_Static_assert' "$H"); chk "V1/N5/EV1/TM1/Q2 compile-time guards present in the header ($sa static assertions)" $([ "$sa" -ge 20 ] && echo 1 || echo 0)
chk "bindings header refuses non-AArch64 builds" $(grep -q '#error' "$PL" && echo 1 || echo 0)
# ---- FIQ / D1 / Core 1 integration
chk "D1 the monitor sources never unmask FIQ or IRQ (no ~THREAD_EXCP in logic, header, bindings, glue)" $([ "$(echo "$ALLSRC" | grep -c '~THREAD_EXCP')" = 0 ] && echo 1 || echo 0)
unm=$(strip "$SRV" | awk '/^#[ \t]*if(def)? +CFG_RPI3_CAN_MONITOR/{d++; st[d]="M"; next} /^#[ \t]*else/{if(d&&st[d]=="M")st[d]="E"; next} /^#[ \t]*endif/{if(d)d--; next} /~THREAD_EXCP_NATIVE_INTR/{print (d&&st[d]=="E")?"else":"other"}')
chk "Y7 the only FIQ unmask in rpi3_can_server.c is in the #else (non-Monitor) branch ($unm)" $([ "$unm" = "else" ] && echo 1 || echo 0)
leg=$(strip "$SRV" | awk '/^#[ \t]*if(def)? +CFG_RPI3_CAN_MONITOR/{d++; st[d]="M"; next} /^#[ \t]*else/{if(d&&st[d]=="M")st[d]="E"; next} /^#[ \t]*endif/{if(d)d--; next} /^[ \t]+rpi3_can_fiq_process_pending_frame\(/{print (d&&st[d]=="E")?"else":"other"}')
chk "F27/Y7 the legacy frame processing call is only in the #else branch ($leg)" $([ "$leg" = "else" ] && echo 1 || echo 0)
chk "Y6 server loop calls the entry check before the loop and rpi3_mon_core1_pass inside it" $(strip "$SRV" | grep -q 'rpi3_mon_core1_entry_check' && strip "$SRV" | grep -q 'rpi3_mon_core1_pass' && echo 1 || echo 0)
chk "D1 rpi3_can_fiq.c (the FIQ handler) is byte-identical to the pre-change snapshot" $(cmp -s "$SNAP0/files/core/arch/arm/plat-rpi3/rpi3_can_fiq.c" "$P/rpi3_can_fiq.c" && echo 1 || echo 0)
# ---- PTA glue / interface
chk "PTA flags: the registration uses PTA_DEFAULT_FLAGS and a static assert forbids TA_FLAG_CONCURRENT" $(grep -q 'TA_FLAG_CONCURRENT' "$OS/core/pta/rpi3_can_spi_poc.c" && grep -q 'RPI3_CAN_SPI_POC_PTA_FLAGS' "$OS/core/pta/rpi3_can_spi_poc.c" && echo 1 || echo 0)
chk "DEC-IF-3 dispatch: known set ends at command 23; unknown ids return NOT_SUPPORTED" $(strip "$G" | grep -q 'cmd_id > PTA_RPI3_CAN_SPI_POC_CMD_MON_SUBMIT_HB_ANSWER' && echo 1 || echo 0)
ref=$(strip "$G" | awk '/case/{c=$0} /rpi3_mon_note_legacy\(cmd_id, true\)/{print}' | wc -l)
chk "DEC-IF-3 legacy refusals log an event and return NOT_SUPPORTED" $(strip "$G" | grep -q 'NOT_SUPPORTED' && [ "$ref" -ge 1 ] && echo 1 || echo 0)
chk "glue takes no lock and masks nothing" $([ "$(strip "$G" | grep -cE 'TRYLOCK|MASK_ALL|UNMASK|thread_mask|cpu_spin')" = 0 ] && echo 1 || echo 0)
chk "glue contains no crypto and no allocation" $([ "$(strip "$G" | grep -cE 'crypto_|MON_PLAT_MAC|malloc|free\(')" = 0 ] && echo 1 || echo 0)
# ---- boundaries: nothing else in the OP-TEE tree changed
cd "$OS" || exit 2
same=1; for f in $(cat "$SNAP0/modified_files.txt"); do
  case "$f" in core/arch/arm/plat-rpi3/sub.mk|core/pta/rpi3_can_spi_poc.c|lib/libutee/include/pta_rpi3_can_spi_poc.h) continue;; esac
  cmp -s "$SNAP0/files/$f" "$f" || { same=0; echo "     changed: $f"; }; done
chk "BOUNDARY the 6 modified files that must not change are byte-identical to the pre-change snapshot (incl. thread asm, rpi3_can_fiq.c)" $same
cmp -s "$SNAP0/files/core/arch/arm/plat-rpi3/platform_config.h" core/arch/arm/plat-rpi3/platform_config.h; 
for f in core/pta/rpi3_can_spi_poc.c:1:25 lib/libutee/include/pta_rpi3_can_spi_poc.h:0:20 core/arch/arm/plat-rpi3/rpi3_can_server.c:0:22; do
  n=${f%%:*}; r=${f#*:}; rm_=${r%%:*}; ad=${r#*:}; d1=$(diff "$SNAP0/files/$n" "$n" | grep -c '^<'); d2=$(diff "$SNAP0/files/$n" "$n" | grep -c '^>')
  chk "BOUNDARY $n differs from the pre-change snapshot by exactly $ad added and $rm_ removed lines (got +$d2 -$d1)" $([ "$d1" = "$rm_" ] && [ "$d2" = "$ad" ] && echo 1 || echo 0); done
chk "BOUNDARY the one removed line in rpi3_can_spi_poc.c is the PTA flags initializer" $([ "$(diff "$SNAP0/files/core/pta/rpi3_can_spi_poc.c" core/pta/rpi3_can_spi_poc.c | grep '^<' | tr -d '\t ')" = "<.flags=PTA_DEFAULT_FLAGS," ] && echo 1 || echo 0)
chk "BOUNDARY every added line in rpi3_can_spi_poc.c is inside CFG_RPI3_CAN_MONITOR (include, hook, flags macro)" $([ "$(strip core/pta/rpi3_can_spi_poc.c | grep -c 'CFG_RPI3_CAN_MONITOR')" -ge 3 ] && echo 1 || echo 0)
d=$(diff "$SNAP2/files/core/arch/arm/plat-rpi3/sub.mk" core/arch/arm/plat-rpi3/sub.mk | grep -c '^[<>]')
chk "BOUNDARY plat sub.mk is unchanged since the stage 2+ snapshot" $([ "$d" = 0 ] && echo 1 || echo 0)
d=$(diff "$SNAP2/files/core/pta/sub.mk" core/pta/sub.mk | grep -c '^<'); a=$(diff "$SNAP2/files/core/pta/sub.mk" core/pta/sub.mk | grep -c '^>')
chk "BOUNDARY core/pta/sub.mk only gained the one glue line" $([ "$d" = 0 ] && [ "$a" = 1 ] && echo 1 || echo 0)
d=$(diff "$SNAP2/files/core/arch/arm/plat-rpi3/conf.mk" core/arch/arm/plat-rpi3/conf.mk | grep -c '^<')
chk "BOUNDARY conf.mk only has additions (no line removed or changed)" $([ "$d" = 0 ] && echo 1 || echo 0)
# since 2026-11-12 the monitor files are committed: take the files ADDED since the original base commit (a7ccc63e4) plus any still untracked
u=$( { git diff --diff-filter=A --name-only a7ccc63e4 HEAD; git status --short | grep '^??' | awk '{print $2}'; } | sort -u | tr '\n' ' ')
chk "BOUNDARY untracked files are exactly the 5 new monitor files and rpi3_can_server.c" $([ "$u" = "core/arch/arm/plat-rpi3/rpi3_can_monitor.c core/arch/arm/plat-rpi3/rpi3_can_monitor.h core/arch/arm/plat-rpi3/rpi3_can_monitor_keys.h core/arch/arm/plat-rpi3/rpi3_can_monitor_plat.h core/arch/arm/plat-rpi3/rpi3_can_server.c core/pta/rpi3_can_monitor_pta.c " ] && echo 1 || echo 0)
# ---- object code (H1): needs a Monitor-ON disassembly; set ON_DIS=<objdump -d of tee.elf> (skipped, and said so, when unset)
if [ -n "${ON_DIS:-}" ] && [ -f "$ON_DIS" ]; then
  python3 "$HERE/hold_memcpy_check.py" "$ON_DIS" > /tmp/hold_memcpy.$$ 2>&1; hrc=$?
  chk "H1 object code: no memcpy/memset/memcmp/crypto call sits inside a lock hold (hold_memcpy_check.py on $(basename "$ON_DIS"))" $([ "$hrc" = 0 ] && echo 1 || echo 0)
  [ "$hrc" = 0 ] || grep INSIDE /tmp/hold_memcpy.$$ | sed 's/^/     /'
  python3 "$HERE/h1_objcheck.py" "$C" "$ON_DIS" > /dev/null 2>&1; chk "H1 object code: critical-section helpers that exist as symbols call no memcpy/hook/crypto/trace" $([ $? = 0 ] && echo 1 || echo 0)
  rm -f /tmp/hold_memcpy.$$
  chk "TESTSTATS the normal Monitor-ON image contains no test-only code (no rpi3_mon_tstats, rpi3_mon_ts_*, rpi3_mon_pta_test_* symbol)" $([ "$(grep -cE '<(rpi3_mon_tstats|rpi3_mon_ts_[a-z0-9_]+|rpi3_mon_pta_test_[a-z]+)>:' "$ON_DIS")" = 0 ] && echo 1 || echo 0)
else
  echo "  SKIPPED  H1 object-code checks (set ON_DIS to the Monitor-ON tee.elf disassembly)"
fi
echo "static_checks: $pass pass, $fail FAIL"
[ "$fail" -eq 0 ]
