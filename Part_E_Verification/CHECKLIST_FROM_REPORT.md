# Checklist from the final report (every row), to tick off from the start

Source: `FINAL_TEST_REPORT.md` (same IDs). Windows: **L** laptop, **R** receiver (`ssh root@10.237.27.212`), **S** sender (`ssh sender@10.237.27.144`, `cd /tmp/demo`). Live commands marked R/S need the helper functions of `LIVE_DEMO_RUNSHEET.md` block A (`alarms`, `evt`, `age`, `snap`, `gapcheck`).

## Three ways to check

1. **One command, no hardware (2 minutes):** `python3 scripts/check_report_evidence.py` -> every row's evidence file exists and every burst/overload series is recomputed from the raw run files. Expected last line: `RESULT: the recorded evidence supports the report`.
2. **Live, short (12 minutes):** follow `LIVE_DEMO_RUNSHEET.md`; it re-checks, smaller and in front of the audience: A3 (part 2), B1 (part 3), C1-C8 (parts 4, 5, 7), D3, D4/D5a (part 6) and E2br (part 2.5). The other rows use the commands in the table.
3. **Full re-run of everything:** the stage commands in the table (`campaign.py A|B|C|E1|R2c|R2d`, `scripts/real_bus/series.sh`); about 1.5 hours; stop the boot service first where it says so (`/etc/init.d/S95can_forwarder stop >/dev/null 2>&1 </dev/null`), start it again afterwards.

Result column: PASS / FAIL / LIMIT is what the report says. FAIL rows are kept on purpose; each has its explanation and a rerun row.

## A. Campaign table (31 PASS, 1 FAIL)

| Done | ID | Test | Expected (report) | Report says | Check it LIVE | Re-check the EVIDENCE (no hardware) |
|---|---|---|---|---|---|---|
| [ ] | A1 | verify_boot on the receiver | 0 FAIL | **PASS** | L: `cd tools/verify_boot && ./verify_boot.sh 10.237.27.212 --expect-core1-pinned` -> `17 pass, 0 FAIL` | open `baseline/final_campaign/A1_verify_boot.txt` |
| [ ] | A2 | board_full_check.sh --mode-p (fresh boot, empty ring) | 0 FAIL (C3 N-04 reservation PASS) | **PASS** | L (4 min, stops and restarts the boot service): `RX=10.237.27.212 python3 scripts/final_campaign/campaign.py R2d` -> `A2r2 ... 32 PASS, 0 FAIL` | open `baseline/final_campaign/A2_board_full_check_modeP.txt` |
| [ ] | A3 | DTB = N-04 (e8ac38fa) and the secure-memory reservation is present | image DTB sha256 e8ac38fa...; reserved-memory node optee_secure@10000000 live | **PASS** | R: `grep -i -E '10000000/08000000' /proc/iomem` -> both `reserved`; `ls /sys/firmware/devicetree/base/reserved-memory/` -> `optee_secure@10000000` | open `baseline/final_campaign/A3_dtb_n04.txt` |
| [ ] | A4 | Reboot both Pis: can0 up in CAN-FD 500k/2M with no manual step | receiver and sender can0 UP, FD on, 500000/2000000 | **PASS** | R: `/tmp/canmode can0 / grep -E 'nominal/data bit/state'` -> 500000, 2000000, ERROR-ACTIVE. S: `ip -details link show can0 / grep -E 'qlen/bitrate'` -> qlen 2000, 500000, dbitrate 2000000. Best proof: reboot a Pi and look again, nothing set by hand | open `baseline/final_campaign/A4_E2_reboot.txt` |
| [ ] | B1 | 600 valid frames at a steady 50 frames/s | 600 judged, all ACCEPTED, 0 loss at every stage, Secure World gap 0 | **PASS** | S: send `1_normal.txt` (run sheet part 3) -> R: `alarms` -> `frame` +10, no alarm. (The report used 600 frames: `campaign.py R2d` does it) | `python3 scripts/real_bus/series_report.py b1x 1 --date 20261122` |
| [ ] | B2 | Heartbeat for 5 minutes | 0 late and 0 missed (the first answer after the forwarder start may be LATE only if its first state was MISSED | **PASS** | R: `/tmp/mon_client --retries 20 status 0 / grep -o 'hb_state [A-Z_]* hb_miss_count [0-9]*'` -> `ANSWERED ... 0`, stays so for minutes. Full 5 min: `campaign.py B` (7 min, own forwarder: stop the boot service first) | open `baseline/final_campaign/B2_B3_300s.txt` |
| [ ] | B3 | Mode P for 5 minutes with 10 frames/s | all requests on time (a restart-phase LATE on the first request is the only allowed exception) | **PASS** | R: `/tmp/mon_client --retries 20 status 2 / head -1` -> `ON_TIME <big number> LATE 1` (LATE 1 = the restart effect). Full 5 min: `campaign.py B` | open `baseline/final_campaign/B2_B3_300s.txt` |
| [ ] | C1 | Replay (old counter b-2, valid tag) | REPLAY +1 detail 1 (exact counter), other attack classes 0, judged 8, last_counter 7207 | **PASS** | S: `3_replay.txt` -> R: `alarms` REPLAY +1; `evt` shows `REPLAY detail 1` | open `baseline/final_campaign/C1.txt` |
| [ ] | C2 | Duplicate (the last frame again, byte for byte) | REPLAY +1 detail 2 (exact counter), other attack classes 0, judged 8, last_counter 7214 | **PASS** | S: `4_duplicate.txt` -> REPLAY +1, `evt` detail 2 | open `baseline/final_campaign/C2.txt` |
| [ ] | C3 | Same counter, different payload, valid tag | REPLAY +1 detail 3 (exact counter), other attack classes 0, judged 8, last_counter 7221 | **PASS** | S: `5_conflict.txt` -> REPLAY +1, `evt` detail 3 | open `baseline/final_campaign/C3.txt` |
| [ ] | C4 | Tampered payload (tag of the original payload) | AUTH_FAIL +1 detail 0 (exact counter), other attack classes 0, judged 8, last_counter 7228 | **PASS** | S: `2_tamper.txt` -> AUTH_FAIL +1, `evt` AUTH_FAIL | open `baseline/final_campaign/C4.txt` |
| [ ] | C5 | Wrong CAN id 0x124 (valid tag for that id) | MALFORMED +1 detail 3 (exact counter), other attack classes 0, judged 8, last_counter 7235 | **PASS** | S: `6_wrongid.txt` -> MALFORMED +1, `evt` detail 3 | open `baseline/final_campaign/C5.txt` |
| [ ] | C5b | Wrong-id frames: reply cost per packet | cheaper than a valid frame (rejected before the HMAC) | **PASS** | evidence only (needs a trace run): `campaign.py C` (9 min) | open `baseline/final_campaign/C5_cost.txt` |
| [ ] | C6 | Counter jump +5 (b -> b+5) | COUNTER_GAP +1 detail 4 (exact counter), other attack classes 0, judged 8, last_counter 7247 | **PASS** | S: `7_jump.txt` -> COUNTER_GAP +1, `evt` detail 4 | open `baseline/final_campaign/C6.txt` |
| [ ] | C7 | Stop the sender (silence 1.5 s) | traffic TIMEOUT detail 1 (exact counter +1 during the silence), TRAFFIC_RECOVERED +1 when frames resume | **PASS** | R after a file, send nothing: `age` grows, `evt` -> `TIMEOUT detail 1 ... s ago` (run sheet part 5) | open `baseline/final_campaign/C7.txt` |
| [ ] | C8 | Stop the forwarder (kill -9) with a healthy heartbeat | after k=3 missed challenges: TIMEOUT detail 2 (exact counter), hb_miss_count reaches 3; on restart HEARTBEAT_R | **PASS** | R: stop the forwarder (run sheet part 7), wait 6 s, `evt` -> `TIMEOUT detail 2` (and 4); `hb_miss_count` passes 3 | open `baseline/final_campaign/C8.txt` |
| [ ] | C9 | Late Mode P answer (forwarder frozen 30-90 ms, 40 times, 20 frames/s) | answers to missed requests judged LATE (or STALE), counted exactly; forwarder count = Secure World count | **PASS** | R: `/tmp/mon_client --retries 20 status 2 / head -1` shows LATE counted. Full test: `campaign.py C` (needs the harness forwarder; stop the boot service) | open `baseline/final_campaign/C9.txt` |
| [ ] | D1a | 1004 burst at governor powersave (600 MHz), 5 runs | 1004 judged, identity PASS, 0 loss | **PASS** | S+R: run sheet part 6 (burst 2000) is the same kind of test; full: `scripts/real_bus/series.sh` (stop the boot service first) | `python3 scripts/real_bus/series_report.py p1 5` |
| [ ] | D1b | 1004 burst at governor performance (1200 MHz), 5 runs | 1004 judged, identity PASS, 0 loss | **PASS** | full re-run only: governor `performance` then `scripts/real_bus/series.sh` (see evidence command) | `python3 scripts/real_bus/series_report.py p2_ 5` |
| [ ] | D2 | 30 x N=500 (504 frames), powersave: min/median/max window and late count | 30 of 30 identity PASS, 504 judged each | **PASS** | run sheet part 6, smaller; full: `series.sh s3_ "500:retry ..."` (30 runs, 25 min) | `python3 scripts/real_bus/series_report.py s3_ 30` |
| [ ] | D3 | 2000-frame burst x5 (2004 frames) | 5 of 5 identity PASS, 0 loss | **PASS** | S: `python3 burst_send.py 8_burst2000.txt retry` -> R: `gapcheck` -> `span 2000 / judged 2000 / missing 0` | `python3 scripts/real_bus/series_report.py s4_ 5` |
| [ ] | D4 | 20008-frame burst x3 (overload) | sent = judged + counted drops; Secure World gap = drops | **PASS** | S: `9_flood12000.txt` (12000 frames, the same idea as 20008): R: `gapcheck` -> `judged + missing = span` | `python3 scripts/real_bus/series_report.py s5_ 3` |
| [ ] | D5a | Flood 35000 frames at ~3587 frames/s x3 | sent = judged + counted drops; Secure World gap = drops | **PASS** | same as D4 (run sheet part 6.4 to 6.6) | `python3 scripts/real_bus/series_report.py s6_ 3` |
| [ ] | D5b | Sustained 480 frames/s x3 (below 640) | 0 loss | **PASS** | full re-run only: paced sender `rate:7000:2` (see evidence command) | `python3 scripts/real_bus/series_report.py s8a_ 3` |
| [ ] | D5c | Sustained 943 frames/s x3 (above 640) | drops counted at one stage, equal to the gap | **PASS** | full re-run only: paced sender `rate:14000:1` | `python3 scripts/real_bus/series_report.py s8b_ 3` |
| [ ] | D5d | Lossless limit: 2400 frames x3 and 2600 frames x3 | 2400 lossless; 2600 counted drops | **PASS** | full re-run only: `plain:2400:retry` and `plain:2600:retry` | `python3 scripts/real_bus/series_report.py s7a_ 3` |
| [ ] | D5e | 2600-frame burst x3 | counted drops = gap | **PASS** | see D5d | `python3 scripts/real_bus/series_report.py s7b_ 3` |
| [ ] | D5f | Sustained 603 frames/s x3 (below 640) | 0 loss at every stage, Secure World gap 0, queue empty | **PASS** | full re-run only: `rate:9000:1-2-2-1-2-1-2-2-1-2-1-2` | `python3 scripts/real_bus/series_report.py s9_ 3` |
| [ ] | D6 | Bucket sizes | documented | **PASS** | R: `ethtool -g can0` -> RX 20 (max 24), TX 4. S: `cat /sys/class/net/can0/tx_queue_len` -> 2000. `grep CB_QCAP tools/monitor_client/canbridge.h` -> 2048 | open `baseline/final_campaign/D6_buckets.txt` |
| [ ] | E1 | Restart the forwarder 10x at random phase | only the first answer may be LATE and only when its first state was MISSED (heartbeat and packet request) | **PASS** | L (3 min, stop the boot service first): `campaign.py E1` -> 10 of 10 match | open `baseline/final_campaign/E1.txt` |
| [ ] | E2a | Reboot receiver: monitor back, boot grace respected | no TIMEOUT before G=60 s although heartbeat misses accumulate; forwarder started inside G -> never a TIMEOUT | **PASS** | R: `reboot`; ssh again at ~25 s uptime; L: `scp tools/monitor_client/mon_client root@10.237.27.212:/tmp/`; R: `cat /proc/uptime` (under 60) and `/tmp/mon_client --retries 20 status 1 / grep TIMEOUT` -> no TIMEOUT line (= 0) | open `baseline/final_campaign/A4_E2_reboot.txt` |
| [ ] | E2b | Reboot receiver: forwarder back by itself | forwarder starts at boot without a manual step | **FAIL** | R: this row FAILED before the fix (forwarder was started by hand). Check the fix with row E2br | open `baseline/final_campaign/A4_E2_reboot.txt` |

## B. Documented limits (nothing to run: read the row)

| Done | ID | Test | Expected (report) | Report says | Check it LIVE | Re-check the EVIDENCE (no hardware) |
|---|---|---|---|---|---|---|
| [ ] | F1 | Event ring `ev_lost` / `ack` behaviour | - | **LIMIT** | documented limit: read the row and its evidence | read the row in `FINAL_TEST_REPORT.md` |
| [ ] | F2 | Key material in the prototype | - | **LIMIT** | documented limit: read the row and its evidence | read the row in `FINAL_TEST_REPORT.md` |
| [ ] | F3 | Event LIST is incomplete next to the exact counters | - | **LIMIT** | documented limit: read the row and its evidence | read the row in `FINAL_TEST_REPORT.md` |
| [ ] | F4 | First answer after a forwarder (re)start can be LATE | - | **LIMIT** | documented limit: read the row and its evidence | read the row in `FINAL_TEST_REPORT.md` |
| [ ] | F5 | Settings that are not persistent | - | **LIMIT** | documented limit: read the row and its evidence | read the row in `FINAL_TEST_REPORT.md` |
| [ ] | F6 | Reading the monitor during a run costs time | - | **LIMIT** | documented limit: read the row and its evidence | read the row in `FINAL_TEST_REPORT.md` |
| [ ] | F7 | CAN chip FIFO and `can1` | - | **LIMIT** | documented limit: read the row and its evidence | read the row in `FINAL_TEST_REPORT.md` |
| [ ] | F8 | Pi 3 has no TZASC; detection only | - | **LIMIT** | documented limit: read the row and its evidence | read the row in `FINAL_TEST_REPORT.md` |
| [ ] | F11 | board_full_check.sh must run with the forwarder service STOPPED | - | **LIMIT** | documented limit: read the row and its evidence | read the row in `FINAL_TEST_REPORT.md` |
| [ ] | F12 | S95can_forwarder `start`/`restart` over ssh do not return (known limit with a workaround;  | - | **LIMIT** | documented limit: read the row and its evidence | read the row in `FINAL_TEST_REPORT.md` |
| [ ] | F9 | Secure audit log | - | **LIMIT** | documented limit: read the row and its evidence | read the row in `FINAL_TEST_REPORT.md` |
| [ ] | F10 | STALE verdict not exercised | - | **LIMIT** | documented limit: read the row and its evidence | read the row in `FINAL_TEST_REPORT.md` |

## C. After the service image (post-flash table)

| Done | ID | Test | Expected (report) | Report says | Check it LIVE | Re-check the EVIDENCE (no hardware) |
|---|---|---|---|---|---|---|
| [ ] | E2br | Reboot/boot with the service image: forwarder back by itself (rerun of E2b) | forwarder running from boot without a manual step, heartbeat answered, no TIMEOUT | **PASS** | R: after a reboot, BEFORE copying anything: `ps / grep can_forwarder` -> running; `/etc/init.d/S95can_forwarder status` -> running | open `baseline/final_campaign/R2_E2b_first_login.txt` |
| [ ] | E2br2 | Reboot of the receiver with the service image: forwarder back by itself | forwarder process and log line present without any manual step; heartbeat answered | **PASS** | same as E2br (software reboot) | open `baseline/final_campaign/E2r_reboot_service.txt` |
| [ ] | E2ar | Boot with the service image: monitor back, boot grace respected (rerun of E2a) | init_ok 1, starved 0, TIMEOUT counter 0 at uptime under 60 s | **FAIL** | FAIL in the report: my runner required a read within 90 s of boot; the first login was at 212 s. Redone as E2ar2 | open `baseline/final_campaign/R2_E2b_first_login.txt` |
| [ ] | E2ar2 | Reboot of the receiver with the service image: monitor back, boot grace respected (read in | read at uptime under 60 s: init_ok 1, starved 0, TIMEOUT counter 0; can0 FD 500k/2M with no manual step | **PASS** | same as E2a, plus `/tmp/mon_client --retries 20 status 0 / grep -o 'hb_state [A-Z_]*'` -> ANSWERED (the boot service answers from boot) | open `baseline/final_campaign/E2r_reboot_service.txt` |
| [ ] | A4r | Both Pis: can0 in CAN-FD 500k/2M with no manual step (receiver first boot of the service i | receiver and sender FD 500000/2000000 ERROR-ACTIVE, sender qlen 2000 | **PASS** | same as A4 (receiver first boot of the service image, sender rebooted) | open `baseline/final_campaign/R2_A4.txt` |
| [ ] | A1r | verify_boot with the service image | 0 FAIL | **PASS** | same as A1 | open `baseline/final_campaign/R2_A1.txt` |
| [ ] | A2r | board_full_check.sh --mode-p with the service image (service running) | 0 FAIL | **FAIL** | FAIL in the report on purpose: board_full_check cannot run while the boot service answers the heartbeat. Redone as A2r2 | open `baseline/final_campaign/R2_A2.txt` |
| [ ] | A2r2 | board_full_check.sh --mode-p with the service image, service stopped for the run | 32 PASS, 0 FAIL, 4 SKIP | **PASS** | same as A2 (service stopped) | open `baseline/final_campaign/R2_A2_service_stopped.txt` |
| [ ] | B1r | 600 valid frames at 50 frames/s with the boot SERVICE as the forwarder (no harness forward | frame total +600, all attack classes +0, last_counter +600 | **FAIL** | FAIL in the report: the first packet after a reboot is COUNTER_RESYNC by design. Redone as B1r2 | open `baseline/final_campaign/R2_B1.txt` |
| [ ] | B1r2 | 600 valid frames at 50 frames/s through the boot SERVICE, not the first packet after boot | frame total +600, attack classes +0, last_counter +600 (a traffic TIMEOUT after the stream is expected) | **PASS** | S: `1_normal.txt` not as the first packet after a reboot -> `frame` +10, no attack alarm | open `baseline/final_campaign/R2_B1r2.txt` |
| [ ] | B1rh | 600 valid frames at 50 frames/s, original harness way (service stopped for the run) | 600 judged, identity PASS, 0 loss | **PASS** | evidence only (harness run with its own forwarder): `python3 scripts/real_bus/series_report.py b1h 1 --date 20261122` | `python3 scripts/real_bus/series_report.py b1h 1 --date 20261122` |
| [ ] | E2c | Service control from the image over ssh: status, stop, start, restart | every command returns within 25 s; start/restart bring the heartbeat to ANSWERED | **FAIL** | R: `ssh root@10.237.27.212 '/etc/init.d/S95can_forwarder start'` hangs; with `>/dev/null 2>&1 </dev/null` it returns (known limit F12) | open `baseline/final_campaign/R2_E2c.txt` |

## How to write down your check
Tick the box when your live result or your evidence check matches the 'Expected' column. If a number differs, write it next to the row and tell me; do not change the expectation to fit it.
