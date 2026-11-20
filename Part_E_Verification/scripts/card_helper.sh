#!/bin/bash
# Card swap helper. NEVER writes to any device: it checks and then PRINTS the sudo commands for the user to run.
#   card_helper.sh check <image> [--allow-old-dtb]   refuses an image whose DTB is not N-04 (e8ac38fa...) unless --allow-old-dtb; find the one removable USB disk, refuse anything but the 8 GB spare (7730102272 bytes), unmount its partitions,
#                                     verify the image against SHA256SUMS, print the exact dd + cmp commands
#   card_helper.sh identify           while the card is mounted in the laptop: say which known firmware its armstub8.bin is
#   card_helper.sh pi <ip>            after the card is in the Pi: drop the stale SSH host key for <ip> (backup first) and show the board identity
# Card roles (changed 2026-11-20): 8 GB = RECEIVER (OP-TEE image, the only card this script accepts); 32 GB = SENDER (Raspberry Pi OS Lite, never gets a monitor image);
# 4 GB = backup card, must not be flashed. Any size other than 7730102272 makes 'check' refuse, so the 32 GB sender and the 4 GB backup are both protected.
set -u
IMG_DIR="$(cd "$(dirname "$0")/../build_scratch/candidate_image" && pwd)"
SPARE=7730102272
case "${1:-}" in
check)
  img="${2:?usage: card_helper.sh check <image file name or path> [--allow-old-dtb]}"; [ -f "$img" ] || img="$IMG_DIR/$img"; [ -f "$img" ] || { echo "image not found"; exit 2; }
  # N-04 guard (added 2026-11-21): the image's boot partition must carry the N-04 device tree (reserves the secure DRAM 0x10000000-0x10ffffff).
  # An image without it silently removed the reservation on 2026-11-18 (board_full_check C3). Rollback to an older image: add --allow-old-dtb.
  dtb_want=$(grep ' candidate-bcm2710-rpi-3-b.dtb$' "$IMG_DIR/../../monitor/n04/SHA256SUMS" | cut -d' ' -f1)
  case "$dtb_want" in e8ac38fa*) ;; *) echo "REFUSED: cannot read the N-04 hash (monitor/n04/SHA256SUMS)"; exit 7;; esac
  poff=$(( $(sfdisk -d "$img" 2>/dev/null | awk '/start=/ {gsub(",","",$4); print $4; exit}') * 512 ))
  dtb_have=$(mcopy -n -i "$img@@$poff" ::bcm2710-rpi-3-b.dtb - 2>/dev/null | sha256sum | cut -d' ' -f1)
  if [ "$dtb_have" != "$dtb_want" ]; then
    if [ "${3:-}" = "--allow-old-dtb" ]; then echo "WARNING: this image's DTB is ${dtb_have:0:8}..., not N-04 (${dtb_want:0:8}...): the secure-DRAM reservation will be MISSING (board_full_check C3 will FAIL). Allowed because of --allow-old-dtb (rollback)."
    else echo "REFUSED: the image's bcm2710-rpi-3-b.dtb is ${dtb_have:0:8}..., not the N-04 DTB ${dtb_want:0:8}.... Compose the image with scripts/compose_image.sh, or add --allow-old-dtb only for a rollback."; exit 7; fi
  else echo "OK: image DTB is N-04 (${dtb_have:0:8}...)"; fi
  disks=$(lsblk -dnb -o NAME,SIZE,TRAN,RM | awk '$3=="usb" && $4=="1" {print $1" "$2}')
  [ "$(echo "$disks" | grep -c .)" = 1 ] || { echo "REFUSED: expected exactly one removable USB disk, found: ${disks:-none}"; exit 3; }
  dev=/dev/$(echo "$disks" | cut -d' ' -f1); size=$(echo "$disks" | cut -d' ' -f2)
  [ "$size" = "$SPARE" ] || { echo "REFUSED: $dev is $size bytes, not the 8 GB spare ($SPARE). This may be the 32 GB SENDER card or the 4 GB BACKUP card; do not write it."; exit 4; }
  for p in $(lsblk -nlo NAME,MOUNTPOINT "$dev" | awk 'NF==2 {print "/dev/"$1}'); do umount "$p" || { echo "could not unmount $p"; exit 5; }; done
  want=$(grep " $(basename "$img")\$" "$IMG_DIR/SHA256SUMS" | tail -1 | cut -d' ' -f1); have=$(sha256sum "$img" | cut -d' ' -f1)
  [ -n "$want" ] && [ "$want" = "$have" ] || { echo "REFUSED: image hash $have does not match SHA256SUMS (${want:-no entry})"; exit 6; }
  n=$(stat -c %s "$img")
  echo "OK: $dev = $size bytes (the 8 GB spare), unmounted; image $(basename "$img") sha256 ${have:0:16}... matches SHA256SUMS. Nothing was written."
  echo "Run these two yourself (they need your password), with no leading '!':"
  echo "  sudo dd if=$img of=$dev bs=4M conv=fsync status=progress"
  echo "  sudo cmp -n $n $img $dev && echo MATCH"
  echo "Then: sudo eject $dev, put the card in the Pi, power on, wait ~90 s.";;
identify)
  f=$(ls /media/*/*/armstub8.bin 2>/dev/null | head -1); [ -n "$f" ] || { echo "no mounted card with armstub8.bin"; exit 2; }
  h=$(sha256sum "$f" | cut -c1-20); echo "armstub $h"
  grep -E "armstub8.*\.bin" "$IMG_DIR/SHA256SUMS" | while read -r sum name; do [ "${sum:0:20}" = "$h" ] && echo "  = $name"; done
  [ "$h" = "$(sha256sum "$IMG_DIR/../../../SD_Card_Backups/pre-S0-sep20/armstub8.bin" 2>/dev/null | cut -c1-20)" ] && echo "  = original Sep-20 (non-monitor) armstub8.bin"; exit 0;;
pi)
  ip="${2:?usage: card_helper.sh pi <ip>}"; cp ~/.ssh/known_hosts ~/.ssh/known_hosts.bak-card-helper-$(date +%Y%m%d-%H%M%S); ssh-keygen -R "$ip" >/dev/null 2>&1
  ssh -o StrictHostKeyChecking=accept-new -o BatchMode=yes -o ConnectTimeout=8 root@"$ip" 'uname -r; tr -d "\0" </proc/device-tree/model; echo; uptime; dmesg | grep -iE "optee: rev|CPU1: failed to" | head -3';;
*) sed -n 2,9p "$0"; exit 1;;
esac
