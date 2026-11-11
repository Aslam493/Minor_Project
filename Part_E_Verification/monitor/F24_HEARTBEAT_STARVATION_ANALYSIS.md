# F24: PTA-call flood can delay the heartbeat answer (analysis, 2026-11-12)

Status: **analysis done from existing data and a read-only look at the Pi; the flood test on the Pi is NOT run yet (needs user approval). No source change, nothing flashed.**

## 1. The finding (from C2, MONITOR_STATE_DESIGN.md F24)
The project PTA is `SINGLE_INSTANCE | MULTI_SESSION | KEEP_ALIVE` without `CONCURRENT`, so OP-TEE serialises every PTA call. Any caller that loops on the PTA makes all other callers wait, including the consumer's heartbeat answer (commands 22 then 23).

## 2. Who can trigger it (checked on the Pi 2026-11-12, read-only)
- `/dev/tee0` is `root:teeclnt`, mode 0660; group `teeclnt` (gid 104) contains the user **`test`**. `/dev/teepriv0` is `root:tee` 0660.
- So only **root** and the **`test` user** (or any account later added to `teeclnt`) can drive F24. Nobody else can open the device.
- Root can already stop the heartbeat by killing the responder, so for root F24 adds no new ability. The one new exposure is the **unprivileged `test` account** (and anything running as it), which can delay answers without being root.

## 3. Data we have (pta_latency_preliminary.txt, idle board, cmd 12, 2000 calls per caller)
| Case | p50 | p99 | max |
|---|---|---|---|
| 1 caller alone | 2.9 ms | 3.3 ms | 5-6 ms |
| 3 callers at once (one per Linux core) | 0.87 ms | 66-76 ms | **131 ms** |
Contention made the tail 20x worse (unfair wake-up order), while the median per call got shorter (F23, still unexplained).

## 4. What the monitor does about it
- Heartbeat: Core 1 issues a challenge every H = 1000 ms; the answer must arrive within D_req = 500 ms; `K_MISS = 3` misses -> TIMEOUT (about 3 s). Measured worst delay with 3 callers is 131 ms = **3.8x below D_req**, so three flooders alone did not reach a miss in the data we have.
- Frames: during a flood real frames can be refused with `BUSY` and Core 1 can raise a traffic timeout although traffic flows (INTERFACE_DECISION_REGISTER.md line 53). Both are recorded as events, so the effect is **visible**, not silent.
- It is **detection only**: the Pi 3 has no TZASC, so Secure World cannot stop Linux from calling the PTA. This must stay in the report as a limit.

## 5. What is NOT known (the reason for the test)
- Only 3 Linux CPUs exist, but the number of concurrent callers is not limited by cores: threads or processes can pile up. How the worst delay scales with 8, 24, 64 callers is **unmeasured**; 3 callers give 131 ms, D_req is 500 ms.
- Whether the responder (`soak` or the real consumer) still answers on time, and whether any answer is LATE or a miss is counted, under that load.
- How many frames get BUSY and how long the monitor needs to flag it.

## 6. Mitigations (ranked)
1. **Configuration, no code:** keep `teeclnt` membership minimal; remove `test` from the group on a production image; document that group membership equals the ability to delay the heartbeat. (Linux-side; not a Secure-World guarantee.)
2. **Detection (already built):** late answer, MISS, TIMEOUT and BUSY events. Needs the test to show they fire.
3. **Design option, not decided:** allow concurrent PTA entry for cmds 22/23 only (needs a thread-safety review of the legacy SPI commands; touches core code). Do not do this without a separate approval and review.
4. **Not possible here:** hardware isolation (no TZASC).

## 7. Test plan (to run on the Pi, after approval; nothing persistent is changed)
Setup on the Pi's /tmp only (removed afterwards): `mon_client` (aarch64, from `build_scratch/monitor_client_aarch64/`), `pta_latency`, and a key file made from the prototype `K_frame`. The monitor image stays as it is.
Each run is 60 s: `mon_client soak 60 --key-file /tmp/kf` (software sender + heartbeat responder, prints ON_TIME / LATE answers, BUSY count, miss_count, starvation) while **N flooders** run `pta_latency <cpu> <calls>` (cmd 12 loop):
| Run | N flooders | Purpose |
|---|---|---|
| F24-0 | 0 | baseline (earlier soak: 599/599 accepted, answers on time) |
| F24-1 | 3 | reproduce the 131 ms data point with the responder present |
| F24-2 | 8 | scaling |
| F24-3 | 24 | scaling, likely to show late answers or BUSY |
Record per run: ON_TIME/LATE answers, BUSY and errors, miss_count, TIMEOUT events from `mon_client events`, starved, `pta_lock_timeouts`, and Linux responsiveness. Stop early if the Pi becomes unresponsive or a panic appears; flooders are killed from the laptop by PID (the Pi's busybox has no `timeout`/`pkill`).
**Expected, not a pass/fail target:** the monitor's own counters stay healthy (Core 1 still polls; `starved=0`) because Core 1 only trylocks; the effect shows up as LATE answers, BUSY, misses. Any `starved=1`, panic or hang is a finding.
**Risk:** load only; no forced fault, no flash, no write to the card. A hung Pi would need a power cycle (the monitor image does not persist state).
