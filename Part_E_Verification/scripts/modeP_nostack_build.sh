#!/bin/bash
# Rebuild the Mode P OP-TEE core with ONLY ONE change against the image that runs now: CFG_CORE_DEBUG_CHECK_STACKS=n (the shipped build has =y, which compiles the whole
# core with -finstrument-functions; measured on 2026-11-21: a trivial system call into the core costs 240 us, an HMAC about 1 ms). The monitor source is NOT touched.
# Builds the four configurations (flag off, on, pull, debug) in scratch directories build_scratch/ns_*, runs the host tests and the static checks on them, builds the TF-A armstub
# for the pull configuration. Report: baseline/modeP_nostack_build_20261121.txt. Never touches a board or a card.
cd "$(dirname "$0")/.." || exit 1
R=baseline/modeP_nostack_build_20261121.txt; OS=../optee_full/optee_os; OD=../optee_full/toolchains/aarch64/bin/aarch64-linux-gnu-objdump
{ echo "# Mode P core with CFG_CORE_DEBUG_CHECK_STACKS=n, build and verification, $(date -Is)"
  echo "optee_os HEAD $(git -C $OS rev-parse HEAD) ($(git -C $OS log -1 --format=%s | cut -c1-90)); uncommitted changes in optee_os: $(git -C $OS status --short | wc -l)"
  echo "Part_E HEAD $(git rev-parse --short HEAD)"; } > $R
b() { n=$1; shift; bash scripts/build_optee_scratch.sh build_scratch/ns_$n "$@" CFG_CORE_DEBUG_CHECK_STACKS=n > build_scratch/ns_$n.log 2>&1; rc=$?
  w=$(grep -c "warning:" build_scratch/ns_$n.log); $OD -d build_scratch/ns_$n/core/tee.elf > build_scratch/ns_$n.dis 2>/dev/null
  echo "BUILD $n ($* CFG_CORE_DEBUG_CHECK_STACKS=n): make exit=$rc, compiler warnings=$w, disassembly lines=$(wc -l < build_scratch/ns_$n.dis), instrumented hook calls in the object code=$(grep -c cyg_profile build_scratch/ns_$n.dis), tee-pager_v2.bin sha256=$(sha256sum build_scratch/ns_$n/core/tee-pager_v2.bin | cut -c1-16)" >> $R; }
b off
b on CFG_RPI3_CAN_MONITOR=y
b pull CFG_RPI3_CAN_MONITOR=y CFG_RPI3_CAN_MONITOR_PULL=y
b debug CFG_RPI3_CAN_MONITOR=y CFG_RPI3_CAN_MONITOR_DEBUG=y
echo "== for comparison: the shipped Mode P core (CHECK_STACKS=y), instrumented hook calls in its object code: $(grep -c cyg_profile build_scratch/p6_pull.dis)" >> $R
echo "== host tests (the monitor source is unchanged; they do not depend on the flag)" >> $R
( cd monitor/source_tests; for t in test test_ts test_uart test_pull; do make clean >/dev/null 2>&1; echo "make $t: $(make $t 2>&1 | grep -E '^total:|error:' | tail -1)"; done; make clean >/dev/null 2>&1
  echo "== static checks (text + object code on ns_on and ns_pull)"
  ON_DIS=$PWD/../../build_scratch/ns_on.dis PULL_DIS=$PWD/../../build_scratch/ns_pull.dis bash static_checks.sh 2>&1 | grep -E "SKIPPED|FAIL|static_checks:" ) >> $R 2>&1
echo "== armstub" >> $R
bash scripts/build_candidate_armstub.sh build_scratch/ns_pull/core build_scratch/armstub_ns > build_scratch/armstub_ns.log 2>&1; echo "TF-A armstub build exit=$?, warnings=$(grep -c 'warning:' build_scratch/armstub_ns.log)" >> $R
ls -l build_scratch/armstub_ns/rpi3/debug/armstub8.bin >> $R 2>&1
echo "armstub8.bin sha256: $(sha256sum build_scratch/armstub_ns/rpi3/debug/armstub8.bin | cut -d' ' -f1)" >> $R
echo DONE >> $R
