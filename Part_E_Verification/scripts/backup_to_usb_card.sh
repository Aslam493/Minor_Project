#!/bin/bash
# Off-laptop backup onto the user-approved 4 GB card (2026-11-12).
# ERASES the target device. Run as: sudo bash backup_to_usb_card.sh /dev/sda <repo.bundle>
# Guards: removable USB disk, size 3.4-4.3 GB (never the Pi's 8 GB spare or 32 GB original).
set -euo pipefail
DEV=${1:?device, e.g. /dev/sda}
BUNDLE=${2:?path to git bundle of the top-level repo}
SRC=/home/aslam/Minor_Project
MNT=/mnt/pibackup

[ "$(id -u)" = 0 ] || { echo "run with sudo"; exit 1; }
[ "$(lsblk -dno TRAN "$DEV" | xargs)" = usb ] || { echo "REFUSED: $DEV is not a USB disk"; exit 1; }
[ "$(lsblk -dno RM "$DEV" | xargs)" = 1 ] || { echo "REFUSED: $DEV is not removable"; exit 1; }
SZ=$(blockdev --getsize64 "$DEV")
if [ "$SZ" -lt 3400000000 ] || [ "$SZ" -gt 4300000000 ]; then
  echo "REFUSED: $DEV size $SZ is not a ~4 GB card"; exit 1; fi
[ -s "$BUNDLE" ] || { echo "bundle missing"; exit 1; }

echo "Erasing $DEV ($SZ bytes) ..."
for p in $(lsblk -lno PATH "$DEV" | tail -n +2); do umount "$p" 2>/dev/null || true; done
wipefs -a "$DEV" >/dev/null
parted -s "$DEV" mklabel gpt mkpart PIBACKUP ext4 1MiB 100%
sleep 2; partprobe "$DEV"; sleep 2
PART=$(lsblk -lno PATH "$DEV" | tail -n +2 | head -1)
mkfs.ext4 -q -F -m 0 -L PIBACKUP "$PART"
mkdir -p "$MNT"; mount "$PART" "$MNT"

echo "Copying ..."
rsync -a --info=progress2 "$SRC/SD_Card_Backups" "$MNT/"
rsync -a "$SRC/source_backups" "$MNT/"
cp "$BUNDLE" "$MNT/minor_project_repo.bundle"
sync

echo "Verifying every file by SHA256 ..."
( cd "$SRC" && find SD_Card_Backups -type f -print0 | sort -z | xargs -0 sha256sum ) > /tmp/src.sums
( cd "$MNT" && find SD_Card_Backups -type f -print0 | sort -z | xargs -0 sha256sum ) > /tmp/dst.sums
if cmp -s /tmp/src.sums /tmp/dst.sums; then
  echo "VERIFIED: $(wc -l < /tmp/src.sums) files identical"
else
  echo "MISMATCH - backup NOT valid"; diff /tmp/src.sums /tmp/dst.sums | head; exit 2
fi
cp /tmp/dst.sums "$MNT/SHA256_of_copy.txt"
git bundle verify "$MNT/minor_project_repo.bundle" | tail -1
df -h "$MNT" | tail -1
sync; umount "$MNT"
echo "DONE. Safe to remove the card."
