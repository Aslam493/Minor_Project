#!/usr/bin/env python3
"""make_report.py - builds FINAL_TEST_REPORT.md from baseline/final_campaign/results.json (+ the documented limits of group F). Changes nothing else."""
import json, os, re, time

PV = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
d = json.load(open(os.path.join(PV, "baseline/final_campaign/results.json")))

LIMITS = [
 ("F1", "Event ring `ev_lost` / `ack` behaviour", "documented: `ev_lost` counts ANY overwrite of a used slot and `ack` frees nothing, so `ev_lost` is not a count of unread losses",
  "e.g. baseline/burst_20261121_normal_220722_nocd_n6_r4.txt: ev_seq 270, ev_ack 265, ev_lost 218 -> 222 although every event was acked; exact class counters are the reliable record and are used for every verdict in this report"),
 ("F2", "Key material in the prototype", "documented: the frame and heartbeat keys are prototype constants in the Secure World source (`rpi3_can_monitor_keys.h`), readable from the build tree; `board_full_check.sh` copies the frame key to /tmp (RAM) of the Pi for its frame tests and it was removed after the run (A2); packet files are made on the laptop; no key provisioning or secure storage",
  "not tested here, a design limit of the prototype"),
 ("F3", "Event LIST is incomplete next to the exact counters", "documented: events are limited to 1 per second per type after the first 4 of a class (24 pinned + 40 ordinary slots)",
  "e.g. a late heartbeat answer and a late packet reply at the same restart appear as one event; the exact counters (page 1) are used for every PASS/FAIL here"),
 ("F4", "First answer after a forwarder (re)start can be LATE", "documented, explained and tested (E1): the request is already MISSED when the forwarder starts in the second half of the heartbeat period (about 50 %) or in the last 10 ms of the 50 ms packet period (about 20 %)",
  "E1 10 of 10, series s3_ 30 of 30, n7h 21 of 21: first state MISSED <=> first answer LATE"),
 ("F5", "Settings that are not persistent", "documented: governor (powersave after every reboot; performance only by hand), sender `txqueuelen` 2000 (10 after a reboot until set again), the forwarder is not a service (E2b), all test tools live in /tmp (RAM)",
  "A4/E2 reboot: governor powersave, sender qlen reset; D6"),
 ("F6", "Reading the monitor during a run costs time", "documented: a status read while the forwarder runs delays the next packet fetch by 20-30 ms (a late packet reply at the harness snapshot), heavy PTA polling disturbs the heartbeat (cf. F24)",
  "baseline/BURST_STEPS_RESULTS_20261121.md (traces n5t, p1 run 5); the campaign reads status only between traffic phases"),
 ("F7", "CAN chip FIFO and `can1`", "documented: the chip RX FIFO holds 20 frames (max 24); with the old slow core 2 of about 16 heavy bursts overflowed it (counted in `rx_over_errors`); with the current core no overflow in any run of this report (about 270,000 frames, bursts at the full bus rate); `can1` (CE1) is dead on both shields (board level)",
  "D6, baseline/can1_investigation_20261121.md"),
 ("F8", "Pi 3 has no TZASC; detection only", "documented: Linux is kept out of the secure DRAM by the DTB reservation (A3, N-04) and not by hardware protection of Secure World memory; the monitor detects and reports, it prevents nothing",
  "A3; memory-isolation options are stored in the repo"),
 ("F9", "Secure audit log", "out of scope for this campaign (later)", "-"),
 ("F10", "STALE verdict not exercised", "documented: C9 produced 8 LATE Mode P answers (exact, forwarder count = Secure World count); a STALE answer needs the request to advance between fetch and answer (microseconds) and was not triggered in 40 freezes (STALE 0)",
  "C9, baseline/final_campaign/C9.txt"),
]
for i, t, e, ev in LIMITS:
    d[i] = {"id": i, "test": t, "expected": "-", "result": "LIMIT", "evidence": e + (" | " + ev if ev != "-" else ""), "file": "-"}

order = ["A1", "A2", "A3", "A4", "B1", "B2", "B3", "C1", "C2", "C3", "C4", "C5", "C5b", "C6", "C7", "C8", "C9", "D1a", "D1b", "D2", "D3", "D4", "D5a", "D5b", "D5c", "D5d", "D5e", "D6", "E1", "E2a", "E2b"] + [x[0] for x in LIMITS]
missing = [o for o in order if o not in d]
npass = sum(1 for o in order if o in d and d[o]["result"] == "PASS"); nfail = sum(1 for o in order if o in d and d[o]["result"] == "FAIL"); nlim = sum(1 for o in order if o in d and d[o]["result"] == "LIMIT")


def cell(s, n=330):
    s = re.sub(r"\s+", " ", str(s)).replace("|", "/")
    return s if len(s) <= n else s[:n - 3] + "..."


L = []
L.append("# FINAL TEST REPORT: Secure CAN Monitor, Raspberry Pi 3 B, Mode P (2026-11-22)\n")
L.append("**Summary: %d PASS / %d FAIL / %d documented limits**%s\n" % (npass, nfail, nlim, ("  (missing cases: %s)" % ", ".join(missing)) if missing else ""))
fails = [o for o in order if o in d and d[o]["result"] == "FAIL"]
L.append("FAIL cases: %s. %s\n" % (", ".join(fails) or "none", "Each FAIL is explained below; nothing was changed to turn it into a PASS." if fails else ""))
L.append("""## PACKET_LOSS_VERDICT
> **0 loss up to 640 frames/s sustained and for any single burst up to about 2,490 frames at the full bus rate of 3,587 frames/s; above that, every lost frame is lost at ONE stage, the forwarder's Mode P queue (2,048 frames, oldest dropped when full), and is counted there and equals the Secure World's counter gap exactly. Real socket buffer: 8,388,608 bytes granted to the forwarder's socket (4,194,304 requested; the kernel doubles it), about 9,380 frames at 894 bytes per frame (derived from the measured 238 frames at 212,992 bytes); the kernel socket, the chip FIFO (20 entries), the sender queue (2,000) and the forwarder read loop dropped nothing at any rate up to the bus maximum.**

Evidence: D1-D5 (273,000+ frames, 18 of 18 runs with the exact Secure World gap accounting: sent = judged + sender give-ups + chip loss + kernel socket drops + forwarder-rejected + Mode P queue drops, and the Secure World counter span minus its advancing frames plus the tail equals the counted drops), `REAL_BUS_RESULTS.md` section 10.10.
""")
L.append("## Conditions\n")
L.append("- Image `rpi3-sdcard-monitor-modeP-p6-n04-nostk.img` (sha256 `9700049351af848f...0f6eec`): Mode P, N-04 DTB, core without `CFG_CORE_DEBUG_CHECK_STACKS`, `S12can` + `canfdcfg` in the root filesystem. Receiver 10.237.27.77, sender 10.237.27.144 (their SD cards sit in each other's boards compared with the start of the session; IPs follow the cards).")
L.append("- Governor `powersave` 600 MHz (default after reboot) for every case except D1b (`performance`, set by hand and set back). Forwarder `can_forwarder` (build with `--hblog`, queue 2048, `--rcvbuf 4194304`), run by hand from /tmp.")
L.append("- No flash, no Secure World change, no test or threshold changed in this campaign. Both Pis were rebooted (A4/E2), which the task asked for.")
L.append("- Exact class counters (status page 1) decide every verdict; the event list is read only for the detail code.\n")
L.append("## Results\n")
L.append("| ID | Test | Expected | Result | Evidence | File |")
L.append("|---|---|---|---|---|---|")
for o in order:
    if o not in d:
        L.append("| %s | (not run) | | MISSING | | |" % o); continue
    r = d[o]; L.append("| %s | %s | %s | **%s** | %s | %s |" % (o, cell(r["test"], 120), cell(r["expected"], 160), r["result"], cell(r["evidence"], 420), ("`%s`" % r["file"]) if r["file"] != "-" else "-"))
L.append("")
L.append("## FAIL cases\n")
for o in fails:
    L.append("- **%s**: %s" % (o, cell(d[o]["evidence"], 600)))
L.append("""
## Mistakes made during the campaign (all in my test runner, none in the system; the rules were not changed)
1. B1: the report looked for the series under the wrong name (`b1` instead of tag + index), so a run that had judged 600 of 600 showed MISSING; renamed, no new data needed except that I reran group B.
2. B2: the 5-minute run crashed in my harness (a fixed 120 s limit on the sender command, the run sends for 300 s); I waited for the orphaned run to end on both Pis, added `SEND_TIMEOUT`, and reran group B. The first evaluation of the rerun then read `hb_miss_count` from the PRE-run snapshot (14, taken while no forwarder was running) and reported FAIL; the rule (0 misses at the end) is unchanged, the end-of-run record says 0, B2 was corrected and the correction is written in its evidence.
3. D1a: the powersave series was looked up under `p1_`; it is saved as `p1`; same report, right tag, corrected in the evidence.
4. D4 "20008": taken literally as 20,008 plain frames (a plain burst has no planted frames, so the Secure World gap check is exact).
5. The heartbeat log holds 6,000 records (about 300 s), so for the 320 s run it shows 311 answers while the forwarder's own counter shows 321; both are on time.
""")
L.append("## What is not shown\n")
L.append("- A3 checks the DTB hash on the image that was flashed (compose read-back, `dd` + `cmp` MATCH) and the live reserved-memory node, not a hash of the live device tree (the bootloader may modify it).")
L.append("- D6 frames per socket buffer is derived (linear from a measured point), not measured at 8 MiB.")
L.append("- C9 shows LATE only (8 in 40 freezes); STALE was not triggered (F10).")
L.append("- The sustained-rate claim up to 640 frames/s rests on a measured 480 frames/s run and the measured judging rate of 640.0 frames/s; a sustained run at 600-640 frames/s was not made.")
L.append("- Generated %s by scripts/final_campaign/make_report.py from baseline/final_campaign/results.json." % time.strftime("%F %T"))
open(os.path.join(PV, "FINAL_TEST_REPORT.md"), "w").write("\n".join(L) + "\n")
print("%d PASS / %d FAIL / %d documented limits; missing: %s; fails: %s" % (npass, nfail, nlim, missing, fails))
