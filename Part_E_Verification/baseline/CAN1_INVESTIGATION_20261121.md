# can1 investigation (read-only), 2026-11-21

**Question (user):** `can1` (the second MCP2518FD, SPI0 CE1) fails with "Failed to detect" on BOTH shields. Hardware fault, or configuration?

**Answer in one line:** the evidence points to **hardware (or board-level wiring/power of the second chip), not to the overlay or kernel configuration**. Software cannot prove which hardware cause it is; section 4 lists the physical checks that will.

Raw output: `baseline/can1_investigation_20261121.txt`. Probe script: `scripts/real_bus/spi_read_readonly.py`.

## 1. What was checked (nothing written to any card; no SPI traffic on the receiver)

| # | Check | Receiver (10.237.27.77) | Sender (10.237.27.144) |
|---|---|---|---|
| 1 | Device tree node `mcp251xfd@1` | reg 1 (CE1), interrupts GPIO 24 level-low, clock 40 MHz, spi-max 20 MHz, own pin group | identical (only phandle numbers differ) |
| 2 | SPI controller chip selects | `cs-gpios` = GPIO8 (CE0), GPIO7 (CE1), both active low | identical |
| 3 | Kernel result | `spi0.1`: "Failed to read Oscillator Configuration Register (osc=0x00000000)", `-ENODEV` | identical message |
| 4 | `spi0.0` (can0) on the same bus, same overlay | initialised normally | initialised normally |
| 5 | Shield documentation (`STEP11_CAN_HARDWARE_SPEC.md`) | CAN_0 = CE0 + GPIO25, CAN_1 = CE1 + GPIO24 | same: the configuration matches the documented wiring |
| 6 | Pin state | not read (no pinctrl tool on the Buildroot image; kept hands off SPI0, Secure World owns it) | GPIO7 and GPIO8 output high (idle chip selects), MISO low when undriven, GPIO24 and GPIO25 input and **high** |

## 2. The decisive test (sender only)

The generic `spidev` driver was attached to `spi0.1` for a few minutes through sysfs (runtime only, undone afterwards) and only the MCP2518FD **READ** instruction was sent, to six registers, at 0.5, 2 and 10 MHz and in SPI modes 0 and 3.

| Registers (hex values read) | Chip B, CE1 | Chip A, CE0 (positive control, same script) |
|---|---|---|
| OSC 0xE00 | 00000000 in all 6 settings | 60040000 (2 and 10 MHz) |
| IOCON 0xE04 | 00000000 in all 6 settings | 03000303 |
| C1CON 0x000 | 00000000 in all 6 settings | 60079804 |
| DEVID 0xE14 | 00000000 | 00000000 (rev 0.0; **not** a discriminator, my first remark about it was wrong) |

Chip A answers with real values through the identical method, so the probe works. Chip B returns all zeros at every speed and mode: **nothing drives the MISO line when CE1 is asserted.** Speed, SPI mode, driver, device tree and the chip-select polarity (the same as chip A's) are therefore not the cause. After the test `spi0.0` is bound to `mcp251xfd` again, `spi0.1` has no driver and no override, `can0` is `<FD,TDC-AUTO>` 500000/2000000 ERROR-ACTIVE with 0 errors.

## 3. Reading of the result

- Configuration error: **not supported** by any evidence. The node matches the vendor spec and the working node; both boards behave identically.
- The same silence on two shields means a common cause. Candidates (all hardware level, none provable from software): (a) CE1 (GPIO7 = header pin 26) does not reach the second chip's CS pin on this shield variant, (b) the second chip is unpowered, held in reset or has no running oscillator, (c) a solder bridge or jumper that enables CAN_1 is open on both boards, (d) the second chip is not populated.
- GPIO24 reads high on a pin whose default pull is down, so the interrupt net has a pull-up on the shield; that only shows the net exists, not that the chip is alive.

## 4. Checks that would settle it (need a meter or scope; not done)

1. With the shield off the Pi, continuity from the shield's header pin 26 (GPIO7, CE1) to the CS pin of the second MCP2518FD, and from pin 18 (GPIO24) to its INT pin.
2. 3.3 V at the second chip's supply pins; the 40 MHz oscillator running (scope).
3. Look for a solder jumper or switch for CAN_1 in the vendor schematic and on the board.
4. Scope on the second chip's CS while `sudo python3 scripts/real_bus/spi_read_readonly.py 1` runs on the sender (after `modprobe spidev` and binding as in the raw file): CS should go low at every read.

## 5. Proposed fix

No configuration change would help, so none is proposed. Not applied and not needed: any overlay edit. If check 1 shows CE1 is routed elsewhere, the overlay's `reg` and `cs-gpios` entry for chip B would be changed to the right GPIO, which would then be a proposal for you to approve.

## 6. What this means for the project

Design 2 / C7 (the second controller, "chip B") stays blocked until the board-level cause is found or a different second controller is used (the sender Pi's own HAT now works as an independent CAN node on the bus, which already gives a real second node for the tests in `REAL_BUS_RESULTS.md`).
