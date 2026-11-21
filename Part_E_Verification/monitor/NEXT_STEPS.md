# NEXT_STEPS.md (rewritten 2026-11-12; the 2026-11-09 handoff it replaces is in git history and `source_snapshots/20261112_u0_pre/`)

## State right now (checked on the Pis 2026-11-21; the 2026-11-12 lines it replaces are in git history)
- **Receiver:** Pi 3 B at **10.237.27.212** since the flash of 2026-11-22 (same board, MAC `b8:27:eb:5c:ba:a9`; DHCP: it was .187, then .77; it changes when the card is re-flashed), 8 GB card, **Mode P image with the N-04 DTB** (`rpi3-sdcard-monitor-modeP-p6-n04.img`, sha256 `cf954724...`). `verify_boot` 16 pass / 0 FAIL / 2 skipped; `board_full_check.sh --mode-p` **32 PASS / 0 FAIL / 4 SKIP** (C3 PASS). CAN-FD (500000/2000000) comes up by itself at boot (`/usr/sbin/canfdcfg` + `/etc/init.d/S12can`, see `scripts/receiver_fd/`). `teeclnt` has a member `test` (F24 mitigation not in place).
- **Sender:** Pi 3 B at **10.237.27.144** (DHCP), 32 GB card, Raspberry Pi OS Lite, user `sender`, SSH key login from the laptop; `can0-up.service` brings up CAN-FD 500000/2000000 at boot.
- Real-bus results: `REAL_BUS_RESULTS.md` (steps 1-7 PASS; C3 found and fixed).
- Components: C1 done (hardware items open), C2 closed, C3 done with accepted deviations, C4 closed with limits, **C5 done with limits (accepted)**, **C6 partial**, C7 not started (a sender Pi now exists and is verified, see below; still no second CAN board for chip B), C8 not started.
- Built and host-tested but **not flashed**: IDLE_EXPIRED and the UART ALIVE line (flag default off). Unit U1 (host tests T11a/T11b/T10b) done 2026-11-12.

## What to read first
`Secure_CAN_Monitor_Plan_8_Components.md` (board + dated log), then `monitor/PLAN_REMAINING_WORK_2026-11-12.md` (units U0-U8, decisions D-A to D-F, schedule, cut rule).

## Open decisions (from the plan)
D-A F27 coalescing (the limiter already exists); D-B contention hint signal; D-C UART stays off; D-D one image; D-E pushes (top-level only, nested repos on explicit OK); D-F software-injected soaks accepted.

## Routine for any card swap
`scripts/card_helper.sh check <image>` (refuses anything but the 8 GB spare and any image whose DTB is not N-04, `--allow-old-dtb` only for a rollback; prints the dd/cmp commands for the user to run). Compose images ONLY with `scripts/compose_image.sh`, `identify`, then `pi <ip>` after boot. Standing rules: card roles are fixed (8 GB = receiver, 32 GB = sender, 4 GB = backup, never flash the 4 GB or the 32 GB with a monitor image); board tests and any forced fault need explicit approval; always `PLATFORM=rpi3`; build in scratch directories with an ABSOLUTE output path; prototype keys are not production; detection only, no prevention claim. Never trust an object-code check without first confirming the build finished and the disassembly is non-empty.

## Sender Pi (added 2026-11-20)
- **Card roles (user decision, 2026-11-20):** 8 GB = RECEIVER (OP-TEE image); 32 GB = SENDER (Raspberry Pi OS Lite 32-bit, 2026-10-06 image, sha256 verified before the write); 4 GB = backup, must not be flashed. The old 32 GB contents (a Sep-20 monitor image) were **discarded by the user's decision and were not backed up** (the file `SD_Card_Backups/sd32gb_before_sender_2026-11-20.img.gz` is an empty 20-byte gzip and must not be treated as a backup). The rollback points are the images in `SD_Card_Backups/`.
- Sender: 10.237.27.170 (DHCP, may change), user `sender`, SSH key login from the laptop, hostname `sender`. `config.txt` has `dtoverlay=seeed-can-fd-hat-v2`; `can0-up.service` brings `can0` up at boot (500000 classic until 2026-11-21, then CAN-FD 500000/2000000). `can-utils` installed from a .deb copied from the laptop (the sender has no HTTP internet).
- First write of the card was corrupted (`kernel7.img` differed in 1018 bytes) and the Pi stopped after the rainbow screen; the re-write with `cmp` read-back against the reference image was byte-identical. Lesson: after every flash of a card, compare it with `cmp` before booting.
- Bus test 2026-11-20: one frame `123#DEADBEEF` sent from the sender, receiver `rx_packets` 0 to 1, 0 errors, sender state ERROR-ACTIVE. Classic CAN at 500000 bit/s only; CAN-FD data rate not chosen yet. `can1` on the sender fails to detect (same as the receiver's).

## Open items (2026-11-21)
1. **Next image build:** put `S12can` and `usr/sbin/canfdcfg` (`scripts/receiver_fd/`) into the Buildroot overlay or into `compose_image.sh`; otherwise a new image drops permanent FD like the DTB was dropped (overlay is in a nested checkout, not touched).
2. Mode P timing runs, idle and under load (`scripts/modeP_timing_test.sh`, a fresh boot each); heavy PTA polling disturbs Mode P (observed).
3. `can1`: physical checks in `baseline/CAN1_INVESTIGATION_20261121.md` (board-level cause; software configuration is correct).
4. Wording fix: "the forwarder holds no key, but a root attacker could extract the prototype key" in documents and PDFs.
5. Prototype keys, no TZASC, detection only (unchanged). The sender Pi never holds a key: packets are made on the laptop by `packet_gen.py` and replayed by `laptop_can_send.py`.


## Queue for the NEXT image (decided 2026-11-22, no flash before the presentation)
1. **S95can_forwarder start/restart over ssh never return (E2c, F12).** Known limit; workaround: `ssh root@<receiver> '/etc/init.d/S95can_forwarder start >/dev/null 2>&1 </dev/null'` (same for `restart`). Fix: in `supervise`/`start` close fds 3-12 in the supervisor (`for fd in 3 4 5 6 7 8 9 10 11 12; do eval "exec $fd>&-"; done`) and use `( exec >/dev/null 2>&1 </dev/null; supervise ) &` so that ash keeps no saved copy of the ssh pipes. Test before building: `ssh host 'S95 start'` must return in under 2 s, `status`, `stop`, `restart`, `kill -9` of the daemon, the boot start. Script: `scripts/receiver_service/S95can_forwarder`; overlay copy in `optee_full/build` (local commit).
2. **Secure World image (separate, with host tests + mutation + 4 builds first):** the `ev_lost`/`ack` fix (C2: `ev_lost` counts only unread overwrites; `ack` must free slots) and the suppressed count (C1: the number of events suppressed by the 1-per-second limit, in the upper 15 bits of the 16-bit event `flags`).
3. Related notes: `board_full_check.sh` must run with the service stopped (F11); governor stays `powersave`; sender `txqueuelen` is already persistent (no flash).
