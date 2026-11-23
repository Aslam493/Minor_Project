#!/usr/bin/env python3
"""make_checklist.py - builds CHECKLIST_FROM_REPORT.md from FINAL_TEST_REPORT.md (same rows, same IDs): for every row a tick box, what the report says,
how to check it LIVE on the hardware, and how to re-check the recorded EVIDENCE without hardware."""
import os, re
PV = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SR = "python3 scripts/real_bus/series_report.py"
RUN = "(run sheet = LIVE_DEMO_RUNSHEET.md)"
LIVE = {
 "A1": "L: `cd tools/verify_boot && ./verify_boot.sh 10.237.27.212 --expect-core1-pinned` -> `17 pass, 0 FAIL`",
 "A2": "L (4 min, stops and restarts the boot service): `RX=10.237.27.212 python3 scripts/final_campaign/campaign.py R2d` -> `A2r2 ... 32 PASS, 0 FAIL`",
 "A3": "R: `grep -i -E '10000000|08000000' /proc/iomem` -> both `reserved`; `ls /sys/firmware/devicetree/base/reserved-memory/` -> `optee_secure@10000000`",
 "A4": "R: `/tmp/canmode can0 | grep -E 'nominal|data bit|state'` -> 500000, 2000000, ERROR-ACTIVE. S: `ip -details link show can0 | grep -E 'qlen|bitrate'` -> qlen 2000, 500000, dbitrate 2000000. Best proof: reboot a Pi and look again, nothing set by hand",
 "B1": "S: send `1_normal.txt` (run sheet part 3) -> R: `alarms` -> `frame` +10, no alarm. (The report used 600 frames: `campaign.py R2d` does it)",
 "B2": "R: `/tmp/mon_client --retries 20 status 0 | grep -o 'hb_state [A-Z_]* hb_miss_count [0-9]*'` -> `ANSWERED ... 0`, stays so for minutes. Full 5 min: `campaign.py B` (7 min, own forwarder: stop the boot service first)",
 "B3": "R: `/tmp/mon_client --retries 20 status 2 | head -1` -> `ON_TIME <big number> LATE 1` (LATE 1 = the restart effect). Full 5 min: `campaign.py B`",
 "C1": "S: `3_replay.txt` -> R: `alarms` REPLAY +1; `evt` shows `REPLAY detail 1`",
 "C2": "S: `4_duplicate.txt` -> REPLAY +1, `evt` detail 2",
 "C3": "S: `5_conflict.txt` -> REPLAY +1, `evt` detail 3",
 "C4": "S: `2_tamper.txt` -> AUTH_FAIL +1, `evt` AUTH_FAIL",
 "C5": "S: `6_wrongid.txt` -> MALFORMED +1, `evt` detail 3",
 "C5b": "evidence only (needs a trace run): `campaign.py C` (9 min)",
 "C6": "S: `7_jump.txt` -> COUNTER_GAP +1, `evt` detail 4",
 "C7": "R after a file, send nothing: `age` grows, `evt` -> `TIMEOUT detail 1 ... s ago` (run sheet part 5)",
 "C8": "R: stop the forwarder (run sheet part 7), wait 6 s, `evt` -> `TIMEOUT detail 2` (and 4); `hb_miss_count` passes 3",
 "C9": "R: `/tmp/mon_client --retries 20 status 2 | head -1` shows LATE counted. Full test: `campaign.py C` (needs the harness forwarder; stop the boot service)",
 "D1a": "S+R: run sheet part 6 (burst 2000) is the same kind of test; full: `scripts/real_bus/series.sh` (stop the boot service first)",
 "D1b": "full re-run only: governor `performance` then `scripts/real_bus/series.sh` (see evidence command)",
 "D2": "run sheet part 6, smaller; full: `series.sh s3_ \"500:retry ...\"` (30 runs, 25 min)",
 "D3": "S: `python3 burst_send.py 8_burst2000.txt retry` -> R: `gapcheck` -> `span 2000 | judged 2000 | missing 0`",
 "D4": "S: `9_flood12000.txt` (12000 frames, the same idea as 20008): R: `gapcheck` -> `judged + missing = span`",
 "D5a": "same as D4 (run sheet part 6.4 to 6.6)",
 "D5b": "full re-run only: paced sender `rate:7000:2` (see evidence command)",
 "D5c": "full re-run only: paced sender `rate:14000:1`",
 "D5d": "full re-run only: `plain:2400:retry` and `plain:2600:retry`",
 "D5e": "see D5d",
 "D5f": "full re-run only: `rate:9000:1-2-2-1-2-1-2-2-1-2-1-2`",
 "D6": "R: `ethtool -g can0` -> RX 20 (max 24), TX 4. S: `cat /sys/class/net/can0/tx_queue_len` -> 2000. `grep CB_QCAP tools/monitor_client/canbridge.h` -> 2048",
 "E1": "L (3 min, stop the boot service first): `campaign.py E1` -> 10 of 10 match",
 "E2a": "R: `reboot`; ssh again at ~25 s uptime; L: `scp tools/monitor_client/mon_client root@10.237.27.212:/tmp/`; R: `cat /proc/uptime` (under 60) and `/tmp/mon_client --retries 20 status 1 | grep TIMEOUT` -> no TIMEOUT line (= 0)",
 "E2b": "R: this row FAILED before the fix (forwarder was started by hand). Check the fix with row E2br",
 "E2br": "R: after a reboot, BEFORE copying anything: `ps | grep can_forwarder` -> running; `/etc/init.d/S95can_forwarder status` -> running",
 "E2br2": "same as E2br (software reboot)",
 "E2ar": "FAIL in the report: my runner required a read within 90 s of boot; the first login was at 212 s. Redone as E2ar2",
 "E2ar2": "same as E2a, plus `/tmp/mon_client --retries 20 status 0 | grep -o 'hb_state [A-Z_]*'` -> ANSWERED (the boot service answers from boot)",
 "A4r": "same as A4 (receiver first boot of the service image, sender rebooted)",
 "A1r": "same as A1",
 "A2r": "FAIL in the report on purpose: board_full_check cannot run while the boot service answers the heartbeat. Redone as A2r2",
 "A2r2": "same as A2 (service stopped)",
 "B1r": "FAIL in the report: the first packet after a reboot is COUNTER_RESYNC by design. Redone as B1r2",
 "B1r2": "S: `1_normal.txt` not as the first packet after a reboot -> `frame` +10, no attack alarm",
 "B1rh": "evidence only (harness run with its own forwarder): `python3 scripts/real_bus/series_report.py b1h 1 --date 20261122`",
 "E2c": "R: `ssh root@10.237.27.212 '/etc/init.d/S95can_forwarder start'` hangs; with `>/dev/null 2>&1 </dev/null` it returns (known limit F12)",
}
EV = {"D1a": SR + " p1 5", "D1b": SR + " p2_ 5", "D2": SR + " s3_ 30", "D3": SR + " s4_ 5", "D4": SR + " s5_ 3", "D5a": SR + " s6_ 3", "D5b": SR + " s8a_ 3",
      "D5c": SR + " s8b_ 3", "D5d": SR + " s7a_ 3", "D5e": SR + " s7b_ 3", "D5f": SR + " s9_ 3", "B1": SR + " b1x 1 --date 20261122", "B1rh": SR + " b1h 1 --date 20261122"}
rows, section = [], "main"
for line in open(os.path.join(PV, "FINAL_TEST_REPORT.md")):
    if line.startswith("## After the service image"): section = "post"
    m = re.match(r"^\| ([A-Z]\d+[a-z0-9]*) \| (.*)$", line)
    if m:
        c = [x.strip() for x in line.strip().strip("|").split(" | ")]
        if len(c) >= 6:
            rid = c[0]; sec = "limits" if re.match(r"F\d+$", rid) else section
            rows.append((sec, c))
L = ["# Checklist from the final report (every row), to tick off from the start\n",
     "Source: `FINAL_TEST_REPORT.md` (same IDs). Windows: **L** laptop, **R** receiver (`ssh root@10.237.27.212`), **S** sender (`ssh sender@10.237.27.144`, `cd /tmp/demo`). Live commands marked R/S need the helper functions of `LIVE_DEMO_RUNSHEET.md` block A (`alarms`, `evt`, `age`, `snap`, `gapcheck`).\n",
     "## Three ways to check\n",
     "1. **One command, no hardware (2 minutes):** `python3 scripts/check_report_evidence.py` -> every row's evidence file exists and every burst/overload series is recomputed from the raw run files. Expected last line: `RESULT: the recorded evidence supports the report`.",
     "2. **Live, short (12 minutes):** follow `LIVE_DEMO_RUNSHEET.md`; it re-checks, smaller and in front of the audience: A3 (part 2), B1 (part 3), C1-C8 (parts 4, 5, 7), D3, D4/D5a (part 6) and E2br (part 2.5). The other rows use the commands in the table.",
     "3. **Full re-run of everything:** the stage commands in the table (`campaign.py A|B|C|E1|R2c|R2d`, `scripts/real_bus/series.sh`); about 1.5 hours; stop the boot service first where it says so (`/etc/init.d/S95can_forwarder stop >/dev/null 2>&1 </dev/null`), start it again afterwards.\n",
     "Result column: PASS / FAIL / LIMIT is what the report says. FAIL rows are kept on purpose; each has its explanation and a rerun row.\n"]
titles = {"main": "## A. Campaign table (31 PASS, 1 FAIL)", "limits": "## B. Documented limits (nothing to run: read the row)", "post": "## C. After the service image (post-flash table)"}
for sec in ("main", "limits", "post"):
    L.append(titles[sec] + "\n")
    L.append("| Done | ID | Test | Expected (report) | Report says | Check it LIVE | Re-check the EVIDENCE (no hardware) |")
    L.append("|---|---|---|---|---|---|---|")
    for s, c in rows:
        if s != sec: continue
        rid, test, exp, res, ev, f = c[0], c[1], c[2], c[3].strip("*"), c[4], c[5].strip("`")
        live = LIVE.get(rid, "documented limit: read the row and its evidence" if sec == "limits" else "see the evidence")
        evid = ("`" + EV[rid] + "`") if rid in EV else (("open `%s`" % f) if f != "-" else "read the row in `FINAL_TEST_REPORT.md`")
        L.append("| [ ] | %s | %s | %s | **%s** | %s | %s |" % (rid, test[:90].replace("|", "/"), exp[:110].replace("|", "/"), res, live.replace("|", "/"), evid))
    L.append("")
L.append("## How to write down your check\nTick the box when your live result or your evidence check matches the 'Expected' column. If a number differs, write it next to the row and tell me; do not change the expectation to fit it.")
open(os.path.join(PV, "CHECKLIST_FROM_REPORT.md"), "w").write("\n".join(L) + "\n")
print("wrote CHECKLIST_FROM_REPORT.md:", len(rows), "rows")
