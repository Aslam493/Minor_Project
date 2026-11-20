#!/usr/bin/env python3
"""burst_send.py - sends a packet file as FAST as it can (the delay column is ignored) and reports what happened to each send() call. Runs on the SENDER Pi.
Needs laptop_can_send.py (parser and frame packing) in the same folder; no key, no HMAC.

  burst_send.py FILE naive|retry [--iface can0]

  naive : a send() that fails with ENOBUFS (the interface's transmit queue is full) is NOT repeated: that frame is lost at the sender. This is what a plain
          burst loop does, and it is what the user-visible "sent by sender (and ENOBUFS)" measures.
  retry : an ENOBUFS is retried (after about 60 us) until the frame is accepted, so the sender runs at the speed the bus allows and the later stages
          (chip FIFO, driver, socket, forwarder, Mode P, Secure World) see the full rate.

Prints ONE json line: attempted, sent_ok, enobufs (ENOBUFS events), lost_at_sender (naive: = enobufs; retry: frames given up after 5 s), retries,
other_errors, duration_s, rate_fps (accepted frames per second), longest_stall_ms (the slowest single send), lost_indices (0-based positions in the file).
"""
import errno, json, os, socket, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import laptop_can_send as L   # noqa: E402


def main(argv):
    if len(argv) < 2 or argv[1] not in ("naive", "retry"):
        print(__doc__); return 2
    path, policy = argv[0], argv[1]
    iface = argv[argv.index("--iface") + 1] if "--iface" in argv else "can0"
    frames = [L.pack_frame(cid, data) for _d, cid, data in L.load(path)]
    s = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
    s.setsockopt(L.SOL_CAN_RAW, L.CAN_RAW_FD_FRAMES, 1)
    s.bind((iface,))
    ok = enobufs = retries = other = 0; lost = []; stall = 0
    t0 = time.monotonic_ns()
    for idx, f in enumerate(frames):
        t_f = time.monotonic_ns()
        while True:
            try:
                s.send(f); ok += 1; break
            except OSError as e:
                if e.errno != errno.ENOBUFS:
                    other += 1; lost.append(idx); break
                enobufs += 1
                if policy == "naive":
                    lost.append(idx); break
                retries += 1
                if time.monotonic_ns() - t_f > 5_000_000_000:
                    lost.append(idx); break
                time.sleep(0.00006)
        stall = max(stall, time.monotonic_ns() - t_f)
    t1 = time.monotonic_ns(); dur = (t1 - t0) / 1e9
    s.close()
    print(json.dumps({"policy": policy, "attempted": len(frames), "sent_ok": ok, "enobufs": enobufs, "lost_at_sender": len(lost), "retries": retries,
                      "other_errors": other, "duration_s": round(dur, 4), "rate_fps": round(ok / dur, 1) if dur > 0 else 0,
                      "longest_stall_ms": round(stall / 1e6, 3), "lost_indices": lost}))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
