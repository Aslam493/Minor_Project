# COMPONENT 6, stages A and B: evidence (2026-11-12). Built in scratch directories only. NOTHING FLASHED, nothing written to the Pi.

Sources: optee_os commits `362452089` (IDLE_EXPIRED = `MODE_CHANGED` detail 2), `d9f0bfd3e` (UART ALIVE line behind `CFG_RPI3_CAN_MONITOR_UART`, default n), and the following commit (numbers printed without a local array). Design: `COMPONENT_6_DESIGN.md`. Pre-edit snapshot: `source_snapshots/optee_os_20261112_c6_pre/`.

## 1. Host tests (real sources, ASan + UBSan)
| Build | Result |
|---|---|
| Flag off (production) | **422 checks, 0 failed** (420 before; +2 for IDLE_EXPIRED: expiry writes detail 2, explicit return keeps detail 0). The new IDLE_EXPIRED check was written first and **failed** against the old code (detail 0), then passed |
| TEST_STATS build | 1481 checks, 0 failed |
| UART flag on | **439 checks, 0 failed** (17 UART tests U1-U10 incl. never-ready UART with 1,000,000 service calls and no byte written, burst limit 8, FIFO room 3, once per second, drop counting, 3 events per line oldest first, no mapping = off, decimal edge values) |
| Static checks | 58 pass / 0 FAIL on source; **61 pass / 0 FAIL with object code** on both images. New checks UART1-5 (single guarded write site; mapping/readiness only in init/service; no lock/heap/crypto in the service; hooks only at the two Core 1 sites; flag-off expands to nothing). Negative control: a planted unguarded UART write makes UART1 FAIL |

## 2. Real builds (PLATFORM=rpi3, scratch `build_scratch/c6_on` and `c6_on_uart`), both exit 0, 0 warnings
- **H1 object code** (no memcpy/memset/memcmp/crypto call inside any lock hold; critical-section helpers call nothing forbidden): PASS on both images.
- **Core 1 call graph** from `rpi3_can_server_loop`: UART-off image **72 functions, identical to the image on the Pi**; UART-on image 76 = the same 72 + `rpi3_mon_uart_service`, `_putc`, `_puts`, `_putu`. Forbidden-symbol path: only the existing accepted `__do_panic` path (`trace_printf <- __do_panic <- rpi3_mon_core1_entry_check`). My first UART version added `__stack_chk_fail` (a local digit buffer triggered the stack protector); the digit routine was rewritten without an array and the path is gone.
- **Stack depth** (static): both images worst case 2000 B of 4096 B (including the panic path), normal path **640 B**, headroom 3456 B, margin 2048 B met; the UART code adds nothing to the deepest path.
- **UART-off image against the image now on the Pi (`final_on_c16384`)**: 2436 functions, 135,107 instructions each, **0 functions with a different mnemonic sequence**. Operand differences: address-type instructions only (`adrp`/`ldr`/`add`, shifted data and string addresses), plus exactly three non-address ones, all expected: the OP-TEE revision constant in `tee_entry_get_os_revision` (the git revision changed), a panic-message line number in `rpi3_mon_core1_entry_check`, and **`mov w2, #0x0` -> `#0x2` in `rpi3_mon_evaluate` = the IDLE_EXPIRED change**.

## 3. Mistakes of mine during this work (kept)
1. U9 read locked state without the lock, and the UART tests did not set the all-exceptions-masked state Core 1 runs in: test bugs, fixed.
2. The static check "only memset is in rpi3_mon_init" and the boundary check ("untracked files") needed updating for legitimate reasons (init-time memset; files now committed); each change is explained in the script.
3. The first scratch builds used a relative output path and left 588 MB inside `optee_os/` (moved out; script now uses an absolute path, commit `8b8f1e1`).
4. **A false "61 pass":** after the first rebuild I ran the object-code checks while the build was still running and against EMPTY disassembly files (a stale `B exit=0` line from the previous round's log fooled my wait). The result was meaningless and was discarded; the checks above were run after verifying both builds, both `tee.elf` files and non-empty disassemblies (149,667 / 150,069 lines).

## 4. What is NOT done
- No armstub or card image has been composed from these builds; nothing flashed; the Pi still runs the previous image.
- The UART output has never run on hardware (no adapter connected yet). `T23` (UART stalled) and the visible `ALIVE` line need the board and the adapter.
- IDLE_EXPIRED is checked on the host and in object code only; board test `T21d` (in `c5_board_tests`) is ready to run on a flashed image.
- Core 1 timing with the UART service present has not been measured.
