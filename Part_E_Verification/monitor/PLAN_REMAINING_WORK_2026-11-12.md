# PLAN FOR THE REMAINING WORK (STEP 2), written 2026-11-12 ~03:00. Evaluation: Saturday 2026-11-14. PLAN ONLY: nothing below has been done; every unit waits for your approval.

Principle: a verified working state beats new features. The Pi runs a proven image today (hardened c16384, `verify_boot` 16/0, C5 26/0). Every new Secure World change costs one card swap plus a full regression, so **all Secure World changes you approve are bundled into ONE image and ONE flash**, and the current image stays as the rollback.

## 0. Facts this plan rests on (all checked on 2026-11-12, source in `optee_full/optee_os`)
- Legacy ids: 0 `READ_OSC_REG`, 3 `ARM_FIQ`, 6 `READ_REG32`, 7 `TRANSMIT_TEST_FRAME`, 13 `WAKE_CORE1`, 14 `WRITE_REG32` are refused in **both** profiles with `NOT_SUPPORTED` before any parameter check plus a `LEGACY_CMD_REFUSED` event (`pta/rpi3_can_monitor_pta.c`). Ids 4, 5, 8, 9, 10, 11, 15, 16 (chip-A debug readers) are refused in the hardened profile, allowed with `LEGACY_CMD_USED` in the debug profile. Ids 1, 2, 12 stay on the legacy path (used by `verify_boot`). Host test `LG1/LG4` (harness.c:430) already covers ids 0,3,6,7,13,14.
- **Every legacy handler checks its parameter types before it acts** (`pta/rpi3_can_spi_poc.c`: generic `exp_pt` before the switch for 0, 3, 13; per-command checks for 6, 7, 14). So a board call with deliberately WRONG parameter types is harmless even if a refusal were missing: refused = `NOT_SUPPORTED`, not refused = `BAD_PARAMETERS` (a FAIL, but nothing is armed, written or transmitted). This makes a safe board T25 possible, including id 3.
- Event limiter today (`rpi3_mon_ev_append`, rpi3_can_monitor.c:259): first 4 events of each of the 6 evidence classes pinned; every other event limited to 1 per second per class; exact counters always exact; an overwrite of an unread ordinary slot is counted in `ev_lost`. Ring = 24 pinned + 40 ordinary.
- F24 flood data: `pta_lock_timeouts` stayed 0 and BUSY-after-retries stayed 0 in all four runs (`baseline/f24_run*_20261112.txt`); the delay arises in OP-TEE's PTA serialisation **before** the monitor lock. Therefore a hint built on BUSY/skip counters (as in your brief) would **not have fired** in the observed case. Alternative signal in U4.
- Nested repos: `optee_os` (remote `github`), TF-A (remote `tfo`), `build` (remote `github`). The top-level repo ignores `optee_os`; your backup commit `91be7d8` holds the Secure World source only as `source_backups/optee_os-latest.bundle` (made 01:54): **my later optee_os commits (IDLE_EXPIRED, UART, digit rewrite) are not in it.** Each unit therefore also refreshes the bundle and the build patch.
- Already built and host-tested, flag-off: `IDLE_EXPIRED` (= `MODE_CHANGED` detail 2) and the UART `ALIVE` line (`CFG_RPI3_CAN_MONITOR_UART`, default n). Evidence: `COMPONENT_6_STAGE_AB_EVIDENCE.md`.

## 1. Decisions I need from you (the plan depends on them)
| # | Question | Options and consequences | My recommendation |
|---|---|---|---|
| D-A | F27: coalescing? | **A** none: document (limiter already exists; ring wraps only if events stay unread). **B** class-specific windows (10 s) for the flapping classes `TIMEOUT`, `TRAFFIC_RECOVERED`, `HEARTBEAT_RECOVERED`, `LATE_ANSWER`, `DUPLICATE_ANSWER`, plus a suppressed-count in the event `flags` field (no layout change). Changes an approved rule (G8-6). **C** larger ring (memory and ABI cost). | **B** only if you want it for the demo; otherwise A. Measurable goal for B: `ev_lost == 0` in a 60 s N=24 flood, counters exact. |
| D-B | "Possible PTA contention" hint | **A** your brief's BUSY/skip-based signal: would not have fired in the F24 data. **B** lock-free PTA call-rate counter at the glue entry; Core 1 sets a hint bit (0x100) in the traffic-TIMEOUT detail when the rate over the last second exceeds a threshold (about 100 calls/s; a legitimate consumer is about 12 to 25 calls/s). A heuristic ("possible"), detection only. **C** drop it. | **B** or C. Not A. |
| D-C | UART `ALIVE` line | Leave the flag **off**, state "implemented, host-tested, object-code checked, NOT verified on hardware" in `RESULTS.md`. Needs an adapter wiring to verify. | Leave off (as you suggested). |
| D-D | Number of images | One image bundling `IDLE_EXPIRED` plus whatever of D-A/D-B you approve. | One. |
| D-E | Pushes | After each unit: top-level repo to `origin nov-2026-backup` (your instruction). Nested repos (`github`, `tfo` remotes) only on your explicit OK. | Top-level only; bundle refreshed. |
| D-F | Soaks use software-injected frames (no CAN sender) | They test the monitor, not the bus. Stated in `RESULTS.md`. | Accept. |

## 2. Units (each: files and functions, rule, host tests first, board test, pass criteria, risk, rollback)

### U0. C5 formal close and status board (documentation only; no code, no board)
- **Files:** `Secure_CAN_Monitor_Plan_8_Components.md` (board rows C3, C4, C5, C6; delete the stacked stale C4 text; add the missing 2026-11-12 progress-log entries for N-04, F24, C4 close, C5, C6; fill the C3-C8 "Progress log." placeholders with pointers); `monitor/COMPONENT_5_HEARTBEAT_AND_TRAFFIC_DEADLINE.md` (add the acceptance map in section 3 below; correct my wrong `T10b` label); `monitor/NEXT_STEPS.md` and `PROGRESS_REPORT_2026-11-11.md/.pdf` (regenerate, they are stale).
- **Rule:** "never mark DONE because a document exists": every status cites a log or build output.
- **Test:** a small script that extracts every path cited in the plan and the C-records and checks it exists (the audit found `RESULTS.md` missing, which is correct for now).
- **Pass:** script reports 0 dangling references except the declared future files. **Risk:** none. **Rollback:** `git revert`.

### U1. Host tests only: close the C5 gaps found in the audit (no Secure World source change)
- **Files:** `monitor/source_tests/harness.c` (+ `mutation_real.py` run). New tests, written to fail first against a mutated source: **T11b** answer at exactly `issue + D_req` is on time, at `+1 tick` late (strict `>`, rule R17/O-11); **T11a** explicit race: answer handler does OPEN->MISSED itself, logs one miss, then LATE, and Core 1's later pass adds no second miss; **T10b (plan meaning, audit O-10)**: `miss_count` already above `k` when G passes still raises TIMEOUT exactly once (`>=`, not `==`).
- **Board test:** none new (T10 and T20 board data already show miss_count above k at the grace).
- **Pass:** new checks pass on the real source; the mutants (flip `>` to `>=`, `>=` to `==`) are CAUGHT. **Risk:** none (test-only). **Rollback:** `git revert`.

### U2. Board negative-test tool (no Secure World change; runs on the image that is on the Pi now)
- **Files:** new `tools/monitor_client/c6_negative_raw.c` (raw `TEEC_InvokeCommand` with crafted parameter types and sizes), Makefile target.
- **Cases:** (a) legacy ids 0, 3, 6, 7, 13, 14 with WRONG parameter types: expect `NOT_SUPPORTED` and exact `ev_count[LEGACY_CMD_REFUSED]` +6 (a `BAD_PARAMETERS` result = refusal missing = FAIL, harmless); (b) debug readers 4, 5, 8, 9, 10, 11, 15, 16: `NOT_SUPPORTED` in hardened; (c) ids 24 to 40: `NOT_SUPPORTED`; (d) commands 17 to 23 with wrong param types: `BAD_PARAMETERS`, state unchanged (`ev_seq`, counters, last_counter); too-small buffers for 18 and 20: `SHORT_BUFFER` with outputs delivered; (e) ids 1, 2, 12 still work.
- **Host first:** the same matrix against the real glue in the harness (extend `LG1/LG4`).
- **Pass:** all cases as expected, `verify_boot` still 16/0, monitor `starved=0`. **Risk:** low (wrong types prevent any action; checked in source). Needs your OK to run on the Pi (it changes RAM-only counters). **Rollback:** reboot clears RAM state; nothing persistent changes.

### U3. F27 event coalescing (Secure World; only if D-A = B)
- **Files:** `plat-rpi3/rpi3_can_monitor.c` `rpi3_mon_ev_append` (+ a per-class window table and `diag_suppressed[]` in `struct rpi3_mon_lstate`, `rpi3_can_monitor.h`); design note `MONITOR_STATE_DESIGN.md` (G8-6 v2) updated first.
- **Rule:** G8-6 amended (one per window per class, count of suppressed in `flags`, exact `ev_count`, first 4 evidence events pinned and unlimited, classes never suppress each other, overwrite of unread still counted).
- **Host tests first:** extend `Q5`, `EV3`, `G8`: a 10 minute flapping storm (TIMEOUT/RECOVERED alternating every 300 ms) writes no more than the window allows; suppressed count exact; exact counters exact; no cross-class effect; pinned entries untouched. Mutation: remove the window / drop the count: caught.
- **Board test:** repeat the F24 N=24 run: `ev_lost == 0` in 60 s, `ev_count` totals still exact, pinned first events readable.
- **Hold rule:** the function runs inside the lock: no memcpy, no unbounded work; H1 object check must stay green. **Risk:** medium (approved rule changes). **Rollback:** reflash the proven hardened c16384 image (hash in `SD_Card_Backups/SHA256SUMS`); `git revert`.

### U4. "Possible PTA contention" hint (Secure World; only if D-B = B)
- **Files:** `core/pta/rpi3_can_monitor_pta.c` `rpi3_mon_pta_invoke` (one lock-free `MON_PLAT_INC32(&rpi3_mon_pta_calls)` at entry, counts every PTA command including legacy 12); `plat-rpi3/rpi3_can_monitor.c` `rpi3_mon_core1_attempt` / `rpi3_mon_evaluate` (window delta, hint bit 0x100 in the traffic-TIMEOUT detail; low byte stays 1); `rpi3_can_monitor.h`.
- **Rule:** detection only, wording "possible"; no new state is trusted for a decision.
- **Host tests first:** mock counter and time: below threshold no hint, above threshold hint, window rolls, counter wrap, detail low byte unchanged.
- **Board test:** F24 N=8 and N=24: traffic TIMEOUT carries 0x101; a 10 minute normal soak: **zero** false hints.
- **Pass:** both. **Risk:** medium (touches the PTA entry path and Core 1 call list: re-run call graph and stack depth). **Rollback:** as U3.

### U5. UART `ALIVE` line: no unit
Flag stays `n`. The record says "implemented, host-tested (439 checks), object-code checked, **not verified on hardware**". If you later wire the adapter, verification is a separate unit.

### U6. One build, one flash, full regression (Secure World units only)
- **Steps:** snapshot, then scratch builds OFF, ON and **DEBUG** with `PLATFORM=rpi3` (0 warnings); host harness (ASan+UBSan) for the flag combinations; mutation run (`mutation_real.py`, last run 2026-11-09, **not run for the C6 changes**); static checks with H1 object code; Core 1 call graph (expect the 72 set plus only what U3/U4 add) and stack depth (margin 2048 B); compose the candidate armstub image in scratch; back up the current image and its sha256; flash **only the 8 GB spare** via `scripts/card_helper.sh` (you run the two printed commands); boot.
- **Board acceptance:** `verify_boot` 16/0; `c5_board_tests` full run (26 PASS expected, plus `T21d` now PASS); `--c6-negative`; U2 matrix; F24 N=24 rerun; N-04 reservation still present.
- **Stop rule:** any failure: stop, tell you, reflash `rpi3-sdcard-monitor-hardened-c16384.img`. **Risk:** one card swap; no TF-A change; no UART needed.

### U7. C8: soaks and `RESULTS.md`
- **Files:** `tools/monitor_client/soak_watch.sh` (new: samples `status 0/1` every 60 s into a log), `Part_E_Verification/RESULTS.md` (new).
- **Runs on the final image (after U6):** 1-hour soak (`mon_client soak 3600`), then an overnight soak (about 10 hours, started Friday evening); software-injected frames and the heartbeat responder, period 100 ms. Pass: zero `BUSY` after retries, zero lock timeouts, `starved=0` throughout, no panic, every heartbeat answer on time except any recorded start-up one, no TIMEOUT or other alarm event while no fault is injected (that count is the false-positive figure), Core 1 poll age bounded.
- **`RESULTS.md` content:** what was measured and how; the C5 matrix; F24 quantification; false-positive notes from the soaks and missed-detection notes from the fault tests; limits stated plainly: detection only, no TZASC, prototype keys readable by root, RAM-only log with no hash chain, all frames software-injected or loopback (not CAN-bus evidence), UART unverified, 3 of 20 boot IDs, Design 2 not built.
- **Risk:** the Pi must stay powered and the laptop/SSH link stable overnight (the soak runs on the Pi; the laptop only samples). **Rollback:** none needed.

### U8. C7: record "designed, not built"
- **Files:** plan board row C7, `RESULTS.md` section. **Reason:** no second working CAN board (`can1` dead) and no external sender; the plan itself says "without them the report says Design 2 designed, not tested". Not implemented; T26 to T47 and the chip-B rows are listed as not run; derive_params rule (b) (fails with the plan's provisional numbers) is listed as an open design question.

## 3. C5 acceptance list mapped to evidence (the plan's list: T1-T25 subset plus T5b, T10b, T11a-c, T18e, T18i heartbeat half)
| Item | Board | Host | Not run / note |
|---|---|---|---|
| T1, T2, T3, T4 (id, EFF) | PASS (`c5_board_tests_run2_full_20261112.txt`) | C17-x tests | T4 length 43/45: host only (client sends 44 bytes; `C17-1`) |
| T5, T6, T7, T8 | PASS | yes | |
| T5b | not run | n/a | chip-B padding row; the PTA frame has no pad bytes (C7) |
| T9, T12-T16 | PASS | N22/N23 | |
| T10 | PASS | EV6/V12 | |
| **T10b (plan meaning)** | data only: T10 (miss_count 4 > k=3) and T20 (miss_count 50+ at the grace) | EV6/V12, EV6 never-started | **dedicated test missing: U1** |
| T11 | PASS | N23 | |
| **T11a** | not run | N23 advances time past the deadline and calls cmd 23 with no Core 1 attempt (answer handler performs MISSED itself) | explicit race test: **U1** |
| **T11b** | not run | **no exact-boundary test found** | **U1** |
| T11c | PASS | N23 (DUP_ANSWER +2) | |
| T17, T18 | PASS (2 real reboots) | | |
| T18b | 3 boot IDs distinct, plan asks 20+ | | partial; optional unattended 20-reboot run |
| T18e | not run | harness.c:213 (`hb_seq` exhaustion, `HB_SEQ_EXHAUSTED` once) | host only (needs a debug profile to set `hb_seq`) |
| T18i heartbeat half | T19 PASS (8 s, 4 callers) | stress harness `STRESS Core 1 + 3 PTA contexts` | |
| T19, T20, T21, T22 | PASS | | T21: no IDLE_EXPIRED event on the flashed image (U6 fixes) |
| T23 | not run | | needs UART |
| T24 | PASS (`verify_boot` 16/0) for the hardened profile | | debug profile not re-run on the board |
| T25 | not re-run on the board | LG1/LG4 | board version safe with wrong types: **U2** |
| stall of 3H: one MISS, one issue, no MON_STARVED | | `C5 stall of 3H` PASS | |
My earlier board log used the label `T10b` for "recovery after silence"; that is a different check and is relabelled in U0.

## 4. Proposed schedule (today is Thursday; evaluation Saturday)
| When | Work | Needs from you |
|---|---|---|
| Thu (today) | U0, U1 (no risk); decisions D-A to D-F; if approved, U2 on the Pi and U3/U4 host work | answers to D-A to D-F; OK for U2 |
| Fri morning | finish Secure World units on the host; U6 builds, static, mutation; compose image; **flash** | you present for the card swap (about 30 min) |
| Fri midday | U6 board regression; start the 1-hour soak, then the overnight soak | Pi powered all night |
| Sat early | read soak results; write `RESULTS.md`; freeze the repo; final push | OK to push |
**Cut rule:** if the flash has not passed its regression by Friday 14:00, the Secure World changes are dropped and the soaks run on the current proven image (which is already verified); `IDLE_EXPIRED` and the UART line then stay "built, not flashed".

## 5. What I will not do
No TF-A change, no edit of `rpi3_can_fiq.c`, commands 0-16 and their numbers unchanged, no memcpy/memset/unbounded work inside a lock hold, keys and MAC contexts never readable, no writing of the 32 GB card, no push without your OK, no claim beyond the evidence.
