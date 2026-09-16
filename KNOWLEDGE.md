# Project Knowledge Base

This file records real incidents on this project, how they were diagnosed, and
how they were fixed — so a future problem that *looks* similar can be solved
the same way instead of being re-debugged from scratch.

Keep entries short and factual: **symptom → root cause → fix → prevention**.
Add a new entry every time something breaks and gets fixed for a non-obvious
reason. Do not delete old entries even after the code moves on; the reasoning
stays useful.

---

## Incident 1 — Phase A secure timer broke Raspberry Pi 3 boot (September 2026)

### Symptom
After Step 10 (the project-specific `secure_can_demo` TA) was verified working
and booting on the physical Pi, a new feature — a custom OP-TEE secure-world
timer/interrupt handler ("Phase A", meant as groundwork for independent
secure-world timing ahead of CAN work) — was added. After that, the SD card
stopped booting correctly. Five separate attempted fixes across five commits
(`3de1eb4` → `9006265`) tried to correct the secure interrupt routing and
timer arming logic. None of them restored a working boot.

### Root cause
The Phase A work touched low-level EL3/secure-world interrupt routing:

- `trusted-firmware-a/plat/rpi/common/rpi3_common.c` — changed
  `plat_ic_get_pending_interrupt_type()` and `plat_interrupt_type_to_line()`
  to route a new secure per-core local timer IRQ to `INTR_TYPE_S_EL1`/the IRQ
  line instead of TF-A's default (route everything to FIQ at EL3).
- `optee_os/core/arch/arm/plat-rpi3/secure_timer.c` (new file) — a per-core
  BCM2836 local timer handler, registered via `conf.mk`/`sub.mk` additions
  (`CFG_CALLOUT`, `srcs-y += secure_timer.c`).
- `build/rpi3/firmware/uboot.env.txt` — added `maxcpus=2` to the kernel
  bootargs (an attempt to work around core-routing issues by limiting Linux
  to 2 CPUs).
- `build/rpi3/firmware/config.txt` — added `hdmi_force_hotplug=1` /
  `hdmi_safe=1` (added during display debugging of the same incident).

Changing EL3 interrupt classification on real, undocumented-in-detail
BCM2837 hardware is exactly the kind of change that can silently wedge early
boot (before any UART/HDMI output exists to explain why) — and that's what
happened. No single one of the five follow-up fix attempts addressed the
actual defect; each was a guess without hardware-level UART evidence, because
**Step 8 in this project never actually captured a UART log** (the roadmap
even lists "UART capture pending"). Debugging EL3/secure-IRQ boot failures
essentially requires a UART capture — HDMI alone doesn't show TF-A/OP-TEE
early boot output reliably.

### Fix
Rather than keep guessing, the working tree was rolled back to exactly the
state that was verified working for Step 10 (7 Sept build):

1. In each of the three separate upstream git checkouts under `optee_full/`
   (`optee_os`, `trusted-firmware-a`, `build` — these are pinned upstream
   trees, not part of this repo's history, see `COMPONENT_REVISIONS.md`),
   ran `git status`/`git diff` to see exactly what was locally modified
   relative to the pinned revision.
2. Distinguished Phase-A-only changes from unrelated legitimate prior work:
   `build/rpi3.mk` and `build/kconfigs/rpi3.conf` also showed as "modified",
   but diffing them against `Build_Config_Patches/rpi3-step9-module-ssh.patch`
   showed they were **Step 9's SSH/overlay patch**, already tracked in this
   repo, unrelated to Phase A. Those were **kept**.
3. Reverted only the Phase A files:
   ```sh
   cd optee_full/optee_os
   git checkout -- core/arch/arm/plat-rpi3/conf.mk \
                    core/arch/arm/plat-rpi3/platform_config.h \
                    core/arch/arm/plat-rpi3/sub.mk
   rm -f core/arch/arm/plat-rpi3/secure_timer.c

   cd ../trusted-firmware-a
   git checkout -- plat/rpi/common/rpi3_common.c \
                    plat/rpi/rpi3/include/rpi_hw.h

   cd ../build
   git checkout -- rpi3/firmware/config.txt rpi3/firmware/uboot.env.txt
   ```
4. `secure_can_demo/host/main.c` and `secure_can_demo/ta/include/secure_can_ta.h`
   had also picked up Phase A test code (CPU pinning, a second pseudo-TA
   invocation) — reverted to the plain Step 10 challenge/response app.
   `secure_can_demo/ta/secure_can_ta.c` itself had never been touched by
   Phase A and needed no change.
5. Rebuilt in dependency order and reflashed. See "Rebuild procedure" below.
6. Verified byte-for-byte on the card and functionally over SSH on the real
   Pi (see "Verification checklist" below). The resulting host binary hash
   matched the *original* Step 10 build's recorded SHA-256 exactly
   (`4de3897e53b1515822881f140a922637a0e33ab99b0fb5045d0b10bc748ab548`),
   confirming a true, exact rollback rather than a "close enough" rebuild.

Phase A's commits (`3de1eb4`…`9006265`) were **not deleted** — they remain in
git history on this branch if the timer work is revisited later.

### Prevention / how to redo this kind of work more safely next time
- Before touching EL3/secure-IRQ code again, **get the UART capture working
  first** (wiring is documented in `RPI3_BOOT_TROUBLESHOOTING.md`). Without
  it you are debugging blind — this is what turned one bad change into five
  failed fix attempts.
- Do experimental secure-world platform changes on a separate branch, and
  keep the last-known-good `rpi3-sdcard.img` (or its SHA-256) recorded so a
  broken experiment can be identified and rolled back in minutes, not hours.
- Change one variable at a time (e.g. IRQ routing *or* the timer handler,
  not both in the same commit) so a boot break points at a single suspect.
- When a "verified" fix still doesn't boot, stop iterating blind and roll
  back to the last physically-confirmed-booting state before trying again.

---

## Incident 2 — `/dev/sda` silently became a regular file, not the SD card (September 2026)

### Symptom
`lsblk /dev/sda` reported `not a block device`, even though `/dev/sda1` and
`/dev/sda2` (the card's partitions) were normal, mountable block devices.
`file /dev/sda` showed a valid DOS/MBR boot sector — i.e. **old SD-card image
contents were sitting in a plain file named `/dev/sda`**, not on the real
device.

### Root cause
At some earlier point, a `dd if=... of=/dev/sda` (or similar) was run while
the real `/dev/sda` device node did not exist (card unplugged, or udev
hadn't created it yet). On Linux, `dd`/`cp` writing to a path that doesn't
exist under `/dev` (a tmpfs/devtmpfs) just **creates an ordinary file there**
instead of erroring — it does not fail loudly. Every subsequent "flash" that
targeted `/dev/sda` was silently writing into that stale file in RAM, never
touching the actual card. This is almost certainly why earlier "verified"
images never actually reached the physical SD card, even though `dd`
reported success each time.

### Fix
```sh
stat /dev/sda                     # confirms: regular file, not block special
cat /sys/class/block/sda/dev      # get the real device's major:minor (8:0 here)
sudo rm -f /dev/sda               # safe: only removes the stray tmpfs file,
                                   # the real card and /dev/sda1 /dev/sda2 are untouched
sudo mknod -m 660 /dev/sda b 8 0  # recreate the proper block special device
sudo chown root:disk /dev/sda
lsblk /dev/sda                    # should now show TYPE=disk, correct SIZE, RM=1
```
(`udevadm trigger` / writing to `.../uevent` did *not* recreate the node in
this environment — manual `mknod` with the major:minor from sysfs was what
worked.)

### Prevention
- **Before every flash**, run `file /dev/sdX` (not just `lsblk`) and confirm
  it says `block special`, not a filesystem/boot-sector description. A
  boot-sector description from `file` on a `/dev/*` path is itself the
  red flag that the "device" is actually a leftover file.
- Never write to a `/dev/sdX` path without having just confirmed it with
  `lsblk -o TYPE,RM,SIZE` immediately beforehand in the same terminal
  session — device names and node validity are not stable across unplug/
  replug or reboots.

---

## Incident 3 — Undersized TA stack hung the whole board during HMAC crypto (September 2026)

### Symptom
Added a new TA command (`PROCESS_FRAME`) that authenticates a simulated CAN
frame with HMAC-SHA256 via the GP TEE Crypto API, deployed it live over SSH
(app-layer files only, not a reflash), and ran it. The command never
returned. Within ~30s the whole board stopped responding to **everything**,
including plain ICMP ping — not just SSH. It stayed down for minutes with no
recovery, and needed a physical power cycle.

### Root cause
`secure_can_demo/ta/user_ta_header_defines.h` set `TA_STACK_SIZE (2 * 1024)`
— fine for the original TA (one XOR on a 32-bit value), far too small once
`compute_frame_tag()` was added: allocating a transient HMAC-SHA256 key
object, an operation handle, and local buffers overflowed the 2KB S-EL0
stack. Corrupting secure-world memory this way doesn't fail gracefully like
a normal TA error return — it took down the whole system, consistent with
corrupting state OP-TEE core (and therefore Linux, since Linux depends on
the same CPU/secure monitor) needed to keep running.

A live-flash side note: right before this, the SD card had been reflashed
*while the system was booted from it* (see Incident 2's flash procedure) and
the kernel had already logged an `mmc_rescan` hung-task warning during that.
That turned out to be a red herring — the actual fix (below) fully resolved
the hang on a cleanly, physically-reflashed card — but it looked like a
strong suspect at the time and was worth ruling out explicitly rather than
assumed away.

### Fix
```c
// secure_can_demo/ta/user_ta_header_defines.h
#define TA_STACK_SIZE (8 * 1024)   /* was (2 * 1024) */
```
Rebuilt the TA, rebuilt the full SD card image (not a live SSH push this
time — a clean physical reflash, to also rule out the mmc angle), and
verified on real hardware: the full round trip + 5-frame HMAC pipeline ran
twice in a row, tags were byte-identical between runs (HMAC is deterministic
for the same key+input, confirming the crypto path itself is correct, not
flaky), and the board stayed responsive (`uptime`/`dmesg` clean) throughout.

### Prevention
- **Any time a TA gains new local state — crypto operation handles, key
  objects, stack buffers — increase `TA_STACK_SIZE` deliberately, don't
  assume the old value still fits.** OP-TEE does not warn you at build time
  if a TA's stack is merely tight; it only shows up as a runtime corruption,
  and on this hardware that corruption isn't contained to the one TA
  session — it can take the whole board down.
- **After any change to secure-world code (TA or platform), test the new
  code path in isolation first**, not bundled behind an already-proven step
  in the same binary. Here the plain challenge/response ran fine before the
  new frame pipeline; a hang after that point should immediately point at
  the *new* code, not force re-litigating whether the whole binary is
  suspect.
- When remotely testing new secure-world code on hardware you can't see,
  use a short timeout and treat "no response in a few seconds" as a stop
  signal, not something to wait out for 30s+ — the faster you notice, the
  smaller the risk window before you'd otherwise pile on a second risky
  action.

---

## Incident 4 — Flipping the "primary CPU" constant hung boot completely, no UART needed to find why (September 2026)

### Symptom
On branch `step11-dual-core-worlds`, attempting to dedicate Core 0 to an
independent Secure World worker (Core 1 running Normal World, cores 2/3
excluded) by changing `RPI_PRIMARY_CPU` from `U(0)` to `U(1)` in
`trusted-firmware-a/plat/rpi/rpi3/include/platform_def.h`: after flashing
and powering on, the Raspberry Pi showed its normal GPU-firmware rainbow
splash for about a second, then went to a permanently black screen. No
green (SD activity) LED blinking at all after that point. No UART was
available to capture what happened at the exact failure point.

### Root cause
**Physical core 0 is hardware-mandated to be the one that starts executing
first on this SoC — no software constant changes that.** The GPU firmware
(`bootcode.bin`/`start.elf`, completely unmodified — which is exactly why
the splash screen still appeared normally) only powers on and releases
physical core 0 to run the ARM boot stub (`armstub8.bin`, i.e. TF-A's BL1).
Cores 1–3 stay inert in hardware until core 0's own boot code explicitly
wakes each one via a spin-table/mailbox address.

`RPI_PRIMARY_CPU` only controls what `plat_is_my_cpu_primary()` *reports*
when called — it does not change which physical core hardware actually
starts. Setting it to `1` meant that core 0 (the only core hardware ever
starts) called `plat_is_my_cpu_primary()` and got back "false" (0 ≠ 1), so
it took the *secondary* cold-boot path — straight into the holding-pen loop
(here, the new independent-counter loop) — instead of doing the primary
core's actual job: initializing the platform and releasing cores 1–3 from
their holding pens. Since nothing ever released core 1, core 1 never ran,
Linux never loaded, and core 0 spun forever incrementing a counter nobody
was ever able to read. This traces exactly to the observed symptom: the
GPU-firmware splash is unrelated code that still ran fine, and everything
after the handoff to TF-A simply never produced output again.

This was root-caused entirely from the symptom pattern and reading the
boot code afterward — no UART capture was needed for *this* specific bug,
because "brief GPU splash, then permanently nothing, ever" is a strong
enough signature on its own once you know which unmodified vs. modified
code runs at each stage.

### Fix (not yet attempted — recovery only, see below)
`RPI_PRIMARY_CPU` cannot be used to relocate which physical core boots
first on this hardware. The correct approach for the original goal (Core 0
does independent Secure World work, Core 1 runs Normal World) is
architecturally different: **keep `RPI_PRIMARY_CPU` at `0`** so core 0
keeps doing its real job (platform init, releasing cores 1–3 as normal),
and instead modify BL31's own hand-off logic (`rpi3_bl31_setup.c` /
`bl31_main`'s final jump to BL33) so that, at the very end, core 0
explicitly releases core 1 into the BL33/Linux entry point (reusing the
existing hold-pen release + `sev()` mechanism already used for waking any
secondary core) and only *then* diverts itself into the independent worker
loop — rather than trying to make a non-primary core pretend to be
primary. This is a different, more surgical insertion point than a single
constant, in `bl31`-level C code rather than the early assembly helpers.

### Recovery
Immediate physical reflash of the last known-good backup, taken
deliberately before this attempt:
```text
SD_Card_Backups/pre-dual-core/rpi3-sdcard-known-good-classification.img
SHA-256: e247e459ad2e4ae6ee550a363755d6798fd10b4114e5f0101e46ef8de33acb6b
```
Flashed, verified byte-for-byte (`cmp`), and confirmed booting normally
again. This is exactly why a known-good backup was taken *before* this
attempt (see "Rebuild procedure" below) rather than only relying on git —
the physical card needed restoring, not just the source tree.

### Prevention
- **Before changing any "which core does X" constant, verify what the
  underlying hardware — not just the software abstraction — actually
  guarantees.** TF-A's `plat_is_my_cpu_primary()` is a *report*, not a
  *cause*; changing it doesn't change hardware reset behavior.
- **Always keep a known-good physical image backup before any boot-flow
  change**, separate from git commits. Git preserves the *source*, but
  recovering a bricked-looking board needs the *flashed artifact* on hand
  immediately, not a rebuild-from-source under time pressure.
- Attribute-level symptom reading (what ran vs. didn't, based on what code
  was and wasn't touched) can root-cause an EL3/cold-boot failure without
  UART, if you're precise about which stages are provably unmodified.

---

## Incident 5 — CAN shield overlay hung boot via an invalid device-tree label (September 2026)

### Symptom
Enabling `dtoverlay=seeed-can-fd-hat-v2` in `config.txt` for the Seeed
CAN-BUS FD Shield (see `STEP11_CAN_HARDWARE_SPEC.md`) produced the exact
same failure signature as Incident 4: brief GPU-firmware rainbow splash,
then a permanently black screen, no SD-card (green LED) activity
afterward. This was unexpected — a `dtoverlay=` line is normally one of
the safest, most standard pieces of Raspberry Pi firmware behavior, with
no relation to TF-A/OP-TEE/boot-flow code at all.

### Root cause
The stock overlay (`firmware/boot/overlays/seeed-can-fd-hat-v2.dtbo`,
from `arch/arm/boot/dts/overlays/seeed-can-fd-hat-v2-overlay.dts`) has 10
fragments. Fragments 0–7 add the two MCP2518FD CAN controllers and are
fine. Fragments 8–9 add the shield's bonus onboard RTC chip (unrelated to
CAN) and target a device-tree label called `i2c_arm`:
```c
fragment@9 {
	target = <&i2c_arm>;
	...
```
**That label does not exist in this project's kernel.** `i2c_arm` is
defined only in the Raspberry Pi Foundation's downstream kernel fork
(`arch/arm/boot/dts/broadcom/bcm270x-rpi.dtsi`, not part of this build).
This project deliberately builds **mainline Linux** (see
`COMPONENT_REVISIONS.md`), whose actual base tree
(`bcm283x.dtsi`/`bcm2835-rpi.dtsi`) only defines `serial0`/`serial1` in
`aliases` — no `i2c_arm` anywhere. Confirmed directly by grepping the
compiled tree's source rather than guessing. With an unresolvable target
label, the firmware's device-tree overlay merge at boot could not
complete, which — on this firmware/hardware combination — manifested as a
hang rather than a graceful skip-and-continue.

### Fix
Verified the CAN-only fragments (0–7) target `spidev0`, `spidev1`, `spi0`,
and `gpio` — all confirmed present and valid in this tree — before relying
on them. Created a trimmed overlay,
`seeed-can-fd-hat-v2-nortc-overlay.dts`, keeping only fragments 0–7 and
dropping the RTC fragments entirely (the RTC was already documented as
"unrelated to CAN, safe to ignore" in the hardware spec — losing it costs
nothing). Compiled with the kernel's own toolchain, replicating its exact
build recipe read from the existing `.cmd` file rather than guessing dtc
flags:
```sh
$CC -E -nostdinc -I./scripts/dtc/include-prefixes -undef -D__DTS__ \
  -x assembler-with-cpp -o out.tmp seeed-can-fd-hat-v2-nortc-overlay.dts
./scripts/dtc/dtc -@ -H epapr -O dtb -o out.dtbo -b 0 \
  -i arch/arm64/boot/dts/overlays/ <same -Wno-* flags as the stock overlay> out.tmp
```
Decompiled the result and grepped for `i2c_arm` to confirm it's gone and
the `__fixups__` list only references the four valid labels, before
wiring it into `config.txt`
(`dtoverlay=seeed-can-fd-hat-v2-nortc`) and rebuilding.

### Prevention
- **A `dtoverlay=` addition is not automatically risk-free just because
  it's "normal Raspberry Pi firmware behavior."** Overlays distributed for
  the stock Raspberry Pi Foundation kernel can reference labels/aliases
  that don't exist in a different (e.g. mainline) kernel build. Before
  trusting a third-party overlay, grep the actual compiled device-tree
  source for every label it targets — don't assume compatibility from the
  overlay working elsewhere.
- When an overlay bundles unrelated hardware (here: CAN + a bonus RTC),
  consider trimming it to only the fragments actually needed — smaller
  surface area, fewer chances for an unrelated part to break the whole
  merge.
- The same recovery discipline as Incident 4 applied here and worked
  immediately: a known-good backup image, taken before the risky change,
  flashed straight back the moment the symptom appeared, no time lost
  debugging a live "why won't it boot" on hardware you need working.

### Addendum — this fix alone did not actually solve it

Flashing the corrected overlay produced the **exact same boot failure
again**. The overlay fix above was real (confirmed independently, see
below) but incomplete as an explanation — there was a second, unrelated
bug still present.

**The real cause**: Incident 4's `RPI_PRIMARY_CPU=1` change was still sitting
**uncommitted in the separate `trusted-firmware-a` checkout**. Incident 4's
recovery only restored the curated *binary* (`armstub8.bin`) tracked in
git — it never reverted the actual TF-A *source* files
(`platform_def.h`, `plat_helpers.S`, `rpi3_pm.c`). Every later
`update_bootfs` (including this CAN shield work, since it depends on the
`tf-a` target) silently **rebuilt `armstub8.bin` from the still-broken
source**, overwriting the earlier restoration without any explicit sign
that it had happened. Both "CAN shield" boot failures were actually
Incident 4's bug recurring, not the overlay at all — the overlay was never
reached, because boot hung before Linux (and therefore before any overlay
processing) even ran.

**Found by**: refusing to keep guessing at the overlay a third time, and
instead diffing every file in the known-good and failing boot partitions
directly (`diff -qr`) — which showed `armstub8.bin` differing, despite
nothing in the CAN shield work having any reason to touch TF-A. Also used
`fdtoverlay` to apply the fixed overlay to the real base `.dtb` completely
offline (no hardware needed) and confirmed a clean merge with all
phandles resolved — proving the overlay itself was fine before looking
elsewhere.

**Actual fix**: `git checkout --` the three TF-A files in that separate
checkout back to their committed originals, rebuild `tf-a` explicitly,
and *verify* the resulting `armstub8.bin` — a byte-level diff against the
known-good baseline showed exactly 20 differing bytes, all inside the
embedded build-timestamp string, confirming the binaries are now
functionally identical.

**Prevention, the part that actually mattered**: when a risky change lives
in a separate, uncommitted checkout (not the main tracked repo), "restore
the curated binary artifact" is not the same as "revert the source." Any
later rebuild that target depends on will silently regenerate the broken
binary from the still-dirty source. After recovering from an incident in
an external checkout, always `git status`/`git diff` that checkout
directly and confirm it's clean — don't infer it from the main repo's
state.

### Second addendum — `dtoverlay=` never worked in this boot chain at all

With the TF-A bug actually fixed, boot succeeded — but `can0`/`can1` still
never appeared, and `/proc/device-tree/soc/spi@7e204000/` on the running
Pi still showed the plain `spidev@0`/`spidev@1` nodes, not
`mcp251xfd@0`/`@1`. The overlay was never being applied at all, regardless
of its content.

**Root cause**: `build/rpi3/firmware/uboot.env.txt` loads the device tree
like this:
```text
load_fdt=run select_fdt; fatload mmc 0:1 ${fdt_addr_r} ${fdtfile}
```
U-Boot loads the `.dtb` **file directly from the SD card's FAT boot
partition**. `dtoverlay=`/`device_tree_address=` in `config.txt` only
affect what the *GPU firmware* (bootcode.bin/start.elf) does before U-Boot
even runs — and this project's boot chain never uses that copy. Compounding
this: TF-A is built with `RPI3_PRELOADED_DTB_BASE=0x00010000` while
`config.txt` sets `device_tree_address=0x01000000` — two different
addresses that were never meant to refer to the same thing here. `dtoverlay=`
is simply not a mechanism this boot architecture honors, independent of
whether the overlay itself is correct.

**Fix**: don't rely on runtime overlay application at all — merge the
overlay into the shipped `.dtb` at **build time** instead, using the same
`fdtoverlay` tool already used to verify the merge offline. Added to
`build/rpi3.mk`'s `update_bootfs` target:
```make
$(LINUX_PATH)/scripts/dtc/fdtoverlay -i $(LINUX_DTB_RPI3_B) \
	-o $(BOOT_PARTITION_FILES)/bcm2710-rpi-3-b.dtb \
	$(RPI3_STOCK_FW_PATH)/boot/overlays/seeed-can-fd-hat-v2-nortc.dtbo
```
(and the same for the B+ variant). Removed the now-inert `dtoverlay=` line
from `config.txt` and the now-unnecessary `overlays/` directory on the boot
partition entirely — the merge is baked into the one `.dtb` file U-Boot
already loads, so nothing else needs to change. Verified before flashing:
decompiled the shipped `.dtb` and confirmed `spidev@0`/`@1` show
`status = "disabled"` and `mcp251xfd@0`/`@1` have fully resolved phandles
(not placeholder values) for `clocks`/`interrupt-parent`/`pinctrl-0`.

**Prevention**: `dtoverlay=` being "standard Raspberry Pi firmware
behavior" says nothing about whether a *particular* boot chain actually
uses the copy that mechanism produces. Before relying on it, trace where
the OS's device tree pointer actually comes from (here: grep the U-Boot
env for `load_fdt`) rather than assuming the GPU-firmware-merged copy is
the one that matters.

### Third addendum — the SPI controller driver never auto-loaded

With the DTB fix in place, boot succeeded and the live device tree
correctly showed `mcp251xfd@0`/`@1` with `spi0` `status = "okay"` — but
`can0`/`can1` still didn't appear, and `/sys/bus/spi/devices/` was
completely empty even though `/sys/bus/spi/drivers/mcp251xfd` existed.

**Root cause**: `CONFIG_SPI_BCM2835=m` — the SPI *controller* driver
(distinct from the `mcp251xfd` CAN chip driver) is built as a loadable
module, not built-in. Nothing else in this project ever used SPI before
tonight, so nothing ever triggered loading it. The CAN driver itself was
loaded and ready, but with no SPI master registered, there was no bus for
it to find devices on.

**Fix**: `modprobe spi_bcm2835` (then `mcp251xfd`, in that order) made
`can0` appear immediately, with real hardware detected:
```text
mcp251xfd spi0.0 can0: MCP2518FD rev0.0 (...) successfully initialized.
```
Made permanent via Buildroot's existing `/etc/init.d/S11modules` +
`/etc/modules-load.d/*.conf` mechanism (already used by this rootfs, just
never needed before) — added
`build/br-ext/board/rpi3/overlay/etc/modules-load.d/can-shield.conf`
listing both modules, one per line, no init script changes needed.

**Known follow-up, partially resolved**: `can0` (`spi0.0`) detected real
hardware successfully; `can1` (`spi0.1`) did not —
`error -ENODEV: Failed to detect MCP251xFD` with the oscillator register
reading back all zeros, the classic "nothing responded on this SPI chip
select" signature. Likely a physical wiring/seating issue specific to
that channel rather than anything software-side, since it's the exact
same driver code that worked for channel 0.

Follow-up investigation (17 Sept 2026) found the shield was resting on
improvised bent-wire "supports" rather than being rigidly seated flush
against the Pi's GPIO header. This caused a second, distinct symptom on
`can0`: it would detect the chip fine at boot, then fall into a
continuous SPI CRC-error loop (`CRC read error ... data=00 00 00 00
00 00 00 00, CRC=0x0000`, never recovering) the moment sustained SPI
traffic started (i.e. as soon as the interface was brought `up`). After
remounting the shield rigidly (no wire props) and rebooting, `can0` came
up with **zero CRC errors**, and a live loopback test (`ip link set can0
type can bitrate 500000 loopback on`, then `cansend`/`candump`) round-
tripped a real frame through actual SPI hardware cleanly. **`can0` is
now confirmed solid.**

`can1` remains completely dead through every reseat/remount attempted so
far — unlike `can0`, it has never once detected the chip, even
transiently. This points to a fault on that specific chip/its solder
joints (not the shared header/mounting, since `can0` on the same header
now works reliably) — most likely requires board-level rework or
replacement to fix. Not currently blocking project progress: the
project's real-CAN-hardware requirement is satisfiable with `can0` alone
using internal loopback mode, without needing a second physical node.

**Prevention**: a device tree node existing and being `status = "okay"`
only means the *kernel* knows the device *should* exist — it says nothing
about whether every driver in the dependency chain (here: bus controller
→ chip driver) is actually loaded. When a device doesn't show up despite
a correct device tree, check `/sys/bus/<bus>/devices/` and
`/sys/bus/<bus>/drivers/` separately — an empty devices list with a
present driver directory means the *bus itself* never registered
anything, which points at the bus controller driver, not the device
driver you were focused on.

### Fourth addendum — `can0` detected hardware but never came up (no bitrate, no `up`)

With modules auto-loading (third addendum) and `can1` set aside as a known
hardware issue, `can0` still showed as `NOARP` with `qdisc noop` after
boot — the interface existed and the chip was initialized, but nothing
ever set a bitrate or brought the link up, so `secure_can_demo` had no
usable SocketCAN interface to attach to.

**Root cause**: nothing in the boot chain ever called `ip link set can0
type can bitrate ... up`. `S11modules` only loads kernel modules; bringing
up a CAN link is a separate step SocketCAN always requires manually (it's
not DHCP-style auto-configuring).

**Fix**: added `build/br-ext/board/rpi3/overlay/etc/init.d/S12can`, run
right after `S11modules` in the init sequence, which brings up `can0`
(and `can1`, once its hardware is fixed) at 500000 bps per
`STEP11_CAN_HARDWARE_SPEC.md`. It's written to skip an interface that
isn't present (`can1` today) rather than fail boot, since `can1` isn't
expected to exist until the wiring issue is fixed. Verified by copying
the script to the running device over SSH and confirming `start`/`stop`/
`restart` all behave correctly before baking it into the image.

**Prevention**: a chip driver reporting successful init
(`mcp251xfd ... successfully initialized`) only proves the SPI
transaction layer works — it says nothing about the SocketCAN network
link state, which always needs an explicit bitrate + `up` regardless of
how correctly the lower layers are wired.

### Fifth addendum — `secure_can_demo` now reads real frames from `can0`, needs loopback mode

`secure_can_demo` (`host/main.c`) no longer builds its test frames purely
in memory — it now transmits each one over a real `AF_CAN`/`SOCK_RAW`
socket bound to `can0` and reads it back over that same socket before
handing it to Secure World, exercising the real SPI/driver/hardware path
on every frame instead of simulating it.

Since `can1` is still dead (see above), there is no second physical node
on the bus to ACK a real transmission, so **`can0` must be in
controller-level loopback mode** for this to work at all — otherwise
`secure_can_demo` will time out after `SECURE_CAN_RECV_TIMEOUT_SECONDS`
(2s) waiting for a frame that never arrives, with a message pointing at
this. Enable it before running the demo:
```sh
ip link set can0 down
ip link set can0 type can bitrate 500000 loopback on
ip link set can0 up
```
This is deliberately **not** baked into `S12can` (which brings `can0` up
in normal, non-loopback mode) — loopback is a bench-test-only stand-in
for `can1`, and should not become the silent default in case `can1` gets
fixed/replaced or a real external bus is connected later, either of
which needs loopback back off.

**Verified working end to end (17 Sept 2026)**: with loopback enabled,
`secure_can_demo` sent all 5 test frames over real SPI to the MCP2518FD
and read them back over genuine hardware, with the TA correctly
classifying, authenticating, and verifying each one (including
rejecting a tampered replay) — zero CRC errors throughout the run. This
closes the project's last major gap: real CAN hardware is now proven
into the secure pipeline, not just simulated in memory.

### Sixth addendum — real, provable Normal/Secure World parallelism (no boot-flow changes)

The project's roadmap (`RPI3_SECURE_CAN_FULL_ROADMAP.md`, Step 14+) calls
for Secure World to eventually own the CAN/SPI/GPIO hardware directly and
run independently of any Normal World call. An earlier attempt at a
lighter version of this — running Secure and Normal World simultaneously
on dedicated cores — is what caused **Incident 4** above: flipping
`RPI_PRIMARY_CPU` to move which core does what broke boot outright, since
that constant only changes what TF-A *reports*, not which physical core
the SoC actually releases first.

Instead of resuming that boot-flow surgery, `secure_can_demo` now
demonstrates the same "independent and parallel" property entirely in
userspace, with no TF-A/BL31 changes at all: `main()` pins itself to CPU
0 and runs the real CAN→Secure World pipeline there, while a second
pthread is pinned to CPU 1 and runs a fully independent Normal-World-only
loop (no TEE calls) for the pipeline's whole duration. Both streams print
timestamps sharing the same `CLOCK_MONOTONIC` origin, so their log lines
provably interleave in real time rather than just looking concurrent.
This works because a Secure World call (SMC) only ever blocks the *core
that issued it* — other cores keep running Normal World Linux code the
entire time, which is standard, safe OP-TEE/TrustZone behavior on this
already-SMP-capable kernel (confirmed: `nproc` reports 4, all cores
`0-3` online).

Verified on hardware: `[core 1]`'s independent worker printed 8 messages
(every 20ms) continuously interleaved with `[core 0]`'s real frame
classify/authenticate/verify calls, only stopping after the pipeline
fully completed — proof the two genuinely ran concurrently, not
sequentially.

**Prevention**: "make Secure and Normal World run in parallel" does not
require changing which core boots what (that's what bricked boot in
Incident 4). If the goal is just to *show* independent parallel
execution — as opposed to Step 14's full goal of Secure World owning the
peripheral with zero Normal World involvement — ordinary per-thread CPU
affinity is sufficient and carries none of the boot-flow risk.

### Seventh addendum — Secure Peripheral Ownership, Phase 1: OP-TEE core talks to the CAN chip directly (real threat-model fix)

The actual security gap in this project up to this point: Normal World
(Linux) received every CAN frame first via ordinary SocketCAN, and only
*afterward* asked Secure World "is this SECURE or not?" via
`classify_frame()`. Since TrustZone assumes Normal World may be
compromised, a malicious Normal World could simply skip that call
entirely — Secure World had no independent path to the hardware. This
addendum starts fixing that for real, per roadmap Step 14
("Define Secure Peripheral Ownership"): OP-TEE core must be able to talk
to the CAN chip directly, with no Linux driver involved at all.

**No existing infrastructure to build on**: this platform (`plat-rpi3`)
had zero GPIO/SPI driver code in OP-TEE - `core/drivers/bcm_gpio.c` and
`core/pta/bcm/*` are for an unrelated Broadcom "iProc" server-SoC family
that only happens to share the "bcm" name; nothing there is reusable for
the BCM2835 on this board. Everything below is from-scratch.

**What was built**: a new pseudo-TA, `core/pta/rpi3_can_spi_poc.c`
(UUID `9a986af1-...`, gated by `CFG_RPI3_CAN_SPI_POC_PTA`), implements a
polled, from-scratch BCM2835 SPI0 driver and reads the MCP2518FD's
Oscillator Configuration Register (`0xE00`, `READ` instruction `0x3000`
— same protocol constants as Linux's `mcp251xfd` driver, used here only
as a specification reference, not reused code). `SPI0_BASE`
(`0x3f204000`) and `GPIO_BASE` (`0x3f200000`) were added to
`platform_config.h` and registered `MEM_AREA_IO_SEC` in
`plat-rpi3/main.c`. A standalone test client, `spi_poc_test/`, invokes
it and checks for a non-zero register value (same success/failure
signature as Linux's own driver: non-zero = real chip response, all-zero
= nothing answered).

**Real bug hit and fixed**: the first version read back all-zero even
though the transfer "completed" with no timeout (TXD/RXD/DONE all
toggled correctly). Root cause, found by checking the *live* device tree
on the Pi (`/proc/device-tree/soc/spi@7e204000/cs-gpios`): this board's
DTB gives `spi0` a `cs-gpios` property (GPIO 8 for CS0, active-low) —
Linux drives chip-select as a **plain GPIO**, not the SPI controller's
native 2-bit CS field. The first version relied entirely on the native
CS field, so it never actually asserted the chip's real CS pin — the
controller happily shifted clock/data internally, "succeeding" by its
own bookkeeping, while the chip itself never saw a real transaction.
Fix: drive GPIO 8 directly (`GPCLR0`/`GPSET0`) around the transfer,
exactly mirroring what Linux's own gpio-cs mechanism does. After the
fix: real register value `0x00000468` read directly by OP-TEE core,
zero regression on the existing `secure_can_demo` (same boot, same
image).

**No hardware-enforced isolation on this SoC**: this platform's own
comment says *"No secure memory on RPi..."* - there is no TZASC/TZPC
equivalent. Marking `SPI0_BASE`/`GPIO_BASE` `MEM_AREA_IO_SEC` only
affects OP-TEE's own page tables; it does not and cannot, on this
hardware, physically stop a root-compromised Linux kernel from reaching
the same physical registers via `/dev/mem`. The real isolation here is
that Linux is simply never given a driver/device-tree node for this
controller — a software/configuration boundary, not a hardware-enforced
one. State this precisely in the report rather than overclaiming
hardware isolation - the roadmap's own Step 14 task list anticipates
exactly this ("Document which limitations are enforced by hardware and
which are prototype software policy on RPi3").

**Not yet done** (tracked as the next phases, in this order): Step 15
(secure GPIO interrupt handling - the actually risky part, since GPIO
25/24 share a single GIC line with the *entire* first GPIO bank, meaning
"make can0's interrupt secure" really means claiming that whole shared
line for Secure World); Step 16/17 (a real secure CAN receive driver,
not just a single register read); Step 18 (secure RAM queues); disabling
Linux's `mcp251xfd`/`spi0.0` binding entirely; and reworking
`secure_can_ta.c` so Normal World only ever polls for Secure-World-
released results instead of handing it raw frames to classify.

**Prevention**: when a register-level driver "succeeds" by its own
status bits but returns implausible data (all-zero, all-ones), don't
assume the low-level protocol logic is wrong before checking the
*actual* physical wiring assumption first - specifically, check the
*live* device tree (`/proc/device-tree/...`) for how chip-select is
really wired on the exact board in hand, not just the SPI controller's
generic datasheet behavior. A `cs-gpios` property changes chip-select
from "automatic, tied to the TA bit" to "software must drive it
separately" - a difference invisible from the SPI controller's registers
alone.

---

## Rebuild procedure (reference)

From `/home/aslam/Minor_Project/optee_full`, using the pinned toolchain paths:

```sh
PY=/home/aslam/Minor_Project/optee-build-venv/bin/python

# 1. Core components, in dependency order
make -j4 -C build PYTHON3=$PY optee-os     # regenerates the TA dev kit too
make -j4 -C build PYTHON3=$PY tf-a

# 2. Project TA/host app, against the fresh dev kit
cd ../secure_can_demo
CROSS_COMPILE=../optee_full/out-br/host/bin/aarch64-linux-gnu- \
TEEC_EXPORT=../optee_full/out-br/host/aarch64-buildroot-linux-gnu/sysroot/usr \
TA_DEV_KIT_DIR=../optee_full/optee_os/out/arm/export-ta_arm64 \
make clean all

cp host/secure_can_demo \
   ../optee_full/build/br-ext/board/rpi3/overlay/usr/bin/secure_can_demo
cp ta/abecf637-ca20-467e-8e6c-0b8779ba6b2f.ta \
   ../optee_full/build/br-ext/board/rpi3/overlay/lib/optee_armtz/

# 3. Rootfs + boot files + final image, in that order
cd ../optee_full
make -j4 -C build PYTHON3=$PY buildroot
make -j4 -C build PYTHON3=$PY update_bootfs
make -j4 -C build PYTHON3=$PY sdcard-image
# -> out/rpi3-sdcard.img
```

## Flash + verification checklist

```sh
# Identify the card — NEVER guess. /dev/nvme0n1 is this laptop's system disk.
lsblk -b -o NAME,PATH,SIZE,MODEL,TRAN,RM,RO,TYPE,FSTYPE,LABEL,MOUNTPOINTS
file /dev/sdX                      # must say "block special", see Incident 2

udisksctl unmount -b /dev/sdX1
udisksctl unmount -b /dev/sdX2
sudo dd if=out/rpi3-sdcard.img of=/dev/sdX bs=4M conv=fsync status=progress
sync
sudo cmp --bytes="$(stat -c %s out/rpi3-sdcard.img)" out/rpi3-sdcard.img /dev/sdX \
  && echo VERIFICATION PASS
udisksctl power-off -b /dev/sdX
```

Functional check once the card is booted on the Pi (find its IP, then):

```sh
ssh -i ~/.ssh/id_ed25519 root@<pi-ip> "
  dmesg | grep -i optee
  /usr/bin/secure_can_demo
  strings /usr/bin/secure_can_demo | grep -i 'phase a'   # must find nothing
"
```

Expected `secure_can_demo` output (matches `Step10-runtime.log`):
```
Normal World challenge: 0x12345678
Secure World response: 0xb791f3dd
Secure World marker:   0x5343414e (SCAN)
PASS: project-specific Normal World to Secure World round trip
```

---

## Where things are

| Thing | Location |
|---|---|
| Authoritative build tree | `optee_full/` (gitignored — 20GB+ of upstream checkouts) |
| Pinned upstream revisions | `COMPONENT_REVISIONS.md` |
| Project-specific TA + host app | `secure_can_demo/` |
| SD card boot/troubleshooting steps | `RPI3_BOOT_TROUBLESHOOTING.md` |
| Full step-by-step roadmap | `RPI3_SECURE_CAN_FULL_ROADMAP.md` |
| Step-by-step completion status | `README.md`, `Step*_Verification/` |
