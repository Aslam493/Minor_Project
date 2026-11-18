# Direct sender: the packets go from the laptop onto the CAN bus, into the Pi's CAN controller (the receiver)

**Design (user, 2026-11-18):** the laptop is the sender ECU. It makes the packet and puts it on the CAN bus. The MCP2518FD on the Pi's shield is only the **receiver**. The Pi (Linux, then Secure World) works on what the controller received. The file and `scp` and `can_replay` are not used.

```
LAPTOP: packet_gen.py -> packets.txt (kept on the laptop) -> laptop_can_send.py
   -> USB CAN-FD adapter -> CAN_H / CAN_L wires -> shield terminals -> MCP2518FD (receiver, normal mode, acknowledges)
   -> SPI -> Linux driver -> can_forwarder -> Secure World (as before)
```

## What you need (not available yet)
1. A **USB CAN-FD adapter** (for example CANable 2.0, PEAK PCAN-USB FD) that the laptop sees as a SocketCAN interface (the `gs_usb` driver). A classic-CAN-only adapter (8 bytes per frame, MCP2515-based boards) cannot carry the 48-byte frames.
2. Wires: adapter CAN_H to shield CAN_H, CAN_L to CAN_L, ground to ground.
3. **Termination:** 120 ohm between CAN_H and CAN_L at **each end** of the bus. Check whether the shield has a termination jumper or resistor (I have not checked your board) and whether the adapter has one.

## Setup (the user runs the `sudo` lines)
Laptop (adapter plugged in, it appears as `can0` or `can1`; name below as `can0`):
```
sudo ip link set can0 down
sudo ip link set can0 type can bitrate 500000 dbitrate 2000000 fd on
sudo ip link set can0 up
```
Pi (as root, after copying `canfdcfg`; no loopback, **not** listen-only):
```
/tmp/canfdcfg can0 bus            # 500 kbit/s nominal, 2 Mbit/s data, FD on, loopback off
```
**Both ends must use the same bit rates.** Listen-only must stay off: with one receiver on the bus, the sender gets its acknowledge from the Pi's controller. If the controller does not acknowledge, the adapter reports errors and goes bus-off.

## Test
1. Pi: `scripts/direct_bus_board_test.sh <pi-ip>` (starts the forwarder for 40 s and prints what arrived).
2. Laptop, within those 40 s: `python3 tools/monitor_client/packet_gen.py demo packets.txt` then `python3 tools/monitor_client/laptop_can_send.py --iface can0 packets.txt`.
3. Expect: the same 57 frames and verdicts as `baseline/precreated_packets_board_test_20261118.txt`, with the controller's `rx_packets` rising by the number of frames the forwarder submitted. Now the frames came over the wires.

## Troubleshooting
| Symptom | Likely cause |
|---|---|
| Adapter shows errors or bus-off, nothing arrives | no termination, CAN_H and CAN_L swapped, different bit rates, or the Pi's can0 is down or in listen-only |
| Only short frames arrive | the adapter or the Pi side is not in FD mode |
| Frames arrive with a bad tag | the file was made with a different key than Secure World's |

## Status
`laptop_can_send.py` and `canfdcfg ... bus` are written and checked on the laptop only (frame packing and file parsing: 13 checks; the tool builds without warnings for the laptop and the Pi). **Sending over a real adapter and the bus mode on the Pi are NOT tested: no adapter is available.**
