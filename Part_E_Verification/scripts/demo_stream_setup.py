#!/usr/bin/env python3
"""demo_stream_setup.py - LAPTOP, once before the live stream demo (and again after a reboot of either Pi).
Puts the pieces in place (RAM only, nothing is written to a card):
  sender:    /tmp/demo/stream.py (+ laptop_can_send.py) and /tmp/demo/stream_cfg.json (the demo key, mode 600, and the counter to start from)
  receiver:  /tmp/packet_view (the live viewer) and /tmp/mon_client
The sender plays the car's trusted ECU, so for this demo its RAM holds the key; the receiver's forwarder (the postman) still has none.
  python3 scripts/demo_stream_setup.py            to start     |     python3 scripts/demo_stream_setup.py --remove-key     to take the key away again"""
import json, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "real_bus"))
import burst_run as B                      # noqa: E402
sys.path.insert(0, B.MC)
import packet_gen as g                     # noqa: E402
PV = B.PV; RX, SN = B.RX, B.SN
if "--remove-key" in sys.argv:
    B.ssh("sender", SN, "rm -f /tmp/demo/stream_cfg.json /tmp/demo/stream_state.json"); print("key removed from the sender's RAM"); sys.exit(0)
if not B.ssh("root", RX, "cat /sys/class/net/eth0/address").strip(): print("The receiver does not answer at %s." % RX); sys.exit(2)
if B.ssh("sender", SN, "echo ok").strip() != "ok": print("The sender does not answer at %s." % SN); sys.exit(2)
# a leftover stream or viewer from an earlier run would overlap with the new one (two streams with the same counters look like replays): stop them first
B.ssh("sender", SN, "pkill -f '[s]tream.py' 2>/dev/null; true"); B.ssh("root", RX, "kill $(ps | grep '[p]acket_view' | awk '{print $1}') 2>/dev/null; true")
pv = os.path.join(PV, "build_scratch/live_demo/packet_view")
if not os.path.exists(pv): print("packet_view is not built: see tools/live_demo/README (cross compile)"); sys.exit(2)
B.scp([pv, os.path.join(PV, "tools/monitor_client/mon_client")], "root", RX, "/tmp/"); B.ssh("root", RX, "chmod +x /tmp/packet_view /tmp/mon_client")
L = B.page0(B.ssh("root", RX, B.MCL + " status 0"))["last_counter"]
B.ssh("sender", SN, "mkdir -p /tmp/demo && rm -f /tmp/demo/stream_state.json")
cfg = os.path.join("/tmp", "stream_cfg_%d.json" % os.getpid()); json.dump({"key_hex": g.key_from_source().hex(), "start_counter": L + 1}, open(cfg, "w"))
B.scp([os.path.join(PV, "tools/live_demo/stream.py"), os.path.join(PV, "tools/monitor_client/laptop_can_send.py")], "sender", SN, "/tmp/demo/")
B.scp([cfg], "sender", SN, "/tmp/demo/stream_cfg.json"); os.remove(cfg); B.ssh("sender", SN, "chmod 600 /tmp/demo/stream_cfg.json; chmod +x /tmp/demo/stream.py")
if int((B.ssh("root", RX, "ps | grep -c '[u]sr/bin/can_forwarder'").strip() or "0")) == 0:
    print("note: the forwarder service is not running: start it with  /etc/init.d/S95can_forwarder start >/dev/null 2>&1 </dev/null (receiver)")
print("READY. The Secure World's last counter is %d, the stream will start at %d." % (L, L + 1))
print("  RECEIVER window:  /tmp/packet_view")
print("  SENDER window:    cd /tmp/demo && python3 stream.py      (keys: t r d c w j, a = auto attacks, p pause, + - speed, q quit)")
