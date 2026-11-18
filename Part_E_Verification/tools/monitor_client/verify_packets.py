#!/usr/bin/env python3
"""verify_packets.py - INDEPENDENT check of a packet file made by packet_gen.py (does not import it). For every line it recomputes the HMAC-SHA256 tag
over the first 12 bytes with the frame key and compares: lines whose note says valid/after/counter must verify, lines noted 'tag bit flipped' must NOT.
Also checks the layout: 48 data bytes, counter little-endian, 8 zero pad bytes. usage: verify_packets.py FILE [--key-file F]"""
import hmac, hashlib, re, struct, sys
KEYS = "../../../optee_full/optee_os/core/arch/arm/plat-rpi3/rpi3_can_monitor_keys.h"
def key():
    if "--key-file" in sys.argv:
        b = open(sys.argv[sys.argv.index("--key-file") + 1], "rb").read(); return b if len(b) == 32 else bytes.fromhex(b.decode().strip())
    t = open(KEYS).read(); t = t[t.index("RPI3_MON_KEY_FRAME_INIT"):]; t = t[:t.index("}")]
    return bytes(int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", t))
K = key(); assert len(K) == 32
n = bad = 0; counts = {"valid-tag": 0, "bad-tag": 0}
for ln, line in enumerate(open(sys.argv[1]), 1):
    if line.startswith("#") or not line.strip(): continue
    body, _, note = line.partition("#"); f = body.split()
    delay, cid, data = int(f[0]), int(f[1], 16), bytes.fromhex(f[2]); n += 1
    problems = []
    if len(data) != 48:
        problems.append("length %d" % len(data))
    else:
        msg = struct.pack("<I", cid) + data[0:8]                       # signed part: can_id(4) | counter(4) | payload(4) = 12 bytes
        good = hmac.compare_digest(hmac.new(K, msg, hashlib.sha256).digest(), data[8:40])
        expect_bad = "tag bit flipped" in note
        counts["valid-tag" if good else "bad-tag"] += 1
        if good == expect_bad: problems.append("tag %s but the note says %r" % ("valid" if good else "invalid", note.strip()))
        if data[40:48] != bytes(8): problems.append("pad bytes 40..47 not zero")
    if problems: bad += 1; print("line %d: %s" % (ln, "; ".join(problems)))
print("verify_packets: %d packets, %d with a valid tag, %d with an invalid tag, %d problems" % (n, counts["valid-tag"], counts["bad-tag"], bad)); sys.exit(1 if bad else 0)
