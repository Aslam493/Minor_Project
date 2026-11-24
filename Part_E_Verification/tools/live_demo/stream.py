#!/usr/bin/env python3
"""stream.py - runs on the SENDER Pi. ONE command that never stops: it sends a new signed CAN packet again and again (counter 1, 2, 3 ... and a
changing speed/rpm value in every packet, so no two normal packets are the same). You can slip an attack in at any time with a key.

    python3 stream.py                 10 packets per second until you press q; an attack every 2 to 3 s by itself (all six types in turn; change with --every 5-8)
    python3 stream.py --quiet         only valid (green) packets until you press an attack key
    python3 stream.py --rate 20       faster
    python3 stream.py --auto          also injects an attack every 5 to 8 seconds by itself (all six types in turn)

Keys while it runs:   t tamper    r replay (old packet again)   d duplicate (last packet again)   c conflict (same counter, other data)
                      w wrong id  j counter jump (skip 4)       a auto-attacks on/off             p pause    + - speed    q quit
Repeats are never continuous: after any attack the stream waits at least 1.5 seconds before the next one (that also matches the Secure World's
limit of one logged event per second and attack type, so every attack gets its own colour on the receiver).

The sender plays the car's trusted ECU, so it needs the key: stream_setup writes it into /tmp/demo/stream_cfg.json (RAM only, mode 600).
"""
import hashlib, hmac, json, math, os, random, select, socket, struct, sys, termios, time, tty
from collections import deque

HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import laptop_can_send as L                      # noqa: E402   (frame packing, already used by the file demos)

CFG = os.path.join(HERE, "stream_cfg.json"); STATE = os.path.join(HERE, "stream_state.json")
ID_OK, ID_BAD = 0x123, 0x124
COOLDOWN = 1.5
C = (lambda code, s: "\033[%sm%s\033[0m" % (code, s)) if sys.stdout.isatty() else (lambda code, s: s)
# one colour per packet type (the receiver uses the same colours): valid green, tampered red, replay orange, duplicate light blue, conflict purple, wrong id pink, gap yellow
GREEN, RED, ORANGE, BLUE, PURPLE, PINK, YEL, DIM = (lambda s: C("32", s)), (lambda s: C("1;31", s)), (lambda s: C("38;5;208", s)), (lambda s: C("38;5;75", s)), \
    (lambda s: C("38;5;135", s)), (lambda s: C("38;5;213", s)), (lambda s: C("1;33", s)), (lambda s: C("38;5;245", s))


def packet(key, can_id, counter, payload):
    msg = struct.pack("<III", can_id, counter, payload)
    return struct.pack("<II", counter, payload) + hmac.new(key, msg, hashlib.sha256).digest() + bytes(8)      # 48 bytes: counter, data, signature, zeros


def signal(c):                                      # a changing "speed" and "rpm" so that every packet carries different data
    speed10 = int(800 + 200 * math.sin(c / 25.0) + 15 * math.sin(c / 4.0))          # 0.1 km/h
    rpm50 = int(40 + 12 * math.sin(c / 9.0) + 3 * math.sin(c / 2.0))                  # rpm / 50
    return (speed10 << 16) | rpm50


def describe(data):
    c, p = struct.unpack("<II", data[:8])
    kmh = (p >> 16) / 10.0; f = max(0, min(12, int(kmh / 10.0 + 0.5)))
    return "ctr %-6d speed %5.1f km/h %s rpm %-5d tag %s.." % (c, kmh, "\u2588" * f + "\u2591" * (12 - f), (p & 0xFFFF) * 50, data[8:12].hex())


def main():
    rate = 10.0; auto = "--quiet" not in sys.argv
    lo, hi = (float(x) for x in sys.argv[sys.argv.index("--every") + 1].split("-")) if "--every" in sys.argv else (2.0, 3.0)      # seconds between automatic attacks          # automatic attacks are ON by default (--quiet = only valid packets until you press a key)
    duration = float(sys.argv[sys.argv.index("--duration") + 1]) if "--duration" in sys.argv else None            # for tests
    inject = sorted((float(a.split(":")[1]), a.split(":")[0]) for a in sys.argv[sys.argv.index("--inject") + 1].split(",")) if "--inject" in sys.argv else []   # e.g. t:5,r:9
    if "--rate" in sys.argv: rate = float(sys.argv[sys.argv.index("--rate") + 1])
    if not os.path.exists(CFG):
        print("No key file. On the laptop run:  python3 scripts/demo_stream_setup.py"); return 2
    cfg = json.load(open(CFG)); key = bytes.fromhex(cfg["key_hex"])
    c = cfg["start_counter"]
    if os.path.exists(STATE):
        try: c = max(c, json.load(open(STATE))["next"])
        except Exception: pass
    s = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW); s.setsockopt(L.SOL_CAN_RAW, L.CAN_RAW_FD_FRAMES, 1); s.bind(("can0",))
    hist = deque(maxlen=80); last = None; sent = attacks = 0; paused = False; pending = None; last_attack = 0.0
    next_auto = time.monotonic() + 2.0; deck = []
    fd = sys.stdin.fileno(); old = termios.tcgetattr(fd) if sys.stdin.isatty() else None
    if old: tty.setcbreak(fd)
    print("STREAM to the receiver: new packet every %.0f ms.  Keys: t r d c w j a p + - q   (auto-attacks %s)" % (1000.0 / rate, "ON" if auto else "off"))
    print(("AUTO-ATTACKS ARE ON: one attack every %.0f to %.0f seconds, all six types in turn (press a to switch off). " % (lo, hi) if auto else "No attack is sent until you press a key. ") +
          "To attack yourself: click THIS window first, then press t r d c w j.")
    print("colours: %s  %s  %s  %s  %s  %s  %s" % (GREEN("valid"), RED("t tampered"), ORANGE("r replay"), BLUE("d duplicate"), PURPLE("c conflict"), PINK("w wrong id"), YEL("j gap")))
    nxt = time.monotonic(); t0 = nxt
    try:
        while True:
            now = time.monotonic()
            if duration is not None and now - t0 >= duration: break
            while inject and now - t0 >= inject[0][0]:
                pending = inject.pop(0)[1]
            if now >= nxt and not paused:
                nxt += 1.0 / rate
                kind = None
                if pending and now - last_attack >= COOLDOWN: kind, pending = pending, None
                elif auto and now >= next_auto and now - last_attack >= COOLDOWN:
                    if not deck: deck = list("trdcwj"); random.shuffle(deck)          # every attack type once before any repeats
                    kind = deck.pop(); next_auto = now + random.uniform(lo, hi)
                label, col = None, GREEN
                if kind == "t":
                    d = bytearray(packet(key, ID_OK, c, signal(c))); d[4] ^= 1                # one data bit changed, the old signature stays
                    data, cid, label, col = bytes(d), ID_OK, "TAMPERED: one data bit changed, old signature", RED
                elif kind == "r" and len(hist) > 12:
                    old_c = random.choice(list(hist)[:-8])[0]; data, cid = [h for h in hist if h[0] == old_c][0][1], ID_OK
                    label, col = "REPLAY: old packet (counter %d) sent again" % old_c, ORANGE
                elif kind == "d" and last:
                    data, cid, label, col = last, ID_OK, "DUPLICATE: the last packet sent a second time", BLUE
                elif kind == "c" and last:
                    data, cid, label, col = packet(key, ID_OK, c - 1, signal(c - 1) ^ 0x00FF0000), ID_OK, "CONFLICT: same counter as before, other data", PURPLE
                elif kind == "w":
                    data, cid, label, col = packet(key, ID_BAD, c, signal(c)), ID_BAD, "WRONG ID 0x124 (not allowed on this link)", PINK
                elif kind == "j":
                    c += 4; data, cid, label, col = packet(key, ID_OK, c, signal(c)), ID_OK, "JUMP: 4 counters skipped (then normal again)", YEL
                    hist.append((c, data)); last = data; c += 1
                else:
                    kind = None; data, cid = packet(key, ID_OK, c, signal(c)), ID_OK; hist.append((c, data)); last = data; c += 1
                s.send(L.pack_frame(cid, data)); sent += 1
                if kind: attacks += 1; last_attack = now
                line = "t+%7.1f #%-6d id 0x%03X  %s" % (now - t0, sent, cid, describe(data))
                print((col(line + "   <-- " + label) if label else GREEN(line)))
                if sent % 20 == 0:
                    json.dump({"next": c}, open(STATE, "w"))
            r, _, _ = select.select([fd] if old else [], [], [], max(0.0, min(0.05, nxt - time.monotonic())))
            if r:
                k = os.read(fd, 1).decode(errors="ignore").lower()
                if k == "q": break
                elif k == "p": paused = not paused; print(DIM("-- paused --" if paused else "-- running --")); nxt = time.monotonic()
                elif k == "a": auto = not auto; print(DIM("-- auto attacks %s --" % ("ON" if auto else "off"))); next_auto = time.monotonic() + 2
                elif k == "+": rate = min(100.0, rate * 1.5); print(DIM("-- %.1f packets per second --" % rate))
                elif k == "-": rate = max(1.0, rate / 1.5); print(DIM("-- %.1f packets per second --" % rate))
                elif k and k in "trdcwj":
                    if pending or time.monotonic() - last_attack < COOLDOWN: print(DIM("-- wait: attacks are spaced at least %.1f s apart; queued --" % COOLDOWN))
                    pending = k
    except KeyboardInterrupt:
        pass
    finally:
        if old: termios.tcsetattr(fd, termios.TCSADRAIN, old)
        json.dump({"next": c}, open(STATE, "w"))
        print("\nstopped. packets sent: %d, attacks injected: %d, next counter: %d" % (sent, attacks, c))
    return 0


if __name__ == "__main__":
    sys.exit(main())
