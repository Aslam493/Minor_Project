# Burst fix plan: results per step, each compared with the prediction written before it (2026-11-21)
Predictions: `BURST_STEPS_PREDICTIONS_20261121.md` (committed before any run). Harness: `scripts/real_bus/burst_run.py` (settings in force are logged per run).

## Step 1, baseline (unchanged system), 3 repeats of N=1004: prediction MET
judged 239 / 239 / 239 (predicted 239 +-2), derived socket loss 765 each, window 2.39 / 2.37 / 2.36 s (predicted ~2.3), reply p99 48.7 / 44.5 / 52.7 ms (predicted ~52), LATE_ANSWER 2 / 0 / 3, late heartbeat 1 / 0 / 0, lock timeouts 0, starved 0, chip overflow 0.

## Step 2a, receiver `net.core.rmem_default = rmem_max = 4194304` (only change), 3 repeats: prediction MET for the main claim, one deviation investigated
| | predicted | r1 | r2 | r3 |
|---|---|---|---|---|
| judged | 1004 | 1004 | 1004 | **980** |
| planted exact | yes | yes | yes | AUTH 1, REPLAY 2, MALFORMED 1; COUNTER_GAP **7** |
| derived socket loss | 0 | 0 | 0 | 0 |
| window | ~9.7 s | 9.88 | 9.89 | 9.64 |
Reply p99 58.1 / 58.0 / 58.2 ms (max 58.8-59.7), LATE_ANSWER 2 / 3 / 0, STALE 1 / 1 / 1, late heartbeat 1 / 0 / 0, lock timeouts 0, starved 0, `last_counter` = expected end in all three.
- **Confirmed:** the bigger buffer removes the socket loss (765 -> 0) and every frame is judged in about 9.9 s (101 frames/s), as predicted. The slow drain is the next thing to fix (step 3b).
- **Deviation, r3:** 24 frames were lost in the **receiver chip FIFO** (`rx_over_errors` 6, `rx_errors` 6, chip delivered 980 of 1004), socket loss 0. Investigated: the sender's load was identical in all six runs (0.280 s, ~3580 frames/s, similar stalls), the loss is upstream of the socket so the buffer change cannot cause it mechanically, memory is fine (29 MB used). Same signature as the one-off of Phase 1 (5 events, 17 frames). It is an intermittent chip overflow seen in **2 of 13** heavy bursts so far; not attributable to step 2a. The plan reserved chip settings for "if the loss returns"; it returned, so a chip step (ring 20 -> 24, interrupt placement) gets its own prediction later, and the chip loss is counted in every run from now on.
- Longer drain, as expected: late replies persist (p99 58 ms, a little higher than the baseline's 44-53 ms, because the busy period is 4 times longer).

## Step 2b, sender `txqueuelen 2000` (only change; receiver keeps 2a), naive sender, 3 repeats: prediction MET
Refused sends (ENOBUFS) 0 / 0 / 0 (was 689 of 1004), lost at the sender 0, sender tx 1004 = receiver rx 1004 in all three, chip overflow 0, socket loss 0, judged 1004 / 1004 / 1004, planted counts exact (AUTH_FAIL 1, REPLAY 2, MALFORMED 1, COUNTER_GAP 1), `last_counter` = expected end, window 9.91 / 9.93 / 9.95 s.
The sender no longer refuses: it **waits** (longest single send 30.7 ms in all three, the send buffer filling and draining). Late replies persist: reply p99 58.8 / 58.9 / 58.1 ms, LATE_ANSWER 5 / 2 / 2, late heartbeat 0 / 1 / 1. Heavy bursts so far: 16, chip overflow in 2 of them.

## Step 2c, forwarder under `chrt -f 50` (only change; 2a and 2b kept), 3 repeats: outcome "no change", tail WORSE; do not keep
Confirmed in force: forwarder `SCHED_FIFO 50`. judged 1004 x3, planted exact, no socket loss, no chip overflow. Reply p99 59.0 / 59.5 / 58.2 ms (2a: 58.1 / 58.0 / 58.2), reply max **108.5 / 89.6 / 107.9** ms (2a: 58.8 / 59.7 / 59.3), STALE_ANSWER 4 / 9 / 5 (2a: 1 / 1 / 1), LATE_ANSWER 4 / 2 / 1, late heartbeat 1 / 0 / 0, window 10.38 / 10.44 / 10.30 s (2a: 9.9).
- The simple preemption hypothesis (the chip's real-time interrupt thread preempts the normal-priority forwarder) is **not supported**: equal priority does not reduce late replies, and it worsens the tail. Real-time priority is **not kept**.
- Open: why p99 sits at 58-59 ms, almost equal to the max, in 2a and 2c (a cluster of replies at about 58 ms, a fixed delay). Next: per-reply timing in the forwarder (batch size, time of the Secure World call), then the CPU placement test (step 4).

## Step 3a-V, counter validation (new forwarder `can_forwarder_3a`, default buffer, 1 run): prediction MET
Judged 239; the forwarder's own new line `socket drops (SO_MEMINFO): 765 frames dropped by the kernel for this socket; SO_RCVBUF 212992` equals the 765 derived from the chip counter minus frames read. The counter is trustworthy.

## Step 3a, forwarder `--rcvbuf 4194304` (system buffer back at the default 212992), 3 repeats: prediction MET
judged 1004 / 1004 / 1004, kernel drops reported by the forwarder 0 / 0 / 0, effective buffer 8,388,608 in all (forced 4 MiB, doubled by the kernel), planted counts exact, `last_counter` = expected end, window 9.97 / 9.92 / 9.89 s, no chip loss in these 3. Reply p99 58.8 / 58.8 / 58.1 ms, reply max 215.0 (one outlier) / 64.3 / 59.7 ms, LATE_ANSWER 1 / 1 / 2, STALE 1 / 1 / 1, late heartbeat 0 / 1 / 1, lock timeouts 0, starved 0.
The socket loss is fixed inside the forwarder itself and every loss at its socket is now counted exactly (kernel counter validated: 765 = 765 in 3a-V). Still slow (9.9 s for 1004 frames): step 3b.

## Step 3-trace (instrumentation only, `--trace`, 1 run with the 3a settings): the reply time is 4.0 ms + 4.95 ms PER PACKET
`baseline/burst_20261121_s3t_trace.csv` (4,904 events). Same outcome as 3a (judged 1004). Reply (Secure World call) time by batch size, p50: n=0 3.9 ms, n=1 8.9, n=2 13.9, n=3 18.6, n=4 24.0, n=5 28.7, n=6 33.7, n=7 38.3, n=8 43.7, n=9 48.5, n=10 53.4, n=11 58.4 ms (least-squares fit 4.0 ms + 4.95 ms per packet). A plain request fetch costs 3.36 ms (idle and in the drain alike). During the drain: 1004 frames read in 10.18 s (99 request fetches/s, one frame per fetch), 114 of 712 replies over 20 ms, replies about every 100 ms because a reply of 54-58 ms outlasts the 50 ms request period. Heartbeat polls: 38 of 567 took 8.0-8.8 ms (3.4 ms + about 5 ms), about one per second: issuing a fresh challenge computes an HMAC too.
**Prediction check:** "slow replies are not tied to large batches": REFUTED. The 58 ms replies ARE the batches of 11. **The step 3b prediction (about 1.6 s at 640 frames/s) is refuted by the data before the code was written.**

## Experiment W (300 wrong-id frames, rejected as MALFORMED before the HMAC), forwarder `--rcvbuf 4194304 --trace`
judged 300 of 300, all MALFORMED, `last_counter` unchanged, kernel drops 0. Reply time at batch 11-12: **5.9 ms** (fit 5.3 ms + 0.05 ms per packet) against 58 ms for valid frames: **the whole per-packet cost is the authentication path, one HMAC about 4.9 ms.** Drain of 300 frames in 1.41 s (about 210 frames/s: with the HMAC out of the way the forwarder's own loop, one frame per 3.4 ms fetch iteration, is the next limit).
**Prediction check:** my prediction (fixed per-packet overhead, about 4.9 ms per packet also for wrong-id frames) was WRONG.

## What this changes (corrections to earlier statements)
1. "Secure World was not the bottleneck" (10.2 point 5) is wrong in effect: it lost and refused nothing, but the time it needs per packet is the throughput limit.
2. "Mode P design ceiling 640 frames/s" is not reachable here: inside the 40 ms reply deadline 4.0 + 4.95 n <= 40 gives n <= 7, so about 120-140 frames/s at best (today 99-103). A reply of 32 packets would take about 160 ms and would be LATE, and 3 missed requests raise TIMEOUT detail 4.
3. The late replies (N5) are caused by batch size times the HMAC cost (batches of 8 to 11 take 44-58 ms), **not** by Linux scheduling. That is why `chrt` did not help.
4. Hence the targets "1000 frames within 2 s" cannot be met by Normal World changes alone.
5. New facts: the receiver CPU governor is `powersave` and the clock is 600 MHz (maximum 1.2 GHz); the A53 has no crypto extensions; an HMAC-SHA256 over 12 bytes should take tens of microseconds, so 4.9 ms is far too slow for the work, and the cause is not yet known (candidates: CPU at half speed, software crypto path, memory attributes of the secure world, per-call overhead in the crypto layer).

## Step 2d, receiver CPU governor `powersave` -> `performance` (runtime; only change), 3 runs: the Secure World cost is mostly a CPU-frequency effect
Before: governor powersave, 600 MHz (min 600, max 1200 MHz, all online CPUs share policy0). After: performance, 1,200,000 kHz on cpu0/2/3 (read back). Reverts at reboot.
| | 600 MHz | 1200 MHz |
|---|---|---|
| plain request fetch | 3.36 ms | **0.86 ms** (3.9x) |
| empty reply | 3.9 ms | **0.9 ms** |
| reply cost | 4.0 ms + 4.95 ms/packet | **0.8 ms + 1.07 ms/packet** (4.6x per packet) |
| heartbeat poll p99 | 8.8 ms | 2.0 ms |
| 1004-frame drain | 9.9 s (101 fps) | **2.02-2.04 s (492-496 fps)** |
| reply p99 / max | 58 / 59 ms | 35.0-35.3 / 35.5-47.1 ms |
Runs: judged 1004 x3, planted exact, kernel drops 0, chip loss 0, batches up to 32 (42-43 replies carry packets), LATE_ANSWER 2 / 0 / 1, STALE 0 / 1 / 1, late heartbeat 1 / 0 / 0, lock timeouts 0, starved 0.
**Prediction check:** predicted "about 2x faster if CPU-bound" (drain about 5 s); measured 4-5x faster (drain 2 s): the dependence on the clock is stronger than linear, so halving the clock costs more than half the speed (cause not yet known: candidates are memory/bus clocks tied to the ARM clock at 600 MHz, or timer-based waits). The batch of 32 now takes 35 ms, inside the 40 ms deadline but close to it: occasional late replies remain.
**Side effects to validate before adopting 1200 MHz permanently:** the Core 1 loop cadence is counted in passes (C4 measured pass 26.6 ns, attempt 12.6 us, interval 448 us at 600 MHz); at 1200 MHz it runs about twice as often. `verify_boot` and `board_full_check --mode-p` must be repeated at 1200 MHz.
