# Check report: does everything work? (run 2026-11-16, all results from this run)

**Answer: yes for what the project claims, with one expected FAIL that is explained below.** Nothing was changed to make a check pass. All frames are software-injected through the PTA, not CAN-bus traffic. Detection only, no prevention.

## 1. Host side (laptop, real monitor sources, ASan + UBSan)
| Check | Result |
|---|---|
| Harness, production configuration | **434 checks, 0 failed** |
| Harness, test-statistics build | **1493 checks, 0 failed** |
| Harness, UART flag on | **451 checks, 0 failed** |
| Static checks on the source | **58 pass, 0 FAIL** |
| Cited paths in the documents | 511 checked, 0 unresolved |
| Mutation testing (re-run 2026-11-12, post-C6 sources) | 141 of 144 caught, the 3 survivors are the same equivalent ones |

## 2. Board side (Raspberry Pi 3 B, hardened image, fresh boot)
| Check | Result | Log |
|---|---|---|
| Boot and hardware (`verify_boot`) | **16 pass, 0 FAIL, 2 skipped** (kernel 6.7.0-rc6, CPUs 0,2-3 so Core 1 is reserved, Core 1 heartbeat rising, PTA answers, `can0` up, MCP2518FD found, no SPI CRC errors) | `baseline/verify_boot_full_check_20261116.txt` |
| C5 board tests (without the 60 s boot-grace test T6) | **25 PASS, 1 FAIL, 1 skip, 6 notes** | `baseline/full_check_c5_20261116.txt` |
| Live demo (valid, tampered, duplicate, replay, gap, malformed, heartbeat, silence) | all as expected | `baseline/demo_run_20261116.txt` |
| 20 reboots | 21 boots, 21 distinct boot IDs, `init_ok` 1 and `starved` 0 every time | `baseline/t18b_boot_ids_20261116.csv` |
| Soaks | 1 h + 6.3 h, no false alarm in steady state (see limits) | `RESULTS.md` sections 6 and 7 |
| UART boot log | monitor initialises before Core 1 exists (W2 literal form) | `baseline/uart_boot_log_20261116_005102.txt` |

## 3. The one FAIL, explained
- **T21d** checks that an idle-lease expiry is reported as `MODE_CHANGED` detail 2 (`IDLE_EXPIRED`).
- That change is written, host-tested and object-checked, but **was deliberately not flashed** (decision D-D: the proven image is frozen). On the flashed image the expiry shows detail 0, so the check fails. The test text itself says so.
- The earlier full run (`baseline/c5_board_tests_run2_full_20261112.txt`, 26 PASS, 0 FAIL) was made before T21d existed; it was added later with the C6 stage A change. So this is **not a regression**: same image, a new test written for the not-flashed change.
- Everything else in the board test passed, including all detection and heartbeat checks, the 1000-frame flood limiter (983 suppressed, earliest evidence kept, `ev_lost` 0) and the idle lease checks.
- The honest board result for the flashed image is therefore **25 PASS, 1 FAIL (expected, T21d), 1 skip**. I did not change the test or the code.

## 4. What is not verified (unchanged, stated limits)
- No real CAN-bus traffic; keys are prototype values; no prevention (Pi 3 has no TZASC).
- Not done: Design 2 (chip B, C7), CAN/SPI floods, gap histogram, T23 (stalled UART), the `ALIVE` line, `IDLE_EXPIRED` on the board.
- Soak 2 was stopped at 6.3 h (planned 10 h) and has no tool summary; one of 432 monitoring samples was lost (SSH).
- Boot-ID uniqueness is a sample of 21, not a proof.

## 5. State left behind
The Pi was rebooted after the tests (clean boot, `/tmp` empty, hardened c16384 image, `verify_boot` 16/0/2). The repo is committed and pushed. The off-laptop backup card is verified.
