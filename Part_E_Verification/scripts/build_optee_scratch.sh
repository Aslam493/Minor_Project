#!/bin/bash
# Build optee_os for the Raspberry Pi 3 into a SCRATCH output directory. Never touches optee_os/out/.
# usage: build_optee_scratch.sh <output-dir> [extra make variables...]
# It always passes PLATFORM=rpi3 and the exact variables the project makefile (build/rpi3.mk) prints, plus
# CFG_CORE_DEBUG_CHECK_STACKS=y, which the shipped Sep-20 build used but no makefile sets (see IMPLEMENTATION_BASELINE.md).
# Pass CFG_CORE_DEBUG_CHECK_STACKS=n as an extra argument to build the other configuration.
set -e
OUT="${1:?usage: $0 <output-dir> [extra make vars]}"; shift || true
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
T="$ROOT/optee_full/toolchains"
mkdir -p "$OUT"
exec make -j"$(nproc)" -C "$ROOT/optee_full/optee_os" O="$OUT" \
  CFG_USER_TA_TARGETS=ta_arm64 CFG_ARM64_core=y PLATFORM=rpi3 \
  CROSS_COMPILE="$T/aarch64/bin/aarch64-linux-gnu-" CROSS_COMPILE_core="$T/aarch64/bin/aarch64-linux-gnu-" \
  CROSS_COMPILE_ta_arm64="$T/aarch64/bin/aarch64-linux-gnu-" CROSS_COMPILE_ta_arm32="$T/aarch32/bin/arm-linux-gnueabihf-" \
  CFG_TEE_CORE_LOG_LEVEL=3 DEBUG=0 CFG_IN_TREE_EARLY_TAS="trusted_keys/f04a0fe7-1f5d-4b9b-abf7-619b85b4ce8c" \
  CFG_CORE_DEBUG_CHECK_STACKS=y "$@"
