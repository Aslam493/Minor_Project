#!/usr/bin/env python3
"""burst_run.py - PHASE 1 of the burst study: measure where frames are lost. Changes NOTHING on the Pis except RAM files in /tmp and the monitor's own
counters and event log (no card write, no reboot, no setting). Runs on the LAPTOP; needs ssh key login to root@RX and sender@SN.

  burst_run.py --runs normal,150:naive,150:retry,500:naive,500:retry,1000:naive,1000:retry,normal [--date YYYYMMDD]

  normal       100 valid packets at 10/s (10 s), nothing planted: every one must be ACCEPTED (harness control, and the recovery check after the bursts)
  N:naive      a burst of N valid + 4 planted frames + 1 jump, sender does NOT repeat a send that fails with ENOBUFS
  N:retry      the same burst, the sender repeats until the frame is accepted (so the later stages see the full rate)

Per run: the expected counts are written to baseline/burst_<date>_<tag>.expected.txt BEFORE anything is sent. Then: receiver candump (-d: counts socket drops) and the
Mode P forwarder (--pull --verbose --stats, 36 s) are started, the sender runs, all counters are read before and, once the system is idle (about 22 s into the
run, forwarder still running), after. Nothing is polled while the burst is in flight (a poller loaded the monitor in the 2026-11-21 incident).
Output: baseline/burst_<date>_<tag>.txt (everything) and baseline/burst_<date>_SUMMARY.txt (the table).
"""
import collections, datetime, json, os, re, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__)); PV = os.path.abspath(os.path.join(HERE, "../.."))
MC = os.path.join(PV, "tools/monitor_client"); BL = os.path.join(PV, "baseline")
RX = os.environ.get("RX", "10.237.27.77"); SN = os.environ.get("SN", "10.237.27.144")
FWD_SECONDS, SNAP_AT = 36, 22
SSHO = ["-o", "BatchMode=yes", "-o", "ConnectTimeout=8", "-o", "ControlMaster=auto", "-o", "ControlPath=/tmp/burst_cm_%C", "-o", "ControlPersist=600"]


def ssh(user, ip, cmd, timeout=180):
    r = subprocess.run(["ssh"] + SSHO + ["%s@%s" % (user, ip), cmd], capture_output=True, text=True, timeout=timeout)
    return r.stdout + (("\n[stderr] " + r.stderr) if r.returncode and r.stderr else "")


def scp(files, user, ip, dest):
    subprocess.run(["scp", "-q"] + SSHO + files + ["%s@%s:%s" % (user, ip, dest)], check=True, timeout=120)


STAT = 'for f in /sys/class/net/can0/statistics/*; do echo "stat $(basename $f) $(cat $f)"; done'
MCL = "/tmp/mon_client --retries 20"


def snap_rx():
    return ssh("root", RX, STAT + "; " + MCL + " status 0; " + MCL + " status 1; /tmp/canmode can0 | grep -E 'controller state|error counters'")


def snap_sn():
    return ssh("sender", SN, STAT + "; ip -details -statistics link show can0 | grep -E 'can <|state|re-started' -A1 | grep -v -E '^--'")


def stats(text):
    return {m.group(1): int(m.group(2)) for m in re.finditer(r"^stat (\S+) (\d+)", text, re.M)}


def page0(t):
    d = {}
    for pat, names in ((r"last_counter (\d+) hb_seq (\d+) hb_state (\w+) hb_miss_count (\d+)", ("last_counter", "hb_seq", "hb_state", "hb_miss_count")),
                       (r"skip_count (\d+) skip_total (\d+) pta_lock_timeouts (\d+) pta_timeouts_seen (\d+)", ("skip_count", "skip_total", "pta_lock_timeouts", "pta_timeouts_seen")),
                       (r"core1_poll_age_ticks (\d+) poll_count (\d+)", ("core1_poll_age_ticks", "poll_count")),
                       (r"ev_seq (\d+) ev_ack (\d+) ev_lost (\d+) pinned_used (\d+)", ("ev_seq", "ev_ack", "ev_lost", "pinned_used")),
                       (r"traffic_age_ticks (\d+)", ("traffic_age_ticks",)), (r"now_ticks (\d+)", ("now_ticks",)), (r"init_ok=(\d) starved=(\d)", ("init_ok", "starved"))):
        m = re.search(pat, t)
        if m:
            for n, v in zip(names, m.groups()):
                d[n] = int(v) if v.isdigit() else v
    return d


def page1(t):
    d = {}
    m = re.search(r"verdict totals frame (\d+) heartbeat (\d+) crosscheck (\d+)", t)
    if m:
        d["frame_total"], d["heartbeat_total"], d["crosscheck_total"] = map(int, m.groups())
    for m in re.finditer(r"^\s{2}([A-Z_]+)\s+(\d+)\s*$", t, re.M):
        d["ev_" + m.group(1)] = int(m.group(2))
    return d


def events(t):
    return [dict(seq=int(m.group(1)), name=m.group(2), counter=int(m.group(3)), detail=int(m.group(4)), ticks=int(m.group(5)))
            for m in re.finditer(r"^seq (\d+) (\S+)\s+counter (\d+) detail (\d+) ticks (\d+)", t, re.M)]


def sender_file_multiset(path):
    ms = collections.Counter(); order = []
    for ln in open(path):
        t = ln.split("#", 1)[0].split()
        if len(t) == 3:
            k = (int(t[1], 16), t[2].lower()); ms[k] += 1; order.append(k)
    return ms, order


def candump_parse(text):
    seen = collections.Counter(); ts = []; drops = 0
    for m in re.finditer(r"\((\d+\.\d+)\)\s+can0\s+(?:RX\s+\S\s+\S\s+)?([0-9A-Fa-f]+)\s+\[(\d+)\]\s+((?:[0-9A-Fa-f]{2} ?)*)", text):
        ts.append(float(m.group(1))); seen[(int(m.group(2), 16), m.group(4).replace(" ", "").lower())] += 1
    for m in re.finditer(r"DROPCOUNT: dropped (\d+) CAN frame", text):
        drops += int(m.group(1))
    return seen, ts, drops


def fwd_parse(text):
    d = {}
    reps = [(int(a), int(b), c, int(e), int(f)) for a, b, c, e, f in re.findall(r"request (\d+): sent (\d+) packet\(s\) -> (\w+), Secure World judged (\d+) \(queue left (\d+)\)", text)]
    d["replies_with_packets"] = len(reps); d["batches"] = [r[1] for r in reps]; d["max_queue_left"] = max([r[4] for r in reps] or [0])
    d["reply_verdicts"] = dict(collections.Counter(r[2] for r in reps))
    m = re.search(r"PULL: frames received from can0 and queued: (\d+) \| requests answered: (\d+) \(ON_TIME (\d+), LATE (\d+), STALE (\d+), UNISSUED (\d+), DUPLICATE (\d+)\) \| packets sent in replies and judged: (\d+) \| still queued: (\d+) \| dropped by the queue: (\d+)", text)
    if m:
        for k, v in zip(("fwd_received_queued", "requests", "on_time", "late", "stale", "unissued", "dup", "handed_judged", "still_queued", "queue_dropped"), map(int, m.groups())):
            d[k] = v
    m = re.search(r"dropped by the forwarder: error frame (\d+), remote frame (\d+), wrong length (\d+)", text)
    if m:
        d["fwd_dropped_err_rtr_len"] = list(map(int, m.groups()))
    m = re.search(r"heartbeat answers: ON_TIME (\d+) LATE (\d+) other (\d+)", text)
    if m:
        d["hb_answers_on_time_late_other"] = list(map(int, m.groups()))
    for key, pat in (("sw_side", r"SECURE-WORLD-SIDE[^\n]*"), ("linux_reply", r"LINUX-SIDE reply time[^\n]*"), ("linux_gap", r"LINUX-SIDE gap[^\n]*"), ("cpu", r"CPU COST[^\n]*"),
                     ("raised", r"Secure World judged \d+ packets in this run[^\n]*"), ("rx_counter", r"can0 rx_packets counter[^\n]*"), ("end_state", r"end state:[^\n]*")):
        m = re.search(pat, text)
        d[key] = m.group(0) if m else ""
    return d


def run_one(kind, date, scratch):
    """kind: 'normal' or 'N:policy'. Returns the result dict."""
    if kind == "normal":
        N, policy, tag = 100, "paced", "normal_%s" % time.strftime("%H%M%S")
    else:
        N, policy = int(kind.split(":")[0]), kind.split(":")[1]; tag = "N%d_%s" % (N, policy)
    p0 = page0(ssh("root", RX, MCL + " status 0")); L = p0["last_counter"]
    pre = os.path.join(scratch, "burst"); exp_path = os.path.join(BL, "burst_%s_%s.expected.txt" % (date, tag))
    if kind == "normal":
        sys.path.insert(0, MC); import packet_gen as g
        key = g.key_from_source(); lines = ["# normal traffic: 100 valid packets, 100 ms apart\n"]
        for i in range(1, 101):
            c = L + i; lines.append(g.line(100, 0x123, g.packet(key, 0x123, c, 0xC0DE0000 | (c & 0xFFFF)), "valid counter %d" % c))
        open(pre + ".txt", "w").write("".join(lines))
        resync = 1 if L == 0 else 0
        exd = {"last_counter_start": L, "frames_attempted": 100, "frames_judged": 100, "AUTH_FAIL": 0, "REPLAY": 0, "MALFORMED": 0, "COUNTER_GAP": 0,
               "COUNTER_RESYNC": resync, "ACCEPTED": 100 - resync, "last_counter_end": L + 100}
        open(exp_path, "w").write("# EXPECTED counts for 10 s of normal traffic, written BEFORE it is sent\n" + "".join("%s=%s\n" % kv for kv in exd.items()))
    else:
        out = subprocess.run([sys.executable, os.path.join(MC, "burst_packets.py"), pre, "--last", str(L), "--n", str(N)], capture_output=True, text=True, check=True).stdout
        exd = json.loads(out.splitlines()[0]); os.replace(pre + ".expected", exp_path)
    print("\n##### run %s: expected counts written to %s before sending:" % (tag, os.path.basename(exp_path)), flush=True)
    print(open(exp_path).read(), flush=True)
    scp([pre + ".txt"], "sender", SN, "/tmp/burst/burst.txt")
    sent_ms, sent_order = sender_file_multiset(pre + ".txt")
    b_rx, b_sn = snap_rx(), snap_sn(); M = page0(b_rx)["ev_seq"]
    ssh("root", RX, MCL + " ack %d" % M)
    ssh("root", RX, "rm -f /tmp/burst_cd.log /tmp/burst_fwd.log; /tmp/candump_aarch64 -d -ta can0 > /tmp/burst_cd.log 2>&1 < /dev/null & echo $! > /tmp/cd.pid; "
        "/tmp/can_forwarder run %d --pull --verbose --stats > /tmp/burst_fwd.log 2>&1 < /dev/null & echo $! > /tmp/fwd.pid" % FWD_SECONDS)
    t_start = time.time(); time.sleep(4)
    cmd = ("python3 /tmp/burst/burst_send.py /tmp/burst/burst.txt %s" % policy) if kind != "normal" else "python3 /tmp/burst/laptop_can_send.py --iface can0 /tmp/burst/burst.txt"
    send_out = ssh("sender", SN, cmd, timeout=120); t_sent = time.time() - t_start
    sj = None
    for ln in send_out.splitlines():
        if ln.startswith("{"):
            sj = json.loads(ln)
    if sj is None:
        m = re.search(r"sent (\d+) CAN-FD frames", send_out)
        sj = {"policy": "paced", "attempted": N, "sent_ok": int(m.group(1)) if m else 0, "enobufs": 0, "lost_at_sender": 0, "retries": 0, "other_errors": 0, "duration_s": None,
              "rate_fps": 10.0, "longest_stall_ms": None, "lost_indices": [], "raw": send_out.strip()[:200]}
    time.sleep(max(1, SNAP_AT - (time.time() - t_start)))
    a_rx, a_sn = snap_rx(), snap_sn()
    ev_txt = ssh("root", RX, MCL + " events --from %d --all" % (M + 1))
    time.sleep(max(1, FWD_SECONDS + 3 - (time.time() - t_start)))
    fwd_txt = ssh("root", RX, "cat /tmp/burst_fwd.log"); cd_txt = ssh("root", RX, "kill $(cat /tmp/cd.pid) 2>/dev/null; sleep 1; cat /tmp/burst_cd.log")
    # ---- analysis ----
    sx, rx_b, rx_a = stats(b_sn), stats(b_rx), stats(a_rx); sa = stats(a_sn)
    dS = lambda k: sa.get(k, 0) - sx.get(k, 0); dR = lambda k: rx_a.get(k, 0) - rx_b.get(k, 0)
    q0, q1 = page0(b_rx), page0(a_rx); r0, r1 = page1(b_rx), page1(a_rx); evs = events(ev_txt)
    lost_idx = set(sj.get("lost_indices", [])); sent_ok_ms = collections.Counter(k for i, k in enumerate(sent_order) if i not in lost_idx)
    seen, ts, cd_drops = candump_parse(cd_txt); cd_total = sum(seen.values())
    miss_rx = sum((sent_ok_ms - seen).values())     # sent by the sender but never seen by candump on the receiver
    extra_rx = sum((seen - sent_ok_ms).values())
    fw = fwd_parse(fwd_txt)
    classes = ("AUTH_FAIL", "REPLAY", "MALFORMED", "COUNTER_GAP", "COUNTER_RESYNC", "TRAFFIC_RECOVERED", "TIMEOUT", "HEARTBEAT_RECOVERED", "LATE_ANSWER", "STALE_ANSWER")
    cdelta = {c: r1.get("ev_" + c, 0) - r0.get("ev_" + c, 0) for c in classes}
    recorded = collections.Counter(e["name"] for e in evs)
    suppressed = {c: max(0, cdelta[c] - recorded.get(c, 0)) for c in classes if cdelta[c]}
    judged = r1.get("frame_total", 0) - r0.get("frame_total", 0)
    # judged window in the Secure World clock: first TRAFFIC_RECOVERED/RESYNC ... last traffic TIMEOUT (detail 1) minus the 300 ms deadline
    first = [e["ticks"] for e in evs if e["name"] in ("TRAFFIC_RECOVERED", "COUNTER_RESYNC")]
    last_to = [e["ticks"] for e in evs if e["name"] == "TIMEOUT" and e["detail"] == 1]
    window_s = ((min(last_to[-1:] or [0]) - 0.3 * 19.2e6) - min(first or [0])) / 19.2e6 if first and last_to and last_to[-1] > first[0] else None
    ts_window = (ts[-1] - ts[0]) if len(ts) > 1 else None
    maxgap = max([b - a for a, b in zip(ts, ts[1:])] or [0])
    res = {"tag": tag, "N": N, "policy": policy, "L": L, "expected": exd, "sender": sj,
           "sender_tx_packets": dS("tx_packets"), "sender_tx_errors": dS("tx_errors"), "sender_tx_dropped": dS("tx_dropped"), "sender_all_stat_deltas": {k: dS(k) for k in sa if dS(k)},
           "rx_can0": dR("rx_packets"), "rx_dropped": dR("rx_dropped"), "rx_over_errors": dR("rx_over_errors"), "rx_fifo_errors": dR("rx_fifo_errors"), "rx_errors": dR("rx_errors"),
           "rx_missed_errors": dR("rx_missed_errors"), "rx_all_stat_deltas": {k: dR(k) for k in rx_a if dR(k)},
           "candump_seen": cd_total, "candump_drops": cd_drops, "sent_ok_not_seen_by_candump": miss_rx, "candump_extra_unexplained": extra_rx,
           "candump_window_s": ts_window, "candump_rate_fps": ((cd_total - 1) / ts_window if ts_window else None), "candump_max_gap_ms": round(maxgap * 1000, 2),
           "fwd": fw, "judged": judged, "class_delta": cdelta, "events_recorded": dict(recorded), "events_suppressed": suppressed, "events_total_recorded": len(evs),
           "ev_lost_delta": q1.get("ev_lost", 0) - q0.get("ev_lost", 0), "judged_window_s": window_s, "sender_elapsed_s": round(t_sent, 2),
           "status_before": q0, "status_after": q1, "last_counter_after": q1.get("last_counter"),
           "lock_timeouts_delta": q1.get("pta_lock_timeouts", 0) - q0.get("pta_lock_timeouts", 0), "skip_total_delta": q1.get("skip_total", 0) - q0.get("skip_total", 0),
           "starved": q1.get("starved"), "init_ok": q1.get("init_ok"), "core1_poll_age_ticks": q1.get("core1_poll_age_ticks"), "hb_state": q1.get("hb_state"), "hb_miss_count": q1.get("hb_miss_count")}
    with open(os.path.join(BL, "burst_%s_%s.txt" % (date, tag)), "w") as f:
        f.write("# burst run %s on %s\n## EXPECTED\n%s\n## SENDER (json)\n%s\n## SENDER can0 before\n%s\n## SENDER can0 after\n%s\n## RECEIVER before\n%s\n## RECEIVER after\n%s\n"
                % (tag, datetime.datetime.now().isoformat(timespec="seconds"), open(exp_path).read(), json.dumps(sj), b_sn, a_sn, b_rx, a_rx))
        f.write("## EVENTS from seq %d\n%s\n## FORWARDER log (tail)\n%s\n## CANDUMP (first 3 / last 3 lines, %d lines, drops %d)\n%s\n## ANALYSIS (json)\n%s\n"
                % (M + 1, ev_txt, "\n".join(fwd_txt.splitlines()[-14:]), cd_total, cd_drops, "\n".join(cd_txt.splitlines()[:3] + ["..."] + cd_txt.splitlines()[-3:]), json.dumps(res, indent=1, default=str)))
        f.write("## FORWARDER full log\n" + fwd_txt + "\n")
    open(os.path.join(BL, "burst_%s_%s.candump.log" % (date, tag)), "w").write(cd_txt)
    return res


def row(r):
    e, fw, s = r["expected"], r["fwd"], r["sender"]
    return [r["tag"], e["frames_attempted"], s.get("sent_ok"), s.get("enobufs"), r["sender_tx_packets"], r["rx_can0"], "%d/%d/%d/%d" % (r["rx_over_errors"], r["rx_fifo_errors"], r["rx_dropped"], r["rx_errors"]),
            "%d (-%d, drops %d)" % (r["candump_seen"], r["sent_ok_not_seen_by_candump"], r["candump_drops"]), fw.get("fwd_received_queued"), fw.get("queue_dropped"), fw.get("handed_judged"),
            r["judged"], e["frames_judged"]]


def main(argv):
    runs = argv[argv.index("--runs") + 1].split(",") if "--runs" in argv else ["normal"]
    date = argv[argv.index("--date") + 1] if "--date" in argv else time.strftime("%Y%m%d")
    scratch = "/tmp/burst_scratch"; os.makedirs(scratch, exist_ok=True)
    # tools: RAM only
    scp([os.path.join(MC, "mon_client"), os.path.join(MC, "can_forwarder"), os.path.join(PV, "tools/canmode/canmode"), os.path.join(PV, "tools/canutils/candump_aarch64")], "root", RX, "/tmp/")
    ssh("root", RX, "chmod +x /tmp/mon_client /tmp/can_forwarder /tmp/canmode /tmp/candump_aarch64")
    ssh("sender", SN, "mkdir -p /tmp/burst"); scp([os.path.join(MC, "burst_send.py"), os.path.join(MC, "laptop_can_send.py")], "sender", SN, "/tmp/burst/")
    cm = ssh("root", RX, "/tmp/canmode can0 | grep -E 'FD mode|data bit|controller state'"); sn = ssh("sender", SN, "ip -details link show can0 | grep -E 'can <|dbitrate'")
    print("receiver:", " | ".join(x.strip() for x in cm.splitlines())); print("sender:", " | ".join(x.strip() for x in sn.splitlines()), flush=True)
    if "ENABLED" not in cm or "ERROR-ACTIVE" not in cm or "FD" not in sn or "ERROR-ACTIVE" not in sn:
        print("ABORT: both sides must be FD and ERROR-ACTIVE"); return 2
    results = []
    for k in runs:
        r = run_one(k, date, scratch); results.append(r)
        print("run %s done: attempted %s, sender ok %s, ENOBUFS %s, receiver can0 %s, forwarder queued %s, handed %s, judged %s" % (
            r["tag"], r["expected"]["frames_attempted"], r["sender"].get("sent_ok"), r["sender"].get("enobufs"), r["rx_can0"], r["fwd"].get("fwd_received_queued"),
            r["fwd"].get("handed_judged"), r["judged"]), flush=True)
        time.sleep(8)
    hdr = ["run", "attempted", "sender_ok", "ENOBUFS", "sender_tx_pk", "rx_can0", "over/fifo/drop/err", "candump_seen", "fwd_queued", "queue_dropped", "handed_judged", "judged", "expected_judged"]
    with open(os.path.join(BL, "burst_%s_SUMMARY.txt" % date), "w") as f:
        f.write("\t".join(hdr) + "\n")
        for r in results:
            f.write("\t".join(str(x) for x in row(r)) + "\n")
        f.write("\n" + json.dumps([{k: v for k, v in r.items() if k not in ("status_before", "status_after", "sender_all_stat_deltas", "rx_all_stat_deltas")} for r in results], indent=1, default=str) + "\n")
    print("\n" + "\t".join(hdr))
    for r in results:
        print("\t".join(str(x) for x in row(r)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
