#!/usr/bin/env python3
"""series_report.py TAG COUNT [--date YYYYMMDD]  -  reads the run files of one series (burst_<date>_*_<TAG><i>_r1.txt, i = 1..COUNT) plus the trace and heartbeat
logs (trace_<date>_<TAG><i>.csv, hblog_<date>_<TAG><i>.csv) and prints, per run, the STAGE IDENTITY and the late-answer match. Read-only, changes no test or threshold.

Stage identity (every frame the sender was asked to send must be in exactly one place):
  attempted = sender gave up (lost_at_sender) + sent_ok                      (ENOBUFS events are retried by the 'retry' sender and are not loss)
  sent_ok   = sender_tx_packets (the interface counter must agree)
  sent_ok   = rx_can0 (receiver chip delivered) + chip/bus loss                (chip loss is explained by rx_over_errors + rx_fifo_errors + rx_missed_errors + rx_dropped)
  rx_can0   = frames read by the forwarder + kernel socket drops (SO_MEMINFO) + frames the forwarder rejected itself (error/remote/length)
  frames read and queued = handed to Secure World and judged + still queued + dropped by the forwarder queue (Mode P overload, counted)
  handed and judged (forwarder) = judged by Secure World (status page 1 frame-verdict total delta)
FAIL if any equation has a residual, or a loss has no counter that explains it.
Late match: heartbeat - first poll state MISSED(3) <=> first heartbeat answer verdict LATE (hblog); packet request - first request seen MISSED(3) <=> late packet replies
(trace kind 2, b = request state; the forwarder counts late replies from the Secure World verdict).
"""
import csv, glob, json, os, re, statistics as st, sys

BASE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "baseline")


def load(tag, i, date):
    fs = [f for f in glob.glob(os.path.join(BASE, "burst_%s_*_%s%d_r1.txt" % (date, tag, i))) if not f.endswith(".expected.txt")]
    if not fs:
        return None
    s = open(fs[0]).read()
    return json.loads(s[s.index("## ANALYSIS (json)") + 19:s.index("## FORWARDER full log")]), s


def first_states(tag, i, date):
    hb = pk = None
    p = os.path.join(BASE, "hblog_%s_%s%d.csv" % (date, tag, i))
    if os.path.exists(p):
        rows = [r.split(",") for r in open(p).read().splitlines()[2:]]
        pl = [r for r in rows if r[0] == "22" and r[5] == "0x0" and r[7] in ("1", "3") and r[8] == "1"]
        an = [r for r in rows if r[0] == "23"]
        if pl and an:
            hb = (int(pl[0][7]), int(an[0][9]))
    p = os.path.join(BASE, "trace_%s_%s%d.csv" % (date, tag, i))
    if os.path.exists(p):
        tr = [r for r in csv.reader(open(p)) if r and r[0].isdigit() and r[1] == "2" and int(r[3]) in (1, 3)]
        if tr:
            pk = int(tr[0][3])
    return hb, pk


def main(argv):
    tag, n = argv[0], int(argv[1]); date = argv[argv.index("--date") + 1] if "--date" in argv else "20261121"
    print("series %s, %d runs" % (tag, n))
    bad = 0; wins = []; hbm = [0, 0]; pkm = [0, 0]; pk_other = [0]; unver = []; tot = dict(att=0, judged=0, lost_sender=0, chip=0, sock=0, fq=0, rej=0); lates = [0, 0]
    for i in range(1, n + 1):
        r = load(tag, i, date)
        if r is None:
            print(" run %d: MISSING" % i); bad += 1; continue
        j, s = r; fw = j["fwd"]; sd = j["sender"]
        gov = re.search(r"cpu governor (\w+) at (\d+) kHz", j["settings"]); gov = "%s %s MHz" % (gov.group(1), int(gov.group(2)) // 1000) if gov else "not recorded"
        att = sd["attempted"]; ok = sd["sent_ok"]; ls = sd["lost_at_sender"] + sd["other_errors"]
        chip = ok - j["rx_can0"]; chip_expl = j["rx_over_errors"] + j["rx_fifo_errors"] + j["rx_missed_errors"] + j["rx_dropped"]
        sock = j["kernel_sock_drops"] if j["kernel_sock_drops"] is not None else 0
        rej = sum(fw.get("fwd_dropped_err_rtr_len") or [0])
        rq = fw["fwd_received_queued"]; hj = fw["handed_judged"]; sl = fw["still_queued"]; qd = fw["queue_dropped"]
        eq = [("attempted = lost_at_sender + sent_ok", att - ls - ok), ("sent_ok = sender_tx_packets", ok - j["sender_tx_packets"]),
              ("chip loss explained by counters", chip - chip_expl), ("rx_can0 = read + socket drops + rejected", j["rx_can0"] - rq - sock - rej),
              ("read = judged + still queued + queue dropped", rq - hj - sl - qd), ("handed = Secure World judged", hj - j["judged"])]
        res = [(nm, v) for nm, v in eq if v != 0]
        pk_late = fw["late"]
        lost_total = ls + chip + sock + rej + qd + sl
        # the identity as one line
        ident = "sent %d = judged %d + sender %d + chip %d + socket %d + forwarder-rejected %d + Mode-P-queue %d + still-queued %d" % (att, j["judged"], ls, chip, sock, rej, qd, sl)
        okid = (att == j["judged"] + lost_total) and not res
        snap_missing = not (j["status_after"] or {}).get("now_ticks")
        if snap_missing:      # the after-run snapshot (status page and counters) was not captured: chip, socket and Secure World stages cannot be verified; forwarder side only
            fres = [nm for nm, v in eq[4:5] if v != 0]
            okid = None; unver.append(i)
            ident = "UNVERIFIED (after-run snapshot missing, not a frame loss): forwarder side only: sender attempted %d, read %d = handed %d + still queued %d + queue dropped %d%s" % (att, rq, hj, sl, qd, "" if not fres else " MISMATCH")
            res = []
        hbs, pks = first_states(tag, i, date)
        hb_late = j["hb_late"]; pk_late = fw["late"]
        lates[0] += hb_late; lates[1] += pk_late
        if hbs: hbm[1] += 1; hbm[0] += (hbs[0] == 3) == (hbs[1] == 1)
        # where were the late packet replies? Secure World events (detail 1) timed against the forwarder start (clock check line); suppressed events stay unlocated
        m0 = re.search(r"clockcheck at start: CNTVCT before the status call (\d+), Secure World now_ticks in it (\d+)", s)
        sn = int(m0.group(2)) if m0 else (j["status_before"] or {}).get("now_ticks")
        evs = s[s.index("## EVENTS from seq"):s.index("## FORWARDER log")]
        times = [(int(e.group(3)) - sn) / 19.2e6 for e in re.finditer(r"^seq (\d+) LATE_ANSWER\s+counter (\d+) detail 1 ticks (\d+)", evs, re.M)] if sn else []
        at_start = sum(1 for t in times if t < 1.0); later = [round(t, 1) for t in times if t >= 1.0]
        unloc = max(0, pk_late - len(times))
        pk_start_expected = pks == 3
        if pks is not None:
            pkm[1] += 1
            # match: a MISSED first request must come with a late reply at the start (listed, or unlisted while nothing else explains it); an OPEN one must not
            pkm[0] += (at_start > 0 or (unloc > 0 and not later)) == pk_start_expected
        pk_other[0] += len(later)
        # Secure World gap accounting (plain bursts only: no planted frame, so every counter step is a real frame). Exact, no event list:
        #   counter span seen by Secure World = last_counter_after - last_counter_start; each frame that ADVANCES the counter (ACCEPTED, GAP, RESYNC) is one valid frame;
        #   missing counters inside the span = span - advancing frames; counters after the last judged frame (tail) = expected end - last_counter_after.
        #   PASS needs: counted drops (sender + chip + socket + rejected + Mode P queue) == missing in the span + tail.
        swline = ""
        ex = j.get("expected") or {}
        if ex.get("plain") and not snap_missing:
            cd = j["class_delta"]; nonadv = sum(cd.get(k, 0) for k in ("AUTH_FAIL", "REPLAY", "MALFORMED", "ALTERED", "UNACCOUNTED"))
            adv = j["judged"] - nonadv; span = j["last_counter_after"] - ex["last_counter_start"]
            miss = span - adv; tail = ex["last_counter_end"] - j["last_counter_after"]; counted = ls + chip + sock + rej + qd
            gapok = (miss + tail == counted) and nonadv == 0
            swline = " | SECURE-WORLD GAP: span %d - advancing %d = missing %d, tail %d => %d vs counted drops %d %s" % (span, adv, miss, tail, miss + tail, counted, "MATCH" if gapok else "MISMATCH")
            if not gapok: okid = False
        w = j["judged_window_s"]; wins.append(w) if w else None
        cl = {k: v for k, v in j["class_delta"].items() if v and k in ("AUTH_FAIL", "REPLAY", "MALFORMED", "COUNTER_GAP")}
        print(" run %2d %s | %s | %s | %s | window %s s | reply p99 %s max %s ms | late: heartbeat %d, packet %d | hb first %s, pk first %s | classes %s%s" % (
            i, "UNVERIFIED" if okid is None else ("PASS" if okid else "FAIL"), gov, ident, "ENOBUFS-retried %d" % sd["enobufs"], "%.2f" % w if w else "-", j["reply_p99_ms"], j["reply_max_ms"],
            hb_late, pk_late, ("state %d verdict %d" % hbs) if hbs else "n/a", ("state %d" % pks) if pks is not None else "n/a", cl,
            ("" if okid else " | RESIDUALS " + str(res)) + swline))
        bad += 0 if (okid or okid is None) else 1
        if snap_missing: continue
        tot["att"] += att; tot["judged"] += j["judged"]; tot["lost_sender"] += ls; tot["chip"] += chip; tot["sock"] += sock; tot["fq"] += qd; tot["rej"] += rej
    print("TOTAL over the series: %s" % tot)
    if wins: print("judging window s: min %.2f median %.2f max %.2f" % (min(wins), st.median(wins), max(wins)))
    print("late answers: heartbeat %d, packet %d" % tuple(lates))
    print("late packet replies NOT at the restart (event later than 1 s after the start, i.e. the harness status snapshot at about 22 s): %d" % pk_other[0])
    print("restart-phase match: heartbeat %d of %d runs (first state MISSED <=> first answer LATE), packet %d of %d runs (first request MISSED <=> late packet reply)" % (hbm[0], hbm[1], pkm[0], pkm[1]))
    print("RESULT: %s (%d run(s) with an uncounted frame or a missing run; %d run(s) UNVERIFIED because the after-run snapshot is missing: %s)" % ("ALL PASS" if bad == 0 and not unver else ("FAIL" if bad else "PASS, BUT RERUN THE UNVERIFIED RUN(S)"), bad, len(unver), unver))
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
