# Burst behaviour, Phase 2: causes and proposed fixes (2026-11-21). Nothing has been changed on the Pis or in the code. Waiting for the user's OK.

Data: Phase 1 (`baseline/burst_20261121_*`, `REAL_BUS_RESULTS.md` section 10) plus two experiments done for this phase:
**E1** (`baseline/sockcap_experiment_20261121.txt`, tool `tools/monitor_client/sockcap.c`, measures one extra socket, changes nothing) and **E2** (`baseline/burst_20261121_E2_nocd_SUMMARY.txt`, four heavy bursts again, this time without `candump`).

## 1. In simple words

A burst is lost in four places, and only one of them is big. The sender drops frames if it sends faster than the bus (a test-sender problem). Once in eight heavy bursts the receiver chip dropped 17 frames. The big loss is a **waiting room that is too small in front of a reader that is too slow**: the forwarder's socket holds 238 frames, and the forwarder takes only about 103 frames per second out of it, so everything after the first 238 frames of a burst is thrown away by the kernel. Secure World, which is the part that matters most, lost nothing and was never overloaded. A last problem is not loss but **delay**: during a burst the forwarder answers Secure World late (about 52 ms against the 40 ms limit), and in 6 of 10 burst runs one heartbeat answer was late as well (no miss was counted).

## 2. Each negative result: cause and evidence

| # | Negative result | Cause (layer) | Evidence | Confidence |
|---|---|---|---|---|
| N1 | Naive burst loses 97 / 424 / 689 frames before they reach the bus | **Sender**: transmit queue `qlen 10` plus a 4-frame hardware ring; a loop faster than the bus gets ENOBUFS | `lost_at_sender` equals the ENOBUFS count in all naive runs; sender `tx_packets` = `sent_ok` in every run; with `retry` the same bursts lose 0 (208 / 719 / 1445 ENOBUFS events retried); `tx_dropped` 0 | high |
| N2 | 17 frames lost in the receiver chip (N500 retry, Phase 1) | **Receiver chip FIFO** (20 objects, one interrupt per frame, `rx-frames-irq 1`): the interrupt thread was held up for a few frames' time | `rx_over_errors` +5, `rx_errors` +5, 504 sent / 487 received, in that run only. Not seen in the other 7 heavy bursts (Phase 1 N1000 retry, E1 twice, E2 four times: overflow, FIFO, dropped all 0, sender tx = receiver rx). The chip's interrupt is not pinned (smp_affinity `f`, counts 679/677/667 on the three CPUs). | medium for the layer, **low for the trigger** (one observation) |
| N3 | 249 / 77 / 765 frames lost in the forwarder's socket; exactly 238-239 reach the forwarder | **Linux kernel socket buffer** of the forwarder's raw socket: 212,992 bytes = 238 frames at about 894 bytes charged per frame; the forwarder reads too slowly to keep it empty (N4) | **E1 measured it**: default buffer queued exactly **238** frames (counters 1 to 237, the oldest) and the kernel's own drop counter said **766** (239 + 766 = 1005 offered); with a forced 1 MiB buffer (doubled by the kernel to 2 MiB) the same burst queued **all 1004**, drop counter 0. E2 (no `candump`): forwarder received 239 in all 4 runs. The forwarder queue (256) lost 0 in every run. | **high (measured)** |
| N4 | Judged rate is about 103 frames/s in every run (design ceiling 640/s); a 239-frame backlog takes 2.3 s | **Forwarder code** (`can_forwarder.c` main loop): it reads **one frame per iteration**, then calls Secure World (`mon_get_packet_request`, about 3.8 ms) every iteration, plus the heartbeat call every 50 ms | window / frames = 107, 103, 101, 103, 103, 102 frames/s across the six Phase 1 runs of 57 to 239 judged frames (101 to 107); idle loop 13.8 ms (10 ms `poll` + 3.8 ms call, from `--stats`); fetch count 2297 idle vs 2330 to 2355 in the bursts (the loop barely speeds up); replies carried at most 12 packets (never near 32) | high (code + timing agree) |
| N5 | Replies late under burst: Linux-side p99 52 ms vs 9.3 ms idle, 0 to 2 `LATE_ANSWER` per run, deadline 40 ms; heartbeat still answered | **Linux scheduling / load on the 3 usable CPUs** is the leading hypothesis (the chip's interrupt thread and the SPI pump are real-time threads at 3500 interrupts/s; the forwarder runs at normal priority and is not pinned). **Not** `candump` (E2). **Not** Secure World lock contention (`skip_total` +48 to +68 per run in bursts and +51 in the idle control; PTA lock timeouts 0). | E2 without `candump`: p99 50.4 to 54.6 ms, max 57 to 62 ms, LATE 0 to 2 per run, the same as with it (the extreme tail of 147 ms did shrink without `candump`); Secure World-side p99 bucket 24-30 ms, slowest 38-48 ms | **medium: a hypothesis, not proven** |
| N6 | The event log hides most of a loss (1 recorded of 43 `COUNTER_GAP`; 42, 64, 220 suppressed) | **Secure World event limiter by design**: a non-pinned event of a type recorded less than 1 s earlier is counted, not recorded | `rpi3_mon_ev_append` (read), counter deltas vs recorded events in every run | high |
| N7 | `ev_lost` rises (3 and 7) with only 8 or 9 events recorded | **Secure World accounting**: `ack` frees no ring slot, `ordinary_used` is never cleared, so after the ring has wrapped once every ordinary event counts as lost | `rpi3_mon_ev_append` lines 296-301 and `rpi3_mon_cmd19` ("bookmark only"); deltas 0,0,0,0 then 3, 7 as the ring filled | high (code); not a burst effect |

Not a cause (checked): Secure World ingestion (lost 0 in every run, handed = judged, never starved, 0 PTA lock timeouts, Core 1 poll age at most 0.4 ms), the Mode P batch size (max 12 of 32), the forwarder queue (256, never full), `rx_dropped` and `rx_fifo_errors` (0).

## 3. Proposed fixes, in the order you asked for

### A. Runtime settings (no code, reversible by reboot or one command)

| id | Setting | Where | Targets | Expected effect |
|---|---|---|---|---|
| A1 | `ip link set can0 txqueuelen 2000` (persist in `can0-up.service` on the sender card) | sender | N1 | naive sender no longer loses a burst of up to about 2000 frames (qlen 10 is the CAN default) |
| A2 | Larger receive buffer for **the forwarder's socket**. As a pure setting for a test: `sysctl -w net.core.rmem_default=4194304 net.core.rmem_max=4194304` (affects every new socket on the receiver, so only for the test; the clean permanent place is B1) | receiver | N3 | E1 shows a forced 1 MiB (kernel doubles it) holds 1004 frames; 4 MiB holds about 4,700. No frame lost in the socket for bursts up to that size, but **latency grows** (N4): a 1000-frame burst is judged over about 9.7 s |
| A3 | Start the forwarder with real-time priority: `chrt -f 50 /tmp/can_forwarder ...` (BusyBox `chrt` exists on the receiver) and with `--cpus 0,2` (existing option, cores 0/2/3 only) | receiver | N5 | tests the scheduling hypothesis; target: reply p99 under 40 ms and 0 `LATE_ANSWER`. Risk: a real-time loop that polls with `--spin` could starve other tasks; I would not use `--spin` with FIFO. |
| A4 | Only if the chip overflow comes back in repeats: `ethtool -G can0 rx 24` (20 to the maximum 24, needs `can0` down/up, then `canfdcfg can0 bus`), `ethtool -C can0 rx-frames-irq <n>` (interrupt coalescing), and pin the chip's interrupt (`echo 1 > /proc/irq/186/smp_affinity`) away from the forwarder's CPU | receiver | N2 | small; N2 happened once in 8 heavy bursts, so I would not touch the chip before repeating the burst at least 20 times and seeing it again |

### B. Normal World code (forwarder and sender), no Secure World change

| id | Change | Targets | Expected effect |
|---|---|---|---|
| B1 | Forwarder sets the buffer of its own socket (`SO_RCVBUFFORCE`, 4 MiB) and logs the socket drop counter (`SO_RXQ_OVFL`) in its report, so that every frame lost in front of it is counted and printed | N3 | same as A2 without a system-wide change; loss in the socket becomes visible instead of inferred |
| B2 | Forwarder **drains the socket** (read until empty, up to the queue's room) **before** each Secure World call, and calls `mon_get_packet_request` only when a request can be open (at most every 5 to 10 ms, the request period is 50 ms) | N4 | the read rate stops being tied to the 3.8 ms call; the handover rate becomes the Mode P ceiling, **32 packets per 50 ms = 640/s**; a 1000-frame burst drains in about 1.6 s instead of 9.7 s |
| B3 | Raise `CB_QCAP` from 256 to 2048 (44 bytes each, 90 KB) so the forwarder queue can hold what B2 now reads quickly; the policy when it is full stays "drop the oldest" unless you decide otherwise (see decision D4) | N3, N4 | without this, B2 would move the loss from the socket to the 256-entry queue |
| B4 | Heartbeat answer in its **own thread** (it shares the loop today and is polled every 50 ms) | N5 | a long packet reply can no longer delay a heartbeat answer; **CORRECTED 2026-11-21 (my earlier text said the heartbeat was never late, which was wrong):** the forwarder's own report shows `heartbeat answers ON_TIME 36 LATE 1` in 4 of the 6 Phase 1 burst runs and 2 of the 4 E2 runs (6 of 10 burst runs), and `LATE 0` in the 2 normal runs and the 3 earlier 60 s runs; `hb_miss_count` stayed 0 and the state ended ANSWERED, which hid it. So burst load does delay a heartbeat answer (cause open), and this is a measured effect to fix or at least to track, not only protection |
| B5 | Packets are already handed over **oldest first** (`cb_q_peek` takes the oldest); keep it. Retry of a refused/BUSY reply already exists (`RETRY`); keep it | | |
| B6 | Sender: `burst_send.py` policy `retry` stays the test default; the production-style sender (`laptop_can_send.py`) should also retry on ENOBUFS instead of crashing | N1 | a sender that cannot lose frames to its own queue |

### C. Secure World changes (only last, only if you approve)

| id | Change | Reason |
|---|---|---|
| **C1 (your option)** | **Each recorded event carries the number of same-type events suppressed by the rate limiter since the previous record of that type.** Design: a per-type counter in the local state (`diag_suppressed[type]`, saturating); increments where the limiter returns today; when an event is recorded its count is put into the upper 15 bits of the 16-bit `flags` field of the 24-byte event (bit 0, the overwrite flag, is unchanged, so the record size and old clients are unaffected) and the counter is reset; pinned events always carry 0. The clients print `(+N suppressed)`. | the log would then show the real size of a loss: 1 recorded `COUNTER_GAP` would say "+42 suppressed"; counters stay exact as today |
| C2 (optional) | `ev_lost` counts only **unread** overwrites: compare the victim's `seq` with `ev_ack` instead of testing `ordinary_used` | N7: today `ev_lost` is meaningless once the ring has wrapped |
| C3 (not proposed now) | raise the Mode P ceiling (batch above 32, period below 50 ms) | only if the requirement (D4) is more than 640 frames/s; the measurements never got near it |

If C1/C2 are approved, the order of work is: host tests first (new cases for suppression counting, saturation, reset, pinned events, ABI/size asserts), mutation run, scratch builds with the flag OFF / ON / PULL / DEBUG with **0 warnings**, static checks including the object-code checks on the critical path, then the image: `compose_image.sh` (N-04 DTB), **plus `canfdcfg` and the new `S12can` in the root filesystem (today the composed images do not carry them, open item)**, then **STOP and give you the `dd` and `cmp` commands**. Nothing is flashed by me.

## 4. Predictions written BEFORE Phase 3 (to compare with the re-measurement)

| combination | N150 retry | N500 retry | N1000 retry | drain of 1000 | late replies |
|---|---|---|---|---|---|
| today (Phase 1) | 154 judged | 238-239 judged | 239 judged | 2.3 s for 239 | p99 52 ms |
| A2 only (bigger buffer) | 154 | 504 | 1004 (all, planted frames exact) | **about 9.7 s** | unchanged |
| A1 + B1 + B2 + B3 (+ A3) | 154 | 504 | 1004 | **about 1.6 s** (1004 / 640 per s) | target p99 under 40 ms, to be measured |
| + C1 | as above | as above | as above | as above | the log shows +N suppressed on every recorded event |

Acceptance for Phase 3: for N = 150 / 500 / 1000 with `retry`, judged = N + 4, AUTH_FAIL 1, REPLAY 2, MALFORMED 1, COUNTER_GAP 1, last_counter = expected end, 0 lost at every stage, plus the 10 s normal-traffic check all ACCEPTED and no `LATE_ANSWER` in 3 repeats. The naive sender is re-run with A1.
Beyond the buffers: a **sustained** flood at the full bus rate (about 3500 frames/s) is 5 times the Mode P ceiling (640/s); no setting makes that lossless. What the monitor must then do is a decision (D4).

## 5. Decisions needed from the user

1. **D1** Approve the runtime settings A1, A2 (as a test), A3 for the re-measurement? (A4 only if the chip overflow returns.)
2. **D2** Approve the Normal World changes B1 to B4 (scratch builds, host-tested, the repo's committed forwarder changes only after the re-measurement passes)?
3. **D3** Approve the Secure World options: C1 (suppressed count), C2 (`ev_lost` semantics)? Each separately yes/no.
4. **D4** The requirement: how large a burst must be judged completely, and how fast (for example "1000 frames within 2 s")? And what must happen above the ceiling: drop the oldest (today), drop the newest, or sample? Whatever is dropped should be counted and reported.
5. **D5** Is a delay of several seconds acceptable for A2 alone (9.7 s for 1000 frames), or must B2 come with it?
6. **D6** May the sender's `can0-up.service` get `txqueuelen 2000` permanently (sender card write)?

My recommendation: D1 yes (A1, A2 as test, A3), D2 yes (B1 to B4), D3 C1 yes and C2 yes, D6 yes; answer D4/D5 from the project's real requirement.
