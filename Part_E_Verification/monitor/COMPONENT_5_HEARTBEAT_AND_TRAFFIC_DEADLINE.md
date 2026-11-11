# COMPONENT 5: Heartbeat and traffic deadline (plan S3), board acceptance 2026-11-12

Image on the Pi: hardened c16384 (armstub `775ce384...`), 8 GB spare card, SSH only, no UART. Frames are **injected through the PTA (command 17), not sent on the CAN bus**: this is software-class evidence, not CAN-bus evidence. Prototype keys.
Tool: `tools/monitor_client/c5_board_tests.c` (cross-built `-Werror` clean, runs on the Pi, uses only commands 17-23, no legacy command, no CAN chip, no FIQ). Logs: `baseline/c5_*_20261112.txt`.

## 1. Result
**26 PASS, 0 FAIL, 1 skip** in the full run (`c5_board_tests_run2_full_20261112.txt`), plus the boot-level tests (T17, T18, T20) over two real reboots. Two checks failed on the way, both **bugs in my test, not in the monitor** (section 4).

## 2. Acceptance set (plan C5: T1-T25 except D2-only rows, plus T5b, T11a-c, T18e, heartbeat half of T18i) and evidence
| Test | Result | Evidence / limit |
|---|---|---|
| T1 valid frames | **PASS** board | 5 ACCEPTED, no frame-class events |
| T2 bad tag | **PASS** board | AUTH_FAIL, event +1, traffic age kept growing (deadline not refreshed), last_counter unchanged |
| T3 payload altered, tag kept | **PASS** board | AUTH_FAIL |
| T4 malformed | **PASS** board (id 0x124, EFF flag); **length 43/45 not run on board** | fixed 44-byte client API; covered by host harness command 17 length checks |
| T5 counters 90/100/100'/101/105 | **PASS** board | REPLAY, DUPLICATE, CONFLICT, ACCEPTED, GAP(detail 3), relative to last = 3999 |
| T5b pad bytes | **NOT RUN** | concerns the chip-B path (Component 7); the 44-byte PTA message has no pad bytes |
| T6 counter 0 after 0xFFFFFFFF | **PASS** board | REPLAY, no wraparound (the board was rebooted afterwards) |
| T7 stop sender | **PASS** board | exactly one TIMEOUT(traffic) 296.5 ms after the last accepted frame (T = 300 ms); flag set; TRAFFIC_RECOVERED on resume, flag cleared |
| T8 counter restart | **PASS** board | counter 1 -> REPLAY, TIMEOUT(traffic) after silence (replays do not refresh the deadline) |
| T9 fetch and answer | **PASS** board | ON_TIME, state ANSWERED, no answer-related events |
| T10 consumer stopped | **PASS** board | 4.6 s without answers: miss_count 0 -> 4, exactly one TIMEOUT(heartbeat), flag set; HEARTBEAT_RECOVERED on return |
| T11 late answer | **PASS** board | answer at 700 ms (D_req 500 ms): LATE, state LATE_ANSWERED, LATE_ANSWER +1, miss_count not reset |
| T11a deadline race with a stalled Core 1 | **host only** | `t_stall` in the host harness (needs fault injection) |
| T11b answer exactly at D_req | **host only** | needs tick-exact timing |
| T11c same late answer three times | **PASS** board | 2nd and 3rd -> DUPLICATE, miss_count unchanged, DUPLICATE_ANSWER +2 |
| T12 wrong nonce | **PASS** board | BAD_NONCE, request stays OPEN |
| T13 previous answer resent | **PASS** board | STALE |
| T14 answer twice | **PASS** board | DUPLICATE |
| T15 unissued | **PASS** board (seq + 5); wrong boot_id not testable through the API | UNISSUED |
| T16 consumer restart | **PASS** board | session closed and reopened: same request and nonce, no false event |
| T17 monitor reboot | **PASS** board (2 real reboots) | new boot_id; old consumer values -> UNISSUED; refetch of a fresh request answered ON_TIME (T17c rerun, section 4); first frame -> RESYNC |
| T18 nonce reuse | **PASS** board | nonce at hb_seq 100 differs across two boots (`da0ee04d...` vs `3f90e470...`) |
| T18b boot_id uniqueness | **PARTIAL** | 3 boot_ids seen (`0xb60b72e0`, `0xcc56b0e6`, `0x25071c4d`), all distinct; the plan asks for >= 20 reboots, **not done** (about 1 minute each) |
| T18e hb_seq exhaustion | **host only** | needs the debug profile |
| T18i heartbeat half (exactly-once under contention) | **host + board** | host stress harness (10^4 iterations) and board T19 |
| T19 four parallel callers | **PASS** board | cmds 20/22/23/17 from four sessions for 8 s: 8 of 8 heartbeat answers ON_TIME, 171 frames accepted, 0 BUSY after retries, miss_count 0, no TIMEOUT, not starved |
| T20 grace | **PASS** board, with a stated precision | no consumer commands: CONSUMER_NEVER_STARTED (TIMEOUT detail 3) first seen at Linux time 52.2 s, not present at the first read (27.8 s); consistent with G = 60 s counted from the monitor's start a few seconds before Linux uptime 0. The monitor's init time is not exposed, so the 60 s is not measured to the second |
| T21 idle mode | **PASS** board; **deviation from the plan text** | lease 2000 ms GRANTED, no traffic TIMEOUT during it, TIMEOUT(traffic) after it, lease flag cleared. **No separate IDLE_EXPIRED event type exists** in this build (LEASE_GRANTED/RENEWED/REFUSED, MODE_CHANGED only) |
| T22 log flood | **PASS** board; UART part not testable | 1000 bad tags: 1000 AUTH_FAIL verdicts, only 15 events written (985 suppressed), the earliest pinned event survived, not starved |
| T23 UART blocked | **NOT RUN** | no UART |
| T24 Step 2 regression | **PASS** for the hardened profile | `verify_boot` 16 pass / 0 FAIL / 2 skipped after all of the above; the debug profile was not re-run here |
| T25 hardened profile refuses cmds 3/6/7/13/14 | **not re-run on the board** | covered by the C2 gate G4 (board). Not repeated: command 3 arms the FIQ if it were ever accepted, so a repeat adds risk without new information |

## 3. Findings
- **F27 (corrected 2026-11-12):** all non-pinned events are limited to one per second per class (AUTH_FAIL: 985 of 1000 suppressed). `TIMEOUT`/`TRAFFIC_RECOVERED` pairs are limited the same way; the ring wrapped in the F24 flood only because about 2-4 entries per second went into a 40-slot ring nobody acknowledged. No code change needed (COMPONENT_6_DESIGN.md section 1).
- **T21 wording:** the plan expects `IDLE_EXPIRED`; the implemented design reports expiry as the following traffic TIMEOUT. Needs a decision: accept the deviation or add the event.
- **Test accuracy:** T7 gave 296.5 ms for a 300 ms deadline; the measurement starts after the frame call returns, so a few ms of call latency is inside the figure.

## 4. My mistakes during this component (kept, not hidden)
1. **T22 FAIL in run 1:** I compared "newest TIMEOUT event" before and after the flood; the 9 s silent flood legitimately produced new TIMEOUTs. The check should look at the earliest pinned event. Fixed (`first_ev`), rerun: PASS. Run 1 log kept (`c5_board_tests_run1_no_t6_20261112.txt`).
2. **T17c FAIL:** I answered a request fetched while it was already older than D_req (after 60 s without a consumer), so the monitor correctly said LATE. The test must wait for a freshly issued request. Fixed and rerun alone on the same boot: ON_TIME (`c5_t17c_rerun_20261112.txt`). The unchanged first-run log of the other T17 checks is in `c5_t17_after_reboot_20261112.txt`.
3. The earlier plan note said the T1-T25 list was "not found in the repository": it was in `~/.claude/plans/` (now copied to `monitor/design/`).

## 5. Gate and open items
Plan gate: Mode-F tests T1-T25 subset plus T5b, T11a-c, T18e, heartbeat half of T18i; stall of 3H gives one MISS, one issue, no MON_STARVED.
**Status: COMPLETE WITH LIMITS for software-injected frames (board, hardened profile).** Open: T23 (UART), T18b (20 reboots), T5b (chip-B path, Component 7), T4b/T11a/T11b/T18e (host-harness only), T25 (C2 evidence, not repeated), no CAN-bus traffic evidence, the IDLE_EXPIRED decision (proposal: `MODE_CHANGED detail 2`, see COMPONENT_6_DESIGN.md), `mon_catchup()` (behaviour covered, name absent).
