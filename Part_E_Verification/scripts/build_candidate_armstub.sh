#!/bin/bash
# Build a candidate armstub8.bin (TF-A + OP-TEE + U-Boot) into a SCRATCH directory, never into trusted-firmware-a/build or optee_os/out.
# usage: build_candidate_armstub.sh <dir with tee-header_v2.bin tee-pager_v2.bin tee-pageable_v2.bin> <scratch-out-dir>
# Uses the exact TF_A_FLAGS of optee_full/build/rpi3.mk; only BL32* and BUILD_BASE differ. Reads the TF-A and U-Boot trees, writes nothing there.
set -e
TEE="$(cd "${1:?tee dir}" && pwd)"; OUT="${2:?out dir}"; mkdir -p "$OUT"; OUT="$(cd "$OUT" && pwd)"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)/optee_full"
CC="$ROOT/toolchains/aarch64/bin/aarch64-linux-gnu-"
exec make -C "$ROOT/trusted-firmware-a" CROSS_COMPILE="$CC" LDFLAGS="--no-warn-rwx-segments" NEED_BL32=yes \
  BL32="$TEE/tee-header_v2.bin" BL32_EXTRA1="$TEE/tee-pager_v2.bin" BL32_EXTRA2="$TEE/tee-pageable_v2.bin" BL33="$ROOT/u-boot/u-boot.bin" \
  DEBUG=1 V=0 CRASH_REPORTING=1 LOG_LEVEL=40 PLAT=rpi3 RPI3_PRELOADED_DTB_BASE=0x00010000 SPD=opteed BUILD_BASE="$OUT" all fip
