# IMPLEMENTATION_COMPONENTS_2_TO_5_PROGRESS.md

Last updated: 2026-11-07 (night). Source of truth: `Secure_CAN_Monitor_Plan_8_Components.md` (sections C2 to C5). This file is the durable state for resuming work.

## Rules in force while the user is away (from the user's autonomous prompt)
No flashing, no block-device writes, no reboot or power-cycle of the Pi, no boot-config or device-tree change, no production keys, no deleting backups, no `git stash`. Software work, host tests, scratch builds, object-code checks and read-only SSH checks are allowed. Hardware items are marked NOT RUN, never claimed.
(Earlier in the day the user approved flashing the 8 GB spare; the prompt above supersedes that for unattended work.)

## State of the board (known facts)
Pi 3 B at 10.237.27.215 (DHCP, may change), running the monitor candidate from the 8 GB spare card (sha256 `135c0473b6c87ab7...`). Original 32 GB card untouched. Sep-20 restore onto the spare is proven. `/tmp` on the Pi holds `mon_client`, `atomic_test`, `v14_probe` (RAM, lost on reboot).

## Requirement matrix (plan wording)

| ID | Requirement (plan) | Existing implementation | Missing | Status |
|---|---|---|---|---|
| C2 | Monitor foundation: config gate, constants/ABI, keys K_frame/K_hb/K_test, MAC contexts, dispatch, counter validation | `rpi3_can_monitor*.{c,h}`, glue, conf.mk gate. 414 host checks, 141/144 mutants, 55 static checks, 53-row and 86-row inventories | board: W1, X9, H3, W7; literal W2; production keys | **COMPLETE for software**; hardware items NOT RUN |
| C3.1 | One guarded hook in `rpi3_can_server_loop` named `CFG_RPI3_CAN_MONITOR_POLL` | Hook exists, guarded by `CFG_RPI3_CAN_MONITOR` (not a separate `_POLL` flag) | decide: keep single flag (deviation recorded) or add `_POLL` | IMPLEMENTED (deviation) |
| C3.2 | Decision O-01 FIQ policy | D1 Option B decided and implemented: FIQ masked for whole loop, entry assertion, legacy path compiled out | none | **COMPLETE** (host/static/object); board ran 3 boots with FIQ_CONTROL 0 |
| C3.3 | A1.6 stack test on AArch64 (SPSel=0, 4096-byte tmp stack watermark, one HMAC at depth plus a FIQ) | not done | needs a test build and the board. Under D1 Option B no FIQ ever fires on Core 1 and Core 1 never calls HMAC (proved by call graph), so the FIQ-on-top-of-HMAC case is unreachable by design; a watermark measurement remains | static analysis DONE (640 B normal path / 2000 B incl. fatal path of 4096 B, margin 2048 proposed); board watermark RUN 2026-11-09: 688 B deepest of 4096 B, headroom 3408 B | **COMPLETE** (margin 2048 proposed, awaiting confirmation) |
| C3.4 | Allowed/forbidden Core 1 call list (O-13) | `callgraph.py` audit from `rpi3_can_server_loop`: 72 functions, none forbidden | document written: `COMPONENT_3_CORE1_CALL_LIST_AND_SYMBOL_DIFF.md` | COMPLETE (host/object) |
| C3.5 | Acceptance: `nm` diff shows only new symbols; regression clean | OFF vs baseline identical except timestamp and 3 line-number immediates; verify_boot 16/0 on the monitor build | done: `COMPONENT_3_CORE1_CALL_LIST_AND_SYMBOL_DIFF.md` | IMPLEMENTED (symbol diff written; see the deviation: 3 legacy symbols dropped by D1) |
| C4.1 | Loop timing behind `CFG_RPI3_CAN_LOOP_STATS` | measured without a flag: 75.2 ns per pass from `poll_count` over `now_ticks` (two runs) | the guarded in-firmware stats build (needs flash) | COMPLETE for the figures (see COMPONENT_4_TIMING_AND_MEASUREMENT.md; flag deviation) |
| C4.2 | PTA latency harness (100k calls under idle/CPU/SPI/CAN flood) | `tools/monitor_client/latency_probe.c` (one session, command 20, CLOCK_MONOTONIC_RAW). **Idle: 100,000 calls done; CPU load: 20,000 calls done** (below) | SPI flood and CAN flood need a sender or a second board; loopback numbers are not valid evidence | PARTIAL (idle and CPU done, flood BLOCKED) |
| C4.3 | Sender (absolute-deadline `clock_nanosleep`, SCHED_FIFO, send log) | none | needs a CAN-FD sender or a second board; user has neither | BLOCKED (hardware) |
| C4.4 | Gap histogram | none | depends on C4.3 | BLOCKED |
| C4.5 | CNTPCT sanity on Core 1 (prerequisite of C5) | secure counter 19.2 MHz confirmed by kernel log and bracketed against wall clock (18.7 to 19.7 MHz, coarse); Core 1 poll age 4.6 to 5.5 us, consistent with the PTA's reading | a direct Core 1 vs PTA monotonicity run over minutes | **COMPLETE** (board 2026-11-09, 3000 samples, PASS) |
| C4.6 | `derive_params.py` producing `mon_params.h` (D_req < H, 32T >= 1.25(grace+R), ring >= N_req, default 128 records) | constants in `rpi3_can_monitor.h` with static asserts for part of it | the script and the derived header, driven by measured values | IMPLEMENTED (rules b and c NOT CHECKED: R, grace, N_req undefined) |
| C4.7 | Acceptance: recorded loop time, latency percentiles, jitter; "loopback numbers are not valid evidence" | loop time recorded; latency and jitter not | see C4.2, C4.3 | PARTIAL |
| C5.1 | Core 1 issue schedule (no burst, next_issue = issue_ticks + H) | implemented; host test "no burst" and new stall test | none | **COMPLETE** (host) |
| C5.2 | cmd 22 lazy nonce; cmd 23 per R17 check order | implemented and tested (88 checks across N22-x, N23-x) | board shows cmd 22/23 only by `answer` path, not yet run | COMPLETE (host); board NOT RUN |
| C5.3 | per-request deadline, k misses, boot grace G, NEVER_STARTED | implemented; board shows `TIMEOUT detail 3` after 60 s with no responder | a responder run on the board | COMPLETE (host); board partial |
| C5.4 | traffic deadline (R3), exactly-once transitions | implemented and tested | traffic test needs frames from the Pi (software frames can be sent with `mon_client frame` if K_frame is supplied) | COMPLETE (host) |
| C5.5 | `mon_catchup()` for starvation (O-05) | no function of that name; behavior covered by evaluate-on-success and the stall test | decide whether a separate function is needed | PARTIAL (behavior verified, name absent) |
| C5.6 | FIQ mask around Core 1 critical sections (O-06) | satisfied by D1 Option B (mask everywhere) | none | COMPLETE |
| C5.7 | Acceptance: T1-T25 subset plus T5b, T10b, T11a-c, T18e, heartbeat half of T18i; stall 3H gives one MISS, one issue, no MON_STARVED | stall case now tested on the host (this session, 5 checks); the T-numbers are defined in a plan document not present in this tree | locate the T-list; run those needing the board | PARTIAL |

## Done this session (2026-11-07 night)
- **PTA latency (C4.2), measured on the Pi at 600 MHz (powersave), monitor candidate, command 20, one session:**
  - idle, 100,000 calls: 0 BUSY, 0 errors, 0 lock timeouts; min 3.46 ms, p50 3.88, p90 3.90, p99 4.30, p99.9 4.76, max 6.60 ms; 257 calls/s (`baseline/pta_latency_idle_monitor_20261107.txt`)
  - 3 busy loops on the Linux cores, 20,000 calls: 0 BUSY, 0 errors, 0 lock timeouts; p50 4.36 ms, p90 9.54, p99 9.99, p99.9 10.97, max 13.02 ms; 180 calls/s (`baseline/pta_latency_cpuload_monitor_20261107.txt`)
  - Core 1 lock collisions: 60,582 over the 100,000 idle calls (0.61 per call). INFERENCE, not a measurement: if Core 1 attempts the lock every 19.2 us and a collision happens whenever an attempt lands inside the PTA's hold, the PTA hold per status call is about 0.61 x 19.2 us = 11.6 us. It rests on the attempts being periodic and on no other lock holder; the real hold time still needs the H3 test build.
- C5 stall acceptance test added to the host harness (`t_stall`): harness now 414 checks, 0 failed.
- Timer facts: kernel `arch_timer: cp15 timer(s) running at 19.20MHz (phys)`; wall-clock bracket 18.7 to 19.7 MHz; Core 1 poll age 4.6 to 5.5 us.
- Measured Core 1 pass time 75.2 ns (13.29 M passes/s) and lock attempts about 52,000/s; finding recorded in `COMPONENT_2_2_FINAL_CLOSURE.md` section 14. Decision on `RPI3_MON_CORE1_CADENCE` is open (raise to about 8192, or keep after the H3 hold-time measurement).

## Where the user is needed (nothing below can be done while the user sleeps)
1. **Card swaps (flash of a new test image)**, each needing: Pi off, 8 GB card to the laptop reader, explicit go-ahead. Needed for: the cadence change; `CFG_RPI3_CAN_LOOP_STATS` and hold-time (H3/X9) test builds; the C3 stack watermark test; the Core 1 CNTPCT run inside firmware.
2. **A CAN sender or a second CAN board** for C4.3, C4.4, C4.7 and the traffic half of C5 acceptance. The user has neither.
3. **An external drive** for the backup copy (P1) before further risky flashes.
4. **UART adapter** (optional but strongly useful): W2 literal form, any boot failure of a new image.
5. **A decision on the cadence constant** and on whether `CFG_RPI3_CAN_MONITOR_POLL` should be a separate flag.
6. **The T1-T25 test list**: the plan refers to it; it is not in this tree. Please point to the file.
7. **Production key provisioning**: a separate design decision.

## Next software tasks (no hardware)
1. `derive_params.py` + `mon_params.h` with asserts, fed by measured values (C4.6).
2. Write the Core 1 allowed/forbidden call list document from the call-graph output (C3.4) and an `nm` symbol diff (C3.5).
3. Decide and record `CFG_RPI3_CAN_MONITOR_POLL` handling (C3.1).
4. Prepare, but not flash, a test build with `CFG_RPI3_CAN_LOOP_STATS` and hold-time stamps, with its host tests and a scratch build (C4.1, H3, X9).
5. Keep this file and the evidence file current.

## Update 2026-11-08: TEST-STATISTICS firmware build prepared (NOT flashed)
Flag `CFG_RPI3_CAN_MONITOR_TEST_STATS` (default n; requires the monitor; test builds only). Adds: lock-hold stamps (forced-inline, hook-free) for PTA and Core 1 holds with a log2 histogram and per-context maxima (**H3**); Core 1 attempt-interval statistics (**C4.1**); the wait time and attempts of the last exhausted PTA acquire (**X9**); a Core 1 hold request clamped to 10 ms (**X9**); test-only PTA commands 24 (read, 192 bytes) and 25 (control). Snapshot before edit: `source_snapshots/optee_os_20261108_teststats_pre/` (7 files, verified).
Verified: host harness 417 checks (flag off) and 430 checks (flag on, ASan/UBSan, strict warnings); static checks 55 pass; scratch builds OFF, default ON and TEST ON all exit 0, 0 warnings; default ON identical to the previous ON image except trace line numbers; OFF identical to the baseline except trace line numbers; TEST build: hooks fully inlined (no symbols), lock-path entry counts unchanged (PTA 7/9, Core 1 4/5 and 1/2), no call inside any hold, Core 1 call graph 72 functions with nothing forbidden; client struct layout equals the real one (192 bytes). Candidate image: `build_scratch/candidate_image/rpi3-sdcard-monitor-teststats.img` (sha256 `dc1f96c5dbb4ee9281a8...`, one boot file differs, rootfs identical). Tools (cross-built, not yet on the Pi): `tools/monitor_client/tstats_probe` (dump, reset, x9).
**NEXT, needs the user:** the card swap to flash this image; then `tstats_probe reset`, a 100,000-call `latency_probe`, `tstats_probe dump` (hold-time max/p99), `tstats_probe x9 200`. Results decide the cadence constant. After measuring, return the Pi to the hardened candidate (proven image) and mark H3/X9 accordingly.

## Update 2026-11-08 (swap-free, SSH only): functional tests on the real board
Heartbeat responder, frame verdicts and the traffic deadline all behave as designed on the Pi (`baseline/board_functional_monitor_candidate_20261108.txt`). Observation worth keeping: a responder must poll faster than D_req (500 ms) to stay on time; at 1 s polling the phase drifts against Core 1's fixed 1 s schedule and answers turn LATE (late answers do not clear misses, by design). The prototype K_frame was supplied from a 0600 file in the Pi's RAM and deleted afterwards. Monitor RAM state on the Pi is now non-default (traffic armed, counters raised); a reboot resets it.
Still needing the user: the card swap for the test-statistics image (H3, X9, in-firmware C4.1).

## Update 2026-11-08 (swap-free, user away from the lab): improvements made
1. **Sender/soak tool (plan C4.3 style, C8 soak):** `mon_client soak SECONDS --key-file F [--period-ms N] [--rt]`: one session, absolute-deadline `clock_nanosleep`, a valid frame every period, heartbeat answered promptly, jitter and latency percentiles, anomaly exit code. Frames are injected through the PTA, so it is NOT CAN-bus evidence. Found and fixed a leak in my own first version through AddressSanitizer. Host integration test now 104 checks. Board: 60 s soak = 599 of 599 frames ACCEPTED, heartbeat 60 on time (+1 late first), 0 BUSY, 0 lock timeouts, send lateness max 80 us.
2. **C3 documents:** `COMPONENT_3_CORE1_CALL_LIST_AND_SYMBOL_DIFF.md` (allowed/forbidden call list from the 72-function audit; nm diff: 43 symbols only in ON, **3 only in OFF** = the legacy chip-A frame processing, dropped by D1 Option B: a deliberate deviation from the plan's "only new symbols" wording; hook-flag decision recorded).
3. **Design document 4b.2:** measured Core 1 pass 75.2 ns and the resulting lock attempt every about 19 us recorded next to the old assumptions (backup `.bak13`).
4. **Test image improved before its swap:** run-time cadence option (test build only): one flash can measure H3/X9 at several cadences. 439 host checks (flag on), 417 (flag off); default ON image unchanged. New image hash in `candidate_image/README.md`.
Open decisions unchanged: cadence value; `CFG_RPI3_CAN_MONITOR_POLL` (kept as one flag, recorded).

## Update 2026-11-08 (later): soak results, a client bug found by the soak, and a safety guard
- **10-minute soak on the real Pi (normal hardened image):** 5,999 of 5,999 software-injected frames ACCEPTED; 0 BUSY, 0 errors, 0 lock timeouts, no starvation; 599 heartbeat answers on time; send lateness p50 76 us, p99 97 us, max 10.2 ms (one non-real-time scheduling delay); no panic or oops (the 2 grep hits were the word "debug"). Log: `baseline/board_functional_monitor_candidate_20261108.txt`.
- **Bug found by the soak, in my own client (not in the monitor):** one heartbeat answer was a BAD_NONCE. Cause: when Core 1 issues a new challenge between the two steps of command 22, the monitor correctly returns a zero nonce with the new sequence (design K7: "retry"), and the client submitted the zero nonce. Regression test written first and shown to FAIL on the old client; fix: bounded retry, never submit a zero nonce (`get_nonce_retry` in `mon_cli.c`); client integration test now 106 checks, 0 failed (ASan/UBSan, no leaks). Re-soak 180 s on the Pi with the fixed client: 1,799 of 1,799 ACCEPTED, 180 on time, 0 other.
- **Safety guard added to `static_checks.sh`:** the normal Monitor-ON image must contain no test-only symbol (56 checks now, negative control against the test image fails as it should). On the real Pi the normal image refuses test commands 24 and 25 with 0xffff000a.
Everything above is swap-free. Still needing the user at the lab: flash `candidate_image/rpi3-sdcard-monitor-teststats.img` (H3, X9, in-firmware loop stats, runtime cadence experiments).


## Update 2026-11-07 (board, test-statistics image)
H3 PASS (max PTA hold 6.30 us vs target 100 us), X9 PASS (BUSY 200/200, wait 1.002 ms), cadence sweep done; 11.6 us hold inference withdrawn. Open: cadence decision (recommend 16384), return Pi to the hardened image. Details: EVIDENCE file, same date.
