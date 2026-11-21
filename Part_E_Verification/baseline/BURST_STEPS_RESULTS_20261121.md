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
