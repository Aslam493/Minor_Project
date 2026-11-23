#!/usr/bin/env python3
"""check_report_evidence.py - ONE command, NO hardware needed: re-checks the recorded evidence of every row of FINAL_TEST_REPORT.md.
 1. every row's evidence file exists (and is not empty);
 2. every burst/overload series is RECOMPUTED from the raw run files (scripts/real_bus/series_report.py: sent = judged + counted drops, and the Secure World gap);
 3. prints the report's own verdict per row next to what was recomputed.
Usage: python3 scripts/check_report_evidence.py"""
import os, re, subprocess, sys
PV = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SERIES = {  # report id -> (tag, runs, date)
    "D1a": ("p1", 5, "20261121"), "D1b": ("p2_", 5, "20261121"), "D2": ("s3_", 30, "20261121"), "D3": ("s4_", 5, "20261121"), "D4": ("s5_", 3, "20261121"),
    "D5a": ("s6_", 3, "20261121"), "D5b": ("s8a_", 3, "20261121"), "D5c": ("s8b_", 3, "20261121"), "D5d": ("s7a_", 3, "20261121"), "D5e": ("s7b_", 3, "20261121"),
    "D5f": ("s9_", 3, "20261121"), "B1": ("b1x", 1, "20261122"), "B1rh": ("b1h", 1, "20261122")}
rows = []
for line in open(os.path.join(PV, "FINAL_TEST_REPORT.md")):
    m = re.match(r"^\| ([A-Z]\d+[a-z0-9]*) \| (.*)$", line)
    if m:
        cells = [c.strip() for c in line.strip().strip("|").split(" | ")]
        if len(cells) >= 6: rows.append(cells)
print("%-6s %-7s %-12s %-26s %s" % ("ID", "report", "evidence", "recomputed from raw files", "test"))
missing = 0; bad = 0; recomputed = 0
for c in rows:
    rid, test, res, f = c[0], c[1], c[3].strip("*"), c[5].strip("`")
    ev = "n/a (limit)" if f == "-" else ("found" if os.path.isfile(os.path.join(PV, f)) and os.path.getsize(os.path.join(PV, f)) > 0 else "MISSING")
    if ev == "MISSING": missing += 1
    rc = ""
    if rid in SERIES:
        tag, n, date = SERIES[rid]
        out = subprocess.run([sys.executable, os.path.join(PV, "scripts/real_bus/series_report.py"), tag, str(n), "--date", date], capture_output=True, text=True).stdout
        r = re.search(r"^RESULT: (.*)$", out, re.M); tot = re.search(r"^TOTAL over the series: (.*)", out, re.M)
        ok = bool(r) and "ALL PASS" in r.group(1); recomputed += 1
        rc = ("ALL PASS " if ok else "NOT PASS ") + (re.sub(r"[{}']", "", tot.group(1))[:60] if tot else "")
        if not ok and res == "PASS": bad += 1
    print("%-6s %-7s %-12s %-26s %s" % (rid, res, ev, rc[:60], test[:60]))
print("\n%d rows in the report; evidence files missing: %d; series recomputed from raw run files: %d; recomputed series that disagree with a PASS: %d" % (len(rows), missing, recomputed, bad))
print("RESULT: %s" % ("the recorded evidence supports the report" if missing == 0 and bad == 0 else "CHECK THE ROWS ABOVE"))
sys.exit(0 if missing == 0 and bad == 0 else 1)
