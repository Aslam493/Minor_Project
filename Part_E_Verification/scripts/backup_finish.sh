#!/bin/bash
# Resume after backup_to_usb_card.sh aborted at the source_backups copy (path bug, fixed).
# Does NOT erase anything. Run: sudo bash backup_finish.sh <repo.bundle>
set -euo pipefail
BUNDLE=${1:?bundle}; SRC=/home/aslam/Minor_Project; MNT=/mnt/pibackup
[ "$(id -u)" = 0 ] || { echo "run with sudo"; exit 1; }
mountpoint -q "$MNT" || { echo "$MNT not mounted"; exit 1; }
[ "$(lsblk -no LABEL "$(findmnt -no SOURCE "$MNT")" | xargs)" = PIBACKUP ] || { echo "wrong volume"; exit 1; }
rsync -a "$SRC/source_backups" "$MNT/"
cp "$BUNDLE" "$MNT/minor_project_repo.bundle"
sync
( cd "$SRC" && find SD_Card_Backups -type f -print0 | sort -z | xargs -0 sha256sum ) > /tmp/src.sums
( cd "$MNT" && find SD_Card_Backups -type f -print0 | sort -z | xargs -0 sha256sum ) > /tmp/dst.sums
if cmp -s /tmp/src.sums /tmp/dst.sums; then echo "VERIFIED: $(wc -l < /tmp/src.sums) files identical"
else echo "MISMATCH - backup NOT valid"; diff /tmp/src.sums /tmp/dst.sums | head; exit 2; fi
cp /tmp/dst.sums "$MNT/SHA256_of_copy.txt"
git bundle verify "$MNT/minor_project_repo.bundle" | tail -1
ls "$MNT"; df -h "$MNT" | tail -1
sync; umount "$MNT"; echo "DONE. Safe to remove the card."
