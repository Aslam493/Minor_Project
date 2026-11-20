#!/usr/bin/env python3
"""burst_packets.py - makes ONE burst packet file and, before anything is sent, the file of EXPECTED counts (run on the LAPTOP; the prototype K_frame is read
from the Secure World source by packet_gen.py and never written to a file).

  burst_packets.py OUTPREFIX --last L --n N          writes OUTPREFIX.txt (packet file, every delay 0 = "as fast as the sender can")
                                                          and OUTPREFIX.expected (key=value lines + the model prediction)

The file holds N valid packets with consecutive counters starting at L+1 (L = the monitor's last_counter) and FOUR planted frames and ONE jump:

  at 20 % of N   bad tag        counter c+1 with one flipped tag bit                  -> AUTH_FAIL            (no state change)
  at 40 % of N   old counter    valid packet with counter c-5                          -> REPLAY detail 1
  at 55 % of N   duplicate      the packet just sent, byte for byte                    -> REPLAY detail 2 (REPLAY class)
  at 70 % of N   one jump       the counters skip 2 (c, then c+3)                      -> COUNTER_GAP detail 2
  at 85 % of N   wrong ID       id 0x124, valid tag for that id                        -> MALFORMED            (no state change)

So the sender attempts N+4 frames; with nothing lost and in order, Secure World judges N+4 frames: AUTH_FAIL 1, REPLAY 2, MALFORMED 1, COUNTER_GAP 1,
COUNTER_RESYNC 1 only if L == 0 (first packet after a boot of the receiver), all the other valid packets ACCEPTED (N - 1 - RESYNC), last_counter ends at L+N+2.
"""
import json, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import packet_gen as g   # noqa: E402

# ---- model constants (design values, see MODE_P_PULL_DESIGN.md and canbridge.h) ----
FRAME_US = 290.0          # one 48-byte CAN-FD frame at 500k nominal / 2M data, estimated: ~34 nominal bits x 2 us + ~420 data-phase bits x 0.5 us
PQ_PERIOD_MS, PQ_BATCH, QCAP = 50.0, 32, 256


def payload(c):
    return 0xC0DE0000 | (c & 0xFFFF)


def build(key, L, N):
    a, b, d, j, w = (int(f * N) for f in (0.20, 0.40, 0.55, 0.70, 0.85))
    assert len({a, b, d, j, w}) == 5 and a >= 1 and N >= 100
    c = lambda i: L + i + (2 if i > j else 0)         # counter of valid packet number i (1..N)
    out = ["# burst: %d valid + bad tag@%d old counter@%d duplicate@%d jump@%d wrong id@%d (indices of the valid packets); every delay 0\n" % (N, a, b, d, j, w)]
    for i in range(1, N + 1):
        pk = g.packet(key, 0x123, c(i), payload(c(i)))
        out.append(g.line(0, 0x123, pk, "valid counter %d (#%d)" % (c(i), i)))
        if i == a:
            out.append(g.line(0, 0x123, g.packet(key, 0x123, c(i) + 1, payload(c(i) + 1), True), "one tag bit flipped, counter %d -> AUTH_FAIL" % (c(i) + 1)))
        elif i == b:
            out.append(g.line(0, 0x123, g.packet(key, 0x123, c(i) - 5, payload(c(i) - 5)), "valid counter %d, older than %d -> REPLAY detail 1" % (c(i) - 5, c(i))))
        elif i == d:
            out.append(g.line(0, 0x123, pk, "valid counter %d again (duplicate) -> REPLAY detail 2" % c(i)))
        elif i == w:
            out.append(g.line(0, 0x124, g.packet(key, 0x124, c(i) + 1, payload(c(i) + 1)), "valid counter %d but id 0x124 -> MALFORMED" % (c(i) + 1)))
    return out, c(N), (a, b, d, j, w)


def expected(L, N, last):
    resync = 1 if L == 0 else 0
    judged = N + 4
    return {"last_counter_start": L, "n_valid": N, "frames_attempted": N + 4, "frames_judged": judged,
            "AUTH_FAIL": 1, "REPLAY": 2, "MALFORMED": 1, "COUNTER_GAP": 1, "COUNTER_RESYNC": resync,
            "ACCEPTED": N - 1 - resync, "last_counter_end": last}


def model(N):
    """Prediction written BEFORE the run, from the design limits only: not a measurement."""
    burst_ms = (N + 4) * FRAME_US / 1000.0
    drained_during = min(N + 4, int(burst_ms // PQ_PERIOD_MS + 1) * PQ_BATCH)       # one reply of up to 32 per 50 ms request while the burst arrives
    queue_lost = max(0, (N + 4) - drained_during - QCAP)
    drain_ms = (((N + 4) - queue_lost) / PQ_BATCH) * PQ_PERIOD_MS
    return {"model_burst_on_bus_ms": round(burst_ms), "model_ceiling_fps_secure_world": int(PQ_BATCH * 1000 / PQ_PERIOD_MS),
            "model_drained_while_arriving": drained_during, "model_lost_at_forwarder_queue(256, drop oldest)": queue_lost,
            "model_judged": (N + 4) - queue_lost, "model_total_drain_ms": round(drain_ms)}


def main(argv):
    if len(argv) < 5 or "--last" not in argv or "--n" not in argv:
        print(__doc__); return 2
    pre = argv[0]; L = int(argv[argv.index("--last") + 1]); N = int(argv[argv.index("--n") + 1])
    key = g.key_from_source()
    lines, last, pos = build(key, L, N)
    open(pre + ".txt", "w").write("".join(lines))
    ex = expected(L, N, last); ex["planted_positions(a,b,d,j,w)"] = list(pos)
    with open(pre + ".expected", "w") as f:
        f.write("# EXPECTED counts for this burst, written BEFORE it is sent (lossless, in order)\n")
        for k, v in ex.items():
            f.write("%s=%s\n" % (k, v))
        f.write("# MODEL PREDICTION from the design limits (50 ms request, 32 per reply, queue 256, ~290 us per frame); not a measurement\n")
        for k, v in model(N).items():
            f.write("%s=%s\n" % (k, v))
    print(json.dumps(ex)); print(json.dumps(model(N)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
