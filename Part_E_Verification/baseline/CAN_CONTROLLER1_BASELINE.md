# CAN_CONTROLLER1_BASELINE.md (Component 1, Subcomponent 1.2)

Date: 2026-11-06. Target: Raspberry Pi at 10.237.27.187 (SSH as root, key login), running the Sep-20 image.
Method: read-only commands over SSH. Nothing on the Pi was changed. No frame was sent.

Scope: prove that **Linux** sees the CAN controller and (Task 4) receives frames. This does not show that frames reach Secure World; that is Subcomponent 1.3.

Expected data path:

```
CAN controller (MCP2518FD) -> SPI0 (spi0.0) + mcp251xfd driver -> can0 -> Linux application
```

## Summary table

| Task | What | Result |
|---|---|---|
| 1 | Identify controller hardware | PASS |
| 2 | Identify SPI connection and driver | PASS |
| 3 | Verify can0 configuration | PASS with one gap (bitrate not directly readable) |
| 4 | Verify packet reception | **PENDING** (no traffic source; needs your decision) |
| 5 | Receive errors and driver logs | PASS, but weak evidence until Task 4 passes |
| 6 | Document the data path | Done below; reception still unproven |

## Task 1: controller identity

| Evidence (command) | Output | What it means |
|---|---|---|
| `ls -l /sys/class/net/can0/device` | `-> ../../../spi0.0` | can0 belongs to the SPI device `spi0.0`: SPI bus 0, chip-select 0 |
| `readlink -f .../device/driver` | `/sys/bus/spi/drivers/mcp251xfd` | the kernel driver is `mcp251xfd` |
| `cat .../device/uevent` | `DRIVER=mcp251xfd`, `OF_FULLNAME=/soc/spi@7e204000/mcp251xfd@0`, `OF_COMPATIBLE_0=microchip,mcp251xfd` | device-tree node `mcp251xfd@0` under the SPI controller |
| `dmesg` | `mcp251xfd spi0.0 can0: MCP2518FD rev0.0 (-RX_INT -PLL -MAB_NO_WARN +CRC_REG +CRC_RX +CRC_TX +ECC -HD o:40.00MHz c:40.00MHz m:20.00MHz ...) successfully initialized` | the chip is a Microchip **MCP2518FD** (CAN-FD controller), 40 MHz oscillator, SPI CRC and ECC enabled |

Board identification (Seeed CAN-BUS FD Shield v2) comes from `STEP11_CAN_HARDWARE_SPEC.md`, not from this probe. The probe proves the chip, not the shield brand.

## Task 2: SPI connection and driver

- SPI master: `spi0` = platform device `3f204000.spi`. In the device tree the same block is `spi@7e204000` (7e20_4000 is the bus address, 3f20_4000 the ARM physical address on the BCM2837).
- Two SPI devices are registered: `spi0.0` (works) and `spi0.1` (fails, see below).
- Loaded modules: `spi_bcm2835`, `mcp251xfd`, `can_dev`. Loaded at boot by `/etc/modules-load.d/can-shield.conf`.
- Device tree: `mcp251xfd@0` and `mcp251xfd@1` exist with `compatible`, `interrupts`, `spi-max-frequency`, `clocks`, `pinctrl`. The old `spidev@0/@1` nodes have `status = "disabled"`. (A node with no `status` property means "okay"; the shell errors about a missing `status` file are not faults.)
- `spi0.1` / `can1`: `Failed to read Oscillator Configuration Register (osc=0x00000000)` then `error -ENODEV`. Known dead chip, not a regression.

Why this matters later: the same chip behind `spi0.0` is also read directly by OP-TEE over SPI0 (Secure Peripheral Ownership). Linux and Secure World therefore share one SPI controller and one chip. The monitor design must not assume Linux has exclusive access.

## Task 3: can0 configuration

| Item | Value |
|---|---|
| operstate | `up` |
| type | 280 (ARPHRD_CAN) |
| mtu | **16** = classic CAN, not CAN-FD (an FD-enabled interface shows 72) |
| flags | `0x40081` = UP (0x1) + NOARP (0x80) + ECHO (0x40000). *Corrected 2026-11-06: an earlier version of this file said LOWER_UP; LOWER_UP is 0x10000 and is not in the sysfs value. BusyBox prints the unknown bit as `LOWER_UP40000`; the 40000 is IFF_ECHO.* |
| bitrate | **500000 bit/s, confirmed with `canmode`** (see below). Earlier gap closed |
| mode | normal (not loopback), per `S12can` |
| `/proc/net/can` | absent (kernel built without the CAN proc files). Not a fault, but it removes a place to inspect receive filters |

Consequence for the project: the monitor frames are CAN-FD (DLC 14, 48 bytes). With mtu 16, `can0` cannot carry them. Component 4 needs `fd on` (plus a data bitrate) before FD frames are valid evidence.

## Task 4: packet reception (PENDING)

| Check | Result |
|---|---|
| `candump -T 5000 -t A can0` (passive, 5 s) | no frames, exit 0 |
| `rx_packets`, `tx_packets`, `rx_errors`, `tx_errors`, `rx_dropped`, `tx_dropped` | all 0 |

Zero frames is expected: `can1` is dead and no external node is attached, so nothing transmits. A normal-mode controller with no other node cannot even complete a transmit (no ACK).

Options to complete Task 4 (needs your choice):

| Option | What it proves | Cost |
|---|---|---|
| A. Loopback on `can0` (`ip link set can0 down; ip link set can0 type can bitrate 500000 loopback on; ip link set can0 up`), then `candump can0 &` and `cansend can0 123#DEADBEEF` | whole Linux path: SPI, driver, CRC, socket, counters. Not the physical bus or transceiver | runtime change only; revert with `loopback off` or a reboot (`S12can` brings it up in normal mode) |
| B. External CAN node or sender | the physical bus path | needs hardware you may not have yet (Hardware readiness, 1.6) |

Recommendation: Option A now (it is the same state the demo already uses), then Option B when a sender exists.

## Task 5: errors and driver logs

- No `crc`, `bus-off`, `overflow` or `error` message for `spi0.0`/`can0` in `dmesg`. The only matches are the `can1` ENODEV lines and an unrelated `regulatory.db` Wi-Fi firmware message.
- Error counters are all 0.
- Caveat: with no traffic, a clean log is weak evidence. The historical CRC-error loop appeared only under sustained SPI traffic. Re-check after Task 4.

## Task 6: verified data path so far

```
MCP2518FD (spi0.0)  --SPI0 @ 3f204000.spi-->  mcp251xfd driver  -->  can0 (UP, classic, mtu 16)
   verified: chip detected, driver bound, interface up, no errors
   NOT yet verified: a frame actually arrives at a Linux application
```

## Problem -> solution log

Purpose: keep how each problem was found and fixed, so the same method can be reused. Format: symptom, what we checked, root cause, fix, lesson.

### A. Problems met during this session

| # | Symptom | Steps taken | Root cause | Fix | Lesson |
|---|---|---|---|---|---|
| A1 | `ip -details link show can0` printed only the BusyBox usage text | Compared with `ip -V`, found `/sbin/ip -> busybox` | Image uses BusyBox `ip`, which has no `-details` | Read `/sys/class/net/can0/*`, `/proc/net/dev`, `dmesg` instead | Check which tool implementation you have before relying on its flags |
| A2 | `timeout`, `pgrep`, `python3` "not found" on the Pi | Ran `command -v` for each | Minimal Buildroot image | Used `candump -T <ms>` for a time limit; avoided the others | Prefer a tool's own timeout flag on a minimal image |
| A3 | Earlier note said "Sep-20 image never booted" | Hashed partition 1 on the card (`dd if=/dev/mmcblk0p1 ... | sha256sum`) and the same range of the saved image | The note was an assumption, never tested | Both hashes equal (`6cef8675...`): the Pi runs the Sep-20 image. Plan notes corrected | Verify identity by hash, not by assumption |
| A4 | Output of a long command was cut at `head -120` and hid the tool check | Re-ran the missing part as a separate command | Pipe truncation | Split into smaller commands | Do not pipe a long diagnostic through `head` |
| A5 | "can't open .../status" while listing the device tree | Listed the node's properties | `status` absent = default "okay" | None needed | Absent property is not an error |
| A6 | `canmode` printed `ctrlmode SUPPORTED unavailable`, but the kernel source says the reply always contains it | Read `can_fill_info()`: the CTRLMODE_EXT nest is added unconditionally, so "unavailable" contradicted the source. Suspected my own tool first. Counted the enum in the header, then grepped the cross toolchain's header | The toolchain header (older) lacks `IFLA_CAN_CTRLMODE_EXT`, so my hand-written fallback `#define ... 18` was used; the real value is 17. The tool asked the kernel for the wrong attribute | Vendored the kernel's own `netlink.h`, built with `-I include`, removed the fallback, added `_Static_assert(IFLA_CAN_CTRLMODE_EXT == 17)`. Re-ran: SUPPORTED = `0x1b3` | A hard-coded constant for an interface you did not read is a guess. When a result contradicts the source, test your own code before blaming the system. A contradiction between tool and source was the signal |
| A7 | My first guard, `#ifndef IFLA_CAN_CTRLMODE_EXT / #error`, broke the build even with the right header | Read the compiler error | Enum members are not preprocessor macros, so `#ifndef` can never see them | Dropped it; the `_Static_assert` already fails the build if the name is missing (verified: a build without the vendored header fails with "undeclared") | A guard must be proven to fire and proven not to misfire |
| A8 | This file said flags `0x40081` included LOWER_UP | Compared bit values when decoding `0x500c1` from `canmode` against `0x40081` from sysfs | I mis-decoded: 0x40000 is IFF_ECHO, LOWER_UP is 0x10000 and sysfs does not show it | Corrected the Task 3 table | Decode flag words bit by bit against the header, not by eye |

### B. Earlier CAN-controller problems (from `KNOWLEDGE.md`, Incident 5 and its addenda)

| # | Symptom | How it was diagnosed | Root cause | Fix | Lesson |
|---|---|---|---|---|---|
| B1 | Black screen after rainbow splash when enabling the CAN overlay | Blamed the overlay first, twice. Then `diff -qr` of the good and failing boot partitions showed `armstub8.bin` differing; `fdtoverlay` merged the overlay offline and succeeded | A stale, uncommitted TF-A source (Incident 4) was silently rebuilt into `armstub8.bin`; the overlay was never reached | `git checkout --` the TF-A files, rebuild, check a byte diff of the binary | After a recovery in an external checkout, run `git status` there; do not trust the main repo's state |
| B2 | Overlay fix flashed but `can0` still absent; DT still showed `spidev@0` | Read `/proc/device-tree/...` on the running Pi, then grepped the U-Boot env | U-Boot loads the `.dtb` from the FAT partition; `dtoverlay=` in `config.txt` is never used in this boot chain | Merge the overlay into the `.dtb` at build time with `fdtoverlay` in `rpi3.mk` | Trace where the OS's device tree actually comes from |
| B3 | DT correct but `/sys/bus/spi/devices/` empty | Checked kernel config | `CONFIG_SPI_BCM2835=m`, never auto-loaded | `/etc/modules-load.d/can-shield.conf` lists `spi_bcm2835` and `mcp251xfd` | A DT node only says a device should exist; every driver in the chain must load |
| B4 | `can0` existed but `NOARP`, `qdisc noop` | Looked at link state | Nothing set a bitrate or brought the link up | `S12can` init script (bitrate 500000, `up`) | "Chip initialised" is not "link up" |
| B5 | `can0` detected at boot, then endless SPI CRC errors once up | Compared with `can1`; looked at physical mounting | Shield resting on bent-wire props, not seated in the header | Rigid remount; zero CRC errors afterward | Check physical seating before blaming code |
| B6 | `can1` `-ENODEV`, oscillator register reads 0 | Tried every reseat; `can0` on the same header worked | Faulty chip or solder joints on that channel | Treated as hardware replacement; use `can0` with loopback | Stop debugging software when only one of two identical channels fails |
| B7 | `secure_can_demo` timed out waiting for a frame | Remembered `can1` is dead, so nothing ACKs | Normal mode needs a second node | Run the demo with `can0` in loopback; deliberately not baked into `S12can` | Bench-only modes must not become silent defaults |

## Open items from 1.2

1. Task 4 decision (loopback now, external sender later).
2. Confirm the bitrate independently (needs iproute2 or a small netlink tool; BusyBox cannot show it).
3. Re-check `dmesg` for CRC errors after traffic.
4. `can0` is classic CAN (mtu 16); FD needs `fd on` in Component 4.

## Task 4 execution record

### Step 1: existing configuration (before any change) - DONE

Captured 2026-11-06 over SSH, read-only. The Pi clock reads 1970 (no RTC or NTP), so the Pi's timestamps are not wall-clock time.

```
$ ip link show can0
3: can0: <NOARP,UP,LOWER_UP40000> mtu 16 qdisc pfifo_fast qlen 10
    link/[280]
$ echo $?
0

$ ip -V
BusyBox v1.37.0 (2026-09-01 23:38:01 IST) multi-call binary.

Usage: ip [OPTIONS] address|route|link|tunnel|neigh|rule [ARGS]
...
ip link set IFACE [up|down] [arp on|off] [multicast on|off]
	[promisc on|off] [mtu NUM] [name NAME] [qlen NUM] [address MAC]
	[master IFACE | nomaster] [netns PID] [type TYPE ARGS]
...
$ echo $?
1
```

What this shows:

| Observation | Meaning |
|---|---|
| `ip -V` prints BusyBox usage and exits 1 | `ip` is BusyBox v1.37.0; there is no version flag and no `-details` |
| usage lists `ip link set IFACE ... [type TYPE ARGS]` | the syntax accepts a `type` argument, but the usage text does not say which types (can, bitrate, loopback) are handled |
| `/etc/init.d/S12can` line 15 runs `ip link set "$iface" type can bitrate "$BITRATE" && ip link set "$iface" up` | the image's own boot script already uses `type can bitrate` and `can0` came up, which is evidence (not proof) that this `ip` passes CAN options through |
| `can0` flags `NOARP,UP,LOWER_UP`, mtu 16, qdisc pfifo_fast | recorded start state to restore: **up, normal mode, classic CAN** |
| `link/[280]` and no mode/bitrate text | BusyBox cannot display the CAN mode or bitrate, so "loopback on" cannot be confirmed from `ip` output |

Risk found before Step 2: if this `ip` does not accept `loopback on`, the command will fail after `can0` has already been set down. Normal state is then restored with `ip link set can0 type can bitrate 500000` and `ip link set can0 up` (the same two commands `S12can` uses), or by rebooting.

Open question for Step 2: how to confirm loopback is really enabled when `ip` cannot show it. Candidates: the command's exit status plus a behavioural test (a frame is received with no other node), and the transmit-complete counter. A direct read of the controller's operating mode register (OPMOD) through the PTA `--read-reg` command is possible but touches Secure World code paths, so it is not used without approval.

Steps 2 to 5: not yet run.

### Issue 1 resolution: can BusyBox `ip` configure and show loopback? (2026-11-06, read-only)

Commands run on the Pi, with `can0` state recorded before and after.

| Check | Output | Meaning |
|---|---|---|
| `can0` before | flags `0x40081`, mtu 16, oper `up`, carrier_changes 1 | start state |
| `ip link set can0 type can help` | **exit 0, no output** | a full iproute2 prints the CAN options here. BusyBox printed nothing and returned success |
| `can0` after | identical to before | the command changed nothing |
| `which ip`, `ls -l` | `/sbin/ip -> ../bin/busybox` | `ip` is BusyBox |
| search for other `ip`, `ss`, `tc` binaries | none found; only BusyBox applets (`ipaddr`, `iplink`, `iproute`, `iprule`, `iptunnel`) | **no iproute2 on the image** |
| `/etc/init.d/S12can` | `ip link set "$iface" type can bitrate "$BITRATE" && ip link set "$iface" up` | the `&&` means `up` runs only if the first command succeeded |

Reasoning, with the source of each fact:

1. `help` giving no output shows that BusyBox does not interpret `type can help` the way iproute2 does. It does not tell us whether `loopback on` is accepted.
2. `can0` is nevertheless UP after a fresh boot (uptime 41 min, carrier_changes 1). The kernel refuses to open a CAN interface with no bit timing: `drivers/net/can/dev/dev.c:406` logs `bit-timing not yet defined` and fails. `dmesg` has no such message. So the `bitrate` option from `S12can` did reach the kernel. (Inference from the kernel source in `optee_full/linux`, assumed to be the source the running 6.7.0-rc6 kernel was built from.)
3. So BusyBox passes at least `bitrate` through. Whether it passes `loopback on` is **not established**. It is not read back by `ip`, because `ip link show` prints neither mode nor bitrate.
4. The driver supports it: `mcp251xfd-core.c:2072` lists `CAN_CTRLMODE_LOOPBACK` as supported, and line 656-657 maps it to the controller's **internal loopback** mode (`MCP251XFD_REG_CON_MODE_INT_LOOPBACK`, value 2). So a successful setting is a real controller mode, not a socket flag.
5. The driver sets `IFF_ECHO` (line 2061). Echoed copies of transmitted frames are created from the controller's transmit-event queue, i.e. only after the chip reports the frame as sent. In normal mode with no other node nobody ACKs, so no echo appears. So a frame showing up in `candump` is not a purely software artefact for this driver. If BusyBox silently ignored `loopback on`, the likely result is **no frame at all** (a failed test, not a false pass).

Decision table result:

| Finding | Resolution |
|---|---|
| BusyBox supports loopback | not established |
| BusyBox does not support loopback / iproute2 available | no iproute2 on the image |
| Config may work but mode cannot be read | **this is the situation** -> use an independent read-only method |

Chosen next step (needs approval): a small read-only SocketCAN netlink tool, `canmode`, cross-compiled on the laptop (toolchain `aarch64-none-linux-gnu-gcc` and `linux/can/netlink.h` are present), copied to `/tmp` on the Pi (nothing added to the SD image), that asks the kernel for `can0`'s bit timing and control-mode flags (`RTM_GETLINK`, attributes `IFLA_CAN_BITTIMING` and `IFLA_CAN_CTRLMODE`). It does not change anything. It closes two gaps at once: the unconfirmed bitrate and the unreadable loopback mode. It does not use the PTA, so Secure World stays out of the verification.

Prediction to test (not yet a result): with loopback on, one `cansend` should raise `tx_packets` by 1 and, if the chip also writes the frame to its receive FIFO, `rx_packets` by 1. A `rx_packets` increase is the evidence of controller-level reception; a `tx_packets`-only increase would show transmit completion only.


### Read-only utility `canmode` (built, reviewed, run) - 2026-11-06

Source: `Part_E_Verification/tools/canmode/canmode.c`, with the kernel's own `linux/can/netlink.h` vendored in `tools/canmode/include/`.
Build: `aarch64-none-linux-gnu-gcc -I include -O2 -Wall -Wextra -Werror -static` (also builds natively with `gcc` and passes `-Werror`).
Deployed to `/tmp/canmode` on the Pi (tmpfs, nothing added to the SD image). sha256 `34ade9d0...0b46f` on laptop and Pi (match).

Read-only audit of the source: the only request ever sent is `RTM_GETLINK`. No CAN socket, no `ioctl`, no file writes, no TEE headers.
Safety check: `can0` snapshot (flags, mtu, operstate, carrier_changes, rx/tx packets and errors) identical before and after the run.

Output on the Pi:

```
interface requested : can0        interface reported : can0       link kind : can
flags 0x500c1 (admin UP)          mtu 16                          operstate UP
nominal bit timing   500000 bit/s  sample-point 87.5%  tq 25 ns  prop_seg 34  phase_seg1 35  phase_seg2 10  sjw 5  brp 1
data bit timing      unavailable
can clock            40000000 Hz
controller state     ERROR-ACTIVE (0)
ctrlmode ACTIVE      0x00000000 <>
ctrlmode mask word   0x00000000 (kernel leaves this 0 in a GETLINK reply; not the supported set)
loopback disabled    listen-only disabled     FD mode disabled
ctrlmode SUPPORTED   0x000001b3 <LOOPBACK,LISTEN-ONLY,BERR-REPORTING,FD,FD-NON-ISO,CC-LEN8-DLC>
restart_ms 0 ms      error counters (TEC/REC) tx 0 rx 0
```

Other runs: `canmode eth0` -> "NOT A CAN DEVICE" (exit 2); `canmode can1` -> "no such interface 'can1'" (exit 1; `can1` never registered, consistent with ENODEV).

What this establishes (measured, not assumed):

| Fact | Evidence |
|---|---|
| `can0` bitrate is 500000 bit/s, sample point 87.5%, on a 40 MHz clock | `canmode` |
| `can0` is in normal mode: loopback, listen-only and FD all disabled | `ctrlmode ACTIVE = 0` |
| BusyBox `ip link set ... type can bitrate ...` really configures the kernel | the bitrate above came from the boot-time `S12can` |
| The driver supports **loopback**, **listen-only** and **FD** | `ctrlmode SUPPORTED = 0x1b3` |
| Controller is healthy: ERROR-ACTIVE, error counters 0/0 | `canmode` |

Consequences:
- The loopback experiment is possible at driver level. Whether BusyBox accepts the word `loopback on` is still to be tested; `canmode` can now show the result either way.
- Listen-only is supported, which Design 2 needs for chip B (Component 7).
- FD is supported by the driver, but FD mode needs a data bit timing too (`dbitrate`), which BusyBox may not be able to set. Check this in Component 4; it may need `canmode`-style netlink code for configuration.

Still open before the loopback test: nothing blocks it. Next is Task 4 Step 2 (enable loopback), verified by `canmode`.

### Task 4, Step 2: enable controller loopback - DONE (2026-11-06)

Commands (one SSH session, with an automatic fallback to normal mode if the set command failed; the fallback was not needed):

```
ip link set can0 down                                        rc=0
ip link set can0 type can bitrate 500000 loopback on         rc=0
ip link set can0 up                                          rc=0
```

Independent readback with `canmode` (the kernel's own answer, not BusyBox's):

| Item | Before | After |
|---|---|---|
| ctrlmode ACTIVE | `0x00000000 <>` | `0x00000001 <LOOPBACK>` |
| loopback | disabled | **ENABLED** |
| listen-only / FD | disabled / disabled | disabled / disabled |
| bitrate | 500000 bit/s, sample point 87.5% | 500000 bit/s, sample point 87.5% (unchanged) |
| controller state | ERROR-ACTIVE | ERROR-ACTIVE |
| operstate / mtu | up / 16 | up / 16 |
| rx, tx packets; rx, tx errors | 0, 0; 0, 0 | 0, 0; 0, 0 |
| new `dmesg` lines | n/a | none |

Conclusions:
- BusyBox `ip` **does** accept `loopback on` for CAN and passes it to the kernel. This closes Issue 1: no iproute2 is needed.
- Putting `bitrate` and `loopback on` in one command kept the bitrate; the readback shows 500000 unchanged.
- The kernel only accepts a control-mode change while the interface is down, so the readback is also evidence that the down/up sequence took effect.
- Observation, not explained: `carrier_changes` stayed at 1 across the down/up cycle. It does not affect the result (loopback is read back directly), but note it if the carrier counter is used as evidence later.

State now: **`can0` is in loopback mode.** It is left that way for Step 3. Restore command (Step 4): `ip link set can0 down; ip link set can0 type can bitrate 500000 loopback off; ip link set can0 up`, then confirm with `canmode`. A reboot also restores normal mode, because `S12can` brings `can0` up without loopback.

Steps 3 to 5: not yet run.

### Task 4, Step 3: transmit and receive a test frame - DONE (2026-11-06)

One scripted SSH session: `candump -t A can0` in the background (output to `/tmp/candump.out`), counters before, `cansend can0 123#1122334455667788`, 1 s wait, counters after, `dmesg` diff.

| Item | Before | After |
|---|---|---|
| `canmode` ctrlmode | `<LOOPBACK>`, loopback ENABLED | unchanged |
| rx_packets | 0 | **2** |
| tx_packets | 0 | **1** |
| rx_errors, tx_errors, rx_dropped, tx_dropped | 0, 0, 0, 0 | 0, 0, 0, 0 |
| controller state, TEC/REC | ERROR-ACTIVE, 0/0 | ERROR-ACTIVE, 0/0 |
| new `dmesg` lines | n/a | none (no CRC, bus, or SPI errors) |
| `cansend` exit code | n/a | 0 |

`candump` output (Pi clock reads 1970; only the difference matters):

```
(1970-01-01 00:53:00.606200)  can0  123  [8]  11 22 33 44 55 66 77 88
(1970-01-01 00:53:00.606242)  can0  123  [8]  11 22 33 44 55 66 77 88
```

Two identical frames, 42 us apart, matching the expected frame `can0 123 [8] 11 22 33 44 55 66 77 88`.

#### Why one transmit produced two frames and rx=2 (derived from the driver source in `optee_full/linux`, assumed to match the running kernel)

| Source line | Fact |
|---|---|
| `mcp251xfd-tef.c:114-117` | when the chip reports the transmit done (transmit event FIFO), the driver queues the echo of the sent frame into the RX-offload queue and does `tx_packets++` |
| `dev/rx-offload.c:53-62` | the poll loop does `rx_packets++` for **every** frame in that queue, echoes included |
| `net/can/af_can.c:256` | the CAN core adds its own software loopback copy **only if the interface lacks `IFF_ECHO`**. `can0` has `IFF_ECHO` (flag word `0x500c1`), so the core adds none |
| `mcp251xfd-core.c:656-657` | `loopback on` selects the chip's internal loopback mode, where the chip also writes the transmitted frame into its receive FIFO |

So the two frames are: (1) the **transmit-event echo**, created only after the chip reports the frame sent (`tx=1`, `rx` +1), and (2) the **receive-FIFO copy**, delivered by the chip's receive path (`rx` +1). One transmit makes only one echo, so the second frame, with no second transmit, can only have come from the chip's own receive path. The CAN core adds no software copy.

Acceptance criteria for Task 4:

| Criterion | Result |
|---|---|
| Controller loopback successfully configured | PASS (`canmode`: LOOPBACK) |
| Test frame transmitted | PASS (`cansend` rc 0, `tx_packets` 1) |
| Evidence of controller-level reception, not just software echo | PASS, by derivation: 2 frames and `rx_packets` 2 for 1 transmit, and the core's software loopback is off. Not measured directly (no register read; the PTA was not used) |
| No new driver errors or CRC errors | PASS (all error counters 0, no `dmesg` lines) |
| Normal mode restored | **pending, Step 4** |

Limits: this proves the Linux path through the SPI link, the driver, and the chip's internal receive path. It does **not** prove the CAN transceiver or the physical bus; that needs an external sender (Subcomponent 1.6). It also does not show that Secure World ever sees the frame (Subcomponent 1.3).

Optional negative control (not run): in normal mode with no other node nothing ACKs, so the same `cansend` should produce no frames. It would confirm the frames above come from loopback mode, but it can push the controller to ERROR-PASSIVE until reset, so it is only worth doing if you want it.

State now: **`can0` is still in loopback mode.** Step 4 (restore) and Step 5 (conclusion) pending.

### Task 4, Step 4: restore normal operation - DONE (2026-11-06)

```
ip link set can0 down                                         rc=0
ip link set can0 type can bitrate 500000 loopback off         rc=0
ip link set can0 up                                           rc=0
```

Readback with `canmode`, compared with the original read-only run at the start of Task 4:

| Item | Original (before any change) | After restore |
|---|---|---|
| ctrlmode ACTIVE | `0x00000000 <>` | `0x00000000 <>` |
| loopback / listen-only / FD | disabled / disabled / disabled | disabled / disabled / disabled |
| nominal bit timing | 500000 bit/s, 87.5%, tq 25 ns, brp 1 | identical |
| clock | 40000000 Hz | 40000000 Hz |
| controller state, TEC/REC | ERROR-ACTIVE, 0/0 | ERROR-ACTIVE, 0/0 |
| sysfs flags, mtu, operstate | `0x40081`, 16, up | `0x40081`, 16, up |
| new `dmesg` lines | n/a | none |
| CPU online / offline | `0,2-3` / `1` | `0,2-3` / `1` |
| Core 1 heartbeat | rising | rising (1338452966) |

One difference, expected: `rx_packets` is 2 and `tx_packets` is 1. Interface counters are **not** reset by down/up, so they keep the loopback test's history. Later tests must use before/after deltas, not absolute values.

Left on the Pi (RAM only, gone at reboot): `/tmp/canmode`, `/tmp/candump.out`. Nothing was written to the SD card.

### Task 4, Step 5: conclusion

Subcomponent 1.2 establishes, with evidence:

1. The CAN controller is a Microchip **MCP2518FD** on SPI0 chip-select 0 (`spi0.0`), driven by `mcp251xfd`, exposed as `can0` (500000 bit/s, 87.5% sample point, 40 MHz clock, classic CAN, mtu 16).
2. `can0` is healthy: ERROR-ACTIVE, error counters 0/0, no CRC, bus or SPI messages.
3. With controller loopback enabled, one transmitted frame produced `tx_packets` +1, `rx_packets` +2 and two identical frames in `candump`. From the driver source this is the transmit-event echo plus the chip's own receive-FIFO copy; the CAN core's software loopback is off because `can0` has `IFF_ECHO`.
4. Normal mode was restored and verified to match the original state.
5. The driver supports LOOPBACK, LISTEN-ONLY and FD (`0x1b3`).

Not established (carry forward):
- the physical bus and transceiver (needs an external sender, 1.6);
- that any frame reaches Secure World (1.3);
- FD operation (`can0` is classic; FD needs `fd on` plus a data bit timing; unknown whether BusyBox can set it, Component 4);
- `can1` stays dead (ENODEV).

## Subcomponent 1.2 ledger

| Task | Result |
|---|---|
| 1 Controller identity | PASS |
| 2 SPI connection and driver | PASS |
| 3 `can0` configuration (bitrate now measured) | PASS |
| 4 Packet reception (loopback, controller level, restored) | PASS, with the limits above |
| 5 Errors and driver logs | PASS (re-checked after traffic: clean) |
| 6 Data path documented | PASS |

Tool produced: `Part_E_Verification/tools/canmode/` (read-only; reusable in 1.7, Component 4 and Component 7).
