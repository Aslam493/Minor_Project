# IMPLEMENTATION_STAGE1.md: Component 2.2, stage 1 (PTA-side synchronization foundation), 2026-11-07

Status of the stage: **IMPLEMENTED, SOURCE TEST PASS (host, real file), BUILD PASS (monitor off and on), BOARD: NOT RUN.** Stage 2 has NOT been started.
Labels used: SOURCE TEST PASS = run against the real `rpi3_can_monitor.c` through the host harness with a mock platform; STATIC REVIEW PASS = scripted or read check of the real files; BUILD PASS = OP-TEE scratch build with `PLATFORM=rpi3`; OBJECT-CODE = inspection of the compiled object; BOARD = nothing, no board was touched.

## What changed in the OP-TEE tree (complete diff: `STAGE1_DIFF.patch`)
| File | Change |
|---|---|
| `plat-rpi3/conf.mk` | +10 lines: `CFG_RPI3_CAN_MONITOR ?= n`, `CFG_RPI3_CAN_MONITOR_DEBUG ?= n`, build error when the monitor is on without the PTA |
| `plat-rpi3/sub.mk` | +1 line: `srcs-$(CFG_RPI3_CAN_MONITOR) += rpi3_can_monitor.c` |
| `plat-rpi3/rpi3_can_monitor.h` (new) | state structures and ownership, constants (K11), event types (K10), G7 accessors, API, compile-time guards |
| `plat-rpi3/rpi3_can_monitor_plat.h` (new) | the OP-TEE bindings (`MON_PLAT_*`) |
| `plat-rpi3/rpi3_can_monitor.c` (new) | `rpi3_mon_init`, `init_ok`, bounded `rpi3_mon_acquire`/`rpi3_mon_release`, health pair |
Nothing else in the tree changed: the other 8 modified files and `rpi3_can_server.c` are byte-identical to the pre-change snapshot (checked by `static_checks.sh`). The FIQ handler and the server were not modified. No PTA command, no Core 1 hook, no HMAC, no events logic.

## Evidence (every line is a command that was run)
| What | Command | Result |
|---|---|---|
| Real-source host tests | `cd source_tests && make test` (AddressSanitizer + UBSan, `-Wall -Wextra -Werror`) | **61 checks, 0 failed**, including a 4-thread stress test on the real lock word (79,567 successes, 433 BUSY, counters exact) |
| Mutation test of the real file | `python3 mutation_real.py` | **35 of 37** injected bugs caught; the 2 survivors are equivalent mutants (explained below) |
| Static checks | `./static_checks.sh` | **21 pass, 0 fail, 1 pending** (Y7: the server file is unchanged until the Core 1 stage); negative control: 6 injected problems all reported FAIL |
| Object-code inspection | `./objcheck.sh <rpi3_can_monitor.o>` | no memcpy/memcmp/memset (except init), no heap, no printing, no crypto in any lock or health function |
| Build, monitor OFF | `scripts/build_optee_scratch.sh .../stage1_off CFG_RPI3_CAN_MONITOR=n` | exit 0, 0 errors, 0 warnings; `tee.bin` same size as the baseline, differs in 5 bytes (the embedded build time only); CFG set differs only by the two new lines; 0 monitor symbols |
| Build, monitor ON | `... stage1_on CFG_RPI3_CAN_MONITOR=y` | exit 0, 0 errors, 0 warnings; `rpi3_mon` and `rpi3_mon_init` present; `tee.bin` +64 bytes |
| Config guard | `... CFG_RPI3_CAN_MONITOR=y CFG_RPI3_CAN_SPI_POC_PTA=n` | fails with `conf.mk:33: CFG_RPI3_CAN_MONITOR=y requires CFG_RPI3_CAN_SPI_POC_PTA=y` |
| Shipped build untouched | sha256 of `out/arm/core/tee.bin` | `d6c1784d198e2d6feaf4...`, unchanged |

Equivalent mutants: (1) removing the explicit `now < t0` term, because unsigned wraparound of `now - t0` already ends the loop; (2) removing the explicit lock assignment in init, because the `memset` just before it already zeroes the lock.

## Status of the inventoried checks touched by this stage (real source only)
| Label | Checks |
|---|---|
| SOURCE TEST PASS (host, real file) | X1 X2 X3 X4 X5 X6 X7; V6 V7 (counter increments and no increment on success); V9 (wrap of the counter, the Core 1 delta is not implemented); HP4 (health sentinels and saturation); Z1 (control); BG5 (the same file compiles on the host and in OP-TEE); new INIT-1 to INIT-5 (init, failure handling, `init_ok` last) |
| STATIC REVIEW PASS (stage 1 files only) | V1 (also compile-time), V3, V8, W5, X8, Y8, Z2, H2, H4, TM1, TM2, Q2, EV1 (25 entries, same order as K10), BG1 |
| BUILD PASS | V5 (both configurations, `PLATFORM=rpi3`), BG2, BG3 (OFF equals the baseline except the build time), BG4 |
| OBJECT-CODE | H1 for the lock and health functions (no critical-section code exists yet) |
| Partly exercised, not claimed | W3 (init failure leaves `init_ok` 0; the "every command returns an error" half needs the commands), V4 (call path read, not run on the target), V10/V11/HP2/HP3/HP5/HP6 (need later stages) |
| NOT RUN | everything that needs a command, Core 1, HMAC or the board, including V2 V14 W1 W2 W4 W7 X9 Y10 H3 |
| PENDING | Y7 (D1 = Option B): the guard and the runtime assertion in `rpi3_can_server.c` belong to the Core 1 stage |
The statuses in the design document's inventory are NOT changed yet; that waits for your review of this stage.

## Findings and deviations (decisions for you)
1. **The real lock cycle has seven instrumented function entries, not about four.** From the object code: `rpi3_mon_acquire`, `cpu_spin_trylock`, `spinlock_count_incr`, `rpi3_mon_release`, `cpu_spin_unlock_xrestore`, `cpu_spin_unlock`, `spinlock_count_decr` (each with an enter and an exit hook; `thread_mask/unmask_exceptions`, `__cpu_spin_trylock` and `__cpu_spin_unlock` are hook-free). The design text (G9, L4, F26) says about four and about 10 us per cycle. At the design's own 2.4 us per instrumented call (an inference from F8) that is about 17 us and a hold floor of about 12 us, still under the 100 us target, but unmeasured. I have NOT changed the design numbers.
2. `rpi3_mon_health_fill` calls `rpi3_mon_init_ok()` as a normal (hooked) call. It runs after the unlock, so no rule is violated; it could be inlined.
3. Deviations from the letter of the design, all small and flagged: (a) 32-bit lock-free loads and stores use the `__atomic` builtins directly (the same operation `atomic_load_u32` performs, without the hook that an inlined `atomic_load_u32` still carries); (b) the lock sits alone on its own 64-byte line inside the state struct (the design asked only for 4-byte alignment; the name `rpi3_mon_lock` is kept by a macro); (c) `rpi3_mon` is exported, not `static`, because the Core 1 loop must update G7 inline in a later stage; (d) three event names are abbreviated in the enum (`LEGACY_REFUSED`, `LEGACY_USED`, `DUP_ANSWER`).
4. In the linked ON image only `rpi3_mon_init` and the state survive, because nothing calls the other functions yet and the linker drops them; they are therefore verified at the object level.
5. `boot_id` is 0 and the MAC contexts are NULL; later stages must add their init steps **before** the `init_ok` publication (a comment at the hook says so).

## Remaining risks
- Nothing has run on OP-TEE or on the board. The host mock encodes my reading of OP-TEE's assertions (trylock needs IRQ masked, IRQ must not be unmasked while a spinlock is held); the real behaviour, the timer on Core 1, and memory visibility (W1) are unverified.
- All timing figures assume the stacks-ON build (DEC-BUILD-1) and are estimates.
- Stage 2 will touch `core/pta/rpi3_can_spi_poc.c` and `pta_rpi3_can_spi_poc.h`, which carry your uncommitted work: a fresh snapshot and a minimal, reviewed change will be needed there.
