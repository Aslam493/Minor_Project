# COMPONENT_2_2_REMAINING_WORK.md (what is left inside Component 2.2)

Date: 2026-11-07. Status of 2.2: **COMPLETE for source, documentation, host verification and build checks** (`COMPONENT_2_2_FINAL_CLOSURE.md`, section 13). This report covers only what is NOT done. Nothing below has been run on the board, and no board action was taken for this report.

## 1. Summary

| # | Remaining item | Kind | Status | Blocked by |
|---|---|---|---|---|
| R1 | Nine on-board checks: V2, V14, W1, W2, W4, W7, X9, Y10, H3 | Board validation | NOT RUN | Board access, preconditions P1 to P5, your approval (W7: its own approval) |
| R2 | Hardware timing: lock-hold max and p99, wait calibration, Core 1 pass time and collision rate, cost of one instrumented entry | Measurement | PENDING | The Pi; test-only Secure World code for part of it |
| R3 | Full SD image with the monitor enabled | Build | NOT BUILT | A separate output location, so `out/rpi3-sdcard.img` is not overwritten |
| R4 | Normal-World test clients: commands 17 to 23, and the V2 atomic test | Software | DO NOT EXIST | Writing and testing them |
| R5 | Evidence mapping of the 86 later interface checks | Documentation | NOT MAPPED | Nothing external |
| R6 | Production key provisioning | Security design | NOT STARTED (prototype keys only) | A separate design and decision |

Counts for what is already closed, for reference: 44 of the original 53 checks PASS, 0 FAIL, 9 NOT RUN (exactly the nine in R1); 406/406 host tests; 55/55 static checks; 137/140 mutants (3 equivalent); OFF, ON and debug scratch builds clean.

## 2. Detail per item

### R1. The nine on-board checks
Full purpose, tools, expected output, failure handling and approval need per check are in `BOARD_VALIDATION_READINESS.md` section 2. In short:

| Check | Can be done with SSH only? | Needs new code first? |
|---|---|---|
| V2 (no lost atomic increments, cores 0, 2, 3) | Yes | Yes, the user-space program (R4) |
| V14 (value outputs delivered on error returns) | Yes | Yes, the 17 to 23 client (R4); forcing BUSY needs more |
| W1 (lock memory attributes) | No | Yes: test-only Secure World code, or UART |
| W2 (init before Core 1 runs the loop) | Indirect form yes (init flag set and `poll_count` rising); literal boot-log form no | The 17 to 23 client (R4) |
| W4 (wake refused in the Monitor profile) | Yes (`spi_poc_test --wake-core1`) | No |
| W7 (forced Core 1 panic) | No | Yes: a way to force the panic. Risky without UART and a recovery card; recommended not to run |
| X9 (wait for a simulated holder about 1 ms) | No | Yes: test-only code that holds the lock |
| Y10 (Core 1 pass time, collisions, hold time) | Pass time and collisions yes; hold time no | The client (R4); hold time needs H3's stamps |
| H3 (hold-time max and p99) | No | Yes: test-only code with `CNTPCT` stamps |

### R2. Hardware timing
The figures in the design (about 2.4 us per instrumented call, about 10 us per lock cycle, about 1.6 percent Core 1 overhead, the "under 100 us" hold target) are unmeasured inferences or targets. The verified facts are entry counts only: PTA lock cycle 7 (9 with wrappers); Core 1 attempt 4 (5 with the attempt function). Maximum and p99 hold times are PENDING the Pi. The effect of the H1 fix on hold time was never measured and is not claimed.

### R3. Full SD image
Only the OP-TEE core was built, in scratch directories. The packaging pipeline (optee-os, tf-a, host apps, buildroot, update_bootfs, sdcard-image) has not been run with `CFG_RPI3_CAN_MONITOR=y`. Project lesson: the whole pipeline must be rerun after a source change, with `PLATFORM=rpi3` passed explicitly. The output must go somewhere other than `out/rpi3-sdcard.img`, because the passwordless flash rule writes whatever that file contains to `/dev/sda`.

### R4. Test clients
`spi_poc_test` only knows the legacy options. Needed: a client that calls commands 17 to 23 with the exact parameter types of the interface decisions, and a three-thread atomic test for V2. Neither exists. Both are ordinary Linux programs and can be written and compiled on the laptop without touching the board.

### R5. The 86 later interface checks
They are outside the original 53 and are listed in `MONITOR_STATE_DESIGN.md` (section 4j, "Interface and implementation checks added 2026-11-06", and section 4k). Most are exercised by the host harness (command 17 to 23 tests, event ring, glue mapping, legacy policy), but there is no row-by-row table like `COMPONENT_2_2_INVENTORY.md`. This is documentation work only.

### R6. Key provisioning
The frame and heartbeat keys are compile-time constants in `rpi3_can_monitor_keys.h`, present once each in every Monitor-ON image and readable by anyone with the tree or the flashed card. The Pi 3 has no TZASC, so Normal World can read Secure World memory regardless. A minimum list for the future work is in `COMPONENT_2_2_FINAL_CLOSURE.md` section 7. Nothing was generated or replaced.

## 3. Preconditions for any board work (R1, R2) - current state

| Precondition | State |
|---|---|
| P1 External copy of `SD_Card_Backups/` (2.2 GB, only on the laptop's internal disk) | NOT DONE |
| P2 Spare microSD card (rule: never write to the working card) | NOT AVAILABLE |
| P3 Rollback plan | Written (`ROLLBACK_PROCEDURE.md`), but a restore has never been executed on a real card |
| P4 Recovery access | SSH only; no UART |
| P5 Your approval for each phase | NOT GIVEN |
| P6 Monitor-enabled full image | NOT BUILT (R3) |
| P7, P8 Test clients | DO NOT EXIST (R4) |

## 4. What can be done without the board, in a safe order

1. **R5** the interface-check evidence table. Documentation only.
2. **R4** the two test clients, compiled and checked on the laptop. No board.
3. **R3** the full-image build into a separate output directory, verified by hash. No board.
4. Your decisions on P1 and P2 (copy the backups out, obtain a spare card) and on the UART.
5. Only then board phase B1 and the SSH-only checks, each with your explicit approval. R2 and the checks that need test-only Secure World code come after that, and W7 last or never.

## 5. What is not claimed

No board result, timing number or memory-attribute value is claimed anywhere in the 2.2 documents. The items above are the complete list of what separates "closed for software" from "validated on hardware".

## Update 2026-11-07 (later): R4 and R5 done, R3 not done
- **R5 done:** `COMPONENT_2_2_INTERFACE_INVENTORY.md` maps all 86 interface checks (81 PASS, 5 PARTIAL because a named board part is NOT RUN). A test gap was found and closed (command 23 event counters; harness now 409 checks, 4 new mutants caught). Generator `source_tests/make_inventory86.py`, K10/K11 table check `k10_k11_check.py`.
- **R4 done (software only, never run on the Pi):** `Part_E_Verification/tools/monitor_client/` (`mon_client.c/.h` library for commands 17 to 23, `mon_cli.c`, `atomic_test.c` for V2, Makefile). Cross-built for AArch64 against the real libteec (outputs in `build_scratch/monitor_client_aarch64/`); the V2 binary contains the same ldaxr/add/stxr loop as `atomic_inc32`. Host integration test against the real glue and monitor logic: `source_tests/client_it.c`, 98 checks, 0 failed (`client_it_20261107.txt`).
- **R3 done, by a safer route than the full pipeline (2026-11-07):** running `make sdcard-image` would rewrite `optee_os/out/arm`, `out/boot`, `out/rpi3-sdcard.img` and the Buildroot rootfs in place, so it was NOT run. The monitor reaches the board only through `armstub8.bin`, so a candidate image was composed in scratch: the Sep-20 image with only `armstub8.bin` replaced (TF-A rebuilt with `BUILD_BASE` in scratch and the monitor OP-TEE as BL32). Validation: a control build from the current `out/arm` binaries reproduces the shipped `armstub8.bin` except the 3 timestamp strings; in the candidate only `armstub8.bin` differs among boot files and the rootfs partition is byte-identical. Files and hashes: `build_scratch/candidate_image/` (`README.md`, `SHA256SUMS`). **Not flashed and not booted**; whether it boots is unknown until a board run.
- The final full mutation run on the 144 mutants finished: **141 caught, the same 3 equivalent survivors** (`mutation_interface_20261107.txt`).

## CURRENT STATE (2026-11-09) - supersedes the table in section 1 above
Component 2.2 is COMPLETE for source, documentation, host verification, build/object checks and hardware validation, EXCEPT the items below. The table in section 1 and the 2026-11-07 update are historical.

| # | Item | Status now |
|---|---|---|
| R1 | Nine on-board checks | V2 PASS, V14 PASS, X9 PASS, H3 PASS (board, 2026-11-07 to 11-09); W4 PARTIAL (refusal half verified; the ALREADY_ON half applies only to a non-monitor build); W2 PARTIAL (indirect form only; the literal boot-log form needs UART); Y10 PARTIAL (pass time and collisions measured with PTA-injected frames, not under real CAN traffic); **W1 NOT RUN** (needs MMU-attribute reading code or UART); **W7 NOT RUN** (forced Core 1 panic: needs its own approval, risky without UART and a recovery card) |
| R2 | Hardware timing | DONE: longest PTA lock hold 7.24 us over all runs (target under 100 us), Core 1 hold 0.21-0.31 us, Core 1 pass 75.2 ns, wait bound 1.002 ms with 383 attempts per ms. The 11.6 us inference is withdrawn. Measured on a test-statistics build; the production image has no stamps |
| R3 | Full SD image | DONE by the scratch route (only `armstub8.bin` replaced); NOT the full buildroot pipeline. Flashed and booted: hardened-c16384 image on the 8 GB spare |
| R4 | Test clients | DONE and used on the board (`tools/monitor_client`); a client bug found by the soak was fixed |
| R5 | 86-row interface mapping | DONE (`COMPONENT_2_2_INTERFACE_INVENTORY.md`). Its rows for X9/H3 may still say NOT RUN: the 53-row inventory (regenerated 2026-11-09: 48 PASS, 3 PARTIAL, 0 FAIL, 2 NOT RUN) is the current record |
| R6 | Production keys | NOT STARTED (prototype keys only; separate design) |
| - | Cadence | Changed 256 -> 16384 on 2026-11-09 from board data (`MONITOR_STATE_DESIGN.md` 4b.3); board regression on the final image: 0.024 collisions per call (was 0.44), soak clean |

Still not claimed: W1 (lock memory attributes on the board), W7, prevention (Pi 3 has no TZASC; detection only), and any CAN-bus evidence (frames in all soaks were injected through the PTA). Y10's requirement text still says "256-pass cadence"; the cadence is now 16384 and the 256 figure is historical.

### Update 2026-11-09 (later): W1 is now PASS on the board
`baseline/teststats_W1_20261109.txt`: the monitor lock is mapped Normal, write-back cacheable (ATTR 0xff), inner-shareable (SH=3) on both the PTA core and Core 1, with the MMU and D-cache on (SCTLR_EL1 M=1 C=1). The 53-row inventory is now 49 PASS, 3 PARTIAL, 0 FAIL, 1 NOT RUN. The only NOT RUN row is **W7** (forced Core 1 panic: needs its own approval). W2 (literal boot-log form), W4 (ALREADY_ON half) and Y10 (real CAN traffic) stay PARTIAL.

### Update 2026-11-09 (later): W4 is now PASS on the board (both halves)
`baseline/board_W4_alreadyon_nomonitor_20261109.txt`: on the original non-monitor image three consecutive `--wake-core1` calls each returned PSCI -4 = ALREADY_ON; on the Monitor build the same command is refused (earlier log). Inventory: 50 PASS, 2 PARTIAL (W2 literal form, Y10 real CAN traffic), 0 FAIL, 1 NOT RUN (W7, in progress with the user's approval of 2026-11-09).

### Update 2026-11-09 (later): W7 is now PASS on the board
`baseline/board_W7_before_20261109.txt`, `board_W7_after_20261109.txt`. Inventory: **51 PASS, 2 PARTIAL (W2 literal boot-log form needs UART; Y10 under real CAN traffic needs a sender), 0 FAIL, 0 NOT RUN.** Remaining outside the 53 checks: production keys, CAN-bus (not PTA-injected) evidence, UART.
