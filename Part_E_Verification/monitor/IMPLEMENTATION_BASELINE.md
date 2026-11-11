# IMPLEMENTATION_BASELINE.md (Component 2.2 implementation, stage 1: preservation and baseline), 2026-11-06

Status labels: SNAPSHOT VERIFIED, BUILD PASS (baseline only). No source, document design text, board or SD card was changed in this stage.

## 1. Preservation
- Snapshot: `Part_E_Verification/source_snapshots/optee_os_20261106_pre_monitor/` (README.md inside has the restore steps).
- Contents: `git diff --binary` of the 9 modified tracked files, a copy of those 9 and of the untracked `rpi3_can_server.c`, the shipped `conf.mk`/`tee.bin`/`tee.elf`, and the `optee_full/build` repo's own uncommitted changes.
- Read-back: 22 of 22 checksums OK (after the README was added); a clean `git archive HEAD` export plus the saved diff reproduces the live tree with 0 mismatches. The `optee_os` repo HEAD is a7ccc63e41c4acc5ec73189e5971c3345765e7e2 on branch secure-can-spi-poc, 9 modified + 1 untracked files, diff stat 9 files, 1932 insertions, 38 deletions, before and after the snapshot and after the build.

## 2. Baseline build (BUILD PASS for the unchanged tree)
- Command: `Part_E_Verification/scripts/build_optee_scratch.sh <dir>` (always PLATFORM=rpi3), output `Part_E_Verification/build_scratch/baseline_stacks_on/`, 0 errors, 0 warnings, exit 0.
- Result: `tee.bin` is 737,060 bytes, same as the shipped one. **It differs from the shipped `out/arm/core/tee.bin` in 13 bytes, all of them the embedded build date string** ("Fri Nov  6 18:29:12" against "Sat Sep 19 21:00:19"). The configuration (every CFG_ value) is identical to the shipped `conf.mk`.
- `optee_os/out/arm/` was not touched (shipped `tee.bin` sha256 begins d6c1784d198e2d6feaf4 before and after).

## 3. Finding: the shipped build used a flag that no makefile sets
Running the project makefile's own dry run (`make -n -f build/rpi3.mk optee-os-common`) yields `CFG_CORE_DEBUG_CHECK_STACKS=n`; the shipped image was built with `CFG_CORE_DEBUG_CHECK_STACKS=y` (default in `mk/config.mk:509` is `n`; no rpi3 file overrides it). It was therefore set by a one-off command-line or environment override that is not recorded anywhere. Building with the project makefile as it stands would produce a **different** secure OS than the one on the Pi: no `-finstrument-functions` hooks.

Consequence for the design: every cost and panic-site statement (F8, about 2.4 us per instrumented call, the roughly 10 us lock cycle, the 2.435 us Core 1 pass, the 256-pass cadence of about 0.62 ms, the attempt cap of 2,000 against a 1 ms budget, CS3 and the hook-free rule) was derived from the **stacks-on** build. With stacks off, calls cost nanoseconds: a lock attempt would take tens of nanoseconds, so the attempt cap of 2,000 would be reached in roughly 100 us, long before the 1 ms budget, and the Core 1 pass and cadence would be far shorter.

**Decision needed (DEC-BUILD-1):** which configuration is the product? Recommended: keep stacks **on** (it reproduces the shipped image and matches every figure in the design), pass it explicitly (the script does), and state the assumption in the design; the other configuration would need its parameters re-derived.

## 4. What this stage did not do
No design text, source, FIQ, SD card, device tree or board change. The shipped `out/arm` build and the running Pi are untouched.
