# Burst fix plan: predictions written BEFORE each step (2026-11-21)

Plan: `/home/aslam/.claude/plans/wise-roaming-quilt-of-588f201f-...md` (approved by the user). One change per step; after each step the measurement is compared with the prediction written here; a mismatch is investigated before the next step. Burst = N=1000 (1004 frames on the wire: AUTH_FAIL 1, REPLAY 2, MALFORMED 1, COUNTER_GAP 1 planted), sender policy `retry`, forwarder `run 36 --pull --verbose --stats`, no `candump` (NOCD=1), interface `can0`.

| step | change | prediction (written before the run) | what would refute it |
|---|---|---|---|
| s1 | none (baseline, 3 repeats) | judged 239 (+-2); frames the chip delivered but the forwarder never read ~765; window about 2.3 s; reply p99 about 52 ms; 0 or 1 late heartbeat answers; 0 lock timeouts; starved 0 | judged outside 235-243 |
| s2a | `net.core.rmem_default=4194304 net.core.rmem_max=4194304` on the receiver only | judged 1004, planted counts exact (AUTH_FAIL 1, REPLAY 2, MALFORMED 1, COUNTER_GAP 1), derived socket loss 0, window about 9.7 s (1004 / 103 per s), late replies not predicted | judged < 1004 (buffer did not take effect, or another stage drops) |
| s2b | sender `txqueuelen 2000` | retry sender: no change; a **naive** sender loses 0 for bursts up to 2000 (was 97/424/689) | naive sender still loses frames |
| s2c | forwarder under `chrt -f 50` | either reply p99 falls under 40 ms (preemption hypothesis supported) or no change (hypothesis refuted); both recorded; heartbeat late count recorded | n/a (diagnostic) |
| s3a | forwarder sets its own buffer (SO_RCVBUFFORCE) and logs SO_RXQ_OVFL; sysctl reverted | same as s2a, and the forwarder's own report shows the kernel drop count (about 765 in the baseline) | forwarder's drop count differs from the derived loss |
| s3b | bounded socket drain into a 2048 queue, request fetched at most every 5-10 ms | judged 1004, window about 1.6 s with batch 32 (3.1 s with 16) | window > 3 s |
| s3c | call time versus batch size | time grows with N; N=32 under 20 ms p99 | N=32 over 20 ms: choose a smaller N |
| s4 | forwarder `--cpus 2,3`, chip IRQ to CPU 0 | reply p99 under 40 ms and no late heartbeat if preemption is the cause; no chip overflow | late replies unchanged: hypothesis refuted |
| s5 | Secure World C1/C2 (only after approval of the host-test results; STOP before the flash) | the log shows `+N suppressed` on recorded events; `ev_lost` stays 0 when nothing unread is overwritten | n/a |

## Added before step 3a (2026-11-21), after steps 1 to 2c were measured
State when 3a starts: receiver sysctl reverted to 212992/212992, sender `txqueuelen` 2000 kept (2b), no `chrt` (2c not kept). New forwarder binary `can_forwarder_3a` (old one kept as `/tmp/can_forwarder` for rollback).
- **3a-V (counter validation, forwarder WITHOUT `--rcvbuf`, default buffer, 1 run):** the forwarder's own new report line `socket drops (SO_MEMINFO)` must equal the derived loss, **765** (+-2), and judged 239. If it does not, the counter is wrong and is fixed before anything else.
- **3a (forwarder `--rcvbuf 4194304`, 3 runs):** same outcome as 2a: judged 1004 (a chip overflow, if it happens, is counted separately), planted counts exact, socket drops reported 0, effective buffer reported 8,388,608 (the kernel doubles the forced value), window about 9.9 s, late replies unchanged (p99 about 58 ms).
- **3-trace (instrumentation only, `--trace`, 1 run with 3a settings):** no change in behavior: same numbers as 3a. It must show where the replies near 58 ms come from (batch size, Secure World call time, loop gaps). Prediction: slow replies are not tied to large batches (batches are at most 12 here); they cluster at the start of the drain.

## Added after the step 3-trace result (2026-11-21): the reply time is 3.9 ms + 4.9 ms per packet in the batch
The trace (`baseline/burst_20261121_s3t_trace.csv`) shows reply (Secure World call) time linear in batch size: n=0 3.9 ms, n=1 8.9, n=4 24.0, n=7 38.3, n=8 43.7, n=11 58.4 ms; a plain request fetch costs 3.4 ms; one reply per ~100 ms during the drain because a reply longer than the 50 ms request period skips the next request. This refutes the step 3b prediction (about 1.6 s at 640 frames/s): a reply of 32 packets would take about 160 ms, four times the 40 ms deadline. Realistic ceiling inside the deadline: 3.9 + 4.9 n <= 40 gives n <= 7, so about 120-140 frames/s.
**Experiment W (wrong-id frames, 300 frames, forwarder `--rcvbuf 4194304 --trace`):** wrong-id frames are rejected as MALFORMED **before** the HMAC is computed. Prediction: if the per-packet cost is the HMAC (or work after it), the cost per packet drops clearly (under 1 ms per packet, so a 300-frame burst drains in about 1.5 s or less); if it is a fixed per-packet overhead in Secure World, the cost stays near 4.9 ms per packet and the drain takes about 3 s. I expect the second (fixed overhead), because the HMAC of 12 bytes is a few microseconds in principle.

## Revised predictions (2026-11-21, after the trace and experiment W), to be measured only after the user's decision
| step | change | prediction |
|---|---|---|
| 2d | receiver CPU governor `powersave` -> `performance` (runtime, reversible) | if the per-call and per-packet cost are CPU-bound the fetch call falls from 3.4 ms to about 1.7 ms and the per-packet cost from 4.9 ms to about 2.5 ms (reply for n=6 about 17 ms); if they do not change, the cost is not CPU-bound |
| 3c' | forwarder limits the batch to n=6 (3.9 + 4.9 n = 33.7 ms inside the 40 ms deadline) | LATE_ANSWER 0 and late heartbeat 0 (no reply over 40 ms), reply max under 40 ms, 1004 judged in about 8.4-9 s (120 frames/s), no loss |
| 3b' | bounded drain of the socket + fetch only when a request can be open | no gain in judged frames per second while the HMAC cost stays (the replies are full); gain only for cheap packets (wrong-id: about 210 to several hundred per second) |
| H | Secure World measurement of one HMAC (test image, STOP before any flash) | to be defined with the user |

## Predictions for the user's answers of 2026-11-21 (written before each measurement)
- **2d governor `performance` (receiver, runtime):** the clock goes from 600 MHz to 1200 MHz. If the Secure World cost is CPU-bound: plain request fetch 3.4 ms -> about 1.7 ms, reply 4.0 ms + 4.95 ms/packet -> about 2.0 ms + 2.5 ms/packet, the 1004-frame burst drain 9.9 s -> about 5 s (reading and judging both faster). If the numbers do not change, the cost is not CPU-bound.
- **3c' batch cap 6 (forwarder `--batch 6`, workaround):** every reply under 40 ms (n=6: 33.7 ms at 600 MHz, less at 1200 MHz), LATE_ANSWER 0, late heartbeat 0, all 1004 judged, drain about 8.4-9 s at 600 MHz (6 per 50 ms), about 4-5 s at 1200 MHz if 2d helps.
- **3a xtest hash benchmark (small buffers):** expectation for HMAC/SHA-256 of 16 bytes on a Cortex-A53 without crypto extensions: about 20-60 us per operation at 600 MHz in the crypto library itself; each `xtest` operation also pays one TEE invocation (about 3.9 ms measured for the monitor PTA), so a benchmark that does many operations per invocation shows the library speed and one that does one per invocation shows about 4 ms.
- **3b PTA call cost, no-HMAC vs HMAC, traffic OFF vs ON:** no-HMAC call about 3.4 ms in both, HMAC call about 8.4 ms in both (the 5 ms extra does not depend on CAN traffic); if traffic ON changes the HMAC cost, something is shared with the CAN/SPI path.

## Root-cause check 3b: `hmac_cost` (written before the runs)
Tool `tools/monitor_client/hmac_cost.c`: answers open Mode P requests with n = 0..32 packets, kind `hmac` (id 0x123, invalid tag: HMAC computed, AUTH_FAIL) and `nohmac` (id 0x124: MALFORMED before the HMAC), times the call. Conditions: (1) 1200 MHz, CAN traffic OFF; (2) 1200 MHz, traffic ON (the sender streams 1004-frame bursts in a loop, no forwarder and no reader on the receiver, the chip and its interrupt still work); (3) 600 MHz (powersave), traffic OFF.
- (1) nohmac: base about 0.9 ms (the empty reply seen in the trace), slope about 0.02 ms per packet; hmac slope about 1.07 ms per packet; one HMAC about 1.05 ms.
- (2) the same within +-10 %. If traffic ON is clearly slower, the CAN/SPI activity shares something with the Secure World path.
- (3) base about 4.0 ms, hmac slope about 4.95, nohmac slope about 0.05, one HMAC about 4.9 ms.

## Root-cause check 3d: `speedprobe` test TA (RAM only, no flash), written before the run
A user-mode Trusted Application (S-EL0) that times, with `TEE_GetSystemTime` over loops long enough for 1 % accuracy: (A) a register-only loop (no memory access), (B) a streaming sum over 16 KiB (fits the L1 data cache), (C) the same over 256 KiB (L2), (D) 2000 calls of `TEE_GetSystemTime` (a cheap system call into the core: call overhead), (E) 50 HMAC-SHA256 of 12 bytes through the TEE API (`TEE_MACInit` + `TEE_MACComputeFinal`), (F) 50 SHA-256 of 64 bytes.
Expected on a Cortex-A53 at 1.2 GHz with caches on: (A) 2,000,000 iterations in about 8-15 ms; (B) 200 passes of 2048 words in about 1-3 ms; (C) 20 passes of 32768 words in about 3-8 ms; (D) about 2000 x (1-3 us); (E) 50 HMACs in about 1-3 ms.
Hypotheses: H1 secure memory uncached or caches off: (A) and (B) 30-100x slower than expected; H2 only data accesses uncached: (A) normal, (B) and (C) slow; H3 the secure world is throttled by clock only: all about 2x slower than expected, not 30-100x; H4 the cost is in the core crypto/syscall path only: (A)-(C) normal, (D)-(F) slow.
Method to run without a flash: build the TA with the dev kit and the default signing key, put it in a tmpfs directory and bind-mount that over /lib/optee_armtz for the duration of the test, then unmount (the card is not written).
