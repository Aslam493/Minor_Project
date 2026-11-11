# BASELINE_VERIFICATION.md (Component 1, Subcomponent 1.7)

Date: 2026-11-06. Target: Raspberry Pi 3 Model B at 10.237.27.187 running the Sep-20 image
(`SD_Card_Backups/pre-S0-sep20/rpi3-sdcard-sep20.img`, sha256 `014bb983...802d19`).
Changes made to the Pi in this subcomponent: **one real reboot**, and **one runtime-only loopback cycle on `can0`**, restored and verified. No flash, no file written to the SD card (test tools were placed in `/tmp`, which is RAM).

## Result in one line

**The baseline passes every objective acceptance criterion.** A cold reboot reproduces the same boot, Core 1 comes up diverted with a rising heartbeat, and `secure_can_demo` passes all four of its PASS checks on real SPI hardware.

## The twelve steps

| Step | Planned | What was done | Result |
|---|---|---|---|
| 1.7.1 | Identify the spare SD card with `lsblk` | **Not applicable.** No card is attached to the laptop, and no flash was needed (see 1.7.4). The check is documented in `ROLLBACK_PROCEDURE.md` R3 | N/A |
| 1.7.2 | Verify flash permission | `sudo -n -l` read: two passwordless rules exist, with exact forms (below) | PASS |
| 1.7.3 | Prepare the Sep-20 reference image | `sha256sum -c` OK; the staged `out/rpi3-sdcard.img` is byte-identical to the backup | PASS |
| 1.7.4 | Flash the spare card | **Not run, deliberately.** The Pi already runs this exact image: its boot partition is byte-identical (sha256 `6cef8675...0482da`, measured in 1.2) and 8 of 9 key root-filesystem files match the image (the exception is explained below). A flash would add card wear and prove nothing new. A real cold reboot (1.7.5) was run instead | N/A (equivalent evidence) |
| 1.7.5 | Boot the Pi | `reboot` issued over SSH; the Pi went down and answered SSH again **50 s** later | PASS |
| 1.7.6 | Capture the boot log | `baseline/boot_log_sep20_image.txt` (289 lines of `dmesg`) | PASS |
| 1.7.7 | Run the existing demo | `secure_can_demo`, with `can0` in loopback, exit 0, all four PASS lines (below); output saved in `baseline/demo_run_sep20_image.txt` | PASS |
| 1.7.8 | `--core1-heartbeat` twice | rising: 93,295,600 then 94,135,455 two seconds apart; also 29,383,321 then 30,474,054 inside `verify_boot` | PASS |
| 1.7.9 | `--fiq-count` | runs, returns **0** (correct: the FIQ is not armed) | PASS |
| 1.7.10 | `dmesg` for CRC errors | none; only the driver's banner contains the letters CRC (feature flags) | PASS |
| 1.7.11 | CPU mask | online `0,2-3`, offline `1`, `CPU1: failed to come online` at 5.116 s | PASS |
| 1.7.12 | Save the results | this file, the logs, and the verify_boot outputs | PASS |

## The objective acceptance list (O-03 in the plan)

| Criterion | Result | Evidence |
|---|---|---|
| With `can0` in loopback: round-trip line and `PASS: project-specific Normal World to Secure World round trip` | PASS | challenge `0x12345678`, response `0xb791f3dd`, marker `0x5343414e` (SCAN) |
| `PASS: Linux-controlled real CAN frames classified and routed by Secure World` | PASS | 5 frames: ids `0x100`, `0x101`, `0x102` SECURE; `0x103`, `0x104` NORMAL |
| `PASS: Secure World accepted the authentic frame` | PASS | |
| `PASS: Secure World rejected the tampered frame` | PASS | one data byte flipped |
| No `FAIL:` in the demo output | PASS | |
| `--core1-heartbeat` twice shows a rising counter | PASS | see 1.7.8 |
| `--fiq-count` runs (never `--arm-fiq` or `--last-frame`) | PASS | |
| No mcp251xfd CRC errors in `dmesg` | PASS | before, during and after the demo |
| CPU online mask `0,2-3` | PASS | |
| sha256 of image, `armstub8.bin`, overlay binaries recorded | PASS | table below |
| Repeated-FIQ stability run | **not done, by decision** | the FIQ stays unarmed (open decision D2; recorded as a baseline of "FIQ never armed") |

## Boot reproducibility: two boots of the same image

| Event | Earlier boot (before this session) | Cold reboot today |
|---|---|---|
| `CPU1: failed to come online` | 5.116407 s | 5.116416 s |
| `optee: revision 4.10 (a7ccc63e41c4acc5)` | 7.286122 s | 7.311472 s |
| `optee: initialized driver` | 7.345444 s | 7.370923 s |
| `Run /sbin/init` | not captured | 9.268543 s |
| `mcp251xfd spi0.0 can0 ... successfully initialized` | 10.039841 s | 9.904032 s |
| `spi0.1` | `-ENODEV` (known dead chip) | `-ENODEV` |
| SSH reachable | n/a | about 50 s after the command |
| Heartbeat shortly after boot | n/a | 29,383,321 at 66 s uptime (a fresh start) |

The two boots agree to within a few hundredths of a second for the events that do not depend on mounting and probing. The Core 1 divert fires automatically every boot at the same moment. The boot log contains no panic, oops, BUG or call trace; the only lines containing "error"-like words are two harmless kernel debug messages.

## The demo run in detail

- `can0` was in normal mode before; loopback was enabled by `ip link set can0 type can bitrate 500000 loopback on` and **confirmed by `canmode`** (`ctrlmode <LOOPBACK>`); after the demo it was restored by an exit trap and **confirmed by `canmode`** (`ctrlmode <>`, `loopback disabled`, ERROR-ACTIVE, operstate up).
- Counters for the 5 frames: `tx_packets 5`, `rx_packets 10`, errors 0, i.e. two received frames per transmit (the transmit-event echo plus the chip's receive-FIFO copy). This is the same pattern derived and measured in 1.2, now seen a second time.
- Secure World side after the demo: FIQ count 0, validation status NONE, `secure_last_counter` 0. **The demo does not touch Secure World's own CAN path**; it uses the TA only (path P1 of 1.3).
- The authentication tags are deterministic and form a regression check for the TA's key and HMAC code (frame data `00 01 .. 07`, `11 12 .. 18`, `22 23 .. 29`):

| CAN id | Tag (first 8 bytes of HMAC-SHA256) |
|---|---|
| `0x100` | `a569b39f02c5ba4e` |
| `0x101` | `e8372c03234080cc` |
| `0x102` | `ae5375384469dd1b` |

## Things the run showed that you should know

| # | Observation | Meaning |
|---|---|---|
| 1 | The demo prints `pthread_setaffinity_np: Success` at the start and no `[core 1]` lines | its second thread is pinned to CPU 1, which is offline on this image. The call returns an error code without setting `errno`, so `perror` prints "Success". The worker returns at once; the main pipeline is unaffected. The "two worlds in parallel" display of the old demo no longer applies, because core 1 is now Secure World |
| 2 | `spi_poc_test` on the Pi (`ab989359...`) differs from the copy inside the image (`a567cc2a...`) | same size (25,240 bytes) and the same set of options; the Pi's copy is the newer local build, copied to the Pi earlier. If the image were reflashed, the older copy would return |
| 3 | The first use of a CAN socket after boot logs `can: controller area network core`, `Registered PF_CAN protocol family`, `can: raw protocol` | the CAN core modules load on demand; harmless |
| 4 | `spi_poc_test --fiq-count` prints "Trigger one (e.g. run secure_can_demo)" when the count is 0 | that hint is out of date: the demo does not cause a FIQ in this image, because the FIQ is not armed |
| 5 | `/usr/lib/libteec.so.2` showed as "different" in the image comparison | only because it is a symlink, which a plain read of the image cannot follow; `libteec.so.2.0.0` matches |
| 6 | The demo needs `can0` in loopback; the boot script brings `can0` up in normal mode | by design; it must be set and restored by hand (the run above used an exit trap) |

## Hashes recorded (from `SD_Card_Backups/pre-S0-sep20/SHA256SUMS`)

| File | sha256 |
|---|---|
| `optee_full/out/rpi3-sdcard.img` (the image) | `014bb9833d456e3751d9651f7dc2c832d0684b0f106f679289c1ec1e1f802d19` |
| `optee_full/out/boot/armstub8.bin` (TF-A + OP-TEE + U-Boot) | `f203d09581f2cd70342328395aea2fdbc76bab41333ab0b2d7729049da77998d` |
| `secure_can_demo` (host app, on the Pi too) | `7656722445823644542897245f3e4e4c2837b8723b3a8d6dfcebcddefd941821` |
| `spi_poc_test` (newer local build, on the Pi) | `ab989359ae6cdbc267db37343257efa1cac5c4677fd1f3f911cfa980c5e4d8d1` |
| `spi_poc_test` inside the image | `a567cc2a9a919f8d...` (first 16 digits) |
| TA, signed copy in the image and on the Pi | `8a3ad20f259c69239850379a2bc1674f7744f36078ce28953355cac96bab404c` |
| TA, repo copy (same code, different signature, see 1.3) | `ba619ee86a69b37f60dbeb2a278a33067e9aa2aad85b605ce808619d6b0b6f68` |
| Pi boot partition | `6cef86759f4e3625943c13afd6b46bdb2dc50c35195c515f9479a4a98c0482da` |

## The passwordless flash rule (read with `sudo -n -l`, 1.7.2)

```
(root) NOPASSWD: /usr/bin/dd if=/home/aslam/Minor_Project/optee_full/out/rpi3-sdcard.img of=/dev/sda bs=4M conv=fsync status=progress
(root) NOPASSWD: /usr/bin/cmp --bytes=* /home/aslam/Minor_Project/optee_full/out/rpi3-sdcard.img /dev/sda
```
Both forms are exact: the source must be `out/rpi3-sdcard.img`, the target must be `/dev/sda`, and `dd` must use exactly `bs=4M conv=fsync status=progress`. (The account also has the ordinary password-protected `ALL` rule.) This is now written into `ROLLBACK_PROCEDURE.md` R3.

## What this does not establish

- A restore or flash to a card (not run; no card attached).
- Reception from a **physical bus** and the transceiver (only loopback was used).
- CAN-FD operation (`can0` is classic CAN).
- Any behaviour with the secure FIQ armed (not armed, by decision).
- Booting the older backup images.

## Problems or confusion avoided

| Item | What happened | How it was handled |
|---|---|---|
| Whether to reboot the Pi at all | a reboot could leave a board with no console stranded | set a 240-second limit with "stop, do not retry" in advance; the Pi came back in 50 s |
| The demo would leave `can0` in loopback if it failed | a manual restore after a failure is easy to forget | ran the demo inside a script with an exit trap, then confirmed the restored mode with `canmode` independently |
| Wrong path in my image comparison | I used `/lib/libteec...`, which is a different place in the image | recomputed with `/usr/lib/...` (match) |
| A symlink looked like a difference | a plain read of the image does not follow links | explained in observation 5 |
| `perror` printed "Success" for a failure | `pthread_setaffinity_np` returns an error code and leaves `errno` alone | read the source; benign (observation 1) |
