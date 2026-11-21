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

## Step 2 / 3c', forwarder `--batch 6` (workaround; 1200 MHz, buffer 4 MiB), 3 runs: replies short, but 62 % of the burst LOST at the forwarder queue; prediction NOT met
| | predicted | r1 | r2 | r3 |
|---|---|---|---|---|
| judged | 1004 | **384** | **377** | **377** |
| dropped by the forwarder queue (256, drop-oldest) | 0 | **620** | **627** | **627** |
| reply max (forwarder side) | < 40 ms | 19.3 | 28.5 | 7.8 ms |
| LATE_ANSWER / late heartbeat | 0 / 0 | 0 / 0 | 1 / 1 | 0 / 0 |
| window | about 8.4 s | 3.19 s (120 fps) | 3.15 s | 3.17 s |
All 1004 frames reached the forwarder (kernel drops 0, chip loss 0) and were queued; only 6 per 50 ms request (120/s) could leave, while the forwarder, now fast at 1200 MHz (one frame read per 0.9 ms fetch), filled the 256-entry queue in about a second and dropped the oldest (counted exactly: 620/627/627 = the verbose "queue full" lines). My prediction ignored that the forwarder's read rate rose with the clock.
**Conclusion:** a batch cap is only safe together with a queue large enough for the burst (plan step 3b/B3, `CB_QCAP` 256 -> 2048); alone it converts a speed limit into loss. At 1200 MHz an uncapped batch of 32 (35 ms) judged all 1004 in 2 s with occasional late replies (step 2d); the right cap depends on the clock (reply = 0.8 ms + 1.07 ms/packet at 1200 MHz: n <= 22 for 25 ms; 4.0 + 4.95 n at 600 MHz: n <= 4), so a time-budget (adaptive) cap is better than a fixed 6.

## Root-cause check 3b, `hmac_cost` (`baseline/hmac_cost_20261121.txt`): the cost of one HMAC, no-HMAC vs HMAC packets, traffic OFF/ON, 600 vs 1200 MHz
| | 1200 MHz, traffic OFF | 1200 MHz, traffic ON | 600 MHz, traffic OFF |
|---|---|---|---|
| call with no packet work (n=0), median | 0.93 ms | 0.93 ms | 3.88 ms |
| no-HMAC packet (MALFORMED before the HMAC), per packet | 38 us | 37 us | 177 us |
| HMAC packet, per packet | 1065 us | 1037 us | 4930 us |
| one HMAC (difference of the slopes) | **1.03 ms** | **1.00 ms** | **4.75 ms** |
Predictions MET: about 0.9 ms base, 1.07 ms per HMAC packet, traffic ON within +-10 % for the median; at 600 MHz 4.0 ms base, 4.95 per packet, 4.9 ms per HMAC.
- **The HMAC cost does not depend on CAN activity** (medians equal), **but with traffic ON a few calls stalled for 40-319 ms** (e.g. n=8 max 293 ms, n=32 max 319 ms; OFF: maximum within 10 % of the median). A 300 ms stall equals one 1004-frame burst on the bus: consistent with the real-time interrupt thread (SCHED_FIFO 50) occupying the CPU while a burst arrives. This is the likely source of the rare outliers (215 ms reply, occasional LATE_ANSWER, a late heartbeat); not yet proven per sample.
- **Everything in Secure World scales 4.2-4.7x for a 2x clock** (null call 3.88 -> 0.93 ms, HMAC 4.75 -> 1.03 ms, no-HMAC packet 177 -> 38 us).
- **Absolute speed is still far below a Cortex-A53:** one HMAC-SHA256 of 12 bytes is about 1.0 ms at 1200 MHz (about 1.2 million cycles); OP-TEE's own AES-ECB benchmark (`xtest -t benchmark 2011`) gives 5.4 ms per 1 KiB (0.18 MiB/s) at 1200 MHz. A normal A53 without crypto extensions needs in the order of 10-30 us for the HMAC and 50 us for 1 KiB of table AES, so the Secure World runs about 30-100x slower than expected, for stock OP-TEE code as well as for the monitor. Even a no-HMAC packet takes 38 us (about 45,000 cycles for a few hundred instructions): about 50-100 cycles per instruction, the signature of instruction and data fetches that do not hit a cache (an uncached mapping of secure memory, or caches off in the secure world). This is a HYPOTHESIS consistent with all magnitudes and not yet tested.

## Root-cause check 3a/3d: OP-TEE's own AES benchmark and the `speedprobe` test TA (RAM only, no flash, card not written; `baseline/speedprobe_1200mhz_20261121.txt`)
`xtest -t benchmark 2011` (TEE AES-ECB): 5.43 ms per call (min 5.09), 0.18 MiB/s at 1200 MHz; benchmark 2012 (AES-CBC) the same. The storage benchmarks (1001-1003) were NOT run (they write to the card).
`speedprobe` (a Trusted Application run inside the secure world, S-EL0; built with the project's TA dev kit and the default signing key; placed in a tmpfs directory bind-mounted over /lib/optee_armtz for the test and unmounted after; stock TAs verified restored, 37 files):
| test (1200 MHz) | measured | expected, Cortex-A53 with caches | verdict |
|---|---|---|---|
| A register-only loop, 2M iterations | 9 ms | 8-15 ms | normal |
| B 200 passes over 16 KiB | 2 ms | 1-3 ms | normal |
| C 20 passes over 256 KiB | 4 ms | 3-8 ms | normal |
| D 2000 cheap system calls into the core | **481 ms = 240 us per call** | 2-6 ms | **about 100x too slow** |
| E 50 HMAC-SHA256 of 12 bytes (TEE API) | **98 ms = 2.0 ms each** | 1-3 ms | about 40x too slow |
| F 50 SHA-256 of 64 bytes | **61 ms = 1.2 ms each** | 1-3 ms | about 30x too slow |
**Prediction check:** H1 (secure memory uncached / caches off), H2 (data uncached) and H3 (clock only) are REFUTED: user-mode compute and memory run at normal speed. **H4 holds: the slowness is in the OP-TEE core's system-call and crypto path.** (Two slips on the way, recorded: the first try failed with `0xffff0008` because my TA file was 0600 and the supplicant runs as user `tee`; fixed to 0644.)
**Leading explanation (not yet tested):** the shipped core is built with `CFG_CORE_DEBUG_CHECK_STACKS=y`, which compiles the whole core with `-finstrument-functions`: every core function entry and exit calls `check_stack_limits()` (`core/kernel/thread.c`), so a system call or an HMAC pays hundreds of hook calls. That is a debug option ("most C functions check the stack pointer against the current stack limits on entry and panic if it is out of range", `mk/config.mk`), the Sep-20 baseline had it on although no makefile sets it (`IMPLEMENTATION_BASELINE.md`). The decisive test is a core built with `=n` and the same measurements (needs a flash).

## ROOT CAUSE CONFIRMED (2026-11-21): CFG_CORE_DEBUG_CHECK_STACKS=y; image `rpi3-sdcard-monitor-modeP-p6-n04-nostk.img` flashed by the user, booted
`verify_boot` 17 pass, 0 FAIL, 1 skipped (`baseline/verify_boot_nostk_20261121.txt`); `can0` came up in CAN-FD by itself (new `S12can` + `canfdcfg` in the image work). **Board note:** the two SD cards are now in each other's boards (receiver card on board serial ...1b5cbaa9, MAC 5c:ba:a9; sender card on board ...111e9802, MAC 1e:98:02; IPs follow the cards). A before/after comparison is therefore across boards; the effect below (45-100x) cannot come from a board difference.
Same OP-TEE source (`80b310010`), same monitor code; only `CFG_CORE_DEBUG_CHECK_STACKS=n` (hook calls in the object code: 0 against 5060). Measured (`baseline/nostk_measure_20261121.txt`):
| 1200 MHz | shipped core (=y) | new core (=n) | factor |
|---|---|---|---|
| 2000 trivial syscalls (speedprobe D) | 481 ms | 5 ms | 96x |
| 50 HMAC via TEE API (E) | 98 ms | 1 ms | 98x |
| 50 SHA-256 (F) | 61 ms | 1 ms | 61x |
| empty monitor call (hmac_cost n=0) | 930 us | 65 us | 14x |
| one HMAC in the monitor | 1027 us | 21.9 us | 47x |
| no-HMAC packet | 38 us | 0.7 us | 54x |
| reply with 32 HMAC packets | 35.0 ms | 0.79 ms | 44x |
600 MHz with the new core: empty call 128 us, one HMAC 46.2 us, reply with 32 packets 1.66 ms (shipped core: 3.9 ms, 4.75 ms, 162 ms): 600 -> 1200 MHz is now 2.0-2.1x (HMAC 46.2 -> 21.9 us), no longer 4.2-4.7x: the super-linear clock effect was the instrumentation. User-mode loops unchanged (register loop 11 ms at 600, 5 ms at 1200).
**Prediction check:** every threshold written before the build was met (empty call < 0.1 ms: 65 us; one HMAC < 0.15 ms: 22 us; no-HMAC packet < 5 us: 0.7 us; D < 20 ms: 5 ms; E < 10 ms: 1 ms; 600 MHz about 2x slower: 2.0-2.1x).
**Consequence:** one HMAC is 22-46 us, a reply with 32 packets is under 2 ms even at 600 MHz: the Mode P design ceiling (32 packets per 50 ms = 640 frames/s) is reachable; the 40 ms reply deadline is no longer a problem; the governor change may not be needed. The forwarder's own loop and its 256-entry queue become the limits (next).
