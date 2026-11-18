#!/usr/bin/env python3
"""packet_gen.py - makes the packets BEFORE Linux is involved (on the laptop) and writes them to a packet file for can_replay.
The tag is HMAC-SHA256(K_frame, can_id|counter|payload) (little endian, 12 bytes); the frame data is counter|payload|tag|8 zero bytes (48 bytes).
The Pi never needs the key: it only replays the finished packets.

  packet_gen.py demo OUT                      one valid run, then one of each fault, then silence and recovery (the canbridge board test)
  packet_gen.py healthy OUT SECONDS PERIOD_MS START_COUNTER
  packet_gen.py --selftest                    checks the packing against known answers (the same ones the C host test uses)
Options: --key-file F (64 hex digits or 32 raw bytes)  default: read K_frame from the Secure World source (prototype key).
"""
import hashlib, hmac, os, re, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
KEYS = os.path.abspath(os.path.join(HERE, "../../../optee_full/optee_os/core/arch/arm/plat-rpi3/rpi3_can_monitor_keys.h"))


def key_from_source():
    t = open(KEYS).read()
    blk = t[t.index("RPI3_MON_KEY_FRAME_INIT"):]
    blk = blk[:blk.index("}")]
    k = bytes(int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", blk))
    assert len(k) == 32
    return k


def key_from_file(p):
    b = open(p, "rb").read()
    if len(b) == 32:
        return b
    h = b.decode().strip()
    assert len(h) == 64
    return bytes.fromhex(h)


def packet(key, can_id, counter, payload, bad_tag=False):
    msg = struct.pack("<III", can_id, counter, payload)
    tag = bytearray(hmac.new(key, msg, hashlib.sha256).digest())
    if bad_tag:
        tag[0] ^= 1
    return (struct.pack("<II", counter, payload) + bytes(tag) + bytes(8))      # 48 bytes


def line(delay_ms, can_id, data, note=""):
    return "%d %x %s%s\n" % (delay_ms, can_id, data.hex(), ("  # " + note) if note else "")


def demo(key):
    out = ["# packets made before Linux by packet_gen.py; DELAY_MS ID DATA(48 bytes)\n"]
    for c in range(1000, 1049):
        out.append(line(100, 0x123, packet(key, 0x123, c, 0xC0DE0000 | (c & 0xFFFF)), "valid %d" % c))
    n = 1049
    out.append(line(1000, 0x123, packet(key, 0x123, n, n), "valid %d" % n))
    out.append(line(400, 0x123, packet(key, 0x123, n + 1, n + 1, True), "one tag bit flipped -> AUTH_FAIL"))
    out.append(line(200, 0x123, packet(key, 0x123, n, n), "same counter again -> DUPLICATE"))
    out.append(line(200, 0x123, packet(key, 0x123, n - 5, n - 5), "older counter -> REPLAY"))
    out.append(line(200, 0x123, packet(key, 0x123, n + 10, n + 10), "counter jump of 10 -> GAP"))
    out.append(line(200, 0x124, packet(key, 0x124, n + 11, n + 11), "wrong CAN id -> MALFORMED"))
    out.append(line(200, 0x80000123, packet(key, 0x80000123, n + 11, n + 11), "extended id -> MALFORMED"))
    out.append(line(2500, 0x123, packet(key, 0x123, n + 11, n + 11), "after 2.5 s of silence (TIMEOUT), valid again -> TRAFFIC_RECOVERED"))
    return "".join(out)


def healthy(key, secs, per, start):
    out = ["# healthy run made before Linux by packet_gen.py\n"]
    for i in range(secs * 1000 // per):
        c = start + i
        out.append(line(per, 0x123, packet(key, 0x123, c, 0xC0DE0000 | (c & 0xFFFF))))
    return "".join(out)


def selftest():
    key = bytes(range(32))
    kat = [(0x123, 5, 0xC0DE0005, "0293467bec3088eb285b4c7aed6a508e13df9faca2cfc0771597e6db513fab88"),
           (0x123, 1000, 0xC0DE03E8, "6420560134140b2fe3bc1566308d6d03a9634b53d0a2eb3340827069f9fd015d"),
           (0x80000123, 7, 1, "00673e0606dd6d43140b2f0645170daeec39af2fc526981cf33ae8a4b83a09a8")]
    bad = 0
    for cid, c, p, tag in kat:
        d = packet(key, cid, c, p)
        ok = len(d) == 48 and d[8:40].hex() == tag and d[40:] == bytes(8) and d[:8] == struct.pack("<II", c, p)
        print("  %s  id 0x%x counter %d: tag equals the known answer, layout counter|payload|tag|pad" % ("ok  " if ok else "FAIL", cid, c))
        bad += not ok
    d = packet(key, 0x123, 5, 7, True); g = packet(key, 0x123, 5, 7)
    ok = sum(bin(a ^ b).count("1") for a, b in zip(d, g)) == 1
    print("  %s  bad tag flips exactly one bit" % ("ok  " if ok else "FAIL")); bad += not ok
    print("packet_gen selftest: %d failed" % bad)
    return 1 if bad else 0


def main():
    a = sys.argv[1:]
    if not a:
        print(__doc__); return 2
    key = None
    if "--key-file" in a:
        i = a.index("--key-file"); key = key_from_file(a[i + 1]); del a[i:i + 2]
    if a[0] == "--selftest":
        return selftest()
    key = key or key_from_source()
    if a[0] == "demo" and len(a) == 2:
        open(a[1], "w").write(demo(key)); print("wrote", a[1]); return 0
    if a[0] == "healthy" and len(a) == 5:
        open(a[1], "w").write(healthy(key, int(a[2]), int(a[3]), int(a[4]))); print("wrote", a[1]); return 0
    print(__doc__); return 2


if __name__ == "__main__":
    sys.exit(main())
