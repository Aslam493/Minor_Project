#!/usr/bin/env python3
"""laptop_can_send.py - the DIRECT sender: sends the ready-made packets (made by packet_gen.py) from the laptop onto the CAN bus through a CAN-FD
adapter, so they reach the Pi's CAN controller (MCP2518FD) over the bus wires, never through the Pi. Needs only the Python standard library and a
Linux SocketCAN interface (a USB CAN-FD adapter with the gs_usb driver, configured by the user with `ip link`, see monitor/DIRECT_SENDER_SETUP.md).

  laptop_can_send.py [--iface can0] FILE         send the packets in FILE (format: see canbridge.h, one packet per line)
  laptop_can_send.py --dry-run FILE              parse FILE and print the frames, send nothing
  laptop_can_send.py --selftest                  check the frame packing against known answers (no hardware needed)
The packet file holds finished messages and tags, not the key. This script has no key and no HMAC code."""
import socket, struct, sys, time

FRAME_FMT = "=IBBBB64s"          # struct canfd_frame: can_id u32, len u8, flags u8, res0 u8, res1 u8, data[64]
CANFD_BRS = 0x01
SOL_CAN_RAW = getattr(socket, "SOL_CAN_RAW", 101)
CAN_RAW_FD_FRAMES = getattr(socket, "CAN_RAW_FD_FRAMES", 5)


def parse_line(line):
    """Returns (delay_ms, can_id, data48) or None for a blank/comment line. Raises ValueError for a malformed line (same rules as cb_parse_line)."""
    t = line.split("#", 1)[0].strip()
    if not t:
        return None
    f = t.split()
    if len(f) != 3:
        raise ValueError("expected: DELAY_MS ID_HEX DATA_HEX")
    d, cid, data = int(f[0], 10), int(f[1], 16), bytes.fromhex(f[2])
    if not 0 <= d <= 3600000 or cid > 0xFFFFFFFF or len(data) != 48:
        raise ValueError("delay, id or data length out of range (data must be 48 bytes)")
    return d, cid, data


def pack_frame(can_id, data48):
    return struct.pack(FRAME_FMT, can_id, 48, CANFD_BRS, 0, 0, data48 + bytes(16))


def load(path):
    out = []
    for n, l in enumerate(open(path), 1):
        try:
            r = parse_line(l)
        except ValueError as e:
            sys.exit("%s:%d: %s" % (path, n, e))
        if r:
            out.append(r)
    return out


def send(iface, pkts):
    s = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
    s.setsockopt(SOL_CAN_RAW, CAN_RAW_FD_FRAMES, 1)
    s.bind((iface,))
    t = time.monotonic(); sent = 0
    for d, cid, data in pkts:
        t += d / 1000.0
        w = t - time.monotonic()
        if w > 0:
            time.sleep(w)
        s.send(pack_frame(cid, data)); sent += 1
    s.close()
    return sent


def selftest():
    bad = 0
    def chk(ok, name):
        nonlocal bad
        print("  %s  %s" % ("ok  " if ok else "FAIL", name)); bad += (not ok)
    data = bytes(range(48))
    fr = pack_frame(0x123, data)
    chk(len(fr) == 72, "a frame is 72 bytes (struct canfd_frame)")
    cid, ln, fl, r0, r1, d = struct.unpack(FRAME_FMT, fr)
    chk(cid == 0x123 and ln == 48 and fl == CANFD_BRS and d[:48] == data and d[48:] == bytes(16), "id, length 48, BRS flag, data, zero tail")
    chk(struct.unpack(FRAME_FMT, pack_frame(0x80000123, data))[0] == 0x80000123, "an extended-id flag in the id is kept")
    line = "250 123 " + data.hex() + "  # comment"
    chk(parse_line(line) == (250, 0x123, data), "a packet-file line is parsed")
    chk(parse_line("# only a comment") is None and parse_line("   ") is None and parse_line("") is None, "comment and blank lines are skipped")
    for name, ln_ in (("short data", "10 123 00"), ("non-hex", "10 123 " + "zz" * 48), ("long data", "10 123 " + data.hex() + "00"), ("bad delay", "x 123 " + data.hex()), ("huge delay", "99999999 123 " + data.hex()), ("missing field", "10 123")):
        try: parse_line(ln_); ok = False
        except ValueError: ok = True
        chk(ok, "refused: " + name)
    # the same bytes the C replayer would send: read the file made by packet_gen and compare the first and last packet
    import os, tempfile, subprocess
    here = os.path.dirname(os.path.abspath(__file__))
    with tempfile.TemporaryDirectory() as td:
        p = os.path.join(td, "p.txt")
        if subprocess.run([sys.executable, os.path.join(here, "packet_gen.py"), "demo", p], capture_output=True).returncode == 0:
            pk = load(p)
            chk(len(pk) == 57 and all(len(x[2]) == 48 for x in pk) and pk[0][1] == 0x123 and pk[-1][1] == 0x123, "the demo packet file from packet_gen.py loads: 57 packets of 48 bytes")
            chk(any(x[1] == 0x80000123 for x in pk) and any(x[1] == 0x124 for x in pk), "it contains the extended-id and wrong-id packets")
        else:
            print("  skip  packet_gen.py not runnable here")
    print("laptop_can_send selftest: %d failed" % bad)
    return 1 if bad else 0


def main():
    a = sys.argv[1:]
    if not a: print(__doc__); return 2
    if a[0] == "--selftest": return selftest()
    iface, dry = "can0", False
    while a and a[0].startswith("--"):
        if a[0] == "--iface" and len(a) > 1: iface = a[1]; a = a[2:]
        elif a[0] == "--dry-run": dry = True; a = a[1:]
        else: print(__doc__); return 2
    if len(a) != 1: print(__doc__); return 2
    pk = load(a[0])
    if dry:
        for d, cid, data in pk: print("%5d ms  id 0x%x  %s..." % (d, cid, data[:12].hex()))
        print("%d packets (dry run, nothing sent)" % len(pk)); return 0
    n = send(iface, pk)
    print("sent %d CAN-FD frames on %s (no key, no HMAC in this script)" % (n, iface)); return 0


if __name__ == "__main__":
    sys.exit(main())
