# Mode P (pull) record, 2026-11-18

**Requirement:** packets made outside the Pi reach Linux through the CAN shield; Linux holds them; **Secure World asks**, Linux hands them over, Secure World judges them. Design: `MODE_P_PULL_DESIGN.md`.

## Done (evidence in files, nothing here is a board result)
| Unit | What | Evidence |
|---|---|---|
| P0 | Design note, snapshot of optee_os before the change (clean at `6fe8ffad0`) | `MODE_P_PULL_DESIGN.md`, `source_snapshots/20261118_modeP_pre/` |
| P1 | Host tests written first (PQ1 to PQ10) | `source_tests/harness.c` (the pull build did not compile until P2) |
| P2 | Secure World code, flag `CFG_RPI3_CAN_MONITOR_PULL` (default n): commands 30 and 31, Core 1 request issue, deadline and miss rules, command 17 refused; command 17 split into prepare + apply, behaviour unchanged. optee_os commit `05543140e` (local, not pushed) | `rpi3_can_monitor.[ch]`, `rpi3_can_monitor_pta.c`, `pta_rpi3_can_spi_poc.h`, `conf.mk` |
| P3 | Host: flag off 440 / 1499 / 451 checks (were 434 / 1493 / 451 + 6 new "commands 30 and 31 do not exist" checks; the UART build shows 457), flag on **538**, 0 failed; static checks **70 pass** (F27, F13 and the header boundary updated for the prepare step; PQ1 to PQ8 added, PQ7 and PQ8 on the object code); mutation **27 of 27** caught; scratch builds `PLATFORM=rpi3` for flag off and on: exit 0, 0 warnings, both disassemblies non-empty and checked | `baseline/modeP_host_verification_20261118.txt`, `source_tests/mutation_pull_20261118.txt` |
| P4 | Normal World: client functions for 30 and 31, forwarder queue (drop-oldest, 38 host checks), `can_forwarder --pull` | `tools/monitor_client/` |
| P5a | Candidate image composed: hardened c16384 image with only `armstub8.bin` replaced; read-back, boot-file and rootfs comparisons done | `build_scratch/candidate_image/README.md`, `SHA256SUMS` |

## NOT done
- **Not flashed, not booted, not tested on the Pi.** Whether the image boots and whether the forwarder meets the 40 ms reply time is unknown until a board run. The Pi still runs the proven hardened c16384 image (Mode F).
- The `can_forwarder --pull` decision logic has no host test (it needs the TEE client); only the queue and the packet parsing do.
- Timing of Mode P (request every 50 ms, reply due in 40 ms) is a design value, not a measurement. Under load (F24) late replies would show as `LATE_ANSWER`, missed requests and false traffic TIMEOUTs.
- The plan itself has no Mode P section yet apart from the log entry; the plan status board is unchanged.

## How to try it (needs the user)
1. `bash scripts/card_helper.sh check rpi3-sdcard-monitor-modeP.img` with the 8 GB spare card in the laptop; run the two `sudo` commands it prints; put the card in the Pi.
2. `verify_boot` must show 16 pass, 0 FAIL.
3. `bash scripts/modeP_board_test.sh 10.237.27.187`.
4. Rollback: flash `rpi3-sdcard-monitor-hardened-c16384.img` the same way.

## Board run 1, 2026-11-18 (Mode P image with latency page, flashed by the user on the 8 GB spare) - log `baseline/modeP_board_test_20261118.txt`
- Image sha256 `1646bd9f6978...`; verify_boot 16 pass, 0 FAIL, 2 skipped (`baseline/verify_boot_modeP_p6_20261118.txt`); OP-TEE revision `80b310010` = the build.
- Status page says mode 2. **Push is refused:** the old forwarder (command 17) replayed 5 packets: 0 judged, 5 errors, frame total 0.
- **Pull works:** 57 packets made on the laptop went through can0 (rx_packets +57), were held by the forwarder and judged by Secure World only as replies to requests: 497 requests, **497 ON_TIME, 0 LATE, 0 STALE/UNISSUED/DUPLICATE**, 57 packets judged, 0 left queued, 0 dropped. Heartbeat 26 ON_TIME, 0 LATE.
- Events from the packets: AUTH_FAIL 1 (altered), REPLAY 2 (older counter detail 1, repeated counter detail 2), COUNTER_GAP 1, MALFORMED 2, COUNTER_RESYNC 1, TRAFFIC_RECOVERED 3.
- Missed-request rules seen on the board: TIMEOUT detail 3 (heartbeat never answered) and detail 4 (packet request never answered) at 70 s after boot, because no forwarder ran during the boot grace; HEARTBEAT_RECOVERED detail 0 and 1 once it started; TIMEOUT detail 4 again after the forwarder ended.
- **Two script expectations were wrong, not the monitor:** the script first expected REPLAY 1 and TRAFFIC_RECOVERED 1 and printed 2 FAIL. REPLAY has two events by design (detail 1 older, detail 2 duplicate; the demo has both), and the demo pauses (1000, 400, 2500 ms) all exceed the 300 ms deadline, so 3 recoveries is right. I checked this in the source and in the event log (timestamps), corrected the script, and did not re-run it (a re-run needs a reboot because the counters are now past 1060). The unedited first-run output stays in the log file.
- NOT measured yet: reply-time p50/p99/max (idle and under load), CPU cost of tight polling, `board_full_check.sh --mode-p`.

## ON HOLD until tomorrow (user decision, 2026-11-18): timing runs and full check
The user said to do these on 2026-11-19. State left behind: the Pi runs the Mode P image (sha256 `1646bd9f6978...`, 8 GB spare card), booted, `can0` restored to the boot mode, `/tmp` clean. Nothing else was changed.
Steps for tomorrow (each phase needs a fresh boot because Secure World keeps its maximum since boot; reboots and board time need the user's go at that point):
1. Reboot, wait 90 s, `verify_boot`, then `scripts/modeP_timing_test.sh 10.237.27.187 idle 600` (Linux and Secure World side p50/p99/max, LATE_ANSWER).
2. Reboot, `scripts/modeP_timing_test.sh 10.237.27.187 load 600` (3 busy processes on cores 0, 2, 3).
3. If late replies appear: tune the forwarder (`spin`, pinned to cores 0, 2, 3 only), report the CPU cost, re-measure.
4. Reboot, copy the tools and run `board_full_check.sh --mode-p` (command-17 checks skip with the reason; F2 R4/R4b is the expected KNOWN).
5. Record everything in `baseline/`, update this file, commit. Rollback image: `rpi3-sdcard-monitor-hardened-c16384.img`.
Also open: correct the sentence "Linux can't forge a packet because it has no key" in the documents and PDFs (wording given by the user: the forwarder does not hold the key, but a root attacker could extract the prototype key; forgery is prevented only against a bus attacker).

## 2026-11-21: real CAN-FD bus with a separate sender Pi, and the N-04 finding (full report: `REAL_BUS_RESULTS.md`)
- The Mode P image (p6) ran with frames from a second Pi (Raspberry Pi OS, same Seeed shield) over the real bus: 600 packets at 10/s, all judged, 1490 requests all ON_TIME; six attacks produced AUTH_FAIL, REPLAY (detail 1 older, detail 3 conflict), MALFORMED, COUNTER_GAP (detail = counter - last - 1) and TIMEOUT; Linux misbehaviour (dropped frames, frozen forwarder, heartbeat off) was detected and recovered. Mode P forwarder: `can_forwarder --pull`.
- **Finding (supersedes the note above that `board_full_check --mode-p` was still to be run):** `board_full_check C3` FAILED on the Mode P image. The p6 image carried the ORIGINAL DTB, so flashing it on 2026-11-18 removed the N-04 reservation of the secure DRAM that had been installed by hand on 2026-11-12. Fixed in the image build: `scripts/compose_image.sh` always installs the N-04 DTB; `card_helper.sh check` refuses an image without it. New image `rpi3-sdcard-monitor-modeP-p6-n04.img` (sha256 `cf954724...`), flashed by the user on 2026-11-21; `verify_boot` 16/0/2, C3 PASS.
- **Superseded expectation:** "F2 R4/R4b is the expected KNOWN" is replaced: `c6_negative_raw --mode-p` states the Mode P expectations (R3 commands 30 and 31 exist; R4 command 17 refused). `board_full_check.sh --mode-p` now gives 32 PASS, 0 FAIL, 0 KNOWN, 4 SKIP.
- **Load sensitivity (observed, not yet measured):** a status poller that opened a monitor session twice every 0.3 s while the forwarder ran produced LATE_ANSWER / STALE_ANSWER floods (the 40 ms reply deadline was missed). Heavy PTA polling from Linux disturbs the Mode P request/reply cycle, cf. F24.
- Still open: the idle and load timing runs (`scripts/modeP_timing_test.sh`), the permanent FD for NEW images (overlay), the wording correction about "Linux can't forge a packet because it has no key".
- Rollback image: `rpi3-sdcard-monitor-modeP-p6.img` (old DTB, so `card_helper.sh check ... --allow-old-dtb`, and C3 will FAIL), or `rpi3-sdcard-monitor-hardened-c16384.img` (also needs `--allow-old-dtb`).
