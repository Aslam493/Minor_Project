# Secure CAN Monitor: Progress Report (as of 2026-11-12)

Evaluation: Saturday 2026-11-14. Sources: the status board and dated log in `Secure_CAN_Monitor_Plan_8_Components.md`, the component records in this folder, the board logs in `Part_E_Verification/baseline/`, and the Pi checked read-only on 2026-11-12.

## 1. What the project is
A **detection-only** secure CAN monitor on Raspberry Pi 3 B (TF-A + OP-TEE 4.10 + Linux, MCP2518FD on `can0`). Core 1 runs in Secure World and watches a heartbeat challenge and the CAN-traffic deadline. The Pi 3 has no TZASC, so nothing here prevents anything. Keys are prototype values readable by root Linux. The event log is RAM-only with no hash chain. **All frames used so far are software-injected through the PTA or loopback, not CAN-bus traffic.**

## 2. State of the Pi (checked 2026-11-12)
8 GB spare card (`SD8GB`, serial 0x6d280b3c); hardened c16384 monitor image (`armstub8.bin` sha256 `775ce384...`); N-04 DTB installed; `verify_boot` 16 pass / 0 FAIL / 2 skipped; monitor `init_ok=1`, `starved=0`; `teeclnt` empty. The 32 GB original card was never written.

## 3. Components
| # | Component | Status | Evidence |
|---|---|---|---|
| C1 | Baseline and hardware readiness | **Done** (software gate); hardware items open | `baseline/COMPONENT_1_RECORD.md`; N-04 resolved 2026-11-12 |
| C2 | Secure monitor foundation | **Closed**: 51 PASS / 2 PARTIAL / 0 FAIL of 53 | `monitor/COMPONENT_2_2_INVENTORY.md`, `COMPONENT_2_GATE_CHECK.md` |
| C3 | Core 1 integration | **Done with accepted deviations** | `monitor/COMPONENT_3_CORE1_CALL_LIST_AND_SYMBOL_DIFF.md` |
| C4 | Timing and measurement | **Closed with limits** for Mode F; floods, sender, histogram not done; rule (b) fails with the plan's provisional numbers | `monitor/COMPONENT_4_TIMING_AND_MEASUREMENT.md`, `monitor/derived/derive_params_20261112_plan_values.txt` |
| C5 | Heartbeat and traffic deadline | **Done with limits (accepted)**: board 26 PASS / 0 FAIL / 1 skip, plus 2 real reboots; host tests for T11a/T11b/T10b added | `baseline/c5_board_tests_run2_full_20261112.txt`, `monitor/COMPONENT_5_HEARTBEAT_AND_TRAFFIC_DEADLINE.md` |
| C6 | Hardening (M1) | **Partial**: idle caps verified on the board; IDLE_EXPIRED and the UART line written, host-tested, built, **not flashed** | `baseline/c6_negative_idle_caps_20261112.txt`, `monitor/COMPONENT_6_STAGE_AB_EVIDENCE.md` |
| C7 | Independent CAN and cross-checking | **Not started, blocked** (no second working CAN board, no sender) | |
| C8 | Testing and final report | **Not started** (no `RESULTS.md`; no 1-hour or overnight soak yet) | |

## 4. Verified numbers
Host harness (real sources, ASan + UBSan): 434 checks flag off, 1493 TEST_STATS, 451 with the UART flag, 0 failed. Static checks: 58 pass on source, 61 with object code on the built images. Mutation: 141/144 caught (2026-11-09, not re-run on the C6 source changes); the U1 planted bugs: 5 of 5 caught. Board: C5 26 PASS / 0 FAIL / 1 skip; C6 idle caps 6 PASS; F24 flood runs at 0/3/8/24 PTA flooders.

## 5. Issues found and what happened
| Issue | Status |
|---|---|
| O-01 FIQ overlaps the Core 1 stack | Resolved: FIQ stays masked (D1) |
| N-04 Linux did not reserve OP-TEE memory | **Resolved 2026-11-12** on the Pi; build rule not yet proven by a full image build |
| H1 hidden `memcpy` inside a lock hold | Fixed and re-verified |
| **F24** any `/dev/tee0` user can delay PTA callers: worst frame delay 271 ms (3 flooders), 553 ms (8), 1.18 s (24); false traffic alarms, late answers | **Open, documented limit** (detection only). Unprivileged route closed on the Pi (`teeclnt` empty), not in the image build |
| F27 "events not rate-limited" | **Withdrawn**: the limiter exists (one per second per class); the ring wrapped because events stayed unread |
| Audit O-10 saturating miss counter | The code uses a plain increment (wrap needs about 136 years); recorded, not changed |
| My own mistakes | Wrong R/grace/N_req claim (they are defined in the original plan), two test bugs (T22, T17c), a relative build path that polluted the source tree, a false object-code "pass" on empty disassembly: all found, fixed and kept in the records |

## 6. What is NOT done
UART never connected (W2, T23 and the UART line unverified on hardware); no CAN-bus evidence; Design 2 (chip B) not built; only 3 of 20+ boot IDs sampled; floods and gap histogram not run; IDLE_EXPIRED and the UART line not flashed; 1-hour and overnight soaks not run; `RESULTS.md` not written.

## 7. Next
`monitor/PLAN_REMAINING_WORK_2026-11-12.md`: units U0-U8, decisions D-A to D-F, schedule to Saturday and the cut rule.
