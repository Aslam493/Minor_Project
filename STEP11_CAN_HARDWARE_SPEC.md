# Step 11 — CAN Hardware Specification

Frozen while no CAN hardware was available, so Step 11's software side
(secure frame authentication) could proceed against a simulated source. This
document is what to buy and exactly how it replaces the simulation once it
arrives — no further Secure World changes are needed.

## Recommended hardware

**MCP2515 CAN controller + TJA1050/SN65HVD230 transceiver breakout board**
("MCP2515 CAN module"), connected to the Pi over SPI.

| Property | Value |
|---|---|
| Typical cost | $3–6 for a breakout board (widely available) |
| Interface to the Pi | SPI0 (`MOSI`/`MISO`/`SCLK`/`CE0`) + one GPIO for the controller's `INT` line |
| Linux driver | `mcp251x` — already in mainline Linux, exposed as `CONFIG_CAN_MCP251X` |
| Bus API | Linux SocketCAN (`PF_CAN`, `can0` interface) — standard, not project-specific |

### Why this over the alternatives
- **PiCAN2/PiCAN3 HAT** (same MCP2515 chip, packaged as a HAT with a DB9
  connector): easier physical assembly, no wiring, but 4–6x the cost and
  less flexible GPIO/case layout. Reasonable upgrade later; not required to
  start.
- **USB-CAN adapters** (e.g. common USB-to-CAN dongles): avoid GPIO/SPI
  wiring entirely, but add a second driver/permissions layer and are less
  representative of an embedded automotive-style deployment, which is the
  scenario this project is modeling.
- **A Pi with native CAN silicon**: the Raspberry Pi 3B/3B+ used throughout
  this project has no built-in CAN peripheral, so this isn't an option on
  the current board regardless.

The MCP2515 module is the standard, best-documented choice for Pi + CAN
projects, keeps the bill of materials near zero, and uses a driver already
built into the kernel this project already builds — no separate kernel work
needed to try it once the physical part is in hand.

## Wiring (Raspberry Pi 3B/3B+ 40-pin header)

| MCP2515 module pin | Pi pin (physical) | Function |
|---|---|---|
| VCC | 5V or 3.3V (check board — most breakouts accept 5V and have an onboard regulator; confirm before connecting) | Power |
| GND | GND (e.g. pin 6) | Ground |
| CS | GPIO8 / CE0 (pin 24) | SPI chip select |
| SO (MISO) | GPIO9 / MISO (pin 21) | SPI data in |
| SI (MOSI) | GPIO10 / MOSI (pin 19) | SPI data out |
| SCK | GPIO11 / SCLK (pin 23) | SPI clock |
| INT | Any free GPIO, e.g. GPIO25 (pin 22) | Controller interrupt — **Linux GPIO IRQ, not a Secure World interrupt**; this is ordinary Normal-World kernel driver plumbing, unrelated to the EL3/TrustZone interrupt work that broke boot in the Phase A incident (see `KNOWLEDGE.md`) |
| CANH / CANL | To the CAN bus / a second matching node for loopback testing | Bus lines |

A 120Ω termination resistor is needed at each physical end of the CAN bus
(most breakout boards have a jumper for this — enable it only on the two end
nodes of the bus).

## Kernel/Buildroot config needed once hardware is in hand

Two Buildroot/kernel options to enable (not yet enabled — do this when the
hardware physically arrives so it can be tested immediately, rather than
guessing at config now):

```text
BR2_PACKAGE_CAN_UTILS=y           # Buildroot: candump/cansend/cangen tools
CONFIG_CAN=y                       # Linux: SocketCAN core
CONFIG_CAN_MCP251X=y               # Linux: this project's specific controller driver
```

Also add a device-tree overlay entry (or dtoverlay in `config.txt`, mirroring
how Raspberry Pi OS enables it: `dtoverlay=mcp2515-can0,oscillator=8000000,interrupt=25`
— oscillator speed depends on the exact breakout board's crystal, check its
datasheet) so the kernel probes the controller at boot.

## How this plugs into the code already built and verified

This is the key point: **no Secure World / TA change is needed**. Tonight's
work (commits `63528cb`, `b312392`) already built and verified on real
hardware:
- `TA_SECURE_CAN_CMD_PROCESS_FRAME` — Secure World authenticates a
  `struct secure_can_frame` (`can_id` + 8 data bytes) with HMAC-SHA256.
- `TA_SECURE_CAN_CMD_VERIFY_FRAME` — Secure World independently verifies a
  claimed tag, constant-time, and correctly rejects tampered frames.

Both already take exactly the wire format a real CAN frame has (ID + up to 8
data bytes). The only change needed in `secure_can_demo/host/main.c` once
real hardware exists is **where frames come from**:

```c
/* Replace fill_dummy_frame()'s loop with real SocketCAN reads: */
int s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
struct sockaddr_can addr = { .can_family = AF_CAN,
                             .can_ifindex = if_nametoindex("can0") };
bind(s, (struct sockaddr *)&addr, sizeof(addr));

struct can_frame cf;
while (read(s, &cf, sizeof(cf)) > 0) {
        struct secure_can_frame frame = { .can_id = cf.can_id };
        memcpy(frame.data, cf.data, SECURE_CAN_FRAME_DATA_LEN);
        /* same PROCESS_FRAME / VERIFY_FRAME calls as tonight, unchanged */
}
```

Everything downstream of "a frame arrived" — the TA calls, the HMAC
authentication, the tamper-rejection test — carries over unchanged. This is
exactly why the Normal-World-simulated approach was worth building first: it
already validated the part of the design that's hard to get right (Secure
World crypto + a stack-size bug that hung the board, see `KNOWLEDGE.md`
Incident 3), on a part of the design that's easy to swap later.

## Completion checklist for when hardware arrives

- [ ] MCP2515 module physically wired per the table above
- [ ] `BR2_PACKAGE_CAN_UTILS`, `CONFIG_CAN`, `CONFIG_CAN_MCP251X` enabled and
      rebuilt
- [ ] Device-tree overlay added, `can0` interface appears (`ip link show`)
- [ ] `candump can0` shows traffic from a second bus node or a loopback test
- [ ] `secure_can_demo`'s host app updated to read `can0` instead of
      `fill_dummy_frame()`, everything else unchanged
- [ ] Re-run the same PASS/FAIL checks already verified tonight, now against
      real bus traffic
