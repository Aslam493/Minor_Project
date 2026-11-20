#!/bin/bash
# compose_image.sh BASE.img OUT.img [ARMSTUB.bin]
# The ONE place where a candidate SD image is composed. It copies BASE to OUT and puts these files into the FAT boot partition:
#   - bcm2710-rpi-3-b.dtb  = the N-04 device tree (monitor/n04/candidate-bcm2710-rpi-3-b.dtb, sha256 e8ac38fa...), ALWAYS
#   - armstub8.bin         = ARMSTUB.bin, only if given
# Why the DTB is always installed: the candidate images are the Sep-20 image with files swapped in, and that image carries the ORIGINAL device tree
# (6b90c3e0..., no reservation of the secure DRAM 0x10000000-0x10ffffff). Flashing such an image on 2026-11-18 removed the N-04 reservation that had been
# installed by hand on 2026-11-12 (board_full_check C3 FAIL on 2026-11-21). The full Buildroot build already applies the reservation (build/rpi3.mk,
# RPI3_OPTEE_RESERVE_DTBO); this script covers the swap-in route.
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
O1=$((S1*512)); O2=$((S2*512))
cp "$BASE" "$OUT" || exit 2
mcopy -o -i "$OUT@@$O1" "$N04" ::bcm2710-rpi-3-b.dtb || { echo "FAIL: mcopy dtb"; exit 4; }
[ -z "$ARM" ] || mcopy -o -i "$OUT@@$O1" "$ARM" ::armstub8.bin || { echo "FAIL: mcopy armstub"; exit 4; }
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; rc=0
mcopy -n -i "$OUT@@$O1" ::bcm2710-rpi-3-b.dtb "$T/dtb.rb" && [ "$(sha256sum "$T/dtb.rb" | cut -d' ' -f1)" = "$N04_SHA" ] && echo "OK   DTB read back from $OUT: sha256 ${N04_SHA:0:16}... (N-04)" || { echo "FAIL DTB read-back"; rc=5; }
if [ -n "$ARM" ]; then mcopy -n -i "$OUT@@$O1" ::armstub8.bin "$T/arm.rb" && cmp -s "$T/arm.rb" "$ARM" && echo "OK   armstub8.bin read back equals $ARM" || { echo "FAIL armstub read-back"; rc=5; }; fi
mkdir "$T/b" "$T/n"; mcopy -s -n -i "$BASE@@$O1" :: "$T/b" >/dev/null 2>&1; mcopy -s -n -i "$OUT@@$O1" :: "$T/n" >/dev/null 2>&1
D=$(diff -rq "$T/b" "$T/n" | sed "s|$T/[bn]/||g;s/Files //;s/ and .* differ//" | sort | tr '\n' ' ')
WANT="bcm2710-rpi-3-b.dtb "; [ -z "$ARM" ] || WANT="armstub8.bin bcm2710-rpi-3-b.dtb "
if [ "$D" = "$WANT" ]; then echo "OK   boot-partition files that differ from the base: $D"; else echo "FAIL boot-partition files that differ: '$D' (wanted '$WANT'; a DTB identical to the base would also show here)"; rc=6; fi
cmp -s -n "$O1" "$BASE" "$OUT" && echo "OK   first $O1 bytes (partition table) identical to the base" || { echo "FAIL partition table area differs"; rc=7; }
cmp -s -i "$O2" "$BASE" "$OUT" && echo "OK   root filesystem partition (byte $O2 on) identical to the base" || { echo "FAIL root filesystem differs"; rc=7; }
echo "IMAGE $OUT sha256 $(sha256sum "$OUT" | cut -d' ' -f1)"
[ $rc = 0 ] && echo "compose_image: ALL CHECKS PASS" || echo "compose_image: FAILED (rc=$rc); do not use $OUT"
exit $rc
