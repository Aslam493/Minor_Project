#!/bin/bash
# Rebuild everything for the Mode P image with the request-latency page (P6) from the COMMITTED optee_os, verify it, compose the image.
# Writes build_scratch/p6_* and candidate_image/rpi3-sdcard-monitor-modeP-p6.img; report in baseline/modeP_p6_preflash_20261118.txt. Never touches a board or a card.
cd "$(dirname "$0")/.." || exit 1
R=baseline/modeP_p6_preflash_20261118.txt; OS=../optee_full/optee_os; OD=../optee_full/toolchains/aarch64/bin/aarch64-linux-gnu-objdump
CI=build_scratch/candidate_image; BASE=$CI/rpi3-sdcard-monitor-hardened-c16384.img; IMG=$CI/rpi3-sdcard-monitor-modeP-p6.img
{ echo "# Mode P + request latency page, pre-flash build and verification, $(date -Is)"
  echo "optee_os HEAD $(git -C $OS rev-parse HEAD) ($(git -C $OS log -1 --format=%s | cut -c1-90)); uncommitted changes in optee_os: $(git -C $OS status --short | wc -l)"
  echo "Part_E HEAD $(git rev-parse --short HEAD)"; } > $R
b() { n=$1; shift; rm -rf build_scratch/p6_$n; bash scripts/build_optee_scratch.sh build_scratch/p6_$n "$@" > build_scratch/p6_$n.log 2>&1; rc=$?
  w=$(grep -c "warning:" build_scratch/p6_$n.log); $OD -d build_scratch/p6_$n/core/tee.elf > build_scratch/p6_$n.dis 2>/dev/null
  echo "BUILD $n ($*): make exit=$rc, compiler warnings=$w, disassembly lines=$(wc -l < build_scratch/p6_$n.dis), tee-pager_v2.bin sha256=$(sha256sum build_scratch/p6_$n/core/tee-pager_v2.bin | cut -c1-16)" >> $R; }
b off
b on CFG_RPI3_CAN_MONITOR=y
b pull CFG_RPI3_CAN_MONITOR=y CFG_RPI3_CAN_MONITOR_PULL=y
b debug CFG_RPI3_CAN_MONITOR=y CFG_RPI3_CAN_MONITOR_DEBUG=y
echo "== host tests" >> $R
( cd monitor/source_tests; for t in test test_ts test_uart test_pull; do make clean >/dev/null 2>&1; echo "make $t: $(make $t 2>&1 | grep -E '^total:|error:' | tail -1)"; done; make clean >/dev/null 2>&1
  echo "== static checks (text + object code on p6_on and p6_pull)"
  ON_DIS=$PWD/../../build_scratch/p6_on.dis PULL_DIS=$PWD/../../build_scratch/p6_pull.dis bash static_checks.sh 2>&1 | grep -E "SKIPPED|FAIL|static_checks:"
  echo "== mutation_pull"; python3 mutation_pull.py -j 3 2>&1 | tail -1 ) >> $R 2>&1
echo "== armstub and image" >> $R
bash scripts/build_candidate_armstub.sh build_scratch/p6_pull/core build_scratch/armstub_p6 > build_scratch/armstub_p6.log 2>&1; echo "TF-A armstub build exit=$?, warnings=$(grep -c 'warning:' build_scratch/armstub_p6.log)" >> $R
A=build_scratch/armstub_p6/rpi3/debug/armstub8.bin; cp $A $CI/armstub8.monitor-modeP-p6.bin
# 2026-11-21: compose through compose_image.sh, which always installs the N-04 DTB (the old 'cp + mcopy armstub only' dropped the N-04 reservation)
bash scripts/compose_image.sh $BASE $IMG $A >> $R || { echo 'compose_image FAILED' >> $R; exit 1; }
T=$(mktemp -d); mcopy -n -i $IMG@@1048576 ::armstub8.bin $T/rb.bin; cmp $T/rb.bin $A && echo "read-back of armstub8.bin from the image equals the built file" >> $R
mkdir $T/n $T/b; mcopy -s -n -i $IMG@@1048576 :: $T/n >/dev/null 2>&1; mcopy -s -n -i $BASE@@1048576 :: $T/b >/dev/null 2>&1
echo "boot-partition files that differ from the base image (hardened c16384): $(diff -rq $T/b $T/n | sed 's|'$T'/[bn]/||g;s/Files //;s/ and .* differ/ differs/' | tr '\n' ' ')" >> $R
cmp -i $((133120*512)) $BASE $IMG && echo "rootfs partition (sector 133120 on) is byte-identical to the base image" >> $R
rm -rf $T
( cd $CI; sed -i '/modeP-p6/d' SHA256SUMS; sha256sum rpi3-sdcard-monitor-modeP-p6.img armstub8.monitor-modeP-p6.bin >> SHA256SUMS; echo "IMAGE sha256: $(grep ' rpi3-sdcard-monitor-modeP-p6.img' SHA256SUMS | cut -d' ' -f1)"; echo "ARMSTUB sha256: $(grep ' armstub8.monitor-modeP-p6.bin' SHA256SUMS | cut -d' ' -f1)" ) >> $R
echo "== mutation_real (flag off, 144)" >> $R
( cd monitor/source_tests; python3 mutation_real.py -j 3 2>&1 | grep -v '^caught (' ) >> $R 2>&1
echo DONE >> $R
