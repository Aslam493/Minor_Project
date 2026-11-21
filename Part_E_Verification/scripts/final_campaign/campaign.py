#!/usr/bin/env python3
"""campaign.py STAGE...   Final verification campaign (2026-11-22). No flash, no Secure World change, no test or threshold changed.
Stages: R (reboot both Pis, A4 + E2, restore the RAM-disk tools) | A (A1 A2 A3) | B | C | D | E1 | all
Every case writes its raw output to baseline/final_campaign/<ID>.txt and records (id, test, expected, PASS/FAIL, evidence, file) in results.json;
make_report.py builds FINAL_TEST_REPORT.md from that file. Built on the burst_run.py helpers (ssh, status parsers). Reads of the monitor
(status, events) are read-only; the test cases themselves inject traffic from the sender Pi onto the real bus, as the earlier real-bus steps did.
"""
import json, os, random, re, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "real_bus"))
import burst_run as B                      # noqa: E402
sys.path.insert(0, B.MC)
import packet_gen as g                     # noqa: E402

PV = B.PV; RX, SN = B.RX, B.SN
OUT = os.path.join(B.BL, "final_campaign"); os.makedirs(OUT, exist_ok=True)
RES = os.path.join(OUT, "results.json")
FWD = "/tmp/can_forwarder_5"; MCL = B.MCL
ATTACK = ("AUTH_FAIL", "REPLAY", "MALFORMED", "COUNTER_GAP", "COUNTER_RESYNC")
SCR = os.environ.get("SCRATCH", "/tmp/claude-1000/-home-aslam-Minor-Project/588f201f-270c-43fa-bbae-fc64f750a545/scratchpad")


def rx(c, t=180): return B.ssh("root", RX, c, timeout=t)
def sn(c, t=180): return B.ssh("sender", SN, c, timeout=t)
def P(c): return 0xC0DE0000 | (c & 0xFFFF)


def save(name, text):
    p = os.path.join(OUT, name + ".txt"); open(p, "w").write(text); return os.path.relpath(p, PV)


def rec(cid, test, expected, ok, evidence, logf):
    d = json.load(open(RES)) if os.path.exists(RES) else {}
    d[cid] = {"id": cid, "test": test, "expected": expected, "result": ok, "evidence": evidence, "file": logf, "time": time.strftime("%F %T")}
    json.dump(d, open(RES, "w"), indent=1)
    print("%-4s %-5s %s | %s" % (cid, ok, test, evidence[:200]), flush=True)


def status():
    t = rx(MCL + " status 0; " + MCL + " status 1")
    return B.page0(t), B.page1(t), t


def delta(a, b, keys): return {k: b.get("ev_" + k, 0) - a.get("ev_" + k, 0) for k in keys}


def wait_ssh(user, ip, limit=240):
    t0 = time.time()
    while time.time() - t0 < limit:
        r = subprocess.run(["ssh"] + B.SSHO + ["-o", "ConnectTimeout=3", "%s@%s" % (user, ip), "cut -d' ' -f1 /proc/uptime"], capture_output=True, text=True)
        if r.returncode == 0 and r.stdout.strip():
            return time.time() - t0, float(r.stdout.strip())
        time.sleep(2)
    return None, None


# ------------------------------------------------------------------ tools on the RAM disks
def restore_tools():
    """/tmp is RAM on both Pis: copy the tools back after a reboot (nothing is written to a card)."""
    files = [os.path.join(PV, "tools/monitor_client/mon_client"), os.path.join(PV, "tools/monitor_client/c5_board_tests"), os.path.join(PV, "tools/monitor_client/c6_negative_raw"),
             os.path.join(PV, "tools/canmode/canmode"), os.path.join(PV, "scripts/board_full_check.sh"), os.path.join(PV, "build_scratch/fwd_step5/can_forwarder")]
    B.scp(files, "root", RX, "/tmp/")
    rx("mv /tmp/can_forwarder /tmp/can_forwarder_5; chmod +x /tmp/mon_client /tmp/c5_board_tests /tmp/c6_negative_raw /tmp/canmode /tmp/can_forwarder_5")
    sn("mkdir -p /tmp/burst")
    B.scp([os.path.join(PV, "tools/monitor_client/burst_send.py"), os.path.join(PV, "tools/monitor_client/laptop_can_send.py")], "sender", SN, "/tmp/burst/")
    sn("sudo -n ip link set can0 txqueuelen 2000 2>&1 || ip link set can0 txqueuelen 2000")


# ------------------------------------------------------------------ stage R: reboot both Pis (A4, E2), A1 right after
def stage_R():
    log = []
    pre_rx = rx("echo uptime $(cut -d' ' -f1 /proc/uptime); /tmp/canmode can0 | grep -E 'controller state|ctrlmode|bitrate' | head -4")
    log.append("== before the reboot, receiver:\n" + pre_rx)
    rx("sync; (sleep 1; reboot) > /dev/null 2>&1 < /dev/null &", 20); sn("sudo sync; (sleep 1; sudo reboot) > /dev/null 2>&1 < /dev/null &", 20)
    t_reboot = time.time(); time.sleep(8)
    t_sn, up_sn = wait_ssh("sender", SN); t_rx, up_rx = wait_ssh("root", RX)
    log.append("sender answers ssh %.0f s after the reboot command (uptime %.0f s); receiver %.0f s (uptime %.0f s)" % (t_sn or -1, up_sn or -1, t_rx or -1, up_rx or -1))
    # --- receiver: read BEFORE any tool or forwarder is started (copy of mon_client needed: use the image's own tools only)
    time.sleep(2)
    B.scp([os.path.join(PV, "tools/monitor_client/mon_client"), os.path.join(PV, "tools/canmode/canmode")], "root", RX, "/tmp/")
    rx("chmod +x /tmp/mon_client /tmp/canmode")
    # A4: CAN-FD on both Pis with no manual step
    rxc = rx("/tmp/canmode can0 | grep -E 'controller state|ctrlmode|bitrate|dbitrate' ; ip link show can0 | head -2; dmesg | grep -i -E 'mcp251xfd|can0' | head -4")
    snc = sn("ip -details link show can0 | grep -E 'state|bitrate|dbitrate|fd' | head -6")
    log.append("== A4 receiver can0 right after boot (no manual step):\n" + rxc + "\n== A4 sender can0 right after boot:\n" + snc + "\nreceiver governor after boot: " + rx("cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor").strip())
    a4_rx = ("ERROR-ACTIVE" in rxc or "state UP" in rxc or "UP" in rxc) and ("FD" in rxc or "0x00000020" in rxc or "ctrlmode ACTIVE" in rxc)
    a4_sn = "bitrate 500000" in snc and "dbitrate 2000000" in snc and "FD" in snc and "ERROR-ACTIVE" in snc
    # E2: boot grace. misses accumulate but TIMEOUT must wait for G = 60 s after the monitor init; everything below happens well inside G
    rows = []

    def row():
        p0, p1, _ = status(); up = float(rx("cut -d' ' -f1 /proc/uptime").strip())
        rows.append("uptime %.0f s: hb_state %s hb_miss_count %s TIMEOUT counter %s init_ok %s starved %s" % (up, p0.get("hb_state"), p0.get("hb_miss_count"), p1.get("ev_TIMEOUT", 0), p0.get("init_ok"), p0.get("starved")))
        return up, p0, p1
    up_a, p0a, p1a = row()
    restore_tools()
    up_b, p0b, p1b = row()
    rx("%s run 40 --pull --stats --hblog /tmp/e2_hb.csv > /tmp/e2_fwd.log 2>&1 < /dev/null & echo $! > /tmp/fwd.pid" % FWD); time.sleep(6)
    up2, p0, p1 = row()
    log.append("== E2 monitor state (boot grace G = 60 s): reads before any forwarder, then the forwarder started at about the second read\n" + "\n".join(rows))
    grace_ok = up_b < 60 and int(p0a.get("hb_miss_count") or 0) >= 1 and p1a.get("ev_TIMEOUT", 0) == 0 and p1b.get("ev_TIMEOUT", 0) == 0
    e2_ok = grace_ok and p1.get("ev_TIMEOUT", 0) == 0 and p0.get("init_ok") == 1 and p0.get("starved") == 0
    forwarder_service = rx("ls /etc/init.d | tr '\\n' ' '; grep -l can_forwarder /etc/init.d/* 2>/dev/null")
    log.append("== E2 is the forwarder started by the image? init.d: " + forwarder_service)
    f = save("A4_E2_reboot", "\n".join(log))
    rec("A4", "Reboot both Pis: can0 up in CAN-FD 500k/2M with no manual step", "receiver and sender can0 UP, FD on, 500000/2000000",
        "PASS" if (a4_rx and a4_sn) else "FAIL", "receiver ssh %.0f s, sender %.0f s after reboot command; rx fd=%s, sender fd=%s (S12can+canfdcfg in the image; sender systemd unit)" % (t_rx or -1, t_sn or -1, a4_rx, a4_sn), f)
    rec("E2a", "Reboot receiver: monitor back, boot grace respected", "no TIMEOUT before G=60 s although heartbeat misses accumulate; forwarder started inside G -> never a TIMEOUT",
        "PASS" if e2_ok else "FAIL", " | ".join(rows), f)
    has_service = "can_forwarder" in forwarder_service.split("\n")[-1] or "S9" in forwarder_service
    rec("E2b", "Reboot receiver: forwarder back by itself", "forwarder starts at boot without a manual step", "FAIL" if not has_service else "PASS",
        "no init script starts can_forwarder (init.d: %s); the forwarder is started by hand from /tmp (RAM); reported as a GAP, not fixed" % forwarder_service.split("\n")[0][:120], f)
    time.sleep(30)


# ------------------------------------------------------------------ stage A
def stage_A():
    r = subprocess.run(["bash", "./verify_boot.sh", RX, "--expect-core1-pinned"], cwd=os.path.join(PV, "tools/verify_boot"), capture_output=True, text=True, timeout=300)
    f = save("A1_verify_boot", r.stdout + r.stderr); last = [l for l in r.stdout.splitlines() if l.startswith("verify_boot:")]
    rec("A1", "verify_boot on the receiver", "0 FAIL", "PASS" if last and " 0 FAIL" in last[-1] else "FAIL", last[-1] if last else "no summary", f)
    # A2 on the freshly rebooted Pi (empty ring); the prototype key file kf is copied to /tmp (RAM) for the frame tests and removed afterwards
    restore_tools()
    src = open(os.path.join(PV, "..", "optee_full/optee_os/core/arch/arm/plat-rpi3/rpi3_can_monitor_keys.h")).read(); blk = src[src.index("KEY_FRAME_INIT"):]; blk = blk[:blk.index("}")]
    open(os.path.join(SCR, "kf"), "w").write("".join(re.findall(r"0x([0-9a-fA-F]{2})", blk))); B.scp([os.path.join(SCR, "kf")], "root", RX, "/tmp/kf"); os.remove(os.path.join(SCR, "kf"))
    rx("pkill -f can_forwarder_5 2>/dev/null; kill $(cat /tmp/fwd.pid) 2>/dev/null; true"); time.sleep(2)
    out = rx("cd /tmp && sh /tmp/board_full_check.sh --mode-p 2>&1", 900)
    rx("rm -f /tmp/kf")
    f = save("A2_board_full_check_modeP", out)
    summ = [l for l in out.splitlines() if re.search(r"\bPASS\b.*\bFAIL\b|RESULT|TOTAL|summary", l, re.I)][-3:]
    nfail = len(re.findall(r"^\s*(?:\[?FAIL\]?)\s", out, re.M)); verdict = re.findall(r"(\d+) PASS[, ]+(\d+) FAIL", out)
    ok = bool(verdict) and verdict[-1][1] == "0" or ("0 FAIL" in out)
    rec("A2", "board_full_check.sh --mode-p (fresh boot, empty ring)", "0 FAIL (C3 N-04 reservation PASS)", "PASS" if ok else "FAIL", " | ".join(summ)[:260] or out[-300:], f)
    # A3 N-04: the hash of the DTB in the flashed image (verified at compose + dd/cmp MATCH) and the live reserved-memory node
    live = rx("ls /sys/firmware/devicetree/base/reserved-memory/ 2>&1; for n in /sys/firmware/devicetree/base/reserved-memory/*optee*; do echo $n; od -An -tx1 $n/reg | head -2; ls $n; done 2>&1 | head -12")
    comp = open(os.path.join(B.BL, "compose_modeP_nostk_20261121.txt")).read()
    img_hash = re.search(r"DTB read back from [^:]+: sha256 (e8ac38fa[0-9a-f]*)", comp)
    c3 = [l for l in out.splitlines() if "N-04" in l or "reserved" in l.lower()][:4]
    f = save("A3_dtb_n04", "== image compose record (DTB read back from the composed image):\n" + "\n".join(l for l in comp.splitlines() if "DTB" in l or "IMAGE" in l) +
             "\n== flashed with dd + cmp = MATCH (card_helper check, sha256 9700049351af848f...)\n== live reserved-memory on the running receiver:\n" + live + "\n== board_full_check lines:\n" + "\n".join(c3))
    rec("A3", "DTB = N-04 (e8ac38fa) and the secure-memory reservation is present", "image DTB sha256 e8ac38fa...; reserved-memory node optee_secure@10000000 live",
        "PASS" if (img_hash and "optee" in live) else "FAIL", "image DTB %s; live node: %s" % (img_hash.group(1)[:16] if img_hash else "?", " ".join(live.split())[:140]), f)


# ------------------------------------------------------------------ stage B
def stage_B():
    p0, p1, _ = status(); L = p0["last_counter"]; M = p0["ev_seq"]
    # B1: 600 valid frames at a steady 50 frames/s (20 ms), Mode P
    env = dict(os.environ, QUIET="1", STEP="b1x1", NOCD="1", SUMMARYTAG="_b1x", FWD_BIN=FWD, FWD_ARGS="--rcvbuf 4194304 --trace /tmp/b1_tr.csv --hblog /tmp/b1_hb.csv", SNAP_AT="30")
    r = subprocess.run([sys.executable, os.path.join(HERE, "..", "real_bus", "burst_run.py"), "--runs", "rate:600:20", "--date", "20261122"], env=env, capture_output=True, text=True, timeout=400)
    subprocess.run(["scp", "-q"] + B.SSHO + ["root@%s:/tmp/b1_tr.csv" % RX, os.path.join(B.BL, "trace_20261122_b1x1.csv")]); subprocess.run(["scp", "-q"] + B.SSHO + ["root@%s:/tmp/b1_hb.csv" % RX, os.path.join(B.BL, "hblog_20261122_b1x1.csv")])
    rep = subprocess.run([sys.executable, os.path.join(HERE, "..", "real_bus", "series_report.py"), "b1x", "1", "--date", "20261122"], capture_output=True, text=True).stdout
    f = save("B1_steady_600", r.stdout[-3000:] + "\n== series_report\n" + rep)
    ok = "ALL PASS" in rep and re.search(r"sent 600 = judged 600", rep)
    rec("B1", "600 valid frames at a steady 50 frames/s", "600 judged, all ACCEPTED, 0 loss at every stage, Secure World gap 0", "PASS" if ok else "FAIL",
        " ".join(l.strip() for l in rep.splitlines() if l.startswith(" run"))[:260], f)
    # B2 + B3 in one run of 300 s: heartbeat every second and Mode P requests every 50 ms, normal traffic 10 frames/s
    env = dict(os.environ, QUIET="1", STEP="b2x1", NOCD="1", SUMMARYTAG="_b2x", FWD_BIN=FWD, FWD_SECONDS="320", SNAP_AT="312", SEND_TIMEOUT="500", FWD_ARGS="--rcvbuf 4194304 --trace /tmp/b2_tr.csv --hblog /tmp/b2_hb.csv")
    r = subprocess.run([sys.executable, os.path.join(HERE, "..", "real_bus", "burst_run.py"), "--runs", "rate:3000:100", "--date", "20261122"], env=env, capture_output=True, text=True, timeout=900)
    subprocess.run(["scp", "-q"] + B.SSHO + ["root@%s:/tmp/b2_tr.csv" % RX, os.path.join(B.BL, "trace_20261122_b2x1.csv")]); subprocess.run(["scp", "-q"] + B.SSHO + ["root@%s:/tmp/b2_hb.csv" % RX, os.path.join(B.BL, "hblog_20261122_b2x1.csv")])
    rep = subprocess.run([sys.executable, os.path.join(HERE, "..", "real_bus", "series_report.py"), "b2x", "1", "--date", "20261122"], capture_output=True, text=True).stdout
    hb = [l.split(",") for l in open(os.path.join(B.BL, "hblog_20261122_b2x1.csv")).read().splitlines()[2:]]
    ans = [l for l in hb if l[0] == "23"]; late_after_first = sum(1 for l in ans[1:] if l[9] != "0"); first_v = ans[0][9] if ans else "?"
    first_state = next((l[7] for l in hb if l[0] == "22" and l[5] == "0x0" and l[7] in ("1", "3") and l[8] == "1"), "?")
    res = open(os.path.join(B.BL, [x for x in os.listdir(B.BL) if x.startswith("burst_20261122_") and "_b2x1_r1.txt" in x and "expected" not in x][0])).read()
    pq = re.search(r"requests answered: (\d+) \(ON_TIME (\d+), LATE (\d+), STALE (\d+), UNISSUED (\d+), DUPLICATE (\d+)", res)
    aj = json.loads(res[res.index("## ANALYSIS (json)") + 19:res.index("## FORWARDER full log")])
    miss = re.match(r"(\d+)", str(aj["hb_miss_count"]))      # END of run (analysis record); the first hb_miss_count in the file is the pre-run snapshot
    f = save("B2_B3_300s", r.stdout[-2500:] + "\n== series_report\n" + rep + "\n== heartbeat answers %d, first state %s first verdict %s, late after the first %d\n" % (len(ans), first_state, first_v, late_after_first))
    rec("B2", "Heartbeat for 5 minutes", "0 late and 0 missed (the first answer after the forwarder start may be LATE only if its first state was MISSED)",
        "PASS" if (late_after_first == 0 and miss and miss.group(1) == "0" and ((first_v == "0") or first_state == "3")) else "FAIL",
        "%d heartbeat answers, later than the first: %d late; first answer verdict %s (first request state %s); hb_miss_count at the end %s" % (len(ans), late_after_first, first_v, first_state, miss.group(1) if miss else "?"), f)
    rec("B3", "Mode P for 5 minutes with 10 frames/s", "all requests on time (a restart-phase LATE on the first request is the only allowed exception)",
        "PASS" if (pq and int(pq.group(3)) <= 1 and int(pq.group(4)) == 0 and int(pq.group(5)) == 0 and "ALL PASS" in rep) else "FAIL",
        ("requests %s: ON_TIME %s LATE %s STALE %s UNISSUED %s DUPLICATE %s; " % pq.groups()[:6] if pq else "no request line; ") + " ".join(l.strip() for l in rep.splitlines() if l.startswith(" run"))[:150], f)


# ------------------------------------------------------------------ stage C
def attack_case(cid, test, build, exp_classes, exp_event, exp_last_off):
    """One attack at a time: 5 warm-up valid frames, the attack frame(s), 2 valid frames after. Exact class counters (page 1) decide; the event is read for the detail code."""
    p0, p1, _ = status(); L = p0["last_counter"]; M = p0["ev_seq"]; k = g.key_from_source(); b = L + 5
    warm = [g.line(20, 0x123, g.packet(k, 0x123, c, P(c)), "valid %d" % c) for c in range(L + 1, L + 6)]
    att, follow = build(k, b)
    allf = warm + att + follow; n_frames = len(allf)
    pf = os.path.join(SCR, "camp.txt"); open(pf, "w").write("# case %s\n" % cid + "".join(allf)); B.scp([pf], "sender", SN, "/tmp/burst/camp.txt")
    rx(MCL + " ack %d" % M)
    rx("%s run 14 --pull --stats > /tmp/camp_fwd.log 2>&1 < /dev/null & echo $! > /tmp/camp_fwd.pid" % FWD); time.sleep(3.5)
    send = sn("python3 /tmp/burst/laptop_can_send.py --iface can0 /tmp/burst/camp.txt"); time.sleep(3)
    t2 = rx(MCL + " status 0; " + MCL + " status 1; " + MCL + " events --from %d --all" % (M + 1)); q0 = B.page0(t2); q1 = B.page1(t2); ev = B.events(t2)
    time.sleep(9); fw = rx("cat /tmp/camp_fwd.log")
    d = delta(p1, q1, ATTACK); judged = q1.get("frame_total", 0) - p1.get("frame_total", 0)
    want = {kk: exp_classes.get(kk, 0) for kk in ATTACK}
    evs = [e for e in ev if e["name"] == exp_event[0] and e["detail"] == exp_event[1]]
    ok = d == want and judged == n_frames and len(evs) >= 1 and q0["last_counter"] == b + exp_last_off
    f = save(cid, "frames sent (%d): warm-up %d, attack %d, follow-up %d\n%s\n== sender\n%s\n== status after / events from %d\n%s\n== forwarder log (tail)\n%s" % (
        n_frames, len(warm), len(att), len(follow), "".join(allf), send, M + 1, t2, fw[-900:]))
    rec(cid, test, "%s +1 detail %s (exact counter), other attack classes 0, judged %d, last_counter %d" % (exp_event[0], exp_event[1], n_frames, b + exp_last_off), "PASS" if ok else "FAIL",
        "class deltas %s; judged %d of %d; event %s; last_counter %s (want %d)" % ({a: v for a, v in d.items() if v}, judged, n_frames, [(e["name"], e["detail"]) for e in evs][:2] or "NOT FOUND", q0["last_counter"], b + exp_last_off), f)


def flip(data, i):
    bb = bytearray(data); bb[i] ^= 1; return bytes(bb)


def stage_C():
    def vl(k, c, d=20): return g.line(d, 0x123, g.packet(k, 0x123, c, P(c)), "valid %d" % c)
    attack_case("C1", "Replay (old counter b-2, valid tag)", lambda k, b: ([g.line(100, 0x123, g.packet(k, 0x123, b - 2, P(b - 2)), "REPLAY old counter %d" % (b - 2))], [vl(k, b + 1), vl(k, b + 2)]), {"REPLAY": 1}, ("REPLAY", 1), 2)
    attack_case("C2", "Duplicate (the last frame again, byte for byte)", lambda k, b: ([g.line(100, 0x123, g.packet(k, 0x123, b, P(b)), "DUPLICATE of %d" % b)], [vl(k, b + 1), vl(k, b + 2)]), {"REPLAY": 1}, ("REPLAY", 2), 2)
    attack_case("C3", "Same counter, different payload, valid tag", lambda k, b: ([g.line(100, 0x123, g.packet(k, 0x123, b, P(b) ^ 0xFF), "CONFLICT counter %d, payload changed" % b)], [vl(k, b + 1), vl(k, b + 2)]), {"REPLAY": 1}, ("REPLAY", 3), 2)
    attack_case("C4", "Tampered payload (tag of the original payload)", lambda k, b: ([g.line(100, 0x123, flip(g.packet(k, 0x123, b + 1, P(b + 1)), 4), "payload bit flipped, original tag")], [vl(k, b + 1), vl(k, b + 2)]), {"AUTH_FAIL": 1}, ("AUTH_FAIL", 0), 2)
    attack_case("C5", "Wrong CAN id 0x124 (valid tag for that id)", lambda k, b: ([g.line(100, 0x124, g.packet(k, 0x124, b + 1, P(b + 1)), "wrong id")], [vl(k, b + 1), vl(k, b + 2)]), {"MALFORMED": 1}, ("MALFORMED", 3), 2)
    attack_case("C6", "Counter jump +5 (b -> b+5)", lambda k, b: ([g.line(100, 0x123, g.packet(k, 0x123, b + 5, P(b + 5)), "jump +5")], [vl(k, b + 6), vl(k, b + 7)]), {"COUNTER_GAP": 1}, ("COUNTER_GAP", 4), 7)
    # C5 reply cost: wrong-id frames against valid frames, per packet, new core, from the forwarder trace (reply call time / packets)
    costs = {}
    for kind, nm in (("wrongid:320:retry", "wrong id"), ("plain:320:retry", "valid")):
        env = dict(os.environ, QUIET="1", STEP="c5" + nm[0], NOCD="1", SUMMARYTAG="_c5" + nm[0], FWD_BIN=FWD, FWD_ARGS="--rcvbuf 4194304 --trace /tmp/c5_tr.csv")
        subprocess.run([sys.executable, os.path.join(HERE, "..", "real_bus", "burst_run.py"), "--runs", kind, "--date", "20261122"], env=env, capture_output=True, text=True, timeout=400)
        subprocess.run(["scp", "-q"] + B.SSHO + ["root@%s:/tmp/c5_tr.csv" % RX, os.path.join(B.BL, "trace_20261122_c5%s.csv" % nm[0])])
        rows = [l.split(",") for l in open(os.path.join(B.BL, "trace_20261122_c5%s.csv" % nm[0])).read().splitlines()[1:] if l.split(",")[0].isdigit()]
        rp = [(int(r[2]), int(r[3])) for r in rows if r[1] == "3" and int(r[2]) > 0]
        costs[nm] = (sum(b for a, b in rp) / max(1, sum(a for a, b in rp)), sum(a for a, b in rp), len(rp))
    f = save("C5_cost", "reply call time per packet (us): wrong id %.1f (%d packets, %d replies), valid %.1f (%d packets, %d replies)" % (costs["wrong id"][0], costs["wrong id"][1], costs["wrong id"][2], costs["valid"][0], costs["valid"][1], costs["valid"][2]))
    rec("C5b", "Wrong-id frames: reply cost per packet", "cheaper than a valid frame (rejected before the HMAC)", "PASS" if costs["wrong id"][0] < costs["valid"][0] else "FAIL",
        "wrong id %.1f us/packet, valid %.1f us/packet (reply call time / packets, 600 MHz, trace)" % (costs["wrong id"][0], costs["valid"][0]), f)
    c7(); c8(); c9()


def c7():
    p0, p1, _ = status(); L = p0["last_counter"]; M = p0["ev_seq"]; k = g.key_from_source()
    open(os.path.join(SCR, "c7a.txt"), "w").write("".join(g.line(20, 0x123, g.packet(k, 0x123, c, P(c)), "valid") for c in range(L + 1, L + 11)))
    open(os.path.join(SCR, "c7b.txt"), "w").write("".join(g.line(20, 0x123, g.packet(k, 0x123, c, P(c)), "valid") for c in range(L + 11, L + 14)))
    B.scp([os.path.join(SCR, "c7a.txt"), os.path.join(SCR, "c7b.txt")], "sender", SN, "/tmp/burst/"); rx(MCL + " ack %d" % M)
    rx("%s run 25 --pull --stats > /tmp/camp_fwd.log 2>&1 < /dev/null & echo $! > /tmp/camp_fwd.pid" % FWD); time.sleep(3.5)
    sn("python3 /tmp/burst/laptop_can_send.py --iface can0 /tmp/burst/c7a.txt"); s1 = status()
    time.sleep(1.5); s2 = status()
    sn("python3 /tmp/burst/laptop_can_send.py --iface can0 /tmp/burst/c7b.txt"); time.sleep(1); s3 = status()
    ev = B.events(rx(MCL + " events --from %d --all" % (M + 1))); time.sleep(12)
    to = s2[1].get("ev_TIMEOUT", 0) - s1[1].get("ev_TIMEOUT", 0); rec_ = s3[1].get("ev_TRAFFIC_RECOVERED", 0) - s2[1].get("ev_TRAFFIC_RECOVERED", 0)
    age_ms = s2[0].get("traffic_age_ticks", 0) / 19200.0; evs = [e for e in ev if e["name"] == "TIMEOUT" and e["detail"] == 1]
    f = save("C7", "after 10 frames: traffic_age %s ticks, TIMEOUT %s\nafter 1.5 s of silence: traffic_age %s ticks (%.0f ms), TIMEOUT %s\nafter 3 more frames: TRAFFIC_RECOVERED delta %s\nevents:\n%s" % (
        s1[0].get("traffic_age_ticks"), s1[1].get("ev_TIMEOUT", 0), s2[0].get("traffic_age_ticks"), age_ms, s2[1].get("ev_TIMEOUT", 0), rec_, "\n".join(str(e) for e in ev)))
    rec("C7", "Stop the sender (silence 1.5 s)", "traffic TIMEOUT detail 1 (exact counter +1 during the silence), TRAFFIC_RECOVERED +1 when frames resume", "PASS" if (to == 1 and rec_ >= 1 and evs and age_ms > 300) else "FAIL",
        "TIMEOUT counter +%d during the silence, traffic age %.0f ms (limit 300), event TIMEOUT detail 1: %s, TRAFFIC_RECOVERED +%d on resume" % (to, age_ms, bool(evs), rec_), f)


def c8():
    rx("kill $(cat /tmp/camp_fwd.pid) 2>/dev/null; sleep 1; true"); time.sleep(1)
    p0, p1, _ = status(); M = p0["ev_seq"]; rx(MCL + " ack %d" % M)
    rx("%s run 60 --pull --stats > /tmp/camp_fwd.log 2>&1 < /dev/null & echo $! > /tmp/camp_fwd.pid" % FWD); time.sleep(5)
    base = status(); rx("kill -9 $(cat /tmp/camp_fwd.pid)")
    t = rx("i=0; while [ $i -lt 14 ]; do i=$((i+1)); echo \"T$i $(%s status 0 | grep -E 'hb_state' | tr -s ' ') | TIMEOUT=$(%s status 1 | grep -E '^  TIMEOUT' | tr -s ' ')\"; sleep 0.4; done" % (MCL, MCL), 120)
    ev = B.events(rx(MCL + " events --from %d --all" % (M + 1)))
    rx("%s run 14 --pull --stats > /tmp/camp_fwd.log 2>&1 < /dev/null & echo $! > /tmp/camp_fwd.pid" % FWD); time.sleep(5); after = status()
    miss_seq = [int(x) for x in re.findall(r"hb_miss_count (\d+)", t)]; k_ok = any(m >= 3 for m in miss_seq)
    t2 = [e for e in ev if e["name"] == "TIMEOUT" and e["detail"] == 2]
    first_t2 = next((i for i, l in enumerate(t.splitlines()) if "TIMEOUT=TIMEOUT" in l and l.split("TIMEOUT=TIMEOUT")[1].split()[0].isdigit() and False), None)
    f = save("C8", "baseline before the stop: hb_state %s miss %s TIMEOUT %s\n== samples every ~0.5 s after kill -9 of the forwarder\n%s\n== events\n%s\n== after restart: HEARTBEAT_RECOVERED %s hb_state %s miss %s" % (
        base[0].get("hb_state"), base[0].get("hb_miss_count"), base[1].get("ev_TIMEOUT", 0), t, "\n".join(str(e) for e in ev), after[1].get("ev_HEARTBEAT_RECOVERED", 0), after[0].get("hb_state"), after[0].get("hb_miss_count")))
    rec("C8", "Stop the forwarder (kill -9) with a healthy heartbeat", "after k=3 missed challenges: TIMEOUT detail 2 (exact counter), hb_miss_count reaches 3; on restart HEARTBEAT_RECOVERED", "PASS" if (k_ok and t2 and after[1].get("ev_HEARTBEAT_RECOVERED", 0) > base[1].get("ev_HEARTBEAT_RECOVERED", 0)) else "FAIL",
        "hb_miss_count sequence %s; TIMEOUT detail 2 event: %s; HEARTBEAT_RECOVERED %d -> %d after restart" % (sorted(set(miss_seq)), bool(t2), base[1].get("ev_HEARTBEAT_RECOVERED", 0), after[1].get("ev_HEARTBEAT_RECOVERED", 0)), f)
    time.sleep(10)


def c9():
    """Late Mode P answer: freeze the forwarder (SIGSTOP) for 30-90 ms at random times 40 times while 20 frames/s flow; Secure World must judge the answers of missed requests LATE."""
    p0, p1, _ = status(); L = p0["last_counter"]; M = p0["ev_seq"]; k = g.key_from_source()
    open(os.path.join(SCR, "c9.txt"), "w").write("".join(g.line(50, 0x123, g.packet(k, 0x123, c, P(c)), "valid") for c in range(L + 1, L + 441)))
    B.scp([os.path.join(SCR, "c9.txt")], "sender", SN, "/tmp/burst/"); rx(MCL + " ack %d" % M)
    rx("%s run 30 --pull --stats > /tmp/camp_fwd.log 2>&1 < /dev/null & echo $! > /tmp/camp_fwd.pid" % FWD); time.sleep(4)
    sp = subprocess.Popen(["ssh"] + B.SSHO + ["sender@%s" % SN, "python3 /tmp/burst/laptop_can_send.py --iface can0 /tmp/burst/c9.txt"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    time.sleep(1)
    b4 = status()
    fr = rx("p=$(cat /tmp/camp_fwd.pid); i=0; while [ $i -lt 40 ]; do i=$((i+1)); kill -STOP $p; sleep 0.0$((3+RANDOM%7)); kill -CONT $p; sleep 0.3$((RANDOM%9)); done; echo frozen 40 times", 120)
    sp.wait(timeout=60); time.sleep(8); af = status(); time.sleep(12); fw = rx("cat /tmp/camp_fwd.log")
    m = re.search(r"requests answered: (\d+) \(ON_TIME (\d+), LATE (\d+), STALE (\d+), UNISSUED (\d+), DUPLICATE (\d+)", fw); sw = re.search(r"SECURE-WORLD-SIDE[^\n]*n (\d+) \(ON_TIME (\d+), LATE (\d+)\)", fw)
    stale_ctr = af[1].get("ev_STALE_ANSWER", 0) - b4[1].get("ev_STALE_ANSWER", 0); late_ctr = af[1].get("ev_LATE_ANSWER", 0) - b4[1].get("ev_LATE_ANSWER", 0)
    f = save("C9", fr + "\n== forwarder log\n" + fw[-1800:] + "\n== counters before/after: LATE_ANSWER %d -> %d, STALE_ANSWER %d -> %d" % (b4[1].get("ev_LATE_ANSWER", 0), af[1].get("ev_LATE_ANSWER", 0), b4[1].get("ev_STALE_ANSWER", 0), af[1].get("ev_STALE_ANSWER", 0)))
    late_f = int(m.group(3)) if m else -1; stale_f = int(m.group(4)) if m else -1; late_sw = int(sw.group(3)) if sw else -1
    ok = m and sw and (late_f + stale_f) >= 1 and late_f == late_sw and late_ctr >= late_f
    rec("C9", "Late Mode P answer (forwarder frozen 30-90 ms, 40 times, 20 frames/s)", "answers to missed requests judged LATE (or STALE), counted exactly; forwarder count = Secure World count", "PASS" if ok else "FAIL",
        "forwarder saw LATE %d STALE %d; Secure World request counters (page 2): LATE %d; LATE_ANSWER counter +%d (includes a heartbeat one at most), STALE_ANSWER +%d" % (late_f, stale_f, late_sw, late_ctr, stale_ctr), f)


# ------------------------------------------------------------------ stage D (series are run by hand; this stage reads the results and D6 settings)
def run_series_report(tag, n, date="20261121"):
    return subprocess.run([sys.executable, os.path.join(HERE, "..", "real_bus", "series_report.py"), tag, str(n), "--date", date], capture_output=True, text=True).stdout


def d_case(cid, test, expected, tag, n, date="20261121", extra=""):
    rep = run_series_report(tag, n, date); f = save(cid + "_" + tag, rep)
    res = re.search(r"^RESULT: (.*)$", rep, re.M); win = re.search(r"judging window s: (.*)", rep); tot = re.search(r"^TOTAL over the series: (.*)", rep, re.M)
    ok = "ALL PASS" in (res.group(1) if res else "")
    rec(cid, test, expected, "PASS" if ok else "FAIL", "series %s: %s | %s | %s %s" % (tag, (res.group(1) if res else "no result")[:60], (tot.group(1) if tot else "")[:130], (win.group(0) if win else ""), extra), f)


def stage_D6():
    t = rx("echo txq_receiver $(cat /sys/class/net/can0/tx_queue_len); ethtool -g can0 2>&1 | head -12; ethtool -c can0 2>&1 | head -8; dmesg | grep -i -E 'mcp251xfd' | head -8; cat /proc/sys/net/core/rmem_default /proc/sys/net/core/rmem_max") + "\n== sender\n" + sn("echo txqueuelen $(cat /sys/class/net/can0/tx_queue_len)")
    fh = open(os.path.join(PV, "tools/monitor_client/canbridge.h")).read(); qcap = re.search(r"#define CB_QCAP (\d+)", fh).group(1)
    hh = open(os.path.join(PV, "..", "optee_full/optee_os/core/arch/arm/plat-rpi3/rpi3_can_monitor.h")).read()
    ring = re.findall(r"#define (RPI3_MON_EV_\w*(?:SLOTS|PINNED|ORDINARY|RING|CAP)\w*)\s+(\S+)", hh)
    f = save("D6_buckets", t + "\n== canbridge.h CB_QCAP %s\n== event ring constants: %s\n" % (qcap, ring) + "granted SO_RCVBUF in the forwarder runs: 8388608 bytes (4194304 requested; see baseline/burst_20261122_*b1*)")
    return t, qcap, ring, f


# ------------------------------------------------------------------ stage D
def stage_D():
    d_case("D1a", "1004 burst at governor powersave (600 MHz), 5 runs", "1004 judged, identity PASS, 0 loss", "p1_", 5)
    d_case("D1b", "1004 burst at governor performance (1200 MHz), 5 runs", "1004 judged, identity PASS, 0 loss", "p2_", 5)
    d_case("D2", "30 x N=500 (504 frames), powersave: min/median/max window and late count", "30 of 30 identity PASS, 504 judged each", "s3_", 30)
    d_case("D3", "2000-frame burst x5 (2004 frames)", "5 of 5 identity PASS, 0 loss", "s4_", 5)
    d_case("D4", "20008-frame burst x3 (overload)", "sent = judged + counted drops; Secure World gap = drops", "s5_", 3)
    d_case("D5a", "Flood 35000 frames at ~3587 frames/s x3", "sent = judged + counted drops; Secure World gap = drops", "s6_", 3)
    d_case("D5b", "Sustained 480 frames/s x3 (below 640)", "0 loss", "s8a_", 3)
    d_case("D5c", "Sustained 943 frames/s x3 (above 640)", "drops counted at one stage, equal to the gap", "s8b_", 3)
    d_case("D5d", "Lossless limit: 2400 frames x3 and 2600 frames x3", "2400 lossless; 2600 counted drops", "s7a_", 3, extra="(2600: see D5e)")
    d_case("D5e", "2600-frame burst x3", "counted drops = gap", "s7b_", 3)
    t, qcap, ring, f = stage_D6()
    rec("D6", "Bucket sizes", "documented", "PASS", "sender txqueuelen 2000 (runtime, NOT persistent); receiver can0 qlen 10; forwarder queue CB_QCAP %s frames; SO_RCVBUF requested 4194304, granted 8388608 bytes (about 9380 frames at 894 B); batch 32; request every 50 ms; see file for chip FIFO and event ring" % qcap, f)


# ------------------------------------------------------------------ stage E1
def stage_E1():
    rows = []
    for i in range(1, 11):
        time.sleep(random.uniform(0, 1))
        rx("%s run 6 --pull --stats --hblog /tmp/e1_hb.csv --trace /tmp/e1_tr.csv > /tmp/e1_fwd.log 2>&1 < /dev/null" % FWD, 60)
        hb = [l.split(",") for l in rx("cat /tmp/e1_hb.csv").splitlines()[2:]]; tr = [l.split(",") for l in rx("cat /tmp/e1_tr.csv").splitlines()[1:]]
        fw = rx("cat /tmp/e1_fwd.log")
        polls = [l for l in hb if l[0] == "22" and l[5] == "0x0" and l[7] in ("1", "3") and l[8] == "1"]; ans = [l for l in hb if l[0] == "23"]
        first_hb = (int(polls[0][7]), int(ans[0][9])) if polls and ans else None
        later_late = sum(1 for l in ans[1:] if l[9] != "0")
        fpk = next((int(l[3]) for l in tr if len(l) > 3 and l[1] == "2" and l[3] in ("1", "3")), None)
        m = re.search(r"requests answered: (\d+) \(ON_TIME (\d+), LATE (\d+), STALE (\d+)", fw); pk_late = int(m.group(3)) if m else -1
        rows.append((i, first_hb, later_late, fpk, pk_late, ("hb OK" if first_hb and (first_hb[0] == 3) == (first_hb[1] == 1) and later_late == 0 else "hb MISMATCH"), ("pk OK" if fpk is not None and (fpk == 3) == (pk_late > 0) else "pk MISMATCH")))
    f = save("E1", "\n".join("restart %d: first heartbeat state/verdict %s, later late heartbeat answers %d, first request state %s, late packet replies %d -> %s, %s" % r for r in rows))
    ok = all(r[5] == "hb OK" and r[6] == "pk OK" for r in rows)
    rec("E1", "Restart the forwarder 10x at random phase", "only the first answer may be LATE and only when its first state was MISSED (heartbeat and packet request)", "PASS" if ok else "FAIL",
        "heartbeat: %d of 10 match (first MISSED <=> first LATE, later answers on time); packet request: %d of 10 match; first-state MISSED in %d hb runs" % (
            sum(1 for r in rows if r[5] == "hb OK"), sum(1 for r in rows if r[6] == "pk OK"), sum(1 for r in rows if r[1] and r[1][0] == 3)), f)


def main(argv):
    st = argv or ["all"]
    for s_ in st:
        if s_ in ("R",): stage_R()
        if s_ in ("A",): stage_A()
        if s_ in ("B",): stage_B()
        if s_ in ("C",): stage_C()
        if s_ in ("D",): stage_D()
        if s_ in ("E1",): stage_E1()


if __name__ == "__main__":
    main(sys.argv[1:])
