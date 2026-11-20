#!/usr/bin/env python3
"""watch_packets.py - makes the numbered packet files used in WATCH_IT_YOURSELF.md (run on the LAPTOP; the prototype K_frame is read from the Secure World
source by packet_gen.py and is never written to any file: the files hold only counters, payloads and 12-byte tags).

  watch_packets.py OUTDIR --healthy-last L    [--attacks-last H]    see below
  watch_packets.py OUTDIR --attacks-last H                           only the attack files, starting from counter H

The monitor accepts a counter only if it follows the last accepted one, so the files are a SEQUENCE; run them in the order of their numbers:

  1_healthy.txt  600 valid packets, one per 100 ms (10/s, 60 s), counters L+1 .. L+600          (only with --healthy-last L)
  2_pause2s.txt  10 valid packets; after the 5th the sender is silent for 2 s, then the rest      -> traffic TIMEOUT (detail 1), then TRAFFIC_RECOVERED
  3_badtag.txt   1 packet with one flipped tag bit (counter = last + 1)                           -> AUTH_FAIL, the counter does not advance
  4_replay.txt   1 valid packet with an OLD counter (last - 5)                                    -> REPLAY, detail 1 (older counter)
  5_jump.txt     1 valid packet whose counter is last + 6 (5 frames skipped)                      -> COUNTER_GAP, detail 5 (= counter - last - 1)

L is the monitor's last_counter now (0 after a reboot: the first packet is then COUNTER_RESYNC). H is the counter the monitor will have when the attack
files are used: L + 600 if 1_healthy.txt is run to its end first, or a fresh reading of last_counter ("watch_live.sh attacks" does that).
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import packet_gen as g   # noqa: E402


def payload(c):
    return 0xC0DE0000 | (c & 0xFFFF)


def valid(key, c, delay, note=""):
    return g.line(delay, 0x123, g.packet(key, 0x123, c, payload(c)), "valid counter %d%s" % (c, (" " + note) if note else ""))


def main(argv):
    if len(argv) < 2 or argv[0].startswith("-"):
        print(__doc__); return 2
    out = argv[0]; a = argv[1:]
    def opt(name):
        return int(a[a.index(name) + 1]) if name in a else None
    L, H = opt("--healthy-last"), opt("--attacks-last")
    if L is None and H is None:
        print(__doc__); return 2
    key = g.key_from_source()
    os.makedirs(out, exist_ok=True)
    def write(name, lines):
        open(os.path.join(out, name), "w").write("".join(lines)); print("wrote %-14s %3d packet(s)" % (name, len(lines) - 1))
    cur = H
    if L is not None:
        write("1_healthy.txt", ["# 600 valid packets at 10/s, counters %d..%d\n" % (L + 1, L + 600)] + [valid(key, L + 1 + i, 100) for i in range(600)])
        if H is None:
            cur = L + 600
    if cur < 6:
        print("attacks need a last counter of at least 6 (run the healthy stream first); got %d" % cur); return 1
    pause = ["# 10 valid packets, 2 s of silence after the 5th\n"]
    for i in range(10):
        pause.append(valid(key, cur + 1 + i, 2000 if i == 5 else 100, "(2 s silence before this one)" if i == 5 else ""))
    write("2_pause2s.txt", pause); cur += 10
    write("3_badtag.txt", ["# one tag bit flipped, counter %d -> AUTH_FAIL\n" % (cur + 1),
                           g.line(100, 0x123, g.packet(key, 0x123, cur + 1, payload(cur + 1), True), "one tag bit flipped, counter %d -> AUTH_FAIL" % (cur + 1))])
    write("4_replay.txt", ["# old counter %d (last accepted %d) -> REPLAY detail 1\n" % (cur - 5, cur), valid(key, cur - 5, 100, "(older than the last accepted %d) -> REPLAY" % cur)])
    write("5_jump.txt", ["# counter %d after %d: 5 frames skipped -> COUNTER_GAP detail 5\n" % (cur + 6, cur), valid(key, cur + 6, 100, "(jump of 6 after %d) -> COUNTER_GAP" % cur)])
    print("sequence: files are for last_counter %d at the start of %s" % (cur - 10, "2_pause2s.txt"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
