# NEXT_STEPS.md (rewritten 2026-11-12; the 2026-11-09 handoff it replaces is in git history and `source_snapshots/20261112_u0_pre/`)

## State right now (checked on the Pi 2026-11-12)
- Pi 3 B at 10.237.27.187 runs the hardened c16384 monitor image from the **8 GB spare card** (`SD8GB`, serial 0x6d280b3c); the old 32 GB card was never written with a monitor image. DTB = the N-04 version. `verify_boot` 16 pass / 0 FAIL / 2 skipped. `teeclnt` is empty.
- Components: C1 done (hardware items open), C2 closed, C3 done with accepted deviations, C4 closed with limits, **C5 done with limits (accepted)**, **C6 partial**, C7 not started (a sender Pi now exists and is verified, see below; still no second CAN board for chip B), C8 not started.
- Built and host-tested but **not flashed**: IDLE_EXPIRED and the UART ALIVE line (flag default off). Unit U1 (host tests T11a/T11b/T10b) done 2026-11-12.

## What to read first
`Secure_CAN_Monitor_Plan_8_Components.md` (board + dated log), then `monitor/PLAN_REMAINING_WORK_2026-11-12.md` (units U0-U8, decisions D-A to D-F, schedule, cut rule).

## Open decisions (from the plan)
D-A F27 coalescing (the limiter already exists); D-B contention hint signal; D-C UART stays off; D-D one image; D-E pushes (top-level only, nested repos on explicit OK); D-F software-injected soaks accepted.

## Routine for any card swap
`scripts/card_helper.sh check <image>` (refuses anything but the 8 GB spare, prints the dd/cmp commands for the user to run), `identify`, then `pi <ip>` after boot. Standing rules: card roles are fixed (8 GB = receiver, 32 GB = sender, 4 GB = backup, never flash the 4 GB or the 32 GB with a monitor image); board tests and any forced fault need explicit approval; always `PLATFORM=rpi3`; build in scratch directories with an ABSOLUTE output path; prototype keys are not production; detection only, no prevention claim. Never trust an object-code check without first confirming the build finished and the disassembly is non-empty.

## Sender Pi (added 2026-11-20)
- **Card roles (user decision, 2026-11-20):** 8 GB = RECEIVER (OP-TEE image); 32 GB = SENDER (Raspberry Pi OS Lite 32-bit, 2026-10-06 image, sha256 verified before the write); 4 GB = backup, must not be flashed. The old 32 GB contents (a Sep-20 monitor image) were **discarded by the user's decision and were not backed up** (the file `SD_Card_Backups/sd32gb_before_sender_2026-11-20.img.gz` is an empty 20-byte gzip and must not be treated as a backup). The rollback points are the images in `SD_Card_Backups/`.
- Sender: 10.237.27.170 (DHCP, may change), user `sender`, SSH key login from the laptop, hostname `sender`. `config.txt` has `dtoverlay=seeed-can-fd-hat-v2`; `can0-up.service` brings `can0` up at 500000 bit/s on boot. `can-utils` installed from a .deb copied from the laptop (the sender has no HTTP internet).
- First write of the card was corrupted (`kernel7.img` differed in 1018 bytes) and the Pi stopped after the rainbow screen; the re-write with `cmp` read-back against the reference image was byte-identical. Lesson: after every flash of a card, compare it with `cmp` before booting.
- Bus test 2026-11-20: one frame `123#DEADBEEF` sent from the sender, receiver `rx_packets` 0 to 1, 0 errors, sender state ERROR-ACTIVE. Classic CAN at 500000 bit/s only; CAN-FD data rate not chosen yet. `can1` on the sender fails to detect (same as the receiver's).
