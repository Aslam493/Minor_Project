# F24: PTA-call flood can delay the heartbeat answer (analysis, 2026-11-12)

Status: **flood test RUN on the Pi 2026-11-12 (user approved); F24 CONFIRMED and quantified (section 8). No source change, nothing flashed, nothing written to the card; the monitor image stayed as it was.**

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

## 8. Results of the flood test on the Pi (2026-11-12, hardened c16384 image, SSH only)
Harness: `scripts/f24_flood_run.sh`; logs in `baseline/f24_run*_20261112.txt`. Each run: `mon_client soak 60` (software frame sender every 100 ms + heartbeat responder, `--retries 20`) while N loops of `pta_latency <cpu> 200000` (PTA cmd 12) ran, spread over CPUs 0, 2, 3; all N flooders were confirmed alive at the end of every run.
| Run | N flooders | Frames accepted | Heartbeat answers ON_TIME / LATE | Frame submit latency p50 / p99 / max | Sender lateness max | Event ring at end (ev_seq / ev_lost) |
|---|---|---|---|---|---|---|
| 0 | 0 | 598 + 1 RESYNC of 599 | 60 / 0 | 8.9 / 9.4 / 10.0 ms | 85 us | 4 / 0 |
| 1b | 3 | 599 of 599 | 60 / 0 | 23.5 / 172 / 271 ms | 178 ms | 20 / 0 |
| 2 | 8 | 599 of 599 | 59 / 1 | 22.8 / 331 / 553 ms | 831 ms | 60 / 16 |
| 3 | 24 | 599 of 599 | 84 / 8 | 10.4 / 705 / 1181 ms | 33.3 s (median 14.8 s) | 193 / 149 |
(The first 3-flooder run is kept as `f24_run1_INVALID_flooders_exited_20261112.txt`: my script passed 2,000,000 calls, `pta_latency` accepts at most 200,000, the flooders exited at once. It is a second baseline, not a flood result.)
Monitor totals after run 3 (`status 1`): TIMEOUT 153, TRAFFIC_RECOVERED 146, LATE_ANSWER 10, HEARTBEAT_RECOVERED 5.
Secure World health in every run: `init_ok=1`, **`starved=0`, `pta_lock_timeouts=0`**, Core 1 poll age under 1 ms at each reading, no panic, no hang. After the last run `verify_boot.sh` still gave 16 pass / 0 FAIL / 2 skipped.

### What this shows
1. **F24 is real and gets worse with the number of callers.** 3 flooders already give a 271 ms worst case (twice the earlier 131 ms), 8 flooders exceed D_req (553 ms against 500 ms), and 24 reach 1.18 s.
2. **It is visible, not silent.** Delayed frames made the monitor raise `TIMEOUT` (detail 1, the traffic deadline T = 300 ms) followed by `TRAFFIC_RECOVERED` although frames were sent every 100 ms: a **false traffic alarm** caused only by PTA contention. Late heartbeat answers were recorded as `LATE_ANSWER` (1 at N = 8, 8 at N = 24). The heartbeat never reached a 3-miss TIMEOUT while the responder ran.
3. **The monitor itself held up.** Core 1 only trylocks, so it stayed healthy under the flood (no starvation, no lock timeouts).
4. **New finding F27 (event flooding):** the ordinary event ring overflowed: `ev_lost` 16 at N = 8 and 149 at N = 24. The pinned slots (4 in use) kept the first evidence per class, but the history of ordinary events is lost. A caller who can create contention can also bury the event log.
5. **Limit of this test (F28):** at N = 24 the 24 busy flooders also starve the sender/responder process of CPU on 3 cores (sender lateness median 14.8 s), so part of the lateness there comes from Linux scheduling, not only from the serialised PTA. The test cannot separate the two. N = 3 and N = 8 are the cleaner PTA-contention points. `BUSY` stayed 0 because `--retries 20` hides it; the tool counts BUSY only if all retries fail.
6. **Still detection only.** Nothing here prevents it (no TZASC). Who can do it is unchanged (section 2: root and the `test` user).

### Consequences for the design (proposals, not applied)
- C6: rate-limit or coalesce repeated `TIMEOUT`/`TRAFFIC_RECOVERED` events so a flood cannot overflow the ordinary ring (F27).
- C5/C6: when a TRAFFIC timeout and PTA contention coincide, the report should say "possible PTA contention" rather than "no traffic": the existing contention counters (`skip_total`, BUSY) are the signal.
- Config: remove the `test` account from `teeclnt` on a production image; run the real consumer with a real-time priority so Linux CPU starvation does not add to it.
- Open design option (needs separate review): allow concurrent entry for cmds 22/23 only.

## 9. Config mitigation applied on the Pi (2026-11-12)
- `scripts/harden_teeclnt.sh <ip>` removes the stock `test` account from group `teeclnt` (edits one line of `/etc/group` on the Pi's rootfs; original saved as `/etc/group.pre-f24` on the Pi; `--restore` puts it back). Log: `baseline/f24_harden_teeclnt_20261112.txt`.
- Result: before, user `test` COULD open `/dev/tee0`; after, `teeclnt:x:104:` is empty, **user `test` is denied, root still works**, `verify_boot.sh` 16 pass / 0 FAIL, tee-supplicant running (it uses `/dev/teepriv0`, group `tee`, unaffected).
- First attempt failed and changed nothing: BusyBox `delgroup` only deletes a whole group. Replaced by a `sed` edit of the member list (script fixed, rerun above).
- **Persistence:** the change lives on the Pi's rootfs (ext4 on the card), so it survives reboots but **not a reflash**: the stock `optee_client_ext` Buildroot package (shared by every platform in this tree) recreates `test` in `teeclnt` on each image build. Making it permanent in the image needs an rpi3-only post-build step; NOT done (would touch the shared package or the build scripts).
- What this closes: the unprivileged route to F24. What remains: **root can still do F24** (and can already stop the heartbeat by killing the responder), and any future account added to `teeclnt`.

## 10. F24 status
- Confirmed, quantified (section 8): **open as a documented limit** of a detection-only design on a Pi 3.
- Mitigated: unprivileged access removed on the running Pi (section 9). Not yet permanent in the image build.
- NOT done, needs approval because it changes approved Secure World code and needs a rebuild plus a card swap: event rate-limiting / coalescing for F27 (repeated TIMEOUT and TRAFFIC_RECOVERED events overflowing the ordinary ring), and a "possible PTA contention" hint on traffic timeouts.
