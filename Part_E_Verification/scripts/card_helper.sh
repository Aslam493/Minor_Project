#!/bin/bash
# Card swap helper. NEVER writes to any device: it checks and then PRINTS the sudo commands for the user to run.
#   card_helper.sh check <image>      find the one removable USB disk, refuse anything but the 8 GB spare (7730102272 bytes), unmount its partitions,
#                                     verify the image against SHA256SUMS, print the exact dd + cmp commands
#   card_helper.sh identify           while the card is mounted in the laptop: say which known firmware its armstub8.bin is
#   card_helper.sh pi <ip>            after the card is in the Pi: drop the stale SSH host key for <ip> (backup first) and show the board identity
# The 32 GB original card must never be written: its size (anything other than 7730102272) makes 'check' refuse.
set -u
IMG_DIR="$(cd "$(dirname "$0")/../build_scratch/candidate_image" && pwd)"
SPARE=7730102272
case "${1:-}" in
check)
  img="${2:?usage: card_helper.sh check <image file name or path>}"; [ -f "$img" ] || img="$IMG_DIR/$img"; [ -f "$img" ] || { echo "image not found"; exit 2; }
  disks=$(lsblk -dnb -o NAME,SIZE,TRAN,RM | awk '$3=="usb" && $4=="1" {print $1" "$2}')
  [ "$(echo "$disks" | grep -c .)" = 1 ] || { echo "REFUSED: expected exactly one removable USB disk, found: ${disks:-none}"; exit 3; }
  dev=/dev/$(echo "$disks" | cut -d' ' -f1); size=$(echo "$disks" | cut -d' ' -f2)
  [ "$size" = "$SPARE" ] || { echo "REFUSED: $dev is $size bytes, not the 8 GB spare ($SPARE). This may be the 32 GB original; do not write it."; exit 4; }
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
