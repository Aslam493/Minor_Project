# HARDWARE_READINESS.md (Component 1, Subcomponent 1.6)

Date: 2026-11-06. Method: read-only. The Pi was queried over SSH (`/proc`, `/sys`, `dmesg`, and **read-only** `devmem` reads of the GPIO registers); the source was read for the console and pin facts; the earlier subcomponents supplied the CAN facts. Nothing was written to any register, nothing was changed.
Decision recorded from you: **no UART adapter is connected; the UART item is left open.**

## Summary

| Step | Item | Status |
|---|---|---|
| 1.6.1 | Raspberry Pi board | **DONE** (measured): Raspberry Pi 3 Model B |
| 1.6.2 | UART adapter | **NOT AVAILABLE, left open by your decision.** The wiring and the consequences are documented below |
| 1.6.3 | CAN controller 1 | **DONE** (from 1.2): `can0` works, `can1` dead |
| 1.6.4 | Sender hardware | **UNKNOWN, needs your answer** |
| 1.6.5 | 50 ms CAN-FD sender capability | **UNKNOWN**; the requirement is written down below |
| 1.6.6 | Second CAN board | **UNKNOWN, needs your answer**; the requirement is written down below |
| 1.6.7 | 3.3 V compatibility | **cannot be checked without the board**; the checklist is below |
| 1.6.8 | SPI and GPIO pins | **DONE** (measured): full pin map, free pins, constraints |

## 1.6.1 The board (measured)

| Item | Value | Source |
|---|---|---|
| Model | **Raspberry Pi 3 Model B** (not the B+) | `/proc/device-tree/model`, compatible `raspberrypi,3-model-b brcm,bcm2837` |
| CPU | 4 x Cortex-A53 (part `0xd03`); Linux lists 3 because core 1 is diverted | `/proc/cpuinfo`, Task 1 of 1.4 |
| Memory | 897,228 kB visible to Linux (1 GB minus the GPU and Secure World reservations) | `/proc/meminfo` |
| Serial / MAC | `00000000111e9802` / `b8:27:eb:1e:98:02` | `/proc/cpuinfo`, boot arguments |
| Boot arguments | `console=tty0 console=ttyS0,115200 root=/dev/mmcblk0p2 ...` | `/proc/cmdline` |
| `config.txt` | `enable_uart=1`, `kernel_address=0x02000000`, `device_tree_address=0x01000000` | read from the boot partition (mounted read-only, then released) |

### The microSD card in the Pi (identity, for the open "original or spare?" question)

| Field | Value |
|---|---|
| Name / type | `SD32G`, SD |
| Manufacturer id / OEM id | `0x000003` (SanDisk's code) / `0x5344` ("SD") |
| Serial | `0x3224d710` |
| Manufacture date | 07/2025 |
| CID | `0353445344333247853224d710019797` |
| Size | 62,333,952 sectors (29.7 GiB) |

The kernel sees this identity; a USB card reader on the laptop does not pass the CID through, so the laptop cannot tell the two cards apart by it. **Only you can say which card this is.** Suggested: label the cards physically, and write this serial next to the label of the card that is in the Pi.

## 1.6.2 UART (not available, left open)

What is known, so that connecting an adapter later is a five-minute job:

| Fact | Evidence |
|---|---|
| The Pi's header UART is the **mini UART**, on **GPIO14 (TXD, header pin 8)** and **GPIO15 (RXD, header pin 10)**, function `ALT5` | measured: `GPFSEL1 = 0x00012024`, GPIO14 and GPIO15 decode to `ALT5` |
| **TF-A, OP-TEE and Linux all use that same port** at 115200 | TF-A `rpi3_use_mini_uart()` returns true when GPIO14 is `ALT5` (`rpi3_common.c:110-112`), so it chose the mini UART; OP-TEE `CONSOLE_UART_BASE 0x3f215040` (`platform_config.h:44`); Linux `console=ttyS0,115200` |
| What you would see | the boot messages of TF-A (BL1, BL2, BL31), OP-TEE's `I/TC:` messages (including the FIQ handler's prints), U-Boot, and the Linux console, interleaved |
| A PL011 UART also exists (`ttyAMA1` at `0x3f201000`) but it is not on the header pins in this configuration | `dmesg`, GPFSEL |

Wiring (when an adapter is available):

| Adapter | Pi header |
|---|---|
| GND | pin 6 (any GND pin) |
| RX | pin 8 (GPIO14, the Pi's TXD) |
| TX | pin 10 (GPIO15, the Pi's RXD) |
| VCC / 5 V | **not connected** |

Use an adapter with **3.3 V logic** (CP2102 and FT232 boards usually have a jumper; set it to 3.3 V), 115200 baud, 8N1, no flow control. **Check physically that pins 6, 8 and 10 are reachable with the CAN shield mounted** (the shield is a full 40-pin HAT; I cannot see whether it has a pass-through header).

Consequences while there is no UART (these decide how careful the next components must be):

| Area | Effect |
|---|---|
| Any TF-A or early-boot change | cannot be diagnosed if it fails; the rule "never flash TF-A changes without UART" (project notes) stays in force |
| Component 3 option (a) (enlarge the Core 1 stack, keep FIQ unmasked) | needs a hang to be diagnosable, so it is **not recommended** without UART; option (b) (keep FIQ masked) does not need it |
| Monitor output (plan R11, UART events) | cannot be observed; the RAM event ring read through the PTA is the only view |
| Component 7 (chip B, bit-banged SPI on Core 1) | a fault there hangs Core 1 silently; riskier |
| OP-TEE's own boot messages (for example "Dynamic shared memory is disabled", `IMSG` from the FIQ handler) | cannot be read |

## 1.6.3 CAN controller 1

`can0` works: MCP2518FD on `spi0.0`, 40 MHz oscillator (measured, `dmesg`), 500000 bit/s classic CAN, supports loopback, listen-only and FD in the driver (1.2). **`can1` is dead** (`-ENODEV`, never detected). Shield: Seeed Studio 2-Channel CAN-BUS(FD) Shield v2.0 (from `STEP11_CAN_HARDWARE_SPEC.md`): two MCP2518FD, SPI0 with CE0/CE1, interrupts on GPIO25 (can0) and GPIO24 (can1), screw terminals H/L/GND per channel, a 3-way termination switch per channel (OFF / 120R / ON), a "Power to RPi" switch that must be OFF. The positions of those switches are not visible remotely; **check them by eye**.

Consequence of `can1` being dead: the shield alone has only one working CAN node, and in normal mode a lone node never gets an acknowledgement. Any real-bus operation needs at least one other node.

## 1.6.4 to 1.6.6 Sender and second board (unknown: your answers needed)

Questions:
1. Do you have a **second CAN board** (any CAN-FD capable controller)? Which model, which crystal?
2. Do you have something that can **send CAN-FD frames** (another board, a USB CAN-FD adapter on the laptop, a microcontroller)? Which model?
3. Do you have **a CAN transceiver and the wiring** to put a second node on the bus with the shield (twisted pair, a 120 ohm resistor at each end)?

What each component needs (from the plan):

| Component | Needs |
|---|---|
| C2 to C6 (Mode F, milestone M1) | a **frame source**. Software-injected frames and a generator or file source cover most tests; the **real-bus** tests need a sender node on the CAN bus |
| C7 (Design 2, milestone M2) | the sender **and** a second controller ("chip B") wired to the Pi's pins and listening on the same bus |
| C4 (timing) | a real sender with a fixed 50 ms period (loopback numbers are invalid) |

Requirements for the sender (plan rule R1 and item H3):
- CAN-FD, standard identifier `0x123`, 48 data bytes (DLC 14), bit-rate switching on (the data-phase rate is not yet chosen: **a decision for you**; today `can0` is set to 500000 bit/s and classic mode only).
- Data layout: counter (u32, little endian), payload (4 bytes), HMAC-SHA256 tag (32 bytes) over `id (u32 LE) ‖ counter (u32 LE) ‖ payload`, 8 padding bytes (ignored).
- A fixed period of 50 ms with the jitter measured (not assumed).
- Able to run the test flags later (bad tag, replay, skip, malformed, hide, stop).

Requirements for the second board (plan item H4, constraint C1):
- MCP2518FD (or a controller whose Linux driver exposes listen-only and FD), SPI interface with accessible pins, **3.3 V logic**.
- The crystal (40 MHz like the shield, or 20 MHz) changes the bit timing and the timestamp wrap (40 MHz wraps in about 107 s), so record it.
- Its CAN-bus side wired to the same bus as `can0`, with termination only at the two physical ends.

Possible sources of a sender (suggestions only, none chosen): a second Raspberry Pi with a CAN-FD HAT; a USB CAN-FD adapter on the laptop used through SocketCAN; a microcontroller board with a CAN-FD controller.

## 1.6.7 3.3 V compatibility (checklist; cannot be tested without the board)

- The Pi's GPIO is **3.3 V and not 5 V tolerant**. Every signal from a second board into a Pi pin must be 3.3 V logic (check the board's schematic or a level shifter; do not assume).
- Common ground between the boards is required.
- The CAN transceiver's own supply may differ (3.3 V or 5 V) from the controller's logic supply; the logic-side pins are what touch the Pi.
- The shield's "Power to RPi" switch must stay OFF while the Pi has its own supply.
- 120 ohm termination at the two ends of the bus only (the shield's switch on its end, the other board's resistor at the far end).

## 1.6.8 SPI and GPIO pins (measured)

Read-only `devmem` reads of the BCM2837 GPIO block (`GPFSEL0` `0x21200000`, `GPFSEL1` `0x00012024`, `GPFSEL2` `0x00000000`, `GPLEV0` `0x2300C1FF`, `GPLEN0` `0x02000000`), decoded by script:

| GPIO | Header pin | Function now | Level | Role |
|---|---|---|---|---|
| 7 | 26 | output | 1 | SPI0 CE1 chip-select, driven as a plain GPIO (can1, dead chip) |
| 8 | 24 | output | 1 | SPI0 CE0 chip-select, plain GPIO (can0) |
| 9 / 10 / 11 | 21 / 19 / 23 | ALT0 | 0 | SPI0 MISO / MOSI / SCLK |
| 14 / 15 | 8 / 10 | ALT5 | 1 | mini UART TXD / RXD (the console) |
| 24 | 18 | input | 1 | can1 /INT (the shield), level detect not enabled |
| 25 | 22 | input | 1 | can0 /INT (the shield), **low-level detect enabled** (`GPLEN0` bit 25) |
| 2, 3 | 3, 5 | input | 1 | I2C pins: unused (the shield's RTC is not enabled) |
| all other GPIO 0 to 27 | | input | | free |

This also confirms stage 2 of the interrupt chain from 1.4 on the live hardware: only GPIO25 has level detect armed, and its level is high (inactive).

Where a second controller (chip B) could go, using the measured state:

| Option | Pins | Constraints |
|---|---|---|
| Plan's first choice (bit-banged SPI) | GPIO5, 6, 12, 13 (pins 29, 31, 32, 33) | all free inputs now. GPIO5 and 6 share the function-select word `GPFSEL0` with the SPI0 chip-select pins GPIO7 and 8; GPIO12 and 13 share `GPFSEL1` with SPI0's MOSI and SCLK and with the UART pins 14 and 15. Setting their functions is a read-modify-write of a word that also holds pins that are in use, so it must be done once, carefully |
| Alternative that avoids those shared words | GPIO20 to 23, 26, 27 (pins 38, 40, 15, 16, 37, 13) | all free inputs now and in `GPFSEL2`, which holds no pin that is in use for a function (24 and 25 are plain inputs there) |
| Hardware SPI1 (auxiliary, function `ALT4`) | GPIO16 CE2, GPIO17 CE1, GPIO18 CE0, GPIO19 MISO, GPIO20 MOSI, GPIO21 SCLK = header pins 36, 11, 12, 35, 38, 40 | fallback in the plan only. The pin assignment is from the standard BCM283x multiplexing table, **not verified on this board**; all six pins are free inputs now (measured) |

The CAN shield covers the header. **Whether these pins are reachable with the shield mounted is physical and unknown to me**; a stacking header, a riser or soldering may be needed.

A relevant measurement: `/dev/mem` and `devmem` work for root on this Linux **for device registers** (the GPIO block was read), so a root Linux user can read (and, if it chose, write) the GPIO registers. For **RAM** the same tool is refused (`mmap: Operation not permitted`, `STRICT_DEVMEM`, measured 2026-11-06), so Secure World memory is not readable through `/dev/mem` on this kernel; that is a kernel setting, not hardware protection. That is why the plan states that chip B's pins are reachable by a compromised Linux and checks them every pass.

## What I need from you

| # | Question | Why it matters |
|---|---|---|
| 1 | Do you have a second CAN board? Model and crystal? | Component 7 (milestone M2) is impossible without it |
| 2 | Do you have a CAN-FD sender? Which? | real-bus tests and Component 4's timing need it |
| 3 | Which card is in the Pi, the original or the spare? Compare with the serial above | rollback safety (1.5) |
| 4 | Can you reach header pins 6, 8, 10 (and 29, 31, 32, 33) with the shield mounted? | UART and chip B wiring |
| 5 | The data-phase bit rate for the CAN-FD frames (the plan only fixes 500 kbit/s nominal) | Component 4 |

If answers 1 and 2 are "no", Components 2 to 6 (milestone M1) can still be built and tested with software-injected frames and a generator source; the report would then say that the real-bus and Design 2 parts are "designed, not tested", exactly as the plan allows.

## Problems or confusion avoided

| Item | What happened | How it was handled |
|---|---|---|
| "Is a USB-UART adapter plugged in?" | `lsusb` shows no CP210x, FTDI, CH340 or PL2303; no `/dev/ttyUSB*` | recorded as absent; your decision to leave it |
| Which pins carry the console? | easy to assume the PL011 UART | read the register and TF-A's own selection function: it is the mini UART on GPIO14/15 |
| Decoding the GPIO registers by hand | each register packs ten 3-bit fields | decoded with a script, not by eye |
| Which card is in the Pi | the laptop cannot see the card's CID | recorded the kernel's identity and asked you to compare with the labels |
| Termination and power switches | physical state not visible remotely | listed as "check by eye" |
