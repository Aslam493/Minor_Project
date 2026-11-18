# Secure CAN Monitor on Raspberry Pi 3 B: what we built, how it works, what we tested

M.Tech minor project. A **detection-only** monitor that runs in the **Secure World** (OP-TEE, with TF-A) on a Raspberry Pi 3 B. It watches a CAN message stream and a heartbeat from Linux, and records evidence when something is wrong.

**What it claims:** it detects forged, replayed, missing and delayed frames, and a Linux side that stops answering.
**What it does not claim:** it does not prevent anything. The Pi 3 has no TZASC, so Secure World cannot stop Linux from doing anything. The keys are prototype values that root Linux can read. The event log is in RAM, with no hash chain. **Every frame judged so far was CAN loopback or injected by a software sender through the Secure World interface. No result is evidence about a real CAN bus.**

## 1. Architecture in one picture
```
 Normal World (Linux 6.7, cores 0, 2, 3)            Secure World (OP-TEE 4.10 + TF-A)
 ┌───────────────────────────────┐                 ┌─────────────────────────────────────────┐
 │ mon_client (test tool)        │  /dev/tee0      │ Pseudo-TA "rpi3_can_spi_poc" (PTA)      │
 │  - sends frames (cmd 17)      │ ───────────────▶│  cmd 17 submit frame  18 get events     │
 │  - reads events/status        │  SMC            │  19 ack events  20 status  21 idle lease│
 │  - answers heartbeat (22, 23) │ ◀───────────────│  22 get nonce  23 submit heartbeat ans. │
 └───────────────────────────────┘                 │        │  one lock, bounded wait         │
                                                   │        ▼                                 │
 Core 1 is reserved for the Secure World:          │ Monitor state + event ring (RAM)         │
 Linux never runs on it.                           │        ▲                                 │
                                                   │ Core 1 loop: every ~448 µs one lock try; │
                                                   │ checks the traffic deadline, issues a    │
                                                   │ heartbeat challenge every 1 s, counts    │
                                                   │ missed answers                           │
                                                   └─────────────────────────────────────────┘
```
Only **Design 1 ("Mode F")** is built: Linux forwards frames to Secure World, and Secure World checks them. The independent second CAN path (Design 2, chip B) is not built.

## 2. How detection works
| Mechanism | Rule |
|---|---|
| Frame check | Each frame has a CAN id (only 0x123 is monitored), a counter, a payload and an HMAC tag. Secure World recomputes the tag |
| Verdicts | `ACCEPTED`, `RESYNC` (first frame after boot), `AUTH_FAIL` (bad tag), `MALFORMED` (wrong id or length), `REPLAY` (old counter), `DUPLICATE`, `CONFLICT` (same counter, different content), `GAP` (skipped counters) |
| Traffic deadline | No valid frame within **300 ms** raises one `TIMEOUT`; the next valid frame raises `TRAFFIC_RECOVERED` |
| Heartbeat | Every **1 s** Core 1 issues a fresh challenge (nonce). Linux must answer within **500 ms**. 3 misses raise a heartbeat `TIMEOUT`. A **60 s** boot grace applies |
| Heartbeat answer checks | `BAD_NONCE`, `STALE`, `DUPLICATE`, `UNISSUED`, `LATE` |
| Event log | 64 slots: 24 pinned (first 4 events of each of 6 evidence classes) + 40 ordinary; other events limited to 1 per second per class; the counters stay exact |
| Boot ID | A random 32-bit ID per boot; old answers from a previous boot are rejected |

## 3. Component by component
| # | Component | What we did | Result |
|---|---|---|---|
| **C1** | Baseline and hardware readiness | Saved the proven image and sources; checked the Pi, `can0` (MCP2518FD; `can1` is dead hardware), the PTA, Core 1 and FIQ; wrote and proved a rollback to a spare 8 GB card. Found the Core 1 stack-overlap problem (O-01) and the missing secure-memory reservation (N-04), and fixed N-04 with a `no-map` device-tree reservation | Done. `verify_boot`: 16 pass, 0 FAIL |
| **C2** | Monitor foundation | Wrote the monitor state, PTA commands 17 to 23, HMAC and keys, one lock with bounded wait, and the Core 1 hook. Tested the real sources on the host under ASan + UBSan; mutation-tested them; scratch builds with 0 warnings. Object-code inspection found a hidden `memcpy` inside a lock hold (H1), which we fixed | 434 host checks (1493 test-stats build, 451 with the UART flag), 0 failed; static checks 58 pass; mutation 141 of 144 caught (3 equivalent). Board: 52 PASS, 1 PARTIAL (Y10 needs a real CAN sender), 0 FAIL of 53 |
| **C3** | Core 1 integration | One guarded hook in the Core 1 loop. Decision D1: FIQ stays masked for the whole loop. Stack and call-graph analysis | Stack worst case 2000 B of 4096 B, normal path 640 B (board watermark 688 B); 72 reachable functions, none forbidden. Done, with recorded deviations |
| **C4** | Timing and measurement | Modelled and measured the loop, derived the timing constants, and measured the PTA latency. Added F24 (heartbeat delay caused by other PTA callers) | Pass 26.6 ns; lock attempt 12.6 µs every ~448 µs; longest lock hold 7.24 µs (target below 100 µs). F24: 0 late answers with 0 or 3 flooders; 1 of 60 with 8; 8 of 92 with 24. **Floods, a hardware sender and the gap histogram not done** |
| **C5** | Heartbeat and traffic deadline | Implemented and tested the challenge/answer protocol and the traffic deadline; two real reboots | Board test: 26 PASS, 0 FAIL, 1 skip (earlier run); the current test set has 25 PASS, 1 expected FAIL (T21d, see C6) |
| **C6** | Hardening | Legacy debug and hardware commands refused in the hardened profile; test-only commands compiled out; idle-lease caps; negative tests with wrong parameter types. Wrote two more Secure World changes (`IDLE_EXPIRED` event, UART `ALIVE` line) | Negative tests 12 PASS, idle caps 6 PASS. The two changes are host-tested and built but **not flashed**, so T21d fails by design |
| **C7** | Independent CAN (chip B) | Designed only (needs a second CAN board and a sender) | **Not built** |
| **C8** | Soaks and final report | 1 h and 6.3 h soaks with a software sender; 21 reboots; final on-Pi check; this report | Soaks: no false alarm in steady state; 21 boots gave 21 distinct boot IDs; final check 48 PASS, 0 FAIL, 1 KNOWN, 1 SKIP |

## 4. Main results (details are in the evidence file)
- **Detects, on the board:** a flipped tag bit, altered payload, wrong id, replay, duplicate, conflict, counter gap, a sender that stops (`TIMEOUT` 296.5 ms after the last frame), a counter restart, a counter wrap, heartbeat misuse (wrong, old, repeated or unissued answers), a late answer, and no consumer at boot.
- **Soak:** 1 h + 6.3 h = 7.27 h on one boot. `starved` 0, no lock timeouts, heartbeat never missed, no alarm event in steady state. The events recorded were start-up, the 2 s pause between the two soaks, and my stop (detected correctly).
- **UART:** one boot log captured. `rpi3_mon_init()` runs 7.8 s before Core 1 starts, so the monitor initialises before Core 1 exists.

## 5. Problems we hit and how they were handled
| Problem | Handling |
|---|---|
| Core 1 stack overlap (O-01) | Decision D1 Option B: FIQ masked in the Core 1 loop |
| Hidden `memcpy` in a lock hold (H1) | Found by object-code inspection, fixed, now checked automatically |
| No secure-memory reservation (N-04) | Device-tree `no-map` reservation, installed on the Pi |
| Heartbeat delayed by other PTA callers (F24) | Quantified; unprivileged route closed (`teeclnt` group); **cannot be prevented for root** |
| Refusal counter is best-effort (F29) | Documented; the refusal itself is not affected |
| My wrong claims (R/grace/N_req "undefined"; F27 "not rate-limited"; counts) | Found, corrected and recorded |
| A false object-code "pass" on empty files | Discarded, redone after checking the files were not empty |
| Script mistakes (backup path, port permissions, ModemManager, `stat` missing on BusyBox) | Fixed; first runs kept as evidence |
| One lost monitoring sample during the soak (SSH) | Reported as a FAIL of the analyser's criterion C1 |

## 6. What is not done (stated, not hidden)
Design 2 / chip B (C7); real CAN-bus traffic; CAN and SPI floods; the gap histogram; the stalled-UART test (T23); the `ALIVE` line and `IDLE_EXPIRED` on the board (written, not flashed); production keys; prevention of any kind. The boot-ID uniqueness check is a sample of 21, not a proof.

## 7. How to show it live
`bash scripts/demo_live.sh 10.237.27.187` runs a narrated demo (healthy traffic, then a tampered, duplicate, replayed, skipped and malformed frame, a heartbeat answer, and silence). For a single verdict on the Pi, copy the tools and run `sh /tmp/board_full_check.sh` on a freshly rebooted Pi (about 4 minutes). The recorded outputs are in the evidence file as a fallback.

## 8. Where everything is
`FINAL_PACKAGE/` holds copies of the reports, results, logs and scripts. The repository branch is `nov-2026-backup`. An off-laptop backup of the SD images, source bundles and repo is on the 4 GB card labelled PIBACKUP. The rollback image is the hardened c16384 image, sha256 `775ce3849a38...`.
