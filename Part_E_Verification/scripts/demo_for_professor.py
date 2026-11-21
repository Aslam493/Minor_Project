#!/usr/bin/env python3
"""demo_for_professor.py - ONE command that shows a professor, live and in plain words, what the Secure CAN Monitor does.

    python3 scripts/demo_for_professor.py            press Enter between the steps (you talk, then continue)
    python3 scripts/demo_for_professor.py --auto     no keys needed, pauses of 6 seconds
    python3 scripts/demo_for_professor.py --fast     no pauses at all (test run)
    options: --skip-flood (leave out step 5), --skip-kill (leave out step 6), RX=<receiver ip> SN=<sender ip> to use other addresses

Everything shown is REAL: frames are sent from the sender Pi onto the real CAN-FD bus, the receiver Pi's Secure World (OP-TEE) judges them,
and the numbers printed are read back from the Secure World's own counters. Nothing is written to any SD card or flashed; only RAM and the
monitor's counters change. Needs both Pis on (receiver 10.237.27.212, sender 10.237.27.144 by default).
"""
import os, re, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "real_bus"))
import burst_run as B                      # noqa: E402  (ssh helpers and status parsers, already verified in the campaign)
sys.path.insert(0, B.MC)
import packet_gen as g                     # noqa: E402

PV = B.PV; RX, SN = B.RX, B.SN
MCL = B.MCL
SVC = "/etc/init.d/S95can_forwarder"
SCR = os.environ.get("SCRATCH", "/tmp")
AUTO = "--auto" in sys.argv; FAST = "--fast" in sys.argv
C = lambda code, s: "\033[%sm%s\033[0m" % (code, s) if sys.stdout.isatty() else s
BOLD, GREEN, RED, YEL, CYAN, DIM = (lambda s: C("1", s)), (lambda s: C("1;32", s)), (lambda s: C("1;31", s)), (lambda s: C("1;33", s)), (lambda s: C("1;36", s)), (lambda s: C("2", s))
fails = []


def rx(c, t=120): return B.ssh("root", RX, c, timeout=t)
def sn(c, t=120): return B.ssh("sender", SN, c, timeout=t)
def P(c): return 0xC0DE0000 | (c & 0xFFFF)


def pause(msg="Press Enter for the next step"):
    if FAST: return
    if AUTO: time.sleep(6); return
    try: input(DIM("\n   [ %s ] " % msg))
    except EOFError: time.sleep(3)


def title(n, text):
    print("\n" + CYAN("=" * 100)); print(CYAN("  STEP %s  %s" % (n, text))); print(CYAN("=" * 100))


def say(text): print("  " + text.replace("\n", "\n  "))
def ok(cond, good, bad):
    print("   " + (GREEN("[ OK ] ") + good if cond else RED("[ !! ] ") + bad))
    if not cond: fails.append(bad)
    return cond


def status():
    t = rx(MCL + " status 0; " + MCL + " status 1")
    return B.page0(t), B.page1(t), t


def stat(name): return int(rx("cat /sys/class/net/can0/statistics/%s" % name).strip() or 0)


def send_file(lines, name, policy=None):
    path = os.path.join(SCR, name); open(path, "w").write("".join(lines)); B.scp([path], "sender", SN, "/tmp/burst/" + name)
    if policy: return sn("python3 /tmp/burst/burst_send.py /tmp/burst/%s %s" % (name, policy), 180)
    return sn("python3 /tmp/burst/laptop_can_send.py --iface can0 /tmp/burst/%s" % name, 180)


def wait_idle(frames_before, limit=40):
    """wait until the Secure World stops judging (frame total stable for 1.6 s); returns (status0, status1, seconds)"""
    t0 = time.time(); last = -1; stable_since = time.time(); p0 = p1 = None
    while time.time() - t0 < limit:
        p0, p1, _ = status(); ft = p1.get("frame_total", 0)
        print("\r   Secure World has judged %6d frames so far ...   " % (ft - frames_before), end="", flush=True)
        if ft != last: last = ft; stable_since = time.time()
        elif time.time() - stable_since > 1.6: break
        time.sleep(0.6)
    print()
    return p0, p1, time.time() - t0


# ---------------------------------------------------------------------------------------------------------------------------------- preflight
def preflight():
    print(BOLD("\n  Checking that both Raspberry Pis are ready ..."))
    mac = rx("cat /sys/class/net/eth0/address").strip()
    if not mac:
        print(RED("  The receiver does not answer at %s. Check power, network and the address (receiver MAC b8:27:eb:5c:ba:a9), or run with RX=<ip>." % RX)); sys.exit(2)
    if sn("echo ok").strip() != "ok":
        print(RED("  The sender does not answer at %s (MAC b8:27:eb:1e:98:02). Check power and network, or run with SN=<ip>." % SN)); sys.exit(2)
    print("   receiver %s (MAC %s)%s, sender %s" % (RX, mac, "" if mac.endswith("5c:ba:a9") else YEL("  [not the usual board]"), SN))
    if "ok" not in rx("test -x /tmp/mon_client -a -x /tmp/canmode && echo ok"):
        B.scp([os.path.join(PV, "tools/monitor_client/mon_client"), os.path.join(PV, "tools/canmode/canmode")], "root", RX, "/tmp/"); rx("chmod +x /tmp/mon_client /tmp/canmode")
    if "ok" not in sn("test -f /tmp/burst/laptop_can_send.py -a -f /tmp/burst/burst_send.py && echo ok"):
        sn("mkdir -p /tmp/burst"); B.scp([os.path.join(PV, "tools/monitor_client/burst_send.py"), os.path.join(PV, "tools/monitor_client/laptop_can_send.py")], "sender", SN, "/tmp/burst/")
    n = int((rx("ps | grep -c '[u]sr/bin/can_forwarder'").strip() or "0"))
    if n == 0:
        print("   the forwarder service is not running: starting it"); rx("%s start >/dev/null 2>&1 </dev/null" % SVC); time.sleep(4)
    elif n > 1:
        print(YEL("   warning: %d forwarder processes are running (a test forwarder next to the service?); the demo may show LATE answers" % n))


# ---------------------------------------------------------------------------------------------------------------------------------- the steps
def step1():
    title(1, "What is on the table")
    say("""A car network (CAN bus) carries messages between the car's computers. An attacker who gets onto it can send fake messages.
This project is a MONITOR that sits on a Raspberry Pi 3 and watches the bus. The judge is NOT the normal Linux of the Pi (Linux can be hacked);
the judge is the SECURE WORLD of the processor (ARM TrustZone, OP-TEE), which Linux cannot read or change.

   SENDER Pi  --- real CAN-FD bus (500 kbit/s, data part 2 Mbit/s) --->  RECEIVER Pi
   (plays the car                                                  CAN chip -> Linux "forwarder" (only a postman)
    and the attacker)                                                                    |  hands every message over
                                                                                         v
                                                           SECURE WORLD (OP-TEE): checks the secret signature (HMAC),
                                                           the message counter, the format, the timing -> says what it is.

Every message carries a counter and a signature made with a secret key. The Secure World knows the key and the last counter.""")
    p0, p1, _ = status()
    can = rx("/tmp/canmode can0 | grep -E 'nominal bit timing|data bit timing|controller state'")
    fw = rx("ps | grep '[u]sr/bin/can_forwarder' | head -1 | cut -c1-110").strip()
    kern = rx("uname -r").strip(); opt = re.search(r"optee: revision [0-9.]+", rx("dmesg | grep -i 'optee: revision' | head -1"))
    print(BOLD("\n  LIVE from the receiver right now:"))
    print("   Linux %s, %s; Linux is allowed on CPU cores 0,2,3 only: CPU core 1 is kept for the Secure World (%s)" % (kern, opt.group(0) if opt else "OP-TEE", rx("cat /sys/devices/system/cpu/online").strip()))
    for l in can.splitlines(): print("   CAN: " + " ".join(l.split()))
    print("   Linux forwarder (started by itself at boot): %s" % (fw or RED("NOT RUNNING")))
    print("   Secure World monitor: heartbeat %s, init_ok=%s, starved=%s, last message counter seen: %s" % (p0.get("hb_state"), p0.get("init_ok"), p0.get("starved"), p0.get("last_counter")))
    ok(p0.get("init_ok") == 1 and p0.get("hb_state") in ("ANSWERED", "OPEN"), "the Secure World monitor is running and the Linux forwarder answers its heartbeat", "the monitor is not healthy")
    pause()


def step2():
    title(2, "Normal traffic: 50 correct messages")
    say("The sender sends 50 correct, signed messages (20 ms apart). The Secure World must accept every one and raise no alarm.")
    p0, p1, _ = status(); L = p0["last_counter"]; k = g.key_from_source()
    frames = [g.line(20, 0x123, g.packet(k, 0x123, c, P(c)), "valid %d" % c) for c in range(L + 1, L + 51)]
    print("   sending 50 messages ...", flush=True); send_file(frames, "demo_normal.txt"); time.sleep(2.5)
    q0, q1, _ = status(); judged = q1["frame_total"] - p1["frame_total"]
    bad = {a: q1.get("ev_" + a, 0) - p1.get("ev_" + a, 0) for a in ("AUTH_FAIL", "REPLAY", "MALFORMED", "COUNTER_GAP", "COUNTER_RESYNC")}
    allowed = {"COUNTER_RESYNC": 1} if L == 0 else {}
    print("   Secure World judged %d messages; alarms raised: %s; message counter %d -> %d" % (judged, {a: v for a, v in bad.items() if v} or "none", L, q0["last_counter"]))
    if L == 0: print(DIM("   (the receiver was just booted, its counter was 0: the first message makes the monitor 'resynchronise' ONCE, that is the design and not an alarm about an attack)"))
    ok(judged == 50 and {a: v for a, v in bad.items() if v} == allowed and q0["last_counter"] == L + 50, "50 of 50 accepted, no attack alarm", "unexpected result in the normal traffic")
    pause()


def step3():
    title(3, "Six attacks on the real bus, one after the other")
    say("""Now the sender behaves like an attacker. Between correct messages it slips in ONE bad message every 1.2 seconds.
For each attack you will see what the attacker did and what the Secure World answered (read from its own event log and counters).""")
    p0, p1, _ = status(); L = p0["last_counter"]; M = p0["ev_seq"]; k = g.key_from_source(); rx(MCL + " ack %d" % M)
    c = [L]; lines = []; log = []

    def valid(d=100):
        c[0] += 1; lines.append(g.line(d, 0x123, g.packet(k, 0x123, c[0], P(c[0])), "valid %d" % c[0]))

    def block(n):
        for _ in range(n): valid()
    for _ in range(5): valid()
    last_good = lambda: g.packet(k, 0x123, c[0], P(c[0]))
    attacks = [
        ("Tampered message", "changes one data bit but keeps the old signature", "AUTH_FAIL", 0),
        ("Replay", "sends an OLD message again (counter 3 steps back, valid signature)", "REPLAY", 1),
        ("Duplicate", "sends the last message a second time, byte for byte", "REPLAY", 2),
        ("Conflict", "same counter as the last message but different data (signature valid)", "REPLAY", 3),
        ("Wrong CAN id", "uses an id (0x124) that is not allowed on this link", "MALFORMED", 3),
        ("Counter jump", "skips 4 counters ahead (messages were removed or injected)", "COUNTER_GAP", 4)]
    for i, (nm, what, cls, det) in enumerate(attacks):
        block(11)
        if i == 0:
            d = bytearray(g.packet(k, 0x123, c[0] + 1, P(c[0] + 1))); d[4] ^= 1; lines.append(g.line(100, 0x123, bytes(d), "ATTACK tamper"))
        elif i == 1: lines.append(g.line(100, 0x123, g.packet(k, 0x123, c[0] - 3, P(c[0] - 3)), "ATTACK replay"))
        elif i == 2: lines.append(g.line(100, 0x123, last_good(), "ATTACK duplicate"))
        elif i == 3: lines.append(g.line(100, 0x123, g.packet(k, 0x123, c[0], P(c[0]) ^ 0xFF), "ATTACK conflict"))
        elif i == 4: lines.append(g.line(100, 0x124, g.packet(k, 0x124, c[0] + 1, P(c[0] + 1)), "ATTACK wrong id"))
        else:
            c[0] += 5; lines.append(g.line(100, 0x123, g.packet(k, 0x123, c[0], P(c[0])), "ATTACK jump"))
    block(3)
    n = len(lines); print("   sending %d messages (%d correct, 6 attacks), about %d seconds ..." % (n, n - 6, n // 10 + 1), flush=True)
    send_file(lines, "demo_attacks.txt"); time.sleep(3)
    q0, q1, t = status(); ev = B.events(rx(MCL + " events --from %d --all" % (M + 1)))
    print(BOLD("\n   %-18s %-74s %s" % ("ATTACK", "what the attacker did", "Secure World answer (event log)")))
    allok = True
    for nm, what, cls, det in attacks:
        hit = [e for e in ev if e["name"] == cls and e["detail"] == det]
        good = bool(hit); allok &= good
        print("   %-18s %-74s %s" % (nm, what, (GREEN("%s detected (detail %d)" % (cls, det)) if good else RED("NOT SEEN"))))
    d = {a: q1.get("ev_" + a, 0) - p1.get("ev_" + a, 0) for a in ("AUTH_FAIL", "REPLAY", "MALFORMED", "COUNTER_GAP")}
    judged = q1["frame_total"] - p1["frame_total"]
    print("\n   Exact counters of the Secure World: %s ; messages judged %d of %d sent" % (d, judged, n))
    ok(allok and d == {"AUTH_FAIL": 1, "REPLAY": 3, "MALFORMED": 1, "COUNTER_GAP": 1} and judged == n,
       "every attack was caught, exactly once, with the right reason; the %d correct messages were accepted" % (n - 6), "an attack was not caught as expected")
    pause()


def burst(step, label, N, expect_all):
    p0, p1, _ = status(); L = p0["last_counter"]; k = g.key_from_source()
    frames = [g.line(0, 0x123, g.packet(k, 0x123, c, P(c)), "v") for c in range(L + 1, L + N + 1)]
    rx0, ov0 = stat("rx_packets"), stat("rx_over_errors"); ft0 = p1["frame_total"]
    print("   sending %d messages as fast as the bus allows ..." % N, flush=True); t0 = time.time()
    out = send_file(frames, "demo_burst_%d.txt" % N, "retry"); tsend = time.time() - t0
    m = re.search(r'"rate_fps": ([0-9.]+)', out); rate = float(m.group(1)) if m else 0
    q0, q1, tdrain = wait_idle(ft0)
    chip = stat("rx_packets") - rx0; over = stat("rx_over_errors") - ov0; judged = q1["frame_total"] - ft0
    span = q0["last_counter"] - L; nonadv = sum(q1.get("ev_" + a, 0) - p1.get("ev_" + a, 0) for a in ("AUTH_FAIL", "REPLAY", "MALFORMED"))
    missing = span - (judged - nonadv); dropped = chip - judged
    return N, rate, chip, over, judged, missing, dropped, q0["last_counter"] == L + N


def step4():
    title(4, "A burst: 2000 messages at the full speed of the bus")
    say("A busy or attacked bus can deliver thousands of messages per second. The sender now fires 2000 messages as fast as the bus can carry them.")
    N, rate, chip, over, judged, missing, dropped, last_ok = burst(4, "burst", 2000, True)
    print("   sent %d (about %.0f messages per second)   |   CAN chip of the receiver got %d (lost on the bus: %d)   |   Secure World judged %d" % (N, rate, chip, N - chip, judged))
    ok(chip == N and over == 0 and judged == N and missing == 0 and last_ok, "2000 sent = 2000 judged, nothing lost, Secure World counter gap 0", "frames were lost in the burst")
    pause()


def step5():
    title(5, "Overload: 12000 messages, more than the monitor can judge")
    say("""The Secure World judges at most 640 messages per second (a design limit: 32 every 50 ms). The bus can deliver 3500 per second.
So during a flood some messages cannot be judged. What matters: nothing disappears SILENTLY. The Linux forwarder holds 2048 messages and, when full,
drops the OLDEST one. The Secure World sees the missing message counters and counts them itself. Let us check that both counts agree.""")
    N, rate, chip, over, judged, missing, dropped, last_ok = burst(5, "flood", 12000, False)
    print("   sent %d (about %.0f/s)  ->  CAN chip got %d (bus loss %d, chip overflow %d)  ->  Secure World judged %d  ->  dropped by the forwarder queue: %d" % (N, rate, chip, N - chip, over, judged, dropped))
    print("   Secure World's OWN count of missing message counters (its gap): %d" % missing)
    ok(chip == N and over == 0 and dropped == missing and judged + dropped == N and judged > 2000 and last_ok,
       "sent %d = judged %d + dropped %d; the Secure World noticed exactly the %d missing ones: no message was lost without being counted" % (N, judged, dropped, missing),
       "the drop count and the Secure World gap do not agree")
    pause()


def step6():
    title(6, "What if the Linux forwarder is killed or hacked?")
    say("""Linux is only the postman, but the monitor must notice if the postman stops. Every second the Secure World sends a challenge ("heartbeat");
Linux must answer in 0.5 s. After 3 missed challenges the Secure World raises a TIMEOUT alarm. We stop the forwarder now and watch.""")
    p0, p1, _ = status(); M = p0["ev_seq"]; rx(MCL + " ack %d" % M)
    rx("%s stop >/dev/null 2>&1 </dev/null" % SVC); t0 = time.time(); raised = None; seen = []
    print(YEL("   forwarder STOPPED. Secure World view (about every half second):"))
    for i in range(9):
        a, b, _ = status(); miss = a.get("hb_miss_count")
        if raised is None and any(e["name"] == "TIMEOUT" and e["detail"] == 2 for e in B.events(rx(MCL + " events --from %d --all" % (M + 1)))): raised = time.time() - t0
        print("   t=%4.1f s  heartbeat %-9s missed challenges: %s   alarm 'forwarder is dead' (TIMEOUT, 3 misses): %s" % (time.time() - t0, a.get("hb_state"), miss, RED("RAISED") if raised is not None else "not yet")); seen.append((miss, 0))
        time.sleep(0.4)
    rx("%s start >/dev/null 2>&1 </dev/null" % SVC); time.sleep(4)
    a, b, _ = status(); rec = b.get("ev_HEARTBEAT_RECOVERED", 0) - p1.get("ev_HEARTBEAT_RECOVERED", 0)
    print(GREEN("   forwarder STARTED again.") + " heartbeat %s, missed challenges %s, HEARTBEAT_RECOVERED events +%d" % (a.get("hb_state"), a.get("hb_miss_count"), rec))
    ok(raised is not None and max(m for m, _t in seen if m is not None) >= 3 and a.get("hb_state") in ("ANSWERED", "OPEN"),
       "the Secure World noticed the dead forwarder after 3 missed challenges (TIMEOUT detail 2 after about %.1f s) and recovered when it came back" % (raised or 0), "the dead forwarder was not detected as expected")
    pause("Press Enter for the summary")


def summary():
    title(7, "Summary for the professor")
    say("""What you saw was live on the real hardware, with the numbers read from the Secure World itself:
  * correct messages accepted; six different attacks caught, each with the right reason;
  * 2000 messages at full bus speed: nothing lost; 12000 messages (overload): every message either judged or counted as dropped, and the Secure World's
    own counter agrees exactly with the drop count; a killed Linux forwarder is detected after 3 missed heartbeats.
The complete verified results (31 PASS in the campaign, 273,000+ frames, 18 of 18 overload runs exact) are in FINAL_TEST_REPORT.md.
Honest limits: detection only (it does not block messages); prototype keys; the Pi 3 has no memory-protection hardware (TZASC); the Linux forwarder
drops the OLDEST message when overloaded (the Secure World reports the gap).""")
    print("\n  " + (GREEN("ALL CHECKS OF THIS DEMO PASSED") if not fails else RED("%d CHECK(S) DID NOT GO AS EXPECTED:" % len(fails)) + "\n    - " + "\n    - ".join(fails)))


def main():
    if "--help" in sys.argv or "-h" in sys.argv: print(__doc__); return 0
    print(BOLD("\n  SECURE CAN MONITOR: LIVE DEMONSTRATION") + "   (receiver %s, sender %s)" % (RX, SN))
    preflight(); step1(); step2(); step3(); step4()
    if "--skip-flood" not in sys.argv: step5()
    if "--skip-kill" not in sys.argv: step6()
    summary()
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
