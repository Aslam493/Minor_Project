# NEXT_STEPS.md (handoff written 2026-11-09; read this first)

## State right now
- Pi 3 B runs the hardened production-profile image `rpi3-sdcard-monitor-hardened-c16384.img` from the 8 GB spare card (address alternates between 10.237.27.187 and .215). The 32 GB original card was never written. Nothing is left on the Pi's /tmp.
- Components: C1 gate met (hardware items open), C2 closed (51/53 PASS), C3 complete (deviations recorded), C4 PARTIAL (gate NOT met). C5 to C8 not started.
- Mutation re-run finished: 141 of 144 caught, the same 3 equivalent survivors (`source_tests/mutation_final_20261109.txt`). Nothing to redo.

## Decisions needed from the user (they unblock C3/C4 closure)
1. Define **R**, **grace** and **N_req** (plan rules `32T >= 1.25 (grace + R)` and `ring >= N_req`; derive_params.py reports both NOT CHECKED). Known: with grace = the 60 s boot grace the first rule cannot hold; with N_req = 128 the 64-slot ring fails.
2. Ring size: keep 64 slots (24 pinned + 40 ordinary, decision G8) or move to the plan's 128.
3. Confirm the **2048-byte stack margin** (C3.3) and accept the C3 deviations (single flag, 3 legacy symbols dropped by D1) and the C4 deviation (TEST_STATS build instead of CFG_RPI3_CAN_LOOP_STATS).
4. A CAN sender or second board (floods, gap histogram), or accept them as NOT DONE.
5. Make an external copy of `SD_Card_Backups/` (verify with `baseline/SD_Card_Backups_MANIFEST_20261109.sha256`).

## Suggested order tomorrow
1. Read the mutation result. 2. Get decisions 1 to 3. 3. Start C5 (heartbeat + traffic deadline; most software and board tests already exist from the C2 work, the plan's T1-T25 list is still not found in the repository: ask for it). 4. Optional Q4: measure the hold time of command 18 (needs the test image and one swap).

## Routine for any card swap
`scripts/card_helper.sh check <image>` (refuses anything but the 8 GB spare, prints the dd/cmp commands for the user to run), `identify`, then `pi <ip>` after boot. Standing rules: never write the 32 GB original; board tests and any forced fault need explicit approval; always PLATFORM=rpi3; build in scratch directories; prototype keys are not production; detection only, no prevention claim.
