#!/bin/bash
# compose_image.sh BASE.img OUT.img [ARMSTUB.bin]
# The ONE place where a candidate SD image is composed. It copies BASE to OUT and puts these files into the FAT boot partition:
#   - bcm2710-rpi-3-b.dtb  = the N-04 device tree (monitor/n04/candidate-bcm2710-rpi-3-b.dtb, sha256 e8ac38fa...), ALWAYS
#   - armstub8.bin         = ARMSTUB.bin, only if given
# Why the DTB is always installed: the candidate images are the Sep-20 image with files swapped in, and that image carries the ORIGINAL device tree
# (6b90c3e0..., no reservation of the secure DRAM 0x10000000-0x10ffffff). Flashing such an image on 2026-11-18 removed the N-04 reservation that had been
# installed by hand on 2026-11-12 (board_full_check C3 FAIL on 2026-11-21). The full Buildroot build already applies the reservation (build/rpi3.mk,
# RPI3_OPTEE_RESERVE_DTBO); this script covers the swap-in route.
# Optional: ROOTFS_ADD="src:dest:mode ..." (environment variable) also puts files into the ext4 root filesystem of the image, offline, on a scratch copy of the partition with
# debugfs (no root, no loop mount), for example ROOTFS_ADD="scripts/receiver_fd/canfdcfg_aarch64:/usr/sbin/canfdcfg:755 scripts/receiver_fd/S12can:/etc/init.d/S12can:755".
# With it, the root-filesystem check is: e2fsck clean, every added file read back with the same sha256 and mode, and a file-by-file comparison of the whole tree against the base
# that must show exactly the added or replaced files and nothing else.
# Verifies: DTB hash before and after (read back from the image), every boot-partition file that differs from BASE (must be only the files named above),
# the first MiB (partition table) and the root filesystem partition byte-identical to BASE. Never touches a card or a board. Exit 0 only if all hold.
set -u
HERE="$(cd "$(dirname "$0")/.." && pwd)"; cd "$HERE" || exit 2
N04=monitor/n04/candidate-bcm2710-rpi-3-b.dtb
N04_SHA=$(grep ' candidate-bcm2710-rpi-3-b.dtb$' monitor/n04/SHA256SUMS | cut -d' ' -f1)
BASE="${1:?usage: compose_image.sh BASE.img OUT.img [ARMSTUB.bin]}"; OUT="${2:?usage: compose_image.sh BASE.img OUT.img [ARMSTUB.bin]}"; ARM="${3:-}"
[ -f "$BASE" ] || { echo "REFUSED: base image not found: $BASE"; exit 2; }
[ -z "$ARM" ] || [ -f "$ARM" ] || { echo "REFUSED: armstub not found: $ARM"; exit 2; }
[ "$BASE" != "$OUT" ] || { echo "REFUSED: BASE and OUT must differ"; exit 2; }
[ -n "$N04_SHA" ] && [ "$(sha256sum "$N04" | cut -d' ' -f1)" = "$N04_SHA" ] || { echo "REFUSED: $N04 does not match monitor/n04/SHA256SUMS"; exit 3; }
case "$N04_SHA" in e8ac38fa*) ;; *) echo "REFUSED: the N-04 hash in monitor/n04/SHA256SUMS is not e8ac38fa..."; exit 3;; esac
S1=$(sfdisk -d "$BASE" 2>/dev/null | awk '/start=/ {gsub(",","",$4); print $4; exit}'); S2=$(sfdisk -d "$BASE" 2>/dev/null | awk '/start=/ {n++; if (n==2) {gsub(",","",$4); print $4}}')
[ -n "$S1" ] && [ -n "$S2" ] || { echo "REFUSED: cannot read the partition table of $BASE"; exit 2; }
N2=$(sfdisk -d "$BASE" 2>/dev/null | awk '/size=/ {n++; if (n==2) {gsub(",","",$6); print $6}}')
O1=$((S1*512)); O2=$((S2*512))
cp "$BASE" "$OUT" || exit 2
mcopy -o -i "$OUT@@$O1" "$N04" ::bcm2710-rpi-3-b.dtb || { echo "FAIL: mcopy dtb"; exit 4; }
[ -z "$ARM" ] || mcopy -o -i "$OUT@@$O1" "$ARM" ::armstub8.bin || { echo "FAIL: mcopy armstub"; exit 4; }
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; rc=0
mcopy -n -i "$OUT@@$O1" ::bcm2710-rpi-3-b.dtb "$T/dtb.rb" && [ "$(sha256sum "$T/dtb.rb" | cut -d' ' -f1)" = "$N04_SHA" ] && echo "OK   DTB read back from $OUT: sha256 ${N04_SHA:0:16}... (N-04)" || { echo "FAIL DTB read-back"; rc=5; }
if [ -n "$ARM" ]; then mcopy -n -i "$OUT@@$O1" ::armstub8.bin "$T/arm.rb" && cmp -s "$T/arm.rb" "$ARM" && echo "OK   armstub8.bin read back equals $ARM" || { echo "FAIL armstub read-back"; rc=5; }; fi
mkdir "$T/b" "$T/n"; mcopy -s -n -i "$BASE@@$O1" :: "$T/b" >/dev/null 2>&1; mcopy -s -n -i "$OUT@@$O1" :: "$T/n" >/dev/null 2>&1
D=$(diff -rq "$T/b" "$T/n" | sed "s|$T/[bn]/||g;s/Files //;s/ and .* differ//" | sort | tr '\n' ' ')
WANT=""	# the files whose CONTENT must differ from the base (a file the base already has with the wanted content does not differ)
if [ -n "$ARM" ] && [ "$(sha256sum "$T/b/armstub8.bin" | cut -d' ' -f1)" != "$(sha256sum "$ARM" | cut -d' ' -f1)" ]; then WANT="armstub8.bin "; fi
if [ "$(sha256sum "$T/b/bcm2710-rpi-3-b.dtb" | cut -d' ' -f1)" != "$N04_SHA" ]; then WANT="${WANT}bcm2710-rpi-3-b.dtb "; fi
if [ "$D" = "$WANT" ]; then echo "OK   boot-partition files that differ from the base: $D"; else echo "FAIL boot-partition files that differ: '$D' (wanted '$WANT'; a DTB identical to the base would also show here)"; rc=6; fi
cmp -s -n "$O1" "$BASE" "$OUT" && echo "OK   first $O1 bytes (partition table) identical to the base" || { echo "FAIL partition table area differs"; rc=7; }
if [ -z "${ROOTFS_ADD:-}" ]; then
  cmp -s -i "$O2" "$BASE" "$OUT" && echo "OK   root filesystem partition (byte $O2 on) identical to the base" || { echo "FAIL root filesystem differs"; rc=7; }
else
  RF="$T/rootfs.ext4"; dd if="$BASE" of="$T/rootfs_base.ext4" bs=512 skip="$S2" count="$N2" status=none; cp "$T/rootfs_base.ext4" "$RF"
  e2fsck -fn "$RF" >/dev/null 2>&1 && echo "OK   base root filesystem is clean (e2fsck)" || { echo "FAIL base root filesystem is not clean"; rc=7; }
  : > "$T/cmds"; WANT_DIFF=""
  for spec in $ROOTFS_ADD; do
    src=${spec%%:*}; rest=${spec#*:}; dest=${rest%%:*}; mode=${rest#*:}
    [ -f "$src" ] || { echo "FAIL ROOTFS_ADD source missing: $src"; rc=7; continue; }
    if debugfs -R "stat $dest" "$RF" >/dev/null 2>&1; then echo "rm $dest" >> "$T/cmds"; fi
    printf '%s\n' "write $src $dest" "set_inode_field $dest mode 0100$mode" "set_inode_field $dest uid 0" "set_inode_field $dest gid 0" >> "$T/cmds"
  done
  debugfs -w -f "$T/cmds" "$RF" >/dev/null 2>&1
  e2fsck -fn "$RF" >/dev/null 2>&1 && echo "OK   root filesystem is clean after the additions (e2fsck)" || { echo "FAIL root filesystem is not clean after the additions"; rc=7; }
  for spec in $ROOTFS_ADD; do
    src=${spec%%:*}; rest=${spec#*:}; dest=${rest%%:*}; mode=${rest#*:}
    debugfs -R "dump $dest $T/rb.file" "$RF" >/dev/null 2>&1
    m=$(debugfs -R "stat $dest" "$RF" 2>/dev/null | sed -n 's/.*Mode: *\([0-9]*\).*/\1/p' | head -1)
    if [ "$(sha256sum "$T/rb.file" 2>/dev/null | cut -d' ' -f1)" = "$(sha256sum "$src" | cut -d' ' -f1)" ] && [ "$m" = "0$mode" ]; then echo "OK   $dest read back from the image: same sha256 as $src, mode $m"; else echo "FAIL $dest read-back (mode '$m')"; rc=7; fi
    WANT_DIFF="$WANT_DIFF $dest"
  done
  dd if="$RF" of="$OUT" bs=512 seek="$S2" conv=notrunc status=none
  # file-by-file comparison of the whole tree, base against the result
  mkdir "$T/tb" "$T/tn"; dd if="$OUT" of="$T/rootfs_new.ext4" bs=512 skip="$S2" count="$N2" status=none
  debugfs -R "rdump / $T/tb" "$T/rootfs_base.ext4" >/dev/null 2>&1; debugfs -R "rdump / $T/tn" "$T/rootfs_new.ext4" >/dev/null 2>&1
  GOT=$(diff -rq "$T/tb" "$T/tn" 2>/dev/null | sed -e "s|Only in $T/tn\(/*[^:]*\): \(.*\)|\1/\2|" -e "s|Files $T/tb\(/[^ ]*\) and .* differ|\1|" | sed 's|//|/|g' | sort | tr '\n' ' ')
  WANT=$(for d in $WANT_DIFF; do echo "$d"; done | sort | tr '\n' ' ')
  if [ "$GOT" = "$WANT" ]; then echo "OK   file-by-file comparison of the whole root filesystem: only the added/replaced files differ: $GOT"; else echo "FAIL root filesystem tree differs: got '$GOT' wanted '$WANT'"; rc=7; fi
fi
echo "IMAGE $OUT sha256 $(sha256sum "$OUT" | cut -d' ' -f1)"
[ $rc = 0 ] && echo "compose_image: ALL CHECKS PASS" || echo "compose_image: FAILED (rc=$rc); do not use $OUT"
exit $rc
