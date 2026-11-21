# Real CAN-FD bus results: separate sender Pi to the receiver Pi (2026-11-20 and 2026-11-21)

**What this is.** The first tests on a **real CAN-FD bus**, with the frames created by a **separate sender Pi** and not injected through the PTA.
Steps 1 to 7 passed. Step 8 (the receiver's full check) found one **open regression (C3)** and two expected Mode P differences. Details below.
Status of this file: first written before the C3 fix; updated at the end of the night (section 9). Sections 1 to 8 describe the first runs as they were; section 9 supersedes the open items.

## 1. Setup as tested

| | Sender | Receiver |
|---|---|---|
| Board | Raspberry Pi 3 B Rev 1.2, 32 GB card (overwritten for this role on 2026-11-20 by the user's decision, **old contents not backed up**) | Raspberry Pi 3 B, 8 GB card, our OP-TEE image |
| OS | Raspberry Pi OS Lite 32-bit (2026-10-06), kernel 6.18.50+rpt-rpi-v7, hostname `sender`, 10.237.27.170 | Buildroot, kernel 6.7.0-rc6-v8, 10.237.27.187 |
| Image | n/a | **Mode P (pull)**, status page `mode 2`, armstub `01f7caa9...` = `modeP-p6` |
| CAN | Seeed CAN-FD HAT v2 (MCP2518FD), CAN0 block, `dtoverlay=seeed-can-fd-hat-v2`, 40 MHz | same shield, same chip, same clock |
| Wires | CAN_H-CAN_H, CAN_L-CAN_L, GND-GND, both 120 ohm switches ON | |
| Bit rates | nominal 500000 (87.5 %), data 2000000 (75.0 %), FD on, no loopback, listen-only off | same (set with `tools/canmode/canfdcfg can0 bus` on the receiver; BusyBox `ip` cannot set FD) |

Who holds which key: the sender Pi holds **no key**. The packets (counter, payload, 12-byte HMAC tag, padding = 48 bytes) are made on the
laptop by `tools/monitor_client/packet_gen.py` with the prototype K_frame from the Secure World source, and replayed on the bus by
`laptop_can_send.py` running on the sender. This is the "direct sender" design of `monitor/DIRECT_SENDER_SETUP.md`. The prototype key is also
inside the receiver image (see limits).

## 2. Results table

| Step | What was checked | Result | Key numbers | Log file |
|---|---|---|---|---|
| 1 | Both Pis reachable | PASS | hostnames `sender` / `buildroot`; both up | `baseline/real_bus_step1_reachable_20261120.txt` |
| 2 | Both CAN interfaces configured the same | PASS | UP, ERROR-ACTIVE, 500000 / 87.5 %, tq 25, clock 40 MHz, FD off at that point, no loopback, listen-only off, restart-ms 0 on both; same `mcp251xfd ... successfully initialized` | `..._step2_can_config_20261120.txt` |
| 2b | FD turned on at runtime (user-approved) | PASS | both: nominal 500000/87.5 %, data 2000000/75.0 %, tq 25, prop 7, ph1 7, ph2 5, sjw 2, mtu 72, ERROR-ACTIVE | `..._step3a_fd_enable_20261120.txt` |
| 3 | Raw frames over the wires | PASS | 10 classic + 10 FD (48 B, BRS): all 20 arrived, bytes identical (checked in code); receiver rx 1 to 21, sender tx 1 to 21; rx/tx errors 0, TEC/REC 0, no bus-error/bus-off | `..._step3b_raw_frames_20261120.txt` |
| 3 | Reverse direction | PASS | receiver sent 1 classic + 1 FD, sender received both; receiver tx 0 to 2, sender rx 21 to 23; 0 errors | `..._step3c_reverse_20261120.txt` |
| 4 | Load test | PASS | 1000 FD frames at 100/s sent in 9.990 s (0 late); received 1000, counters 0..999, **0 lost, 0 duplicate, 0 out of order**; receiver rx 23 to 1023; all error counters unchanged (0). No fallback to 1 Mbit/s was needed. | `..._step4_load_20261120.txt`, `..._step4_rx_capture_20261120.log` |
| 5 | Secure path (sender -> CAN -> Linux forwarder -> Secure World), 60 s at 10/s | PASS | 600 packets: controller rx +600, forwarder queued 600, 1490 requests all ON_TIME (0 LATE/STALE/UNISSUED/DUPLICATE), 600 judged, 0 left, 0 dropped; only judgment event COUNTER_RESYNC at counter 1; `last_counter` 600; init_ok=1, starved=0, pta_lock_timeouts=0, Core 1 last poll 0 ms | `..._step5_secure_path_20261121.txt` (+ `.fwd.log`) |
| 6 | Attacks sent over the real bus | PASS (one expectation differs, see 4) | AUTH_FAIL (tag bit flipped), REPLAY detail 1 (old counter), REPLAY detail 3 (same counter, changed payload = the conflict case), MALFORMED (id 0x124), COUNTER_GAP detail 4 (jump of 5), traffic TIMEOUT detail 1 at **300.4 ms** after the last accepted frame | `..._step6_attacks_20261121.txt`, `..._step6b_silence_20261121.txt` |
| 7a | Linux drops every 10th frame (10 s) | PASS | 9 frames dropped on purpose, `COUNTER_GAP` total 1 to 10 (+9), each detail 1; frame total 652 to 742 (= 99 - 9) | `..._step7_linux_misbehaving_20261121.txt` |
| 7b | Forwarder frozen for 2 s | PASS | mid-freeze read: traffic age **1.16 s** (22,220,321 ticks at 19.2 MHz), TIMEOUT counter +2 during the freeze; on resume STALE_ANSWER, HEARTBEAT_RECOVERED, TRAFFIC_RECOVERED | `..._step7b_c_clean_20261121.txt` |
| 7c | Heartbeat responder off for ~5 s | PASS (corrected run) | started from `hb_state ANSWERED`; after ~5 s: 4 misses, last good heartbeat 5.29 s old, TIMEOUT counter +1, **TIMEOUT detail 2** event; traffic age only 72 ms (traffic kept flowing); after restart HEARTBEAT_RECOVERED, miss count 0; the two forwarder swaps lost no frames | `..._step7c_corrected_20261121.txt` |
| 8 | Receiver: `board_full_check.sh --mode-p` | **2 FAIL** | 30 PASS, 2 FAIL, 0 KNOWN, 4 SKIP. C3 FAIL = open regression (section 6). F2 R3/R4 FAIL = Mode P differences (section 6). | `..._step8_board_full_check_20261121.txt` |
| 8 | `verify_boot` | see section 9 | | |

`..._` stands for `baseline/real_bus`.

## 3. How to read the results (simple words)

- The bus works in both directions at 500 kbit/s nominal and 2 Mbit/s data, with no errors in 1000 frames at 100 per second.
- The sender Pi never holds the key. It only replays finished packets. Secure World judged all 600 packets of the 60 s run and raised no fault.
- Every attack produced the event the design says it should. Rejected frames (AUTH_FAIL, REPLAY, MALFORMED) do not refresh the traffic timer; only accepted, GAP and RESYNC frames do (design table `MONITOR_STATE_DESIGN.md`).
- When Linux misbehaves (drops frames, freezes, stops the heartbeat) Secure World notices, and recovers when Linux comes back.

## 4. Differences from the expectations written before the run (nothing was changed to make a test pass)

1. **GAP detail 4, not 5.** The design defines the detail as `counter - last - 1`: a jump from 620 to 625 means 4 missing frames. The monitor behaves as designed; the expectation "detail 5" was off by one.
2. **Conflict is logged as REPLAY detail 3.** The design's event classes are REPLAY with detail 1 older counter, 2 duplicate, 3 conflict; the verdict name CONFLICT is internal.
3. **Wrong CAN id.** `can_forwarder` does not filter by id (it only drops error frames, remote frames and wrong lengths). The wrong id (0x124) was forwarded and judged **MALFORMED in Secure World** (stream list `{0x123}`).
4. **TRAFFIC_RECOVERED after a 1 s silence is counted but not logged.** Events are limited to one per second per class (F27, documented in `COMPONENT_5_HEARTBEAT_AND_TRAFFIC_DEADLINE.md`; the counters stay exact). A 2.5 s silence (Step 6b) showed `TIMEOUT detail 1` 319 ms after the last frame and a logged `TRAFFIC_RECOVERED` on restart.
5. **The TIMEOUT class is shared by details 1, 2, 3 and 4**, so in Step 7b only one TIMEOUT (detail 4, request never answered) is in the log for the freeze. The traffic timeout of the freeze is proven by the 1.16 s traffic age and the TIMEOUT counter, not by an individual event.
6. **The middle TIMEOUT in Step 6** (seq 10) comes from my own test: the first attack frame was sent after a 400 ms gap, longer than the ~300 ms deadline.
7. **ACCEPTED counts are inferred in Mode P.** The forwarder cannot see verdicts in Mode P. "599 ACCEPTED" in Step 5 is inferred from 600 judged, 1 RESYNC and no fault events.
8. `skip_total` rose from 0 to 186 during Step 5: Core 1 found the PTA lock busy that many times (contention with Linux's request polling); never consecutive (`skip_count` 0), never starved.

## 5. Findings and my own mistakes

**Finding: heavy PTA polling disturbs the heartbeat (compare F24).** In a rerun of Step 7b/7c I added a status poller that opened a monitor session twice every 0.3 s while the forwarder ran. The result was a flood of `LATE_ANSWER` and `STALE_ANSWER` events (ev_lost grew by 44 and 80) that the clean runs did not have. The extra load on the monitor lock delayed Linux's answers past the 40 ms Mode P deadline. **That run is INVALID as a fault test** and is kept only as a record (`baseline/real_bus_step7b_c_rerun_INVALID_poller_load_20261121.txt`), but it is evidence that Mode P replies are sensitive to Linux-side load on the monitor, in line with F24.

My mistakes tonight, in order:
1. First sender write of the 32 GB card was corrupted (1018 bytes of `kernel7.img` differed); the Pi stopped after the rainbow screen. Found by comparing with the reference image; the second write was byte-identical. Lesson: `cmp` every card after writing.
2. The file `SD_Card_Backups/sd32gb_before_sender_2026-11-20.img.gz` is an empty 20-byte gzip. It was reported as a verified backup; it is not one. The old 32 GB contents were discarded by the user's decision.
3. The first load script wrote a 56-byte FD frame (a SocketCAN FD frame must be written as the full 72-byte structure) and failed at the first send, before anything reached the bus. Fixed the packing only; the test is unchanged.
4. The poller above.
5. The first 7c started with the heartbeat already timed out (left over from 7b), so it showed no fresh transition. Redone from a healthy heartbeat (`step7c4.sh`).
6. 7c, first run: my kill-and-restart took about 2.6 s (COUNTER_GAP detail 26). Fixed with a one-command swap.

## 6. Step 8: what failed and why

`board_full_check.sh --mode-p`: **30 PASS, 2 FAIL, 4 SKIP** (the 4 skips need command 17, refused on Mode P).

- **C3 FAIL: open regression.** `/proc/iomem` has no reservation for the secure DRAM `0x10000000-0x10ffffff`. **Cause:** the receiver's boot partition holds the original DTB (`6b90c3e0...`, no `optee_secure` node; the running kernel's FDT has none either). The N-04 DTB (`monitor/n04/candidate-bcm2710-rpi-3-b.dtb`, `e8ac38fa...`) was installed by hand on the card on 2026-11-12. The Mode P image flashed on 2026-11-18 was composed from the hardened c16384 image, whose boot partition carries the original DTB, so the flash overwrote the N-04 DTB. `board_full_check` had passed C3 on 2026-11-16, and was not run on the Mode P image until now. Consequence: Linux is not told to keep out of the secure DRAM (no-map); the Secure World code does not depend on it for these tests, but the isolation setting N-04 is missing.
- **F2 R4 FAIL: expected on Mode P.** `MODE_P_RECORD.md` already names it a known item: command 17 is refused on the Mode P image. R4b (state unchanged) passed.
- **F2 R3 FAIL: same cause.** The test expects commands 24 to 30 to be NOT_SUPPORTED; command 30 exists in Mode P (it returned 0xffff0006, not NOT_SUPPORTED).

## 7. Limits that remain

- Real CAN-FD bus; frames created by a separate sender Pi, but the packets were made on the laptop; the sender replays finished packets.
- The **prototype keys** are also inside the receiver image (same key in every build and device, in the source tree).
- **No TZASC** on the Pi 3: Normal World memory protection is not hardware-enforced; N-04 is only a DTB reservation.
- **Detection only**, no prevention claim.
- Mode P timing under load is sensitive (finding in section 5); timing runs (idle/load) are still to be done.
- Steps 1 to 7 ran with the receiver's `can0` in FD **at runtime only**. (Since section 9 it is permanent on both Pis.)
- A `TIMEOUT` detail is not always visible in the log (one event per second per class); counters are the exact record.
- Not tested: `can1`, the second controller (Design 2 / C7).

## 8. Reproducing

Scripts: `scripts/real_bus/` (README there). Packet files are generated with `packet_gen.py`. The test-only `FWD_TEST_DROP_EVERY` option exists only in a scratch copy of the forwarder (`scripts/real_bus/can_forwarder_test_drop_every.patch`); the repository's `can_forwarder.c` is unchanged.

## 9. Update at the end of the night

### 9.1 Everything that happened after the first report (in order)

| # | What | Result | Evidence |
|---|---|---|---|
| 1 | First report committed and pushed | done (`6c3a2ef`) | this file |
| 2 | C3 root-cause fix: `scripts/compose_image.sh` always installs the N-04 DTB (`e8ac38fa...`); `modeP_p6_image_build.sh` uses it; `card_helper.sh check` refuses an image whose DTB is not N-04 (exit 7, negative controls on the p6 and hardened c16384 images; `--allow-old-dtb` for a rollback, with a warning) | new image `rpi3-sdcard-monitor-modeP-p6-n04.img` sha256 `cf954724...e3f`; **only `bcm2710-rpi-3-b.dtb` differs** from the p6 image, partition table and root filesystem byte-identical | `baseline/compose_modeP_p6_n04_20261121.txt` |
| 3 | User flashed the 8 GB card (`dd` + `cmp` = MATCH), receiver booted. Both Pis got NEW DHCP addresses: receiver 10.237.27.77, sender 10.237.27.144 (the old receiver address 10.237.27.187 no longer answered) | `/proc/device-tree/reserved-memory/optee_secure@10000000` present | `baseline/verify_boot_modeP_n04_20261121.txt` |
| 4 | `verify_boot` on the new image | 16 pass, 0 FAIL, 2 skipped | same file |
| 5 | `board_full_check.sh --mode-p`, first run | **C3 PASS** (31 PASS, 1 FAIL, 4 SKIP; the 1 FAIL was F2 R3/R4) | `baseline/board_full_check_modeP_n04_20261121.txt` |
| 6 | Real-bus recheck on the new image (FD on both, 60 s at 10/s + 1 bad tag) | 601 frames in, 601 judged, 1551 requests all ON_TIME, only COUNTER_RESYNC (counter 1) and AUTH_FAIL (counter 601), `last_counter` 600, init_ok=1, starved=0, 0 lock timeouts | `baseline/real_bus_recheck_after_n04_20261121.txt` |
| 7 | F2 made mode-aware: `c6_negative_raw --mode-p` (R3: commands 30 and 31 exist in Mode P, wrong types give BAD_PARAMETERS; R4: command 17 is refused, NOT_SUPPORTED; R4b unchanged; Mode F expectations unchanged and checked: without the flag the same image still shows the 2 FAIL); `board_full_check.sh` passes the flag and tolerates no F2 failure | `board_full_check.sh --mode-p`: **32 PASS, 0 FAIL, 0 KNOWN, 4 SKIP** (C3, C4, F2, H1, H2 PASS) | `baseline/board_full_check_modeP_n04_modeaware_20261121.txt` |
| 8 | FD permanent: sender `can0-up.service` (500000/2000000 FD); receiver `/usr/sbin/canfdcfg` + new `/etc/init.d/S12can` (fallback to classic if the tool is missing; backup `/root/S12can.orig-classic`, not in `/etc/init.d`). Both Pis rebooted with the user's OK | after the reboot, with nothing set by hand: both FD 500000/2000000, mtu 72, ERROR-ACTIVE; 5 FD frames of 48 bytes crossed, receiver rx 0 to 5, 0 errors | `baseline/fd_permanent_after_reboot_20261121.txt`, `scripts/receiver_fd/` |
| 9 | `can1` investigation (read-only) | device tree correct and identical on both Pis and equal to the shield spec; a direct SPI READ probe of CE1 returns all zeros at 3 speeds and 2 modes, the same probe on chip A returns real values: **board-level hardware cause, not configuration** (physical checks listed) | `baseline/CAN1_INVESTIGATION_20261121.md` |

### 9.2 Step 8 status now
C3 is **closed** (root cause fixed in the image build, verified on the board). F2 R3/R4 are closed as Mode P expectations, not tolerated failures. Mode F expectations are unchanged. Remaining SKIPs (4): E1-E12, F1, F3 and G need command 17, refused in Mode P by design; they are covered for Mode F by the earlier Mode F runs (`baseline/board_full_check_fresh_20261116.txt`).

### 9.3 Open items
- The Buildroot overlay (`optee_full/build/br-ext/board/rpi3/overlay/etc/init.d/S12can`) and the root filesystem of the composed images still carry the OLD classic `S12can` and no `canfdcfg`. A new image composed from the Sep-20 base would lose permanent FD on the receiver exactly as the DTB was lost: add `S12can` and `usr/sbin/canfdcfg` to the overlay or to `compose_image.sh` first. The overlay is in a nested checkout and was deliberately not touched.
- Mode P timing runs (idle and under load, `scripts/modeP_timing_test.sh`), which also need a fresh boot each, are not done. The poller incident (section 5) shows why they matter.
- `can1`: physical checks 1 to 4 of `CAN1_INVESTIGATION_20261121.md`.
- `ACCEPTED` is not directly counted in Mode P (inferred), and the forwarder cannot see verdicts there.
- Addresses are DHCP: receiver 10.237.27.77, sender 10.237.27.144 tonight; a reboot can change them.
- Prototype keys, no TZASC, detection only: unchanged (section 7).


## 10. Burst behaviour (2026-11-21), Phase 1: measurement only, nothing was changed

Question: what happens to frames when the sender bursts instead of sending 10 per second? Real bus, receiver Mode P image with N-04, FD 500000/2000000, forwarder `--pull --verbose --stats`, a second `candump -d` on the receiver. Tools: `tools/monitor_client/burst_packets.py` (makes the file and writes the **expected counts before anything is sent**), `burst_send.py` (sender; `naive` = a send refused with ENOBUFS is lost, `retry` = repeated), `scripts/real_bus/burst_run.py` (orchestrator, collects every stage before and after, nothing is polled during a burst). Logs: `baseline/burst_20261121_*` (one report, one `.expected.txt`, one `candump.log` per run, `burst_20261121_SUMMARY.txt`).
Each burst: N valid packets + 4 planted frames (bad tag, old counter, duplicate, wrong ID) + one jump of 2 counters = N+4 frames on the wire; with no loss Secure World must judge N+4: AUTH_FAIL 1, REPLAY 2, MALFORMED 1, COUNTER_GAP 1.
Harness control (10 s of normal traffic through the same setup, before the bursts): 100 in, 100 judged at every stage, 0 loss, window 9.92 s in the Secure World clock against 9.90 s in `candump` (two independent clocks agree).

### 10.1 Where frames were lost, per N (every row adds up)

| run | on the wire | lost at the sender (ENOBUFS) | on the bus (sender tx) | lost in the receiver chip FIFO | lost in the forwarder's socket | lost in the forwarder queue | judged | lost in total | time first to last judged | sender burst time | frames judged per second |
|---|---|---|---|---|---|---|---|---|---|---|---|
| N150 naive | 154 | **97** | 57 | 0 | 0 | 0 | 57 | 97 | 0.53 s | 0.013 s | 107 |
| N150 retry | 154 | 0 | 154 | 0 | 0 | 0 | **154** | **0** | 1.50 s | 0.040 s | 103 |
| N500 naive | 504 | **424** | 80 | 0 | 0 | 0 | 80 | 424 | 0.79 s | 0.020 s | 101 |
| N500 retry | 504 | 0 | 504 | **17** (5 overflow events) | **249** | 0 | 238 | 266 | 2.31 s | 0.139 s | 103 |
| N1000 naive | 1004 | **689** | 315 | 0 | **77** | 0 | 238 | 766 | 2.31 s | 0.086 s | 103 |
| N1000 retry | 1004 | 0 | 1004 | 0 | **765** | 0 | 239 | 765 | 2.34 s | 0.280 s | 102 |
| normal 10 s (after the bursts) | 100 | 0 | 100 | 0 | 0 | 0 | **100, all ACCEPTED** | 0 | 9.92 s | 10 s | 10 |

Other stages, all clean in every run: sender `tx_errors` 0 and `tx_dropped` 0; receiver `rx_dropped` 0; `rx_fifo_errors` 0; forwarder queue `dropped by the queue` 0 and `still queued` 0; frames handed to Secure World = frames judged; PTA lock timeouts 0; `starved` 0; `init_ok` 1; Core 1 poll age at most 7778 ticks (0.4 ms); heartbeat ANSWERED, 0 misses at the end of every run.

### 10.2 What the numbers show (measured unless marked as inferred)

1. **Sender.** A plain burst loop loses most frames at the sender: the interface's transmit queue is 10 frames (`qlen 10`) with a 4-frame hardware ring, so a loop that sends faster than the bus (about 3600 frames/s accepted at best) gets ENOBUFS. 97 of 154, 424 of 504, 689 of 1004 were lost this way with the naive policy. With retry the sender delivered everything it was given and the bus carried all of it (sender tx = frames sent in all retry runs).
2. **Receiver chip.** Once: N500 retry lost 17 frames in the MCP2518FD (its receive FIFO holds 20 objects, interrupt coalescing is off, `rx_over_errors` +5). It did not repeat in N1000 retry at a higher rate, so this loss is intermittent and small, not the main problem.
3. **The forwarder's socket is where the big loss happens.** In the three runs that put more than about 240 frames into the forwarder's socket, exactly **238, 238 and 239** frames reached the forwarder, however many were sent. The forwarder's own queue (256) never filled. (Inferred: the raw socket's receive buffer, `rmem_default` 212,992 bytes, holds about 238 FD frames, about 895 bytes charged per frame; the kernel drops later frames. Not read from the kernel directly, the same constant in three runs is the evidence.) The socket keeps the **oldest** frames: in N1000 retry the monitor's `last_counter` ended at exactly the 238th valid counter plus the bad tag.
4. **Why the forwarder cannot keep up (read from the code and the timings).** Its loop reads one frame per iteration, then makes a call into Secure World (about 3.8 ms). Idle iterations are 13.8 ms (10 ms poll wait + the call); with frames waiting it still reaches only about **103 frames per second**, the same in every run (154 in 1.50 s, 238 in 2.31 s). The Mode P design ceiling (32 packets per reply, one reply per 50 ms = 640 per second) was never reached: replies carried at most 12 packets.
5. **Secure World was not the bottleneck.** Nothing it was handed was lost, no lock timeouts, never starved, heartbeat answered. **Correction (2026-11-21):** one heartbeat answer was LATE (`ON_TIME 36 LATE 1`) in 4 of the 6 burst runs here and in 2 of the 4 E2 runs of 10.7, and in none of the 5 clean runs (2 normal, 3 earlier 60 s runs); no miss was counted (`hb_miss_count` 0, state ANSWERED), which is why this section first said only "answered". Burst load therefore delays a heartbeat answer; the cause is open.
6. **Replies get late under burst load.** Linux-side reply time (fetch to reply) p99 was 9.3 ms idle and 43 to 55 ms in the burst runs (max 59 to 147 ms); the Mode P deadline is 40 ms; LATE_ANSWER / STALE_ANSWER events appeared in 5 of 6 burst runs and once at the start of the final normal run (max 35 ms, no frame lost). Cause not established (the burst loads the 3 usable CPUs with SPI interrupts, `candump` and the forwarder); `candump` itself adds load and was running in every run, including the control, which had no late answers.

### 10.3 Verdicts against the planted frames

| run | judged | AUTH_FAIL (exp. 1) | REPLAY (exp. 2) | MALFORMED (exp. 1) | COUNTER_GAP (exp. 1) | note |
|---|---|---|---|---|---|---|
| N150 retry | 154 of 154 | 1 | 2 | 1 | 1 | **exactly as planted**; ACCEPTED = 149 as expected |
| N150 naive | 57 | 0 | 0 | 0 | **43** | each hole left by a lost frame is a GAP |
| N500 naive | 80 | 0 | 0 | 0 | **65** | same |
| N500 retry | 238 | 0 | 1 | 0 | **6** | bad tag lost in the chip FIFO; only the frames before the socket filled were judged; `last_counter` 249 short of the end |
| N1000 naive | 238 | 0 | 0 | 0 | **223** | same |
| N1000 retry | 239 | 1 | 0 | 0 | 0 | the first 239 frames, nothing after |
| normal 10 s | 100 | 0 | 0 | 0 | 0 | 100 ACCEPTED |

### 10.4 Event accounting (code read: `rpi3_mon_ev_append`)

- Counters are exact. An event of a type that was recorded less than 1 s earlier is **counted but not recorded** (after the first 4 of each evidence class, which are pinned). In the naive runs 42, 64 and 220 `COUNTER_GAP` events were suppressed against 1 to 3 recorded; the log alone would hide the real size of the loss.
- **`ev_lost` is not a reliable "events lost" figure.** `ack` is a bookmark only and the code never clears a ring slot that has been used, so once the 40 ordinary slots have been filled once since boot, every further ordinary event counts as lost whether or not it was read. `ev_lost` rose by 3 and 7 in the two 1000-frame runs with only 8 or 9 events recorded; this is that effect, not event pressure.

### 10.5 The prediction written before the runs was wrong

Model (design limits: 640 packets/s, queue 256 with drop-oldest, about 290 us per frame): N500 loses about 155 and N1000 about 560 at the **forwarder queue**, total drain 0.25 s and 0.7 s. Measured: the loss is **earlier** (the kernel socket in front of the forwarder, 249 and 765), the forwarder queue lost 0, and the drain takes 1.5 to 2.3 s because the forwarder ingests about 103 frames per second, not 640. The model ignored that the forwarder reads one frame per loop iteration and that every iteration costs a call into Secure World.

### 10.6 Limits of this measurement

One run per cell (no repeats, so the chip loss of 17 is a single observation); the sender is a Python loop; `candump` runs alongside the forwarder; the time to drain comes from the Secure World event clock (first TRAFFIC_RECOVERED to the last traffic TIMEOUT minus the 300 ms deadline, checked against `candump` in the control); the socket capacity is inferred, not read; the cause of the late replies is open. Phase 2 (causes and proposed fixes) waits for the user's OK.

### 10.7 Phase 2 evidence (2026-11-21): the socket capacity was measured, not inferred

Two extra experiments, both without any change to the system under test; full text in `baseline/BURST_PHASE2_CAUSES_AND_PROPOSALS_20261121.md`.
- **E1, socket capacity** (`tools/monitor_client/sockcap.c`, `baseline/sockcap_experiment_20261121.txt`): one extra raw socket that does not read during a 1004-frame burst. Default buffer (212,992 bytes): exactly **238** frames queued, the oldest ones (counters 1 to 237), kernel drop counter **766** (239 + 766 = 1005 offered), about 894 bytes charged per frame. The same socket with a forced 1 MiB buffer (the kernel doubles it): **1004** frames queued, 0 dropped, chip counters clean. So the 238 is the kernel's socket buffer, and the loss in N500/N1000 retry is that buffer overflowing.
- **E2, repeat without `candump`** (`baseline/burst_20261121_E2_nocd_SUMMARY.txt`, `burst_20261121_N*_retry_nocd.txt`; the two repeats of each size overwrote each other's per-run files because of a tag collision in the first version of `burst_run.py`, all four runs are in the summary JSON; fixed in the script): N500 retry twice and N1000 retry twice. Forwarder received **239** in all four; chip overflow, FIFO and dropped counters 0 and sender tx = receiver rx in all four (the 17-frame chip loss of Phase 1 did not recur: 1 in 8 heavy bursts so far); reply p99 50.4 to 54.6 ms, max 57 to 62 ms, 0 to 2 `LATE_ANSWER` per run, heartbeat ANSWERED. So neither the socket loss nor the late replies are caused by `candump`.
- Receiver facts read for the proposals: `chrt` and `renice` exist (no `taskset`; the forwarder has `--cpus`), the chip's interrupt 186 is not pinned (`smp_affinity` f; 679/677/667 interrupts on the three CPUs) and its handler is a real-time kernel thread, `rmem_default` = `rmem_max` = 212992, `txqueuelen` 10 on both Pis.
- **State after a cold boot (checked at the start of Phase 2, both Pis had rebooted):** FD 500000/2000000 up with nothing set by hand, `10000000-10ffffff : reserved`, monitor `init_ok=1 starved=0`: the fixes of the night survive a second reboot.

Proposals and the decisions needed (D1 to D6) are in the Phase 2 document; **nothing has been changed**, Phase 3 waits for the user's OK.
