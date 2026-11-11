# IMPLEMENTATION_STAGES2-6.md (Component 2.2, stages 2 to 6)

> **STATUS UPDATE 2026-11-09:** `FINAL_DIFF.patch` was regenerated on 2026-11-09 against `optee_os_20261106_pre_monitor` (2252 lines, now including the test-only W1/W7/stack/statistics code and cadence 16384); the line count quoted below (1867) is historical.


Date: 2026-11-07. Scope: shared-state logic, PTA commands 17-23 and glue, keys/HMAC, Core 1 integration (decision D1 = Option B), audits.
**No board action of any kind was taken.** Nothing was flashed, rebooted, or copied to the Pi. No FIQ armed. W7 not run.
Status labels: HOST (real-source host test), STATIC, BUILD, OBJECT (object-code inspection), BOARD. BOARD items are all NOT RUN.

## What changed in the OP-TEE tree (complete diff: `FINAL_DIFF.patch`, 1867 lines, against `optee_os_20261106_pre_monitor`)

| File | Change | Lines |
|---|---|---|
| `core/arch/arm/plat-rpi3/rpi3_can_monitor.c` | NEW: all monitor logic (events, evaluation, frame rules, HMAC, commands 17-23, legacy note, Core 1 attempt) | 1037 |
| `core/arch/arm/plat-rpi3/rpi3_can_monitor.h` | NEW: constants, static asserts, structs, accessors, prototypes, inline Core 1 pass | 296 |
| `core/arch/arm/plat-rpi3/rpi3_can_monitor_plat.h` | NEW: OP-TEE bindings (`MON_PLAT_*`), AArch64 only | 87 |
| `core/arch/arm/plat-rpi3/rpi3_can_monitor_keys.h` | NEW: three PROTOTYPE keys; included by the `.c` only | 33 |
| `core/pta/rpi3_can_monitor_pta.c` | NEW: PTA glue, parameter checking, dispatch, legacy policy | 245 |
| `core/pta/rpi3_can_spi_poc.c` | +25 -1, all under `CFG_RPI3_CAN_MONITOR` (include, dispatch hook, flags macro + static assert no `CONCURRENT`) | |
| `lib/libutee/include/pta_rpi3_can_spi_poc.h` | +20 (command ids 17-23 and comments) | |
| `core/arch/arm/plat-rpi3/rpi3_can_server.c` | +22 -0, `#ifdef CFG_RPI3_CAN_MONITOR` / `#else` (original lines kept in `#else`) | |
| `conf.mk`, plat `sub.mk`, `core/pta/sub.mk` | additions only (+10, +1, +1) | |

Not touched (byte-identical, checked): `rpi3_can_fiq.c` (FIQ handler), both thread assembly files, `thread_arch.h`, `teesmc_opteed.h`, `platform_config.h`, `optee_os/out/arm`.
Final source hashes (first 16 hex): monitor.c `353bb91bc25edcb2`, monitor.h `40e679be08530065`, keys.h `39f098af363bc918`, plat.h `9ad673eff956c954`, glue `dd467e866c49d9dd`, spi_poc.c `a4b1b1defd193722`, server.c `6875f66d6a14606d`.

## Evidence (each line is a command that was run on the final tree)

| # | What | Result | Label |
|---|---|---|---|
| E1 | `make` in `source_tests` (real `rpi3_can_monitor.c` + real glue, `-Wall -Wextra -Werror`, ASan+UBSan, independent Python HMAC vectors, 2 stress tests with Core 1 + 3 PTA contexts) | **406 checks, 0 failed**, stable over repeated runs | HOST (SOURCE TEST PASS) |
| E2 | `mutation_real.py`: 140 one-bug mutants of the real `.c`/`.h`/glue | **137 caught, 3 survivors, all analysed as equivalent** (see below); 0 skipped, 0 compile errors | HOST |
| E3 | `static_checks.sh` (53 checks incl. boundaries) | **53 pass, 0 FAIL**; negative control (injected memcpy in `evaluate`, HMAC call in Core 1, unlock in `acquire`) failed 5 checks as it should | STATIC REVIEW PASS |
| E4 | OP-TEE build, Monitor **OFF**, `PLATFORM=rpi3`, scratch dir, real flags + stack checks ON | exit 0, 0 warnings, 0 errors | BUILD PASS |
| E5 | OP-TEE build, Monitor **ON** (hardened profile), scratch dir | exit 0, **0 warnings, 0 errors** (3 warnings found in the first ON build were fixed: two strict-aliasing, one missing `default`); tee.bin 749,412 B; 42 `rpi3_mon*` symbols | BUILD PASS |
| E6 | OFF vs the baseline build (`baseline_stacks_on`) | same size (737,060 B); `tee.elf` disassembly differs in exactly **3 instructions**, all `mov w1,#<line>` trace line numbers in `rpi3_can_spi_poc.c` (186->189, 380->394, 411->425) because the guarded edits add lines; the other differing bytes are build-date characters (final measurement: 10 differing bytes = 5 date/time characters + 5 bytes inside those 3 instructions). 0 `rpi3_mon` symbols in OFF. Monitor OFF is functionally identical to the shipped build | BUILD PASS (BG3/DS2 evidence) |
| E7 | F27/Z7 call graph from `rpi3_can_server_loop` in the ON `tee.elf` (`callgraph.py`, 72 reachable functions) | no `crypto_*`, HMAC/MAC, `malloc/calloc/realloc/free`, `mempool`, `nex_*` reachable. `trace_printf` is reachable only through `__do_panic` (terminal failure path). Negative control: `rpi3_mon_cmd17` reaches 6 forbidden symbols and is detected | OBJECT |
| E8 | Object-code calls of every `rpi3_mon_*` function (`objcalls_monitor.txt`) | the two critical-section helpers (`rpi3_mon_evaluate`, `rpi3_mon_ev_append`) call only each other; no `memcpy/memset/memcmp` in them. **CORRECTED 2026-11-07:** the original text here said every `memcpy` in cmd17/18/20 is outside the hold. That is **false for cmd17**: GCC turned the byte loop in `rpi3_mon_frame_store` into a `memcpy` (44 bytes) that runs inside the hold (control-flow analysis, `source_tests/hold_memcpy_closure_20261107.txt`). cmd18 and cmd20 copy-outs are outside the hold. This breaks design rule CS3 and original check H1 (FAIL in `COMPONENT_2_2_INVENTORY.md`); the fix is proposed in `COMPONENT_2_2_FINAL_CLOSURE.md` section 8 and is not applied | OBJECT |
| E9 | Keys in the loaded image | `tee.bin` contains the frame key once and the heartbeat key once; the **test key 0 times**. (`tee.elf` debug macro data names the macros; it is not loaded on the board) | OBJECT |

Equivalent survivors (no behaviour differs): (1) dropping `now < t0` in `acquire` - the unsigned subtraction then wraps to a huge value that already exceeds the budget, so the path still fails closed; (2) dropping the explicit `lock = SPINLOCK_UNLOCK` in init - the preceding `memset` already zeroes it; (3) dropping `last_counter != 0xFFFFFFFF` in the next-counter rule - when `last == 0xFFFFFFFF`, `counter == last+1` means `counter == 0`, which is already below `last` and is classified REPLAY before that branch.

Test gaps found by mutation in this stage and fixed (6): heartbeat timeout after a single miss (test never checked "not yet raised"), REPLAY/DUPLICATE/CONFLICT detail codes, last tag byte not compared, `pinned_used` missing from page 0, starvation threshold (test used the header macro, so a changed constant hid itself; now pinned by literal), and one skipped mutant anchor.

## Findings and deviations (decisions for you)

1. **Lock cycle cost (refined 2026-11-07).** With stack checks ON a **PTA** lock cycle (`rpi3_mon_acquire` + `rpi3_mon_release`) is **7 instrumented function entries** (9 with `rpi3_mon_hold` and `rpi3_mon_wait_init`). The design's "about four" counts the OP-TEE primitives only and is correct for a Core 1 attempt (4 lock entries, 5 with the attempt function). The design document was corrected on 2026-11-07 (section 4b.1, notes on G9, L4, C9, F26); the microsecond figures remain unmeasured inferences.
2. **Direct `__atomic` builtins** are used for the 32-bit health accessors (plat header), and the lock sits on its own 64-byte line, and `rpi3_mon` is exported non-static (needed by the glue and Core 1 inline). Event names are abbreviated relative to the design text.
3. **Keys are prototypes** committed in a header (compile-time constants in the image). Real key provisioning is outside this component.
4. **Core 1 cannot make progress while PTA holds the lock**: by design it skips (never spins). K = 8 skips before STARVED is a provisional value.
5. **HMAC and the OP-TEE heap lock (F27)**: PTA-side commands 17 and 22 use the OP-TEE crypto layer, which can spin on the heap lock. This is accepted for the PTA side; the Core 1 side is proven (E7) not to reach it.
6. `rpi3_mon_cmd22` takes the lock twice per command (hold, HMAC outside, hold again); the wait budget is shared across both acquisitions as designed.
7. `rpi3_mon_core1_entry_check` panics if FIQ/IRQ is unmasked at loop entry (D1). The legacy FIQ path is not run in Monitor builds.

## Status of checks

* The 53 original inventory rows keep their design-time status in `MONITOR_STATE_DESIGN.md` on purpose. **Update 2026-11-07:** the row-by-row evidence mapping now exists in `COMPONENT_2_2_INVENTORY.md` (43 PASS, 1 FAIL = H1, 9 NOT RUN). The list below was written before that mapping. Rows that now have real-source evidence in this record: lock/budget/timeouts (V*, X*, B*), init (I*, INIT-*), events (EV*), frame rules (C17-*), commands 18-23 (N*, K*, N22-*), Core 1 (Y1-Y6, Y8, Y9), build guards (BG1, BG2, BG3/DS2 via E6), F27/Z7 (E7). Rows that remain **board-only, NOT RUN**: V2, V14, W1, W2, W4, W7 (needs separate approval), X9, Y10, H3, and every wall-clock hold-time measurement.
* The 86 interface checks have SOURCE TEST PASS where the harness covers them and are otherwise unchanged. A row-by-row table is the next documentation step if you want it.

## Not done / blocked

* Any board run (BOARD NOT RUN, BLOCKED by your rule: needs separate approval, an external copy of `SD_Card_Backups/`, a rollback plan and recovery access; no UART is available).
* Hold-time measurement in microseconds (needs hardware; host timing is not evidence).
* Monitor profile DEBUG build (`CFG_RPI3_CAN_MONITOR_DEBUG=y`) was not built; host tests cover the debug policy with a mock flag only.
* ~~Updating the 53-row inventory and the design lock-cost figures~~ done 2026-11-07 (see `COMPONENT_2_2_FINAL_CLOSURE.md`). Still open: the H1 fix (needs approval).

## Reproduce

```
cd Part_E_Verification/monitor/source_tests && make                # 406 checks
python3 mutation_real.py -j 6                                       # 140 mutants (about 6 min)
./static_checks.sh                                                  # 53 checks
cd ../.. && scripts/build_optee_scratch.sh $PWD/build_scratch/final_off CFG_RPI3_CAN_MONITOR=n
         scripts/build_optee_scratch.sh $PWD/build_scratch/final_on  CFG_RPI3_CAN_MONITOR=y
python3 monitor/source_tests/callgraph.py <objdump -d of final_on tee.elf> rpi3_can_server_loop 'crypto_|hmac|_mac|malloc|calloc|realloc|(^|_)free$|mempool|nex_'
```

## Update 2026-11-07 (after the closure pass): H1 defect fixed

The object-code defect described in E8 (a 44-byte `memcpy` inside the cmd17 hold) was fixed after your approval by making the destination of `rpi3_mon_frame_store` volatile. Re-verified on rebuilt OFF/ON/debug images: no call inside any hold, 406/406 host checks, 55/55 static checks (with the new object-code guard), 137/140 mutants, call graph clean. New hash prefix of `rpi3_can_monitor.c`: `74ecb2d270495775`; the hashes and line counts in the table at the top of this file for that file (1037 lines) are the pre-fix values (now 1039 lines). Details: `COMPONENT_2_2_FINAL_CLOSURE.md` section 13.
