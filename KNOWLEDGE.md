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
