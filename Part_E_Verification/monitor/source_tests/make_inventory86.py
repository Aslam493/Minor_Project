#!/usr/bin/env python3
"""Generates ../COMPONENT_2_2_INTERFACE_INVENTORY.md: evidence mapping of the 86 interface and implementation checks added to the design on
2026-11-06 (not part of the original 53). IDs, check text and kind are parsed from MONITOR_STATE_DESIGN.md; the mapping is hand-written; harness
line numbers are looked up by title at generation time. Refuses to run if the mapping and the design's IDs differ."""
import re, collections
D = open('../MONITOR_STATE_DESIGN.md').read()
sec = D.split('### Interface and implementation checks added 2026-11-06')[1].split('### Exit criteria')[0]
orig = collections.OrderedDict()
for l in sec.splitlines():
    m = re.match(r'\| ([A-Z]+[0-9]*(?:-[0-9]+)?) \| (.*?) \| (host-side logic test|static / build|static / review|static / review / build|host-side test, then on-board \(H3\)|host-side logic test, then board|static / review / build, then board) \| NOT RUN \| (.*) \|$', l)
    if m: orig[m.group(1)] = (m.group(2), m.group(3))
assert len(orig) == 86, len(orig)
H = open('harness.c').read().splitlines()
def T(prefix):
    for n, l in enumerate(H, 1):
        m = re.search(r'begin\("(' + re.escape(prefix) + r'[^"]*)"\)', l)
        if m: return 'harness.c:%d "%s"' % (n, m.group(1)[:95] + ('...' if len(m.group(1)) > 95 else ''))
    raise SystemExit("no harness test starting with " + prefix)
R = 'run_closure_20261107.txt (before the later test additions) and run_h1fix_20261107.txt / run_interface_20261107.txt (409 checks)'
RUN = 'run_interface_20261107.txt (409/409)'
MUT = 'mutation_real.py mutants; mutation_closure_20261107.txt'
ST = 'static_h1fix_20261107.txt'
HL = "Host test: real monitor source with mock lock, timer and platform bindings; not hardware."
rows = {}
def r(i, loc, test, ev, res, lim): rows[i] = (loc, test, ev, res, lim)
M = 'rpi3_can_monitor.c'; G = 'rpi3_can_monitor_pta.c'
# ---- command 20 and the health pair
r('N1', M+': rpi3_mon_cmd20, rpi3_mon_build_proj0', T('N1/N5/N7/HP5'), RUN+'; '+MUT+' ("command 20 wrong length field")', 'PASS', HL)
r('N2', M+': rpi3_mon_cmd20', T('EV5/N2')+'; '+T('N3/N4/N8'), RUN+'; mutant "command 20 accepts page 2" caught', 'PASS', HL+" Row text is the 2026-11-07 version (page 1 supported, page 2 BAD_PARAMETERS).")
r('N3', M+': rpi3_mon_cmd20', T('N3/N4/N8'), RUN, 'PASS', HL)
r('N4', M+': rpi3_mon_cmd20 (BUSY branch)', T('N3/N4/N8'), RUN+'; mutant "BUSY values not written" caught', 'PASS', HL)
r('N5', 'rpi3_can_monitor.h: _Static_assert N5 (proj0, proj1)', 'compile-time asserts; '+T('V1 runtime layout'), 'build logs final_*_h1.log exit 0 (asserts compile); mutants "page 0 field offset moves", "page 1 layout moves" caught at compile time', 'PASS', "Offsets asserted at compile time on the host compiler and on the AArch64 compiler (both builds compile the same header).")
r('N6', M+': rpi3_mon_cmd20', T('N6 lease'), RUN, 'PASS', HL)
r('N7', M+': rpi3_mon_cmd20', T('N1/N5/N7/HP5'), RUN+'; mutant "command 20 lock-free fields read before the unlock" caught; hold_memcpy_h1fix_20261107.txt (the 128-byte copy-out is after the release)', 'PASS', HL)
r('N8', M+': rpi3_mon_cmd20', T('N3/N4/N8'), RUN, 'PASS', HL)
r('HP1', G+': rpi3_mon_pta_cmd17..23 (exact param_types)', T('HP1 exact param_types'), RUN+'; mutant "command 21 wrong parameter types" caught', 'PASS', HL)
r('HP2', G+', '+M, T('N3/N4/N8')+'; '+T('C17 fail closed')+'; '+T('DS4')+'; '+T('GLUE parameter mapping'), RUN+'; mutants "BUSY values not written", "short-buffer size not returned" caught', 'PASS', HL)
r('HP3', M+': BUSY branches of cmd17..20', T('X7 BUSY path')+'; '+T('C17-6'), RUN, 'PASS', HL)
r('HP4', M+': rpi3_mon_health_fill', T('HP4 health pair'), RUN+'; mutants "health age not saturated", "health skip_count not saturated", "health age in ticks instead of ms" caught', 'PASS', HL)
r('HP5', M+': rpi3_mon_health_fill, rpi3_mon_cmd20', T('N1/N5/N7/HP5'), RUN, 'PASS', HL)
r('HP6', 'rpi3_can_monitor.h: _Static_assert HP6', 'compile-time assert on the bit layout (HP_SKIP_SHIFT == 8, flag bits below 0x100)', 'build logs final_*_h1.log exit 0', 'PASS', "Reserved bits being zero is shown by the HP4 test, not by the assert.")
# ---- command 18
r('Q1', M+': rpi3_mon_cmd18', T('Q1/Q4 command 18'), RUN+'; mutants "command 18 allows 6 entries", "command 18 \'more\' flag missing", "command 18 order broken" caught', 'PASS', HL)
r('Q2', 'rpi3_can_monitor.h: _Static_assert Q2', 'compile-time assert 5 x sizeof(event) <= 128', 'build logs; hold_memcpy_h1fix_20261107.txt', 'PASS', "")
r('Q3', M+': rpi3_mon_ev_append (pinned slots)', T('EV2/Q3')+'; '+T('G8 ring overflow'), RUN+'; mutants "pinned_used not maintained", "pinned slots can be overwritten" caught; "command 20 pinned_used missing" caught', 'PASS', HL)
r('Q4', M+': rpi3_mon_cmd18 (64-slot scan)', T('Q1/Q4 command 18')+' (static assert: pinned + ordinary slots == 64)', RUN, 'PARTIAL', "Host part verified: the scan is bounded by the 64 slots (compile-time assert, loop structure). The on-board part, hold time of this scan inside check H3, is NOT RUN: the H3 board runs timed command 20 (status), not command 18, so the hold time of the 64-slot scan itself was not measured (it needs the test image and a loop of command 18).")
r('Q5', M+': rpi3_mon_ev_append', T('Q5 per-class diagnostic limit'), RUN+'; mutants "limit boundary off by one", "one shared limit bucket for all classes", "evidence flood rule reverted", "exact counter not kept" caught', 'PASS', HL)
# ---- command 17
r('C17-1', M+': rpi3_mon_cmd17', T('C17-1'), RUN+'; mutant "length not enforced (buffer read)" caught', 'PASS', HL+" The 'buffer is not read' part uses an unreadable guard page.")
r('C17-2', M+': rpi3_mon_cmd17', T('C17-2'), RUN+'; mutants "stream list not enforced", "can_id range not enforced" caught', 'PASS', HL)
r('C17-3', M+': rpi3_mon_cmd17', T('C17-3 AUTH_FAIL')+'; '+T('C17-3b'), RUN+'; mutants "unauthenticated frames are applied", "tag compared over 31 bytes", "HMAC over the whole frame" caught', 'PASS', HL)
r('C17-4', M+': rpi3_mon_frame_apply', T('C17-4/5'), RUN+'; mutants "older counter accepted as duplicate class", "no duplicate/conflict distinction", "gap detail off by one", "DUPLICATE mapped to the wrong event detail" caught; "counter wrap guard removed" is equivalent', 'PASS', HL)
r('C17-5', M+': rpi3_mon_frame_apply, rpi3_mon_traffic_refresh', T('C17-4/5'), RUN+'; mutant "every frame refreshes the state" caught', 'PASS', HL)
r('C17-6', M+': rpi3_mon_cmd17', T('C17-6'), RUN, 'PASS', HL)
r('C17-7', M+': rpi3_mon_cmd17', T('C17-7'), RUN+'; hold_memcpy_h1fix_20261107.txt (copy-in before the hold, HMAC outside every hold)', 'PASS', HL)
r('C17-8', M+': rpi3_mon_ct_equal', 'source review: the loop ORs the XOR of all 32 bytes with no early exit; '+T('C17-3b'), 'mutant "tag compared over 31 bytes" caught', 'PASS', "Constant time is by code inspection (no data-dependent branch or early exit in the source). The compiled AArch64 loop was not inspected for timing and no timing measurement was made.")
r('C17-9', M+': rpi3_mon_cmd17', T('C17-1'), RUN, 'PASS', HL)
# ---- command 19
for i in ('ACK1', 'ACK2', 'ACK3'):
    r(i, M+': rpi3_mon_cmd19', T('ACK1-4'), RUN+'; mutants "command 19 not clamped", "can move backwards", "resets the lost counter" caught', 'PASS', HL)
r('ACK4', M+': rpi3_mon_cmd19', T('ACK1-4')+'; '+T('GLUE parameter mapping'), RUN, 'PASS', HL+" The BUSY half of the health-pair rule is covered by the general BUSY tests (HP2), not by an ACK-specific one.")
# ---- command 21
for i, m in (('L21-1', ''), ('L21-2', 'mutant "command 21 request not clamped"'), ('L21-3', 'mutant "NO_CHANGE for a lease it just expired"'), ('L21-4', 'mutant "command 21 cap not enforced"'), ('L21-5', 'mutant "command 21 renewal is additive"'), ('L21-6', 'mutants "return does not restart the window", "grant clears a raised timeout", "return consumes a use"'), ('L21-7', ''), ('L21-8', 'mutant "NO_CHANGE for a lease it just expired"'), ('L21-9', '')):
    lim = HL
    if i == 'L21-9': lim += " Event counts for GRANTED and MODE_CHANGED are asserted; the interaction of the per-class limit with exact counters is covered by Q5, not by a command 21 specific test."
    r(i, M+': rpi3_mon_cmd21', T('L21-1..9'), RUN+('; '+m+' caught' if m else ''), 'PASS', lim)
# ---- command 22
r('N22-1', G+': rpi3_mon_pta_cmd22', T('HP1 exact param_types')+'; '+T('DS4'), RUN, 'PASS', HL+" The order (types checked in the glue before any state check) is by code structure; the combination 'wrong types AND failed init' was not run.")
r('N22-2', M+': rpi3_mon_cmd22', T('N22 command 22')+'; '+T('N22-3 MISSED'), RUN, 'PASS', HL)
r('N22-3', M+': rpi3_mon_cmd22', T('N22 command 22')+'; '+T('N22-3 MISSED'), RUN+'; nonce equals the independent Python HMAC vector (gen_vectors.py)', 'PASS', HL)
r('N22-4', M+': rpi3_mon_cmd22', T('N22 command 22'), RUN+'; mutant "command 22 always recomputes" caught', 'PASS', HL)
r('N22-5', M+': rpi3_mon_cmd22', T('N22-5'), RUN+'; mutant "command 22 stores without the sequence check" caught', 'PASS', HL)
r('N22-6', M+': rpi3_mon_cmd22', T('N22-6/7'), RUN, 'PASS', HL)
r('N22-7', M+': rpi3_mon_cmd22', T('N22-6/7'), RUN+'; mutant "command 22 mac guard not used" caught', 'PASS', HL)
r('N22-8', M+': rpi3_mon_cmd22', T('N22 command 22'), RUN+'; hold_memcpy_h1fix_20261107.txt (HMAC call outside every hold)', 'PASS', HL)
r('N22-9', M+': rpi3_mon_cmd22', T('N22 command 22'), RUN+'; mutants "command 22 input layout: boot_id missing", "nonce words swapped" caught', 'PASS', HL)
r('N22-10', G+', '+M, T('N22 command 22')+'; '+T('N22-6/7'), RUN, 'PASS', HL)
# ---- command 23
n23 = T('N23 command 23')
r('N23-1', M+': rpi3_mon_cmd23', n23+'; '+T('DS4'), RUN, 'PASS', HL)
r('N23-2', M+': rpi3_mon_cmd23', n23, RUN+' (event counts UNISSUED 2, STALE 1 added 2026-11-07); mutants "command 23 stale answer accepted", "command 23 never-issued answer accepted", "command 23 stale answer logs the wrong event", "command 23 unissued answer logs the wrong event" caught', 'PASS', HL)
r('N23-3', M+': rpi3_mon_cmd23', n23, RUN+'; mutants "bad nonce changes state", "bad nonce logs no event", "accepts a never-fetched nonce", "compares nothing" caught', 'PASS', HL)
r('N23-4', M+': rpi3_mon_cmd23', n23, RUN+'; mutants "on-time answer keeps the miss count", "does not clear the raised timeout" caught', 'PASS', HL)
r('N23-5', M+': rpi3_mon_cmd23', n23, RUN+'; mutant "late answer resets the miss count" caught', 'PASS', HL)
r('N23-6', M+': rpi3_mon_hb_issue_if_due', n23, RUN+'; mutant "a new challenge keeps the old nonce valid" caught', 'PASS', HL)
r('N23-7', M+': rpi3_mon_cmd23 (evaluate before compare)', n23, RUN, 'PASS', HL)
r('N23-8', M+': rpi3_mon_cmd23', n23+' (asserts zero HMAC calls)', RUN+'; objcalls_monitor.txt and hold_memcpy_h1fix_20261107.txt: rpi3_mon_cmd23 calls no hmac or memcpy', 'PASS', HL)
r('N23-9', G+', '+M, n23+'; '+T('GLUE parameter mapping'), RUN, 'PASS', HL)
r('N23-10', M+': rpi3_mon_cmd23', n23, RUN+' (exact event counters UNISSUED 2, STALE 1, BAD_NONCE 3, DUP 2, HB_RECOVERED 1, LATE 1, added 2026-11-07); mutant "duplicate answer logs the wrong event" caught', 'PASS', HL+" Gap found and closed in this pass: before 2026-11-07 no test asserted the BAD_NONCE, STALE, UNISSUED or DUP event counts.")
# ---- dispatch and legacy
r('DS1', G+': rpi3_mon_pta_invoke', T('DS1'), RUN+'; mutant "known set extended to 24" caught', 'PASS', HL)
r('DS2', 'core/pta/rpi3_can_spi_poc.c (guarded hook)', 'object comparison of the Monitor-OFF build against the baseline (see BG3)', 'build_scratch: baseline_stacks_on vs final_off: no instruction differs except 3 trace line-number immediates', 'PARTIAL', "Build level: the disabled build runs the baseline code, so its behavior is unchanged by construction. There is no host test of the disabled build's return codes, and there is no host test of the disabled build's return codes. BOARD, partial (2026-11-09, original Sep-20 image, baseline/board_W4_alreadyon_nomonitor_20261109.txt): a monitor-shaped call (command 20, value-inout + memref + value-outputs) returned BAD_PARAMETERS 0xffff0006, matching the 'other types' half; the VALUE_OUTPUT-only half was NOT RUN on the board.")
r('DS3', G+': rpi3_mon_pta_invoke', T('LG1/LG4'), RUN+'; mutant "WRITE_REG32 no longer refused" caught', 'PASS', HL)
r('DS4', G+', '+M, T('DS4'), RUN+'; mutants "init failure still publishes init_ok", "init failure fails the OS boot" caught', 'PARTIAL', HL+" The 'then board' part is NOT RUN.")
r('LG1', G+': rpi3_mon_pta_invoke', T('LG1/LG4'), RUN+'; mutant "commands 12 refused", "WRITE_REG32 no longer refused" caught', 'PASS', HL+" On the Pi (hardened): command 0 and command 13 were refused with 0xffff000a (board_checks_monitor_candidate_20261107.txt); the other refused ids were not called on the board.")
r('LG2', G+': rpi3_mon_pta_invoke', T('LG2'), RUN+'; mutant "hardened profile allows the debug readers" caught; debug-profile image built (final_on_debug_h1.log)', 'PASS', HL+" Debug profile behavior is tested through the mock profile flag; on the board it has never run.")
r('LG3', G+': rpi3_mon_pta_invoke', T('LG3')+'; BOARD: verify_boot_monitor_candidate_20261107.txt', RUN+'; on the Pi (monitor candidate): --fiq-count (cmd 1), --fiq-control (cmd 2) and --core1-heartbeat (cmd 12) all worked, heartbeat rising (baseline/verify_boot_monitor_candidate_20261107.txt, board_checks_monitor_candidate_20261107.txt)', 'PASS', HL+' Board evidence: one kernel build, hardened profile only.')
r('LG4', G+', '+M+': rpi3_mon_note_legacy', T('LG1/LG4'), RUN+'; mutants "legacy event path spins for the lock", "legacy refusal logs nothing" caught', 'PASS', HL)
r('LG5', 'core/pta/rpi3_can_spi_poc.c and the PTA header', 'object comparison of Monitor-OFF vs baseline; static_checks BOUNDARY rows (+25 -1 lines, all guarded)', ST+'; build_scratch comparison', 'PARTIAL', "Build level: identical code except 3 trace line numbers. BOARD, partial (2026-11-09, original Sep-20 image, baseline/board_W4_alreadyon_nomonitor_20261109.txt): the legacy wake command returned ALREADY_ON (PSCI -4) three times, as the baseline does; the other legacy commands were NOT run on the original image.")
# ---- events, timing
r('EV1', 'rpi3_can_monitor.h: enum rpi3_mon_event_type; _Static_assert EV1', 'k10_k11_check.py -> k10_k11_check_20261107.txt', 'all 25 event numbers equal the approved K10 list; compile-time asserts 6 evidence + 25 total', 'PASS', "Four names are abbreviated in the header (HB_RECOVERED, LEGACY_REFUSED, LEGACY_USED, DUP_ANSWER); numbers and order match. A recorded deviation, not a mismatch.")
r('EV2', M+': rpi3_mon_ev_append', T('EV2/Q3'), RUN, 'PASS', HL)
r('EV3', M+': rpi3_mon_ev_append', T('EV3'), RUN+'; mutants "evidence flood rule reverted", "pinned slots can be overwritten" caught', 'PASS', HL)
r('EV4', M+': rpi3_mon_cmd17 verdict switch', T('C17-4/5')+'; '+T('C17-2'), RUN+' (event counts GAP 1, RESYNC 1, REPLAY 3 with details 1/2/3, MALFORMED, AUTH_FAIL); mutant "DUPLICATE mapped to the wrong event detail" caught', 'PASS', HL)
r('EV5', M+': rpi3_mon_cmd20 page 1', T('EV5/N2'), RUN+'; mutant "command 20 page 1 counters wrong" caught', 'PASS', HL)
r('EV6', M+': rpi3_mon_evaluate', T('EV6 never-started')+'; '+T('EV6/V12'), RUN+'; mutants "heartbeat detail codes swapped", "heartbeat timeout raised before the grace period" caught', 'PASS', HL)
r('TM1', 'rpi3_can_monitor.h: _Static_assert TM1', 'compile-time asserts D_req < H and T > sender period + jitter', 'build logs; mutants "D_req not below H", "traffic timeout below period + jitter" caught at compile time', 'PASS', "")
r('TM2', 'rpi3_can_monitor.h constants vs MONITOR_STATE_DESIGN.md K11', 'k10_k11_check.py -> k10_k11_check_20261107.txt', 'all 11 constants equal the K11 table (compiled from the real header; the expected values are also matched against the K11 text)', 'PASS', "The constants are provisional by design; Component 4 measures the real ones.")
r('TM3', M+': rpi3_mon_core1_attempt, rpi3_mon_evaluate', T('Y3/Y4/Y5')+'; '+T('EV6/V12'), RUN+' (K and k pinned by literal: CHECK(K_MISS == 3 && K_STARVE == 8)); mutants "heartbeat timeout after a single miss", "starvation threshold 1" caught', 'PASS', HL)
# ---- build gate, keys
r('BG1', 'core/arch/arm/plat-rpi3/conf.mk', 'static_checks BG1 (two rows)', ST, 'PASS', "")
r('BG2', 'conf.mk, sub.mk, core/pta/sub.mk', 'static_checks BG2 (three rows)', ST, 'PASS', "The error() rule itself was not triggered by a deliberate build with the monitor on and the PTA off; it is checked by text.")
r('BG3', 'Monitor-OFF build', 'object comparison baseline_stacks_on vs final_off', 'same size; disassembly differs only in 3 mov w1,#<line> immediates (trace line numbers); 11 differing bytes = 6 timestamp + 5 inside those instructions; 0 rpi3_mon symbols in OFF', 'PASS', "Scratch builds with DEC-BUILD-1 stack checks on; not the project's own out/arm.")
r('BG4', 'build system', 'scripts/build_optee_scratch.sh ... CFG_RPI3_CAN_MONITOR=y (hardened) and =y CFG_RPI3_CAN_MONITOR_DEBUG=y', 'final_on_h1.log, final_on_debug_h1.log: exit 0, 0 warnings', 'PASS', "Scratch output only; the full SD image pipeline has not been run with the monitor.")
r('BG5', 'source_tests/harness.c, Makefile', 'harness.c line 8: #include <rpi3_can_monitor.c> from the OP-TEE tree; Makefile compiles the real glue by path', 'Makefile PLAT path = optee_full/optee_os/core/arch/arm/plat-rpi3', 'PASS', "The same file is compiled, with different bindings (mock vs OP-TEE) and a different compiler target.")
r('KY1', M+', '+G+', rpi3_can_monitor_keys.h', 'k10_k11_check.py (three keys, 32 bytes each, pairwise distinct); static_checks KEY rows', 'k10_k11_check_20261107.txt; '+ST+': keys appear only as arguments of rpi3_mon_hmac; glue and bindings never name a key; test key not instantiated', 'PASS', "'Never returned' is by static pattern plus the fixed output structures, not by exhaustively testing every output path.")
r('KY2', M+': rpi3_mon_init_crypto', T('INIT-3'), RUN+'; mutants "init ignores an RNG failure", "init ignores a boot_id HMAC failure" caught', 'PASS', HL)
assert set(rows) == set(orig), set(rows) ^ set(orig)
cnt = collections.Counter(v[3] for v in rows.values())
o = ["# COMPONENT_2_2_INTERFACE_INVENTORY.md - evidence mapping of the 86 interface and implementation checks\n",
 "Date: 2026-11-07. Generated by `source_tests/make_inventory86.py`. IDs, check text and kinds are parsed from `MONITOR_STATE_DESIGN.md` (section 4j, \"Interface and implementation checks added 2026-11-06\"); these are **not** the original 53 (`COMPONENT_2_2_INVENTORY.md`). Harness line numbers are looked up by title when the file is generated.\n",
 "Result vocabulary: **PASS** = verified by the cited real evidence; **PARTIAL** = the host or build part is verified and the row also names a board part that is NOT RUN; **NOT RUN** = no evidence; there is no FAIL and no NOT VERIFIED row. Evidence classes: HOST (real monitor source on the host with mock bindings, not hardware), STATIC, BUILD, OBJECT. **Nothing here is hardware evidence.**\n",
 "## Summary\n", "| Result | Rows |\n|---|---|"]
for k in ('PASS', 'PARTIAL', 'NOT RUN', 'FAIL'): o.append("| %s | %d |" % (k, cnt.get(k, 0)))
o.append("| **Total** | **%d** |\n" % sum(cnt.values()))
o.append("The design table's `HOST MODEL PASS` column (a separate reference model, `sync_model/`) is deliberately not used as evidence here; every PASS cites the real source.\n")
o.append("## Gaps found while mapping (and closed)\n\n- **N23-2, N23-3, N23-10:** no test asserted the event counters for `BAD_NONCE`, `STALE_ANSWER`, `UNISSUED_ANSWER`, `DUP_ANSWER`; only state was checked. Added exact assertions to `harness.c` (409 checks now) and four mutants that corrupt those events (all caught). The source was correct; no source change.\n- Evidence vintage: the test run after this addition is `run_interface_20261107.txt`; the other logs are the `*_closure_20261107.txt` and `*_h1fix_20261107.txt` runs, which are identical for every other row.\n")
o.append("## Mapping\n")
o.append("| Check ID | Check (design text) | Source location | Test or static check | Evidence | Result | Limitation |\n|---|---|---|---|---|---|---|")
esc = lambda x: x.replace('|', '\\|')
for i, (txt, kind) in orig.items():
    loc, test, ev, res, lim = rows[i]
    o.append("| %s | %s | %s | %s | %s | **%s** | %s |" % (i, esc(txt), esc(loc), esc(test), esc(ev), res, esc(lim)))
open('../COMPONENT_2_2_INTERFACE_INVENTORY.md', 'w').write("\n".join(o) + "\n")
print(dict(cnt), len(rows))
