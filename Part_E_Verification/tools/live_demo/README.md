# Live stream demo (infinite packets, coloured on the receiver)

Idea: the sender sends a NEW signed packet again and again (counter +1, changing speed/rpm, never the same packet twice); the receiver prints every packet and
colours it by what the Secure World decided: valid green, tampered red, replay orange, duplicate light blue, conflict purple, wrong id pink, counter gap yellow, bus events grey (the sender window uses the same colours). Attacks are slipped in by key press (or `--auto`), never continuously: at least 1.5 s apart.

| Piece | Where | File |
|---|---|---|
| infinite sender with attack keys | sender Pi | `stream.py` |
| live coloured viewer | receiver Pi | `packet_view.c` (build: `scripts/build_packet_view.sh`) |
| setup (key, files, counter) | laptop | `scripts/demo_stream_setup.py` |

Run: (laptop) `python3 scripts/demo_stream_setup.py`; (receiver) `/tmp/packet_view`; (sender) `cd /tmp/demo && python3 stream.py`.
Keys in the sender window: t tamper, r replay, d duplicate, c conflict, w wrong id, j counter jump, a auto-attacks on/off, p pause, + - speed, q quit.
Test options of stream.py: `--duration N` and `--inject t:5,r:8` (attack key at second N), used by the rehearsal.

How the colour is found (no change in the Secure World): every alarm is logged as an event with the packet's counter and the Secure World's time. The viewer stamps each
arrival with the same counter (CNTVCT), polls the events 4 times a second and matches by counter and time (tampered / wrong id: earliest packet with that counter;
replay / duplicate / conflict: latest). A packet without an alarm for 0.7 s is green. Limits: the Secure World logs at most one event per second per alarm type (the exact
counters in the status bar are always exact, and the stream spaces attacks 1.5 s apart); the colour appears about 0.7 s after the packet; a per-packet verdict decided inside
the Secure World would need a new Secure World image (not done).
Honest note: for this demo the sender's RAM holds the key (it plays the car's trusted ECU); `demo_stream_setup.py --remove-key` takes it away again. The receiver's forwarder has none.
Rehearsal 2026-11-24: 400 packets, 10 attacks (tamper, replay, duplicate, conflict, wrong id, jump, tamper, duplicate, conflict, replay) coloured on exactly the right packets;
2 minutes with `--auto`: 1200 packets seen, 16 attacks coloured, heartbeat unchanged (LATE_ANSWER 1 -> 1, 0 misses).
