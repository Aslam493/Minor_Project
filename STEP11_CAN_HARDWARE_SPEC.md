# Step 11 — CAN Hardware Specification

Originally frozen while no CAN hardware was available. **Updated 16 Sept
2026**: the actual board is now identified and verified against this
project's kernel — see photos, confirmed as a Seeed Studio 2-Channel
CAN-BUS(FD) Shield (MCP2518FD, v2.0). This document is both the spec and a
practical step-by-step guide to install, bring up, and wire it into the
already-verified secure pipeline.

## The hardware

**Seeed Studio 2-Channel CAN-BUS(FD) Shield for Raspberry Pi, v2.0**
(`www.seeedstudio.com`, chip: **MCP2518FD**).

| Property | Value |
|---|---|
| Chip | MCP2518FD (CAN-FD capable, backward compatible with classic CAN) — newer/better than a plain MCP2515 |
| Form factor | Full 40-pin HAT — plugs straight onto the Pi's GPIO header, no manual wiring for the SPI/interrupt lines |
| Channels | 2 independent CAN buses: **CAN_0** and **CAN_1**, each with its own MCP2518FD chip |
| Interface | SPI0, using both native chip-selects (CE0 for CAN_0, CE1 for CAN_1) + GPIO25 (CAN_0 IRQ) / GPIO24 (CAN_1 IRQ) |
| Per-channel connections | Screw terminals: `_H`, `_L`, `GND` |
| Termination | A 3-way switch per channel: `OFF / 120R / ON` |
| Extra | An onboard RTC (PCF85063) via two JST "Grove"-style connectors (`GND/5V/SDA/SCL`) — unrelated to CAN, a bonus, safe to ignore |
| Power switch | Labelled **"Power to RPi: OFF / ON"** — see safety note below |

### Already verified present in this project's exact kernel checkout — no kernel rebuild needed
- Driver: `CONFIG_CAN_MCP251XFD=m`, already built (`mcp251xfd.ko.xz` already
  sits in `module_output`)
- The **exact matching overlay** already exists, pre-built:
  `firmware/boot/overlays/seeed-can-fd-hat-v2.dtbo`
- SocketCAN core already enabled: `CONFIG_CAN=m`, `CONFIG_CAN_RAW=m`,
  `CONFIG_CAN_DEV=m`

### Still needed (all safe — no TF-A/OP-TEE/boot-flow changes)
- Copy the `overlays/` directory onto the boot partition (currently not
  copied there)
- Add `dtoverlay=seeed-can-fd-hat-v2` to `config.txt`
- Enable `BR2_PACKAGE_CAN_UTILS` in Buildroot (gives you `candump`/`cansend`
  for testing)

## Physical installation

1. **Power off the Pi completely** — unplug it, don't just shut down Linux.
2. **Check the "Power to RPi" switch before connecting anything.** This
   shield can either draw power from the Pi, or feed its own power *to* the
   Pi through the 40-pin header. Since the Pi already has its own power
   supply in this project, **this switch should be OFF** — leaving it ON
   with the Pi also independently powered risks two power sources fighting
   on the same rail. If you're not certain which way is "off" on the
   physical switch, check for a printed label/silkscreen marking near it,
   or ask before powering on.
3. Align the shield's 40-pin socket with the Pi's GPIO header — pin 1 on
   the shield to pin 1 on the Pi (look for the square pad / marked corner
   on both) — and press down firmly and evenly until fully seated.
4. **Termination switches**: if you're bench-testing without a real second
   CAN device, the simplest setup is to wire **CAN_0** and **CAN_1**
   together directly (jumper `CAN_0_H` to `CAN_1_H`, and `CAN_0_L` to
   `CAN_1_L`) — this turns the two channels on the same board into a
   2-node bus you can test entirely on your desk. In that case, set
   **both** channels' termination switches to `ON` or `120R` (they're the
   two physical ends of that short bus). If you later connect to a real
   external CAN bus instead, only terminate the two actual physical ends
   of that bus, not both channels on this board.
5. Power the Pi back on.

## Software bring-up (do this before or after physical install — order doesn't matter)

1. Copy `firmware/boot/overlays/` (or at minimum `seeed-can-fd-hat-v2.dtbo`
   and `mcp251xfd.dtbo`) into `out/boot/overlays/` as part of the boot file
   assembly.
2. Add to `build/rpi3/firmware/config.txt`:
   ```text
   dtoverlay=seeed-can-fd-hat-v2
   ```
3. Enable in the Buildroot config: `BR2_PACKAGE_CAN_UTILS=y`.
4. Rebuild (`buildroot` → `update_bootfs` → `sdcard-image`), flash, boot.
5. Verify the interfaces exist:
   ```sh
   ip link show
   # expect to see can0 and can1
   ```
6. Bring the interfaces up (classic CAN, safe default bitrate):
   ```sh
   ip link set can0 up type can bitrate 500000
   ip link set can1 up type can bitrate 500000
   ```
7. Test with the loopback wiring from step 4 above:
   ```sh
   candump can1 &
   cansend can0 123#DEADBEEF
   # candump on can1 should print the frame that was sent on can0
   ```
   If that works, the shield, driver, and wiring are all confirmed good —
   completely independent of anything OP-TEE/secure-world related.

## How this plugs into the code already built and verified

**No Secure World / TA change is needed.** The already-verified pipeline
(commits `63528cb`, `b312392`, `00ca1b8`) works on any frame in this shape,
regardless of where it came from:
- `TA_SECURE_CAN_CMD_CLASSIFY_FRAME` — classifies by CAN ID (low ID = safety-critical = `SECURE`)
- `TA_SECURE_CAN_CMD_PROCESS_FRAME` — HMAC-SHA256 authenticates `SECURE` frames
- `TA_SECURE_CAN_CMD_VERIFY_FRAME` — independently verifies a tag, rejects tampering

The only change needed in `secure_can_demo/host/main.c` is **where frames
come from** — replace `fill_dummy_frame()`'s loop with a real SocketCAN
read:

```c
int s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
struct sockaddr_can addr = { .can_family = AF_CAN,
                             .can_ifindex = if_nametoindex("can0") };
bind(s, (struct sockaddr *)&addr, sizeof(addr));

struct can_frame cf;
while (read(s, &cf, sizeof(cf)) > 0) {
        struct secure_can_frame frame = { .can_id = cf.can_id };
        memcpy(frame.data, cf.data, SECURE_CAN_FRAME_DATA_LEN);
        /* same CLASSIFY_FRAME / PROCESS_FRAME / VERIFY_FRAME calls as
         * already verified, unchanged */
}
```

You can then reuse the loopback wiring (step 4) as a live demo: run the
updated `secure_can_demo` listening on `can0`, and `cansend can1 ...` from
a second terminal to inject real CAN frames that get classified,
authenticated, and verified for real.

## Completion checklist

- [ ] Shield physically seated, power switch confirmed OFF
- [ ] `overlays/` copied to boot partition, `dtoverlay=seeed-can-fd-hat-v2` added
- [ ] `BR2_PACKAGE_CAN_UTILS` enabled, image rebuilt and flashed
- [ ] `can0`/`can1` appear in `ip link show`
- [ ] `candump`/`cansend` loopback test passes between the two channels
- [ ] `secure_can_demo` host app updated to read `can0` instead of `fill_dummy_frame()`
- [ ] Full classify → authenticate → verify pipeline re-run against real CAN traffic, same PASS criteria as the simulated version
