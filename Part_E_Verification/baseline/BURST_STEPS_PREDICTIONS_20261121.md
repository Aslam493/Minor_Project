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
