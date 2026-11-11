# ROLLBACK_PROCEDURE.md (Component 1, task 1.5.8)

Date: 2026-11-06. Purpose: if a new OP-TEE build, TF-A change or firmware modification leaves the Pi unable to boot, restore the last working system.
Everything below was checked against the real backup files. What could **not** be tested is listed in section 9; the restore itself was **not** run, because the rule is never to test rollback on the working card.

## 1. Golden rules

1. **Never write to the original working card.** All flashing and all experiments go to the spare card.
2. **Never guess the device.** Identify the target with `lsblk` and `file /dev/sdX` before every write (section 5, Incident 2).
3. **A flash is only trusted after a read-back** (`cmp`) and a boot check (`verify_boot.sh`).
4. **Restore the card and the source together.** An image restores the SD card only; a later rebuild from a still-modified source tree silently recreates the broken firmware (Incident 5). Section 7 covers the source.
5. **If a step fails, stop.** Section 6 says what is allowed next.

## 2. The backups (inventory, verified today)

Folder: `/home/aslam/Minor_Project/SD_Card_Backups/`. All seven images have the same size, **336,592,896 bytes**, the boot partition (FAT16) starts at sector 2048, and they share one kernel (`kernel8.img` sha256 prefix `0dd9fecc2f65`). They differ in the firmware (`armstub8.bin` = TF-A + OP-TEE + U-Boot) and the root filesystem.

| Backup (folder / file) | Image time | sha256 (full value is in `SHA256SUMS`) | `armstub8.bin` prefix | Hash recorded when the backup was made? | What it is (from folder name and project notes) |
|---|---|---|---|---|---|
| `pre-S0-sep20/rpi3-sdcard-sep20.img` | built 2026-09-20 02:40, copied 2026-11-06 | `014bb983...802d19` | `f203d09581f2cd70` | no, first recorded 2026-11-06 | **the image running on the Pi now**: Core 1 diverted, Step 16/17 work in progress |
| `step2-core1-hard-pinning-verified/rpi3-sdcard-known-good-step2.img` | 2026-09-18 23:35 | `bf602859...3153e5d7` | `1d4a82c8e91b71de` | no, first recorded 2026-11-06 | Core 1 hard-pinning verified on hardware; the plan's fallback "Step 2" baseline |
| `working-demo-verified-sep18/rpi3-sdcard-known-good-working-demo.img` | 2026-09-18 13:12 | `7fa5881c...d940b381` | `8cede6894172e3a3` | **yes** (its `README.md`) | `secure_can_demo` verified end to end; stock TF-A, no Core 1 divert |
| `pre-linux-lockout/rpi3-sdcard-known-good-pre-lockout.img` | 2026-09-17 19:33 | `8e478854...38ed36` | `1b5bcb7e9fd9eb12` | no, first recorded 2026-11-06 | taken before the work to take `can0` away from Linux; no divert |
| `step15-can-fiq-routing-complete/rpi3-sdcard-known-good-step15.img` | 2026-09-17 03:16 | `d51a2814...2145a` | `9b4be0e0dce131ad` | **yes** (`KNOWLEDGE.md`) | Step 15: the CAN interrupt reaches Secure World (routed to core 0); no divert |
| `pre-secure-peripheral-ownership/rpi3-sdcard-known-good-parallel-worlds-demo.img` | 2026-09-17 01:21 | `42768813...eec2d4` | `acc7d3b1b4d707c7` | no, first recorded 2026-11-06 | parallel Normal/Secure demo, before the secure SPI work |
| `pre-dual-core/rpi3-sdcard-known-good-classification.img` | 2026-09-16 10:32 | `e247e459...acb6b` | `317428c8546657d3` | **yes** (`KNOWLEDGE.md`, Incident 4) | the oldest: before the dual-core boot attempt that failed |

Notes on this table:
- Three hashes (working-demo, step15, pre-dual-core) match the values written when those backups were made. For the other four there is no earlier record, so today's hash is the reference from now on ("trust on first use"). They are all in the new master file `SD_Card_Backups/SHA256SUMS`.
- The "what it is" column comes from folder names, the notes and file dates. Only the images marked in section 8 have been booted today.
- Whether an image has the Core 1 divert decides which expectation to pass to `verify_boot.sh`: `--expect-core1-pinned` for `step2` and `pre-S0-sep20`; `--expect-stock` for the older ones (by date, to be confirmed by the script itself on first use).

### Which backup to restore

| Situation | Restore |
|---|---|
| A change to OP-TEE, TF-A or `config.txt` after the Sep-20 build stops the Pi booting | `pre-S0-sep20` (the last state known to boot today) |
| The Sep-20 image itself is suspect (Step 16/17 bug, for example) | `step2-core1-hard-pinning-verified` (Core 1 verified on hardware) |
| Anything involving Core 1 or the divert is suspect | `working-demo-verified-sep18` (no divert) |
| Everything newer fails | `pre-dual-core`, the oldest |

## 3. The procedure

### R1. Identify the backup
```sh
cd /home/aslam/Minor_Project/SD_Card_Backups
ls -l pre-S0-sep20/rpi3-sdcard-sep20.img          # expect 336592896 bytes
```
Write down: path, size, sha256 from section 2. Do not continue if the size differs.

### R2. Verify the backup (before it touches any card)
```sh
sha256sum -c SHA256SUMS                            # all seven, expect "OK" for each
# or only the one to be used:
grep step2-core1 SHA256SUMS | sha256sum -c
```
Any `FAILED` means the file changed or is damaged: stop, use a different backup, and do not flash it. (Run today: 7 of 7 OK.)

### R3. Restore to the SPARE card
1. Label the two cards ("ORIGINAL", "SPARE"). Remove the original and keep it away from the laptop. Put the spare in the USB reader.
2. Identify the device, never guess:
   ```sh
   lsblk -o NAME,TYPE,RM,SIZE,MODEL,SERIAL
   file /dev/sdX            # MUST say "block special". "DOS/MBR boot sector" or "data" means a stray regular file
   ```
   The target must be `TYPE disk`, `RM 1`, with the size of the spare. Write down its serial. If anything is unexpected: stop.
3. Unmount its partitions:
   ```sh
   udisksctl unmount -b /dev/sdX1 ; udisksctl unmount -b /dev/sdX2
   ```
4. Write the image. Either of two ways:
   - **Passwordless path** (the sudoers rule covers exactly `optee_full/out/rpi3-sdcard.img` to `/dev/sda`): `cp <backup> optee_full/out/rpi3-sdcard.img`, then the same `sudo -n dd ...` and `sudo -n cmp ...` forms that rule allows, with the target confirmed as `/dev/sda`. **Do not run `make` in between** (`make sdcard-image` deletes that file). Before overwriting the staged file, compare it with a backup (`cmp`); today it is identical to `pre-S0-sep20`.
   - **Direct path** (asks for a password), from the backup's own README:
     ```sh
     sudo dd if=<backup>.img of=/dev/sdX bs=4M conv=fsync status=progress
     sync
     ```
   The sudoers rule was read on 2026-11-06 (`sudo -n -l`) and is exact. It allows only:
   ```
   (root) NOPASSWD: /usr/bin/dd if=/home/aslam/Minor_Project/optee_full/out/rpi3-sdcard.img of=/dev/sda bs=4M conv=fsync status=progress
   (root) NOPASSWD: /usr/bin/cmp --bytes=* /home/aslam/Minor_Project/optee_full/out/rpi3-sdcard.img /dev/sda
   ```
   So the passwordless path works only for source `out/rpi3-sdcard.img`, target `/dev/sda`, and `dd` options exactly `bs=4M conv=fsync status=progress`; the read-back is `sudo -n cmp --bytes=<size> .../out/rpi3-sdcard.img /dev/sda`. Anything else (another device name, another option) asks for the password. Confirm that the spare card really is `/dev/sda` first: a wrong name makes `dd` write somewhere else or create a regular file (Incident 2).

### R4. Verify the restored card
```sh
sudo cmp --bytes=336592896 <backup>.img /dev/sdX && echo VERIFICATION PASS
sudo fdisk -l /dev/sdX          # expect 2 partitions: ~63 MiB FAT16 (type e, bootable) and 256 MiB Linux; the rest of the card unallocated
udisksctl power-off -b /dev/sdX
```
A `cmp` difference or a different partition table means the write failed: repeat R3 once; if it fails again, change the card or reader (R6).

### R5. Boot verification
1. Put the spare card in the Pi, power it on. Expect the GPU rainbow screen, then Linux; allow about 40 seconds.
2. Find the Pi's address (it came from DHCP; today `10.237.27.187`) and run:
   ```sh
   cd /home/aslam/Minor_Project/Part_E_Verification/tools/verify_boot
   ./verify_boot.sh <pi-ip> --expect-core1-pinned      # or --expect-stock for an older image
   ```
   It checks, read-only: ssh reachable; kernel 6.7.0-rc6; OP-TEE revision line and "initialized driver", no OP-TEE error; `/dev/tee0`, `/dev/teepriv0`; `tee-supplicant` running; CPU mask (`0,2-3` plus "CPU1: failed to come online" for a divert image, `0-3` for a stock one); Core 1 heartbeat rising (divert images); the PTA answers; **FIQ_CONTROL is 0 (the FIQ is not armed after boot)**; `can0` exists, is up, chip found, no SPI CRC error; and, if copied to `/tmp` on the Pi, `canmode` (bitrate, mode) and `ta_roundtrip` (TA and PTA round trip).
3. Compare with the reference for the Sep-20 image: `baseline/verify_boot_sep20_reference.txt` (18 pass, 0 FAIL).
4. Pass = exit status 0 and no FAIL. For the full application check run `secure_can_demo` with `can0` in loopback (Subcomponent 1.7), then put `can0` back to normal mode.

### R6. Recovery rule: if any step fails
- **Stop. Do not overwrite the original card.** The original is the only copy of "what works in hardware" that did not pass through this laptop's reader.
- Do not "fix" by repeating the flash with a different image blindly: change one thing at a time and record it.
- Check the hardware before the software (lessons of 2026-09-18): re-seat the microSD card (contacts wear after many insertions) and re-seat the CAN shield on the GPIO header (all 40 pins, even pressure).
- Try the next backup in the "Which backup" table, on the spare card.
- Try a different card or reader.
- If it still fails, a UART capture is needed (Subcomponent 1.6); do not make further firmware changes blind.

## 4. The source tree is part of the rollback

The images restore the card. The Sep-20 **source** state is saved separately in `SD_Card_Backups/pre-S0-sep20/diffs/` (for `optee_os`, `build`, `trusted-firmware-a`: `git diff`, untracked-file tarballs, and the pinned `HEAD` hash). **Verified today** without touching the live repositories: for each tree the pinned `HEAD` was unpacked into a scratch directory, the saved diff applied cleanly, the untracked files restored, and all 23 changed or untracked files compared equal to the live tree (0 mismatches).

To return a tree to the Sep-20 state after a failed change (this discards the failed edits, so save them first):
```sh
cd /home/aslam/Minor_Project/optee_full/<tree>
git diff > /tmp/failed-change-<tree>.diff        # keep what you are about to throw away
git checkout -- .                                 # drops tracked edits only
git apply /home/aslam/Minor_Project/SD_Card_Backups/pre-S0-sep20/diffs/<tree>.diff
tar -xf /home/aslam/Minor_Project/SD_Card_Backups/pre-S0-sep20/diffs/<tree>-untracked.tar    # untracked files (optee_os, build)
```
Then check `git status` and `git diff --stat` match the saved diff before building. A rebuild must run the **whole** pipeline (OP-TEE, TF-A, host apps, Buildroot, `update_bootfs`, `sdcard-image`) with `PLATFORM=rpi3` (lessons: Incident 5 and the stale-pipeline notes).

## 5. Why each rule exists (from this project's own incidents)

| Rule | Incident |
|---|---|
| `file /dev/sdX` must say "block special" | Incident 2: a `dd` to a missing `/dev/sda` created a regular file and every later "flash" silently wrote into it |
| restore card **and** source | Incident 5: an uncommitted TF-A change in a separate checkout was rebuilt into `armstub8.bin` after a binary-only recovery |
| never flash the original | Incidents 1, 4, 5: each was recovered in minutes because a known-good image existed and the original card was untouched |
| check hardware first | 2026-09-18: worn card contacts and a loose shield looked exactly like firmware regressions |

## 6. What was verified today and what was not

| Item | Result |
|---|---|
| Seven backup images exist, all 336,592,896 bytes, partition tables read | verified |
| `sha256sum -c SHA256SUMS`: 7 of 7 OK; 3 match hashes recorded when the backup was made | verified |
| Boot-partition firmware fingerprint of each image (`armstub8.bin`) | read (table in section 2) |
| Staged `out/rpi3-sdcard.img` identical to `pre-S0-sep20` | verified (`cmp`) |
| Saved source diffs apply to the pinned `HEAD` and reproduce the live trees | verified (23 files, 0 mismatches) |
| `verify_boot.sh` passes on the real Sep-20 image (18 of 18), fails on a wrong expectation, exits 2 when unreachable, and its CRC check ignores the driver banner but catches a real error line | verified (two script bugs were found and fixed on the way) |
| **A restore to a card** (`dd`, `cmp` read-back, boot) | **not run**: no spare card is attached to the laptop, and the rule forbids testing on the working card |
| The sudoers rule's exact allowed commands | read in 1.7 (`sudo -n -l`): two exact forms, written into R3 |
| That the older images boot today | **not tested** (each was verified when made, according to the notes) |
| Whether the card now in the Pi is the original or the spare | **unknown** |

## 7. Open items

1. **Which card is in the Pi right now: ORIGINAL or SPARE?** (It is a card of about 29.7 GiB; `lsblk` on the laptop sees neither.) If it is the original, the Pi is currently running on it, which is allowed (we only read), but it must not be flashed.
2. ~~Run `sudo -n -l`~~ done in 1.7; the exact forms are in R3.
3. **The backups exist only on this laptop's internal disk** (2.2 GB in `SD_Card_Backups/`, not tracked by git; the disk is 65 percent full). A disk failure would lose every rollback point. Copy the folder to an external drive.
4. After the first real restore (when a spare card is attached), record its result here.

## 8. Problems met and how they were solved

| # | Problem | Steps | Cause | Fix | Lesson |
|---|---|---|---|---|---|
| P1 | Four backups had no hash recorded when they were made | searched the notes for each hash | hashes were only written down for some backups | wrote `SD_Card_Backups/SHA256SUMS` for all seven, flagged which are first-use | write the hash into the backup folder at backup time |
| P2 | The documented restore command copies from the backup folder, but the passwordless rule covers only the staged file | read the existing README and the memory note | two different flash paths exist | documented both, with the "no `make` in between" warning and a check before overwriting the staged file | state which path a rule covers |
| P3 | `verify_boot.sh` stopped with "unexpected EOF while looking for matching `'`" | bisected with `head -n N \| bash -n` | a missing closing quote on one line | added the quote; the script now parses (`bash -n`) | syntax-check a script before relying on its output |
| P4 | `verify_boot.sh` reported FAIL "no SPI CRC errors" on a healthy Pi | looked at which line matched | the driver's banner contains `+CRC_REG +CRC_RX +CRC_TX` (features, not errors) | match `CRC read error` / `CRC write error` only; tested the pattern on a fake error line and on the banner | a check is only trusted after it has been shown to fail when it should, and to pass when it should |
| P5 | Would the saved diffs really restore the source? | unpacked pinned HEAD with `git archive` into a scratch directory (live repos untouched), applied, compared | n/a | 23 of 23 files equal | a backup is verified by restoring it somewhere safe |
