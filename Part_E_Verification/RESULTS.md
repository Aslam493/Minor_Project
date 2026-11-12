# RESULTS: Secure CAN Monitor (detection only) on Raspberry Pi 3 B

**Status: 2026-11-13. All sections filled. The soaks were shorter than planned (1 h and 6.3 h), one monitoring sample was lost, and soak 2 has no tool summary; see section 6.**

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
| C8 | This report and the soaks | Done with deviations (soak 2 stopped at 6.3 h; 1 sample lost) | this file, `baseline/soak_analysis_20261113.txt` |

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

## 6. Soak results (2026-11-12 evening to 2026-11-13 night)
Setup: a software sender (frames injected through the PTA, period 100 ms, **not CAN-bus traffic**) plus the heartbeat responder in one process on the Pi, no faults injected. A laptop sampled the monitor every 60 s over SSH (`baseline/soak_watch_20261112.csv`, 431 samples) and `analyze_soak.py` judged it against criteria fixed beforehand (`baseline/soak_analysis_20261113.txt`).

**Deviation from the plan: the soaks were 1 h and 6.3 h, not 1 h and 10 h.** The 10-hour soak was stopped by hand after 22,607 s (Pi clock 01:21:51 to 07:38:38) because the user judged about 7 h enough. Total observed: 7.27 h, one boot, no reboot. Stopping the processes was the only intervention.

| Criterion | Result | Evidence |
|---|---|---|
| Frames flowing | counter 1,262 to 262,945 in the CSV, exactly 10.00 frames/s, no pair of samples without progress | analysis C6 |
| Same boot, no panic or reboot | one boot_id, uptime increasing | C2 |
| `starved == 0` in every sample | yes | C3 |
| Zero PTA lock timeouts in every sample | yes (BUSY after retries was 0 in soak 1's own summary; for soak 2 the tool's summary was lost, see below, and a BUSY return would have raised this counter, so zero here implies zero BUSY: an inference, not a measurement) | C4 |
| Heartbeat answered | 392 samples ANSWERED, 39 OPEN (a challenge awaiting its answer, 9.0%), `hb_miss` 0 in every sample | C5 |
| Event ring | `ev_lost` 0 | C8 |
| Core 1 poll age bounded | median 222 us, p99 448 us, max 449 us | C9 |
| **No sample lost** | **FAIL: 1 of 432 sampling attempts (18:47:55Z) was UNREACHABLE.** The next sample, 70 s later, shows the counter advanced by 1,310 in 131 s, so the Pi and the soak kept running; the SSH sample failed. The cause is not known. | C1 |

Soak 1 end-of-run summary from the tool (`baseline/soak_stop_and_soak1_summary_20261113.txt`): 3,600 s, 35,998 frames ACCEPTED, 1 RESYNC (start of the run), GAP, MALFORMED, AUTH_FAIL, REPLAY, DUPLICATE, CONFLICT all 0, BUSY 0, heartbeat answers 3,599 on time and 1 late; send lateness vs the absolute deadline p50 75 us, p99 84 us, max 19.9 ms; frame submit latency p50 8.9 ms, p99 9.8 ms, max 85.6 ms.
**Soak 2 has no end-of-run summary.** `mon_client` has no signal handler, so killing it lost its summary. For soak 2 the CSV and the monitor's own event list are the only evidence.

Event list of the monitor after the run (`baseline/soak_events_after_stop_20261113.txt`), 11 events, none lost:
| Seq | Event | Cause |
|---|---|---|
| 1 | TIMEOUT detail 3 (consumer never started) | normal at boot, before any consumer |
| 2, 3 | LEGACY_CMD_REFUSED | two refused legacy calls about 1,063 s and 1,094 s after boot, before the soak started; the caller was not recorded (probably the pre-soak checks) |
| 4, 5, 6 | COUNTER_RESYNC, LATE_ANSWER, HEARTBEAT_RECOVERED | start of soak 1: the sender's first frames, and the first heartbeat answer was late (the LATE_ANSWER came 12 ms after the resync) |
| 7, 8, 9 | TIMEOUT (traffic), TRAFFIC_RECOVERED, LATE_ANSWER | the hand-over from soak 1 to soak 2: the sender really stopped for about 2 s plus start-up. Correct detections. |
| 10, 11 | TIMEOUT (traffic), TIMEOUT (heartbeat) | **I stopped the soak.** The traffic timeout fired, then the heartbeat timeout about 2.6 s later (after 3 misses). Correct detections, shown by `hb_miss_count 8` and flags 0x1f afterwards. |

After the stop I removed the test files from the Pi's `/tmp` and ran `verify_boot` (`baseline/verify_boot_after_soak_20261113.txt`): 16 pass, 0 FAIL, 2 skipped. The monitor stays in its timed-out state until the next reboot.

## 7. False positives and missed detections
From the soaks:
- **No alarm event appeared during steady state.** Between the start-up of soak 1 and the stop, the only events were the start-up artefacts, the hand-over pause and my stop (all in the table above). That is no false positive in about 7.27 h of software-injected traffic at 10 frames/s, with nothing else loading the PTA. It is not a claim of "no false alarms": a different load can produce them.
- **One late heartbeat answer at the start of soak 1** (also in earlier soaks). The cause is not proven; it is the first answer of each run, so a start-up effect of the test tool is the likely reason.
- The late answer at the hand-over has an obvious cause: the responder was not running for about 2 s.
Known from earlier work:
- **False traffic alarms under PTA contention** (F24): frames delayed beyond 300 ms produce `TIMEOUT` and `TRAFFIC_RECOVERED` although the sender was on time.
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
