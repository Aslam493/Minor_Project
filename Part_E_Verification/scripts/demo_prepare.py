#!/usr/bin/env python3
"""demo_prepare.py - run this ONCE on the laptop just before a live demo (and again whenever you want to start the demo from the beginning).

It reads the receiver's current message counter from the Secure World, builds a CHAIN of packet files whose counters continue from it, and copies them
to the sender Pi (/tmp/demo). Then you show the demo from two Pi terminals: on the sender you send the files in order, on the receiver you watch the
Secure World's answers. The packet files carry finished messages and tags only; the secret key stays on the laptop and in the Secure World image.

  python3 scripts/demo_prepare.py            (RX=<receiver ip> SN=<sender ip> if the addresses changed)

The files must be sent IN ORDER (1, 2, 3 ...). If you send one twice or out of order the Secure World will (correctly) call it a replay: run this script again to start fresh.
"""
import os, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "real_bus"))
import burst_run as B                      # noqa: E402
sys.path.insert(0, B.MC)
import packet_gen as g                     # noqa: E402

PV = B.PV; RX, SN = B.RX, B.SN
OUTDIR = os.path.join(PV, "build_scratch", "demo_files")
def P(c): return 0xC0DE0000 | (c & 0xFFFF)


def main():
    os.makedirs(OUTDIR, exist_ok=True)
    mac = B.ssh("root", RX, "cat /sys/class/net/eth0/address").strip()
    if not mac:
        print("The receiver does not answer at %s (its MAC should be b8:27:eb:5c:ba:a9). Check power/network or run with RX=<ip>." % RX); return 2
    if B.ssh("sender", SN, "echo ok").strip() != "ok":
        print("The sender does not answer at %s. Check power/network or run with SN=<ip>." % SN); return 2
    if "ok" not in B.ssh("root", RX, "test -x /tmp/mon_client && echo ok"):
        B.scp([os.path.join(PV, "tools/monitor_client/mon_client"), os.path.join(PV, "tools/canmode/canmode")], "root", RX, "/tmp/"); B.ssh("root", RX, "chmod +x /tmp/mon_client /tmp/canmode")
    L = B.page0(B.ssh("root", RX, B.MCL + " status 0"))["last_counter"]
    k = g.key_from_source(); c = [L]; files = {}

    def valid(lines, d=100):
        c[0] += 1; lines.append(g.line(d, 0x123, g.packet(k, 0x123, c[0], P(c[0])), "valid %d" % c[0]))

    def new(name):
        files[name] = []; return files[name]
    f = new("1_normal.txt")
    for _ in range(10): valid(f)
    f = new("2_tamper.txt")                        # a message with one changed data bit but the old signature
    for _ in range(3): valid(f)
    keep_old = c[0]
    d = bytearray(g.packet(k, 0x123, c[0] + 1, P(c[0] + 1))); d[4] ^= 1; f.append(g.line(100, 0x123, bytes(d), "TAMPERED data, old signature -> AUTH_FAIL"))
    for _ in range(2): valid(f)
    f = new("3_replay.txt")                        # an old valid message sent again
    for _ in range(2): valid(f)
    f.append(g.line(100, 0x123, g.packet(k, 0x123, keep_old, P(keep_old)), "REPLAY of old counter %d -> REPLAY (detail 1)" % keep_old))
    for _ in range(2): valid(f)
    f = new("4_duplicate.txt")                     # the last message again, byte for byte
    valid(f); f.append(g.line(100, 0x123, g.packet(k, 0x123, c[0], P(c[0])), "DUPLICATE of the last message -> REPLAY (detail 2)")); valid(f)
    f = new("5_conflict.txt")                      # same counter, other data
    valid(f); f.append(g.line(100, 0x123, g.packet(k, 0x123, c[0], P(c[0]) ^ 0xFF), "CONFLICT: same counter, other data -> REPLAY (detail 3)")); valid(f)
    f = new("6_wrongid.txt")                       # a CAN id that is not allowed on this link
    valid(f); f.append(g.line(100, 0x124, g.packet(k, 0x124, c[0] + 1, P(c[0] + 1)), "WRONG ID 0x124 -> MALFORMED")); valid(f); valid(f)
    f = new("7_jump.txt")                          # skips 4 counters: messages were removed or injected
    valid(f); c[0] += 4; f.append(g.line(100, 0x123, g.packet(k, 0x123, c[0] + 1, P(c[0] + 1)), "JUMP +4 -> COUNTER_GAP (detail 4)")); c[0] += 1; valid(f)
    f = new("8_burst2000.txt")                     # 2000 messages as fast as the bus allows
    for _ in range(2000): valid(f, 0)
    f = new("9_flood12000.txt")                    # 12000 messages: more than the monitor can judge
    for _ in range(12000): valid(f, 0)
    B.ssh("sender", SN, "mkdir -p /tmp/demo")
    paths = []
    for name, lines in files.items():
        p = os.path.join(OUTDIR, name); open(p, "w").write("# demo file %s (frames: %d), counters continue from the monitor's counter %d\n" % (name, len(lines), L) + "".join(lines)); paths.append(p)
    B.scp(paths + [os.path.join(PV, "tools/monitor_client/laptop_can_send.py"), os.path.join(PV, "tools/monitor_client/burst_send.py")], "sender", SN, "/tmp/demo/")
    print("READY. The Secure World's last message counter was %d; %d files are on the sender in /tmp/demo (in this order):" % (L, len(files)))
    for name, lines in files.items(): print("   %-18s %5d messages" % (name, len(lines)))
    print("\nOn the SENDER Pi:   cd /tmp/demo   then, for files 1 to 7:   python3 laptop_can_send.py --iface can0 1_normal.txt   (then 2_..., 3_...)")
    print("                    for file 8 and 9 (fast):               python3 burst_send.py 8_burst2000.txt retry")
    return 0


if __name__ == "__main__":
    sys.exit(main())
