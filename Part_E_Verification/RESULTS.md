# RESULTS: Secure CAN Monitor (detection only) on Raspberry Pi 3 B

**Status: DRAFT of 2026-11-12 evening. Sections 6 and 7 are PENDING until the soak chain on the Pi finishes (expected about 05:20 IST on 2026-11-13). Nothing in them is a result yet.**

**Read this first.** This is a *detection-only* monitor. The Raspberry Pi 3 has no TZASC, so Secure World cannot stop Linux from doing anything. The keys are prototype values readable by root Linux. The event log is RAM-only, with no hash chain. **Every frame the monitor has judged so far was either CAN loopback or injected through the PTA by a software sender; no result here is evidence about a real CAN bus.** The independent second CAN path (Design 2, chip B) is **not built**.

## 1. What was built and tested
A monitor in OP-TEE Secure World (TF-A + OP-TEE 4.10 + Linux 6.7.0-rc6, MCP2518FD `can0`) with three parts:
- **Monitor state and commands (PTA commands 17 to 23):** frame submission with HMAC check and counter classification, event ring, status pages, idle lease, heartbeat nonce and answer.
- **Core 1 loop (Secure World, reserved from Linux):** one non-spinning lock attempt about every 448 microseconds; it evaluates the traffic deadline, issues a heartbeat challenge every 1 s, counts missed answers.
- **Hardened profile:** the old chip-A debug commands are refused; test-only commands are compiled out.
Design 1 ("Mode F") only: Linux forwards frames to Secure World; Secure World checks them and watches deadlines. See `monitor/COMPONENT_7_DESIGNED_NOT_BUILT.md` for what is not built.

## 2. Test environment (checked 2026-11-12)
| Item | Value |
|---|---|
| Board | Raspberry Pi 3 B, CAN shield with MCP2518FD (`can0`; `can1` is dead hardware) |
| Image | hardened monitor image, `armstub8.bin` sha256 `775ce3849a38...` (`SD_Card_Backups/SHA256SUMS`, name `armstub8.monitor-hardened-c16384.bin`) |
| Card | 8 GB spare, name `SD8GB`, serial `0x6d280b3c`; the 32 GB original was never written |
| Device tree | N-04 version (`no-map` reservation of 0x10000000-0x10FFFFFF), sha256 `e8ac38fa...` |
| Kernel / OP-TEE | 6.7.0-rc6-v8; OP-TEE 4.10 (`a7ccc63e4`) |
| Access | SSH only; no UART connected |
| Timing constants | traffic deadline T = 300 ms (the sender period in the soaks is 100 ms); heartbeat H = 1000 ms; D_req = 500 ms; boot grace G = 60 s; K_miss = 3; Core 1 lock attempt every 16384 passes |

## 3. Results by component
| # | Component | Result | Evidence |
|---|---|---|---|
| C1 | Baseline and hardware readiness | Done (software gate). N-04 found and resolved | `baseline/COMPONENT_1_RECORD.md`, `monitor/N04_SECURE_MEMORY_RESERVATION.md` |
| C2 | Monitor foundation | 51 PASS, 2 PARTIAL, 0 FAIL of 53 checks | `monitor/COMPONENT_2_2_INVENTORY.md`, `COMPONENT_2_GATE_CHECK.md` |
| C3 | Core 1 integration | Done; stack worst case 2000 B of 4096 B, normal path 640 B (board watermark 688 B) | `monitor/COMPONENT_3_CORE1_CALL_LIST_AND_SYMBOL_DIFF.md` |
| C4 | Timing | Closed with limits; floods, sender, histogram not done | `monitor/COMPONENT_4_TIMING_AND_MEASUREMENT.md` |
| C5 | Heartbeat and traffic deadline | Board: 26 PASS, 0 FAIL, 1 skip; two real reboots | `baseline/c5_board_tests_run2_full_20261112.txt` |
| C6 | Hardening | Idle caps 6 PASS; negative tests 12 PASS; two Secure World changes written but not flashed | `baseline/c6_negative_idle_caps_20261112.txt`, `baseline/c6_negative_raw_run2_20261112.txt` |
| C7 | Independent CAN | **Designed, not built** | `monitor/COMPONENT_7_DESIGNED_NOT_BUILT.md` |
| C8 | This report and the soaks | In progress | this file |

## 4. What the monitor detects (board-verified, software-injected frames)
All of the following were run on the Pi (`baseline/c5_board_tests_run2_full_20261112.txt`):
| Condition | Detected as |
|---|---|
| tag bit flipped, or payload altered with the tag kept | `AUTH_FAIL` (deadline not refreshed) |
| wrong CAN id, extended-id flag | `MALFORMED` |
| old counter, same frame again, same counter with different content | `REPLAY`, `DUPLICATE`, `CONFLICT` |
| counter skipped | `GAP` with the size of the gap |
| sender stops | one `TIMEOUT` (traffic) 296.5 ms after the last frame (limit 300 ms); `TRAFFIC_RECOVERED` when frames resume |
| sender counter restarts at 1 | `REPLAY` for every frame, then `TIMEOUT` |
| counter 0 after 0xFFFFFFFF | `REPLAY`, no wraparound |
| heartbeat: wrong nonce, old answer, repeated answer, unissued answer | `BAD_NONCE`, `STALE`, `DUPLICATE`, `UNISSUED` |
| heartbeat answered after D_req | `LATE`; the miss is not forgiven |
| no consumer for 4.6 s | miss count rises by 4, one heartbeat `TIMEOUT` after 3 misses; recovered on return |
| no consumer from boot | `CONSUMER_NEVER_STARTED` (TIMEOUT detail 3) first seen at Linux time 52 s (grace G = 60 s counted from the monitor's start-up a few seconds before Linux starts) |
| idle lease expires | the traffic check resumes; the expiry is not a separate event on the flashed image (see section 9) |
| 1000 bad-tag frames | 15 events in the ring, 985 suppressed by the limiter; the earliest evidence survives |
| four parallel callers, 8 s | 8 of 8 heartbeat answers on time, no BUSY, no starvation |
| legacy debug and hardware commands (ids 0, 3, 6, 7, 13, 14 and the debug readers) | refused (`NOT_SUPPORTED`): 28 calls per run, 56 of 56 over two runs; counted best-effort |
Host-side, on the real sources under ASan and UBSan: 434 checks (production configuration), 1493 (test-statistics build), 451 (UART flag on), 0 failed; static checks 58 on the source, 61 with object code; mutation testing 141 of 144 mutants caught, re-run on the sources after the C6 changes on 2026-11-12 (`monitor/source_tests/mutation_c6_20261112.txt`; the other 3 survivors are the same 3 equivalent ones as on 2026-11-09; one mutant's anchor had to be updated because the `IDLE_EXPIRED` change rewrote that line; the new UART code has no mutants, only the 5 planted bugs of the host tests), and five bugs planted for the 2026-11-12 host tests were all caught.

## 5. Measurements
| Quantity | Value | Evidence |
|---|---|---|
| Core 1 loop pass | 26.6 ns; lock attempt 12.6 us; attempt interval 448 us | `monitor/COMPONENT_4_TIMING_AND_MEASUREMENT.md` |
| Longest PTA lock hold | 7.24 us (target below 100 us) | C2 board check H3 |
| PTA call latency, one caller, idle | p50 3.88 ms, p99 4.30 ms (100,000 calls) | C4 |
| Secure counter on Core 1 | sane over 3000 samples | C4 |
| **F24: delay caused by other callers of the same PTA** | see the table below | `monitor/F24_HEARTBEAT_STARVATION_ANALYSIS.md` |

F24, 60 s runs with a software sender, the heartbeat responder, and N loops calling the PTA:
| Flooders | Frame-submit worst case | Late heartbeat answers | Ring entries lost |
|---|---|---|---|
| 0 | 10 ms | 0 of 60 | 0 |
| 3 | 271 ms | 0 of 60 | 0 |
| 8 | 553 ms (above D_req 500 ms) | 1 of 60 | 16 |
| 24 | 1.18 s | 8 of 92 | 149 |
The monitor itself stayed healthy in all runs (`starved=0`, no lock timeouts). The effect is visible as false traffic alarms and late-answer events. At 24 flooders the sender process was also starved of CPU, so part of that run's lateness is Linux scheduling, not the PTA.

## 6. Soak results: PENDING
To be filled from `baseline/soak_watch_20261112.csv` (a sample every 60 s) and the end-of-run summaries on the Pi (`/tmp/soak_1.out`, `/tmp/soak_2.out`). Planned: 1 hour, then 10 hours; software sender (frames injected through the PTA, period 100 ms, **not CAN-bus traffic**) plus the heartbeat responder; no faults injected. Pass criteria fixed in advance: zero BUSY after retries, zero lock timeouts, `starved=0` throughout, no panic or reboot, every heartbeat answer on time except possibly the first one after the start, Core 1 poll age bounded.

## 7. False positives and missed detections: PENDING for the soak part
Known already:
- **False traffic alarms under PTA contention** (F24): frames delayed beyond 300 ms produce `TIMEOUT` and `TRAFFIC_RECOVERED` although the sender was on time.
- **One late heartbeat answer at the start of a soak** appears in earlier soaks (`ON_TIME 60 LATE 1`); to be confirmed or explained from the new soaks.
- No claim of "no false alarms" is made.
Missed detections by construction: delays shorter than the deadline; exact retransmission of a valid frame is indistinguishable from a replay; a sender restart looks like a replay storm until the monitor reboots; a root Linux can stop answering, or disable the path, without anything outside noticing.

## 8. Honest limits
From the plan (section 11): the Pi 3 has no TZASC, so isolation is a convention; the key is readable by root Linux, so an HMAC is not provenance against it; the heartbeat proves only that something able to call the PTA answered a fresh challenge; the RAM log is not tamper-evident; the monitor can be starved or disabled by a root Linux, and the UART output stopping is the only external sign; a gap is not proof of attack; no "no false alarms" claim; padding is unauthenticated; **no prevention of any kind.**
Specific to this work:
- **All frames were software-injected or loopback.** No sender or second board was available. Real-bus timing, bus-off and flooding behaviour are untested.
- **F24:** any process that can open `/dev/tee0` (root, and the `test` account until it was removed from the `teeclnt` group on the Pi, not in the image build) can delay the heartbeat path and cause false alarms and late-answer events; it cannot be prevented here.
- **Event counters for refused legacy commands are best-effort** (finding F29): dropped when the monitor lock is busy; the refusal itself is not affected.
- Keys are prototype constants in the source. The Linux-side responder and sender in these tests are software test tools, not a production consumer.
- The boot-ID uniqueness check is a sample of 3 boots (the plan asks for 20 or more); it is not a proof.

## 9. Not done, and not claimed
- **Design 2 (chip B, report cross-check, T26 to T47): not built.**
- **UART:** never connected. The literal boot log (W2), the stalled-UART test (T23) and the `ALIVE` line are unverified. The `ALIVE` line is written, host-tested (451 checks) and object-checked, behind a flag that is **off**, and was **not flashed**.
- **`IDLE_EXPIRED`:** implemented as `MODE_CHANGED` detail 2, host-tested and object-checked, **not flashed**; on the flashed image an idle-lease expiry is visible only as the following traffic timeout (a recorded deviation from the plan's wording).
- CAN and SPI floods, a hardware sender and the gap histogram (C4.3, C4.4); T5b (a chip-B row); T4b, T18e on the board; T24 for the debug profile.
- N-04's build rule has been verified by reproducing its command, not by a full image build.
- No off-laptop copy of the SD backups exists.

## 10. Evidence index
`Secure_CAN_Monitor_Plan_8_Components.md` (board and dated log); `monitor/COMPONENT_*.md` (per component); `monitor/PLAN_REMAINING_WORK_2026-11-12.md` (decisions); `baseline/*.txt` (board logs); `monitor/source_tests/` (host harness, static checks, mutation scripts and results); `monitor/F24_HEARTBEAT_STARVATION_ANALYSIS.md`; `monitor/N04_SECURE_MEMORY_RESERVATION.md`; `source_backups/` (bundles of the Secure World source).
