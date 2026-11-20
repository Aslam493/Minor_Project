# Recheck after the N-04 image (2026-11-21), commands used

1. Receiver (10.237.27.77): `/tmp/canfdcfg can0 bus` (static aarch64 build of tools/canmode/canfdcfg.c); sender (10.237.27.144):
   `sudo ip link set can0 down; sudo ip link set can0 type can bitrate 500000 dbitrate 2000000 fd on; sudo ip link set can0 up`.
2. Packets (laptop): 600 valid at 100 ms with counters 1..600, then one with a flipped tag bit (counter 601), made with
   `packet_gen.packet(key, 0x123, c, 0xC0DE0000|(c&0xFFFF))` and `bad_tag=True`, replayed on the sender by `laptop_can_send.py --iface can0`.
3. Receiver: `/tmp/can_forwarder run 78 --pull --verbose`, started 4 s before the sender; events read from the ev_seq recorded before.
Output: baseline/real_bus_recheck_after_n04_20261121.txt (+ .fwd.log). Result: 601 frames, 601 judged, only COUNTER_RESYNC and AUTH_FAIL.
