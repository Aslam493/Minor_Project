# NEXT_STEPS.md (rewritten 2026-11-12; the 2026-11-09 handoff it replaces is in git history and `source_snapshots/20261112_u0_pre/`)

## State right now (checked on the Pi 2026-11-12)
- Pi 3 B at 10.237.27.187 runs the hardened c16384 monitor image from the **8 GB spare card** (`SD8GB`, serial 0x6d280b3c); the 32 GB original was never written. DTB = the N-04 version. `verify_boot` 16 pass / 0 FAIL / 2 skipped. `teeclnt` is empty.
- Components: C1 done (hardware items open), C2 closed, C3 done with accepted deviations, C4 closed with limits, **C5 done with limits (accepted)**, **C6 partial**, C7 not started (blocked: no second CAN board, no sender), C8 not started.
- Built and host-tested but **not flashed**: IDLE_EXPIRED and the UART ALIVE line (flag default off). Unit U1 (host tests T11a/T11b/T10b) done 2026-11-12.

## What to read first
`Secure_CAN_Monitor_Plan_8_Components.md` (board + dated log), then `monitor/PLAN_REMAINING_WORK_2026-11-12.md` (units U0-U8, decisions D-A to D-F, schedule, cut rule).

## Open decisions (from the plan)
D-A F27 coalescing (the limiter already exists); D-B contention hint signal; D-C UART stays off; D-D one image; D-E pushes (top-level only, nested repos on explicit OK); D-F software-injected soaks accepted.

## Routine for any card swap
`scripts/card_helper.sh check <image>` (refuses anything but the 8 GB spare, prints the dd/cmp commands for the user to run), `identify`, then `pi <ip>` after boot. Standing rules: never write the 32 GB original; board tests and any forced fault need explicit approval; always `PLATFORM=rpi3`; build in scratch directories with an ABSOLUTE output path; prototype keys are not production; detection only, no prevention claim. Never trust an object-code check without first confirming the build finished and the disassembly is non-empty.
