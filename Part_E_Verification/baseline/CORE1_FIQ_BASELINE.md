# CORE1_FIQ_BASELINE.md (Component 1, Subcomponent 1.4)

Date: 2026-11-06. Read-only: source reading, `nm`, `objdump` on the built OP-TEE image, and the runtime facts already recorded in 1.2 and 1.3. No source file, CPU setting or interrupt setting was changed. FIQ was not enabled or armed.
Objective: understand exactly how Core 1 runs the Secure World loop and how the FIQ interacts with it, and record the critical risks O-01, O-05, O-06.

## Summary table

| Task | What | Status |
|---|---|---|
| 1 | Core 1 startup and initialization | DONE (below) |
| 2 | Core 1 loop and heartbeat | DONE (below) |
| 3 | FIQ routing, entry and exit | DONE (below) |
| 4 | Exception stack and register preservation | DONE (below) |
| 5 | Concurrency; risks O-01, O-05, O-06 | DONE (below) |

## How the searches were run

`rg` is not installed here, and a search from the project root would also scan the Linux kernel, Buildroot and `node_modules`. The searches were scoped to the project-specific trees with `grep -rn -E`, excluding `.o`, `.d`, `.cmd`, `.map`, `.dmp` and `out/` build output:

```
TF-A   : trusted-firmware-a/{bl31, services/spd/opteed, plat/rpi}
OP-TEE : optee_os/core/arch/arm/{kernel, include, plat-rpi3}, optee_os/core/pta/rpi3_can_spi_poc.c
Patterns: core1|core_1|secure_server|enter_secure_server   |   heartbeat   |   fiq|SCR_EL3|SCR_FIQ|INTR_TYPE|get_pending_interrupt_type|VBAR
```

## Task 1: Core 1 startup (DONE)

> **Note added 2026-11-06 (during Component 2 design):** every code address in this file (`0x1010....`, `0x101b2f30`) is a link-time virtual address from `tee.elf`. `CFG_CORE_ASLR=y` is set, so a running core may be mapped at an offset. See the corrected memory map in `COMMUNICATION_BASELINE.md` (Task 5).

### The sequence, in order

```
 1. Linux (core 0) SMP bring-up: PSCI CPU_ON for cpu@1                  dmesg: "CPU1: failed to come online" at ~5.1 s
 2. TF-A (EL3): core 1 leaves its holding pen -> bl31_warm_entrypoint -> psci_warmboot_entrypoint()
        the OP-TEE dispatcher runs OP-TEE's  vector_cpu_on_entry  (S-EL1): per-core init, exactly as for cores 2 and 3
        OP-TEE returns to EL3 (smc ON_DONE); PSCI releases its power-domain locks
 3. bl31_entrypoint.S:234   bl opteed_enter_secure_server_if_core1      (strictly AFTER the PSCI locks are released)
 4. opteed_pm.c:243         if (plat_my_core_pos() != 1) return;        (a no-op on every other core)
                            cm_set_elr_el3(SECURE, &optee_vector_table->secure_server_entry);
                            opteed_synchronous_sp_entry(...)            -> ERET into Secure World, never returns
 5. OP-TEE  vector_secure_server_entry  ->  bl rpi3_can_server_loop      (never returns; reaching panic_at_smc_return is a bug)
 6. rpi3_can_server_loop(): unmask FIQ, then forever { heartbeat++; process_pending_frame(); dsb(); }
```

### Your five questions

| # | Question | Answer | Evidence |
|---|---|---|---|
| 1 | Which function starts Core 1? | There is no dedicated "start Core 1" function. **Linux starts it** with the ordinary PSCI `CPU_ON` request for `cpu@1`. **`opteed_enter_secure_server_if_core1()`** (TF-A, `opteed_pm.c:243`, called from `bl31_entrypoint.S:234`) then diverts it into Secure World permanently | source; dmesg |
| 2 | At what address does Core 1 begin execution? | Two entries. First, the normal per-core entry `vector_cpu_on_entry` at **`0x1010181c`**. Then the permanent one: the 10th slot of `thread_vector_table` (**`0x10101900 + 0x24 = 0x10101924`**), a branch to `vector_secure_server_entry` at **`0x10101830`**, which calls `rpi3_can_server_loop` at **`0x1010f300`**. Addresses are the virtual link addresses in `tee.elf` of the Sep-20 build. The physical address was not computed | `nm`, `objdump -d` |
| 3 | Does Core 1 enter Secure World directly? | **Not at reset.** It first passes through Linux's PSCI request and TF-A (EL3) like any secondary core, runs a normal OP-TEE per-core init, and returns to EL3. TF-A then re-enters Secure World a second time with the ELR changed. From then on it never returns to Normal World (no `smc #0` after the vector; no ERET to Non-Secure) | `opteed_pm.c` comment and code |
| 4 | Which function contains its infinite loop? | **`rpi3_can_server_loop()`**, `rpi3_can_server.c:393`, declared `__noreturn`. Disassembly: loop at `0x1010f330` to `0x1010f344`, branching back to `0x1010f330` | `objdump -d` |
| 5 | Where is the heartbeat stored and updated? | `static volatile uint64_t rpi3_can_server_heartbeat` (`rpi3_can_server.c:355`), 8 bytes in `.bss` at **`0x101b2f30`**. Updated by Core 1 only, at `rpi3_can_server.c:398` (disassembly `0x1010f330` load, `0x1010f334` add 1, `0x1010f338` store). Read by PTA command 12 through `rpi3_can_server_get_heartbeat()` (`rpi3_can_server.c:357`) | `nm -S`, `objdump -d` |

### The instruction-level proof

```
thread_vector_table  (10 slots, one 4-byte branch each, from 0x10101900)
  slot 2 (0x10101908)  b vector_cpu_on_entry          <- first Secure World entry
  slot 9 (0x10101924)  b vector_secure_server_entry   <- Core 1's permanent entry

vector_secure_server_entry (0x10101830)
  adr x16 ; ldr x17,[boot_mmu_config+0x20] ; add x16,x16,x17 ; br x16    <- the "readjust_pc" jump from identity-mapped code to the real address
  bl rpi3_can_server_loop (0x1010f300)
  bl __panic_at_smc_return                                              <- unreachable by design

rpi3_can_server_loop (0x1010f300)
  bl thread_get_exceptions ; and w0,w0,#0xfffffffe ; bl thread_set_exceptions     <- clears the native-interrupt (FIQ) mask bit
  loop 0x1010f330: ldr x0,[x19,#3888] ; add x0,x0,#1 ; str x0,[x19,#3888]        <- heartbeat++ (x19 = 0x101b2000, +0xF30 = 0x101b2f30)
                   bl rpi3_can_fiq_process_pending_frame (0x1010efd8)
                   dsb sy
                   b 0x1010f330
```

Everything the source comments claim matches the machine code: the strong `rpi3_can_server_loop` (not the weak stub in `thread_optee_smc_a64.S`) is the one linked and called; the heartbeat address computed from the instructions equals the symbol address; the table has exactly the 10 slots TF-A's `struct optee_vectors` expects.

### Runtime facts that agree (from 1.2 and 1.3)

CPU online `0,2-3`, offline `1`; `CPU1: failed to come online` in `dmesg`; the heartbeat rises about 413,000 per second; PTA command 12 returns it.

### Findings

| # | Finding | Why it matters |
|---|---|---|
| F1 | The AArch64 entry file `thread_optee_smc_a64.S` (lines 92 and 121) still says "core 0" and "only fires once `RPI_PRIMARY_CPU` has been moved off core 0 - not the case in any build produced today". The a32 file carries the updated "core 1" text. Those a64 comments are **stale and wrong**: the divert fires on core 1 in every boot. Comments only: TF-A decides the core number (`linear_id != 1U`) and the vector table is not core-numbered, so the code path is right | A reader of the build-relevant file would believe the wrong core and the wrong condition. Fix the comments only when source edits are approved (a comment edit still changes the file and needs a rebuild of the image to stay consistent) |
| F2 | The weak `rpi3_can_server_loop` stub in the a64 file is harmless: the strong definition overrides it, confirmed by the disassembly | none; recorded to close the question |
| F3 | The divert happens after the PSCI locks are released, by design (an earlier design that diverted inside the CPU_ON path would have stranded a lock and deadlocked later CPU_ONs). Do not move the call | any future change near `bl31_entrypoint.S:234` must preserve this |
| F4 | Core 1 reaches the loop with FIQ unmasked (`& 0xfffffffe` on the exception mask) | the loop can be interrupted by the CAN FIQ at any instruction; this is the setting behind O-01 and O-06 |
| F5 | **Every function in OP-TEE core starts with `bl __cyg_profile_func_enter`**, because `CFG_CORE_DEBUG_CHECK_STACKS=y` compiles with `-finstrument-functions` (`core.mk:70-74`). The hook (`thread.c:347-360`) checks the stack limits and **toggles the exception mask** to find its recursion flag. It runs on Core 1 on every call (the loop calls `rpi3_can_fiq_process_pending_frame` every pass) | it adds per-pass cost to the roughly 2.4 us loop, it briefly masks and unmasks exceptions on Core 1, and it means a stack overrun may **panic** at a function entry instead of corrupting silently. To be examined in Tasks 4 and 5 |
| F6 | The heartbeat is a plain `volatile uint64_t` with one writer (Core 1). An aligned 64-bit access on AArch64 is single-copy atomic, so the PTA reading it from another core sees a consistent value | it is safe as a liveness counter; it is not a synchronisation primitive |

### Not established in Task 1

The physical address of the entry; the exact exception state (`SPSR`, stack pointer) Core 1 has at the loop entry (Task 4); whether Core 1 ever takes an exception other than the CAN FIQ.

### Problems or confusion avoided

| Item | What happened | How it was handled |
|---|---|---|
| The requested `rg` over `.` would be unusable | the tool is not installed, and the project root contains the Linux kernel, Buildroot and `node_modules` | scoped `grep -rn -E` to the project-modified trees, listed above, so the result can be reproduced |
| The a64 file's comments say the opposite of reality | read lines 92 and 121 against `opteed_pm.c:243` | treated as stale comments; verified the behaviour from the code and the disassembly, not from the comments |
| Which "entry address" to report | there are two entries | reported both and labelled which is which |

## Task 2: the Core 1 loop and the heartbeat (DONE, 2026-11-06)

Method: source and disassembly of one idle pass; the stack-check hook's helpers read in full; the core-local initial state read from `thread.c` and `entry_a64.S`; the pass rate measured on the Pi in 7 runs of about 15 s each, using the Pi's own uptime clock around every heartbeat read (midpoints, so Linux scheduling jitter cancels). Read-only; the only load added was three spinning shell loops on cores 0, 2 and 3, killed afterwards (checked: none left, CPU 96% idle, Core 1 still running, CPU mask `0,2-3`).

### What one pass does (idle case, which is almost every pass)

| Step | Code | Notes |
|---|---|---|
| 1 | `heartbeat++` | load, add, store of a 64-bit variable (3 instructions) |
| 2 | call `rpi3_can_fiq_process_pending_frame()` | a normal function call |
| 2a | its entry: `bl __cyg_profile_func_enter` | the stack-check hook, see below |
| 2b | `if (can0_frame_state != PENDING) return false` | one load and compare. Disassembly: `ldr w23,[x19,#392]; cmp w23,#1; b.ne` to `mov w19,#0` and the epilogue |
| 2c | function exit, which calls the (empty) `__cyg_profile_func_exit` | a call that immediately returns |
| 3 | `dsb sy` | full system barrier |
| 4 | branch back | |

The loop's own work is about 7 instructions and the frame check about 20. **Nearly all the time goes into the stack-check hook in step 2a.**

### The stack-check hook, in full (`CFG_CORE_DEBUG_CHECK_STACKS=y`)

On every instrumented function entry the compiler inserts `bl __cyg_profile_func_enter`, which does, in order:

1. `get_stackcheck_recursion_flag()`: **masks IRQ only** (`thread_mask_exceptions(THREAD_EXCP_FOREIGN_INTR)`; on this build FOREIGN is the I bit and NATIVE, the CAN FIQ, is the F bit, from `thread_arch.h:366-367` and the `mov w0,#2` in the disassembly), finds the core's data, picks a recursion flag, then **restores the previous mask** (`thread_unmask_exceptions` sets A, I, F back to what they were).
2. If the flag is not already set: set it, call `check_stack_limits()` (which itself contains a nested `__cyg_profile_func_enter.constprop.0` call and a call to `get_stack_limits()`), clear it. If the stack pointer is outside the limits it **panics** (`__do_panic`).

Which flag Core 1 uses: `flags & (ABORT | TMP)` selects the core-local flag. Every core's data is initialised with `flags = THREAD_CLF_TMP` and `curr_thread = THREAD_ID_INVALID` (`thread.c:647-648`), and the secondary-core entry sets the same (`entry_a64.S:331,361`). So **Core 1 always uses the core-local flag, never the per-thread array**. (Had it used `threads[-1]`, each call would have written one byte before the array; this is why the check was made.) During a FIQ the flags become `TMP | FIQ` (`thread_a64.S:1247`), which still contains TMP, so the same path is used.

Consequences:
- The hook itself opens **no FIQ-masking window** (it masks IRQ, which Core 1 does not take, and restores the state).
- It is a **panic trap** for stack-limit violations at function entry, not a guarantee: a corruption that does not cross the limit, or one that happens between function entries, is not detected.
- It is the **dominant cost** of a pass.

### Measured pass rate

| Condition | Runs | Rate (passes per second) | Time per pass |
|---|---|---|---|
| Idle | 410,641 / 410,754 / 410,758 / 410,890 (4 runs, 15.05 to 15.06 s each) | 410,641 to 410,890 | 2.434 to 2.435 us |
| Cores 0, 2, 3 each running a spinning loop | 410,706 / 410,474 / 410,925 (3 runs, 15.12 to 15.14 s each) | 410,474 to 410,925 | 2.434 to 2.436 us |

The spread is about 0.1%, idle and loaded alike. **One pass takes 2.435 us, and it did not change when the other three cores were busy.** The earlier figure of about 2.4 us from Task 4 of 1.3 is confirmed and tightened.

Limits of this result: the load was register-only spinning, with no memory traffic, no SPI, no CAN traffic, no PTA calls and no FIQ. A busy loop on the other cores says nothing about what happens when they hit the shared memory bus or call into Secure World. The full matrix belongs to Component 4.

### What the heartbeat means, and does not mean

| Fact | Detail |
|---|---|
| It counts passes, not time | 1 count is about 2.435 us at this build. Do not use it as a clock |
| It shows the loop is spinning | it increments whether or not a frame is pending, so it proves nothing about validation, only about progress |
| It stops if the loop stops | including while the loop is inside a long call (for example, a validation with HMAC, not yet measured) |
| One writer | only Core 1 writes it; an aligned 64-bit access on AArch64 is single-copy atomic, so another core reads a consistent value |
| It cannot wrap in practice | 2^64 / 410,700 per second is about 4.5 x 10^13 s, roughly 1.4 million years |
| A reader can fake nothing | the PTA only reads it; but a Linux caller can ask for it at will, so it is not an authentication of anything |

### Findings

| # | Finding | Why it matters |
|---|---|---|
| F5 (refined) | the hook masks only IRQ and restores the old mask; Core 1 uses the core-local flag | removes my earlier worry that the hook opens FIQ windows or writes outside Core 1's data |
| F7 | one pass is 2.435 us and stable to 0.1%, independent of CPU-only load on the other cores | a deterministic loop is easy to reason about; the monitor's per-pass budget can be stated. The monitor deadlines (T = 50 ms, R about 500 ms) are far above one pass |
| F8 | **each instrumented function the loop calls costs about as much as the whole current pass**: the pass has exactly one hook-instrumented call (`process_pending_frame`) and takes 2.435 us. Inference from one data point, not a direct measurement of one hook call | a monitor hook that calls several small functions per pass could multiply the pass time; use one call per pass, or functions marked `__nostackcheck`, and measure in Component 4 |
| F9 | the cycle cost is high for the instruction count (a few hundred instructions by function sizes, rough, not a trace, for 2.4 us). Barriers and system-register writes explain part of it; whether Core 1 runs with caches enabled is not established | Component 4 should read the secure timer and, if possible, the cycle counter, and check Core 1's cache and MMU state before trusting any timing derived from this loop |
| F10 | `CFG_CORE_DEBUG_CHECK_STACKS=y` is a debug setting; switching it off would change speed and remove the stack panic trap | a build decision for later; it is not changed here |

### Not established in Task 2

The cost of a pass that has a frame to process (HMAC), the effect of memory, SPI, PTA and FIQ load on the pass time, the cache state of Core 1, and the real cycle count per pass.

### Problems or confusion avoided

| Item | What happened | How it was handled |
|---|---|---|
| Worry that the hook writes outside Core 1's data | the hook can index `threads[curr_thread]`, and `curr_thread` is -1 on Core 1 | read the initial flags (`TMP`) in `thread.c` and `entry_a64.S`: the core-local flag is always chosen |
| My earlier statement that the hook "toggles the exception mask" sounded like a FIQ risk | | read the exact bits: it masks IRQ only and restores the previous state |
| Timing from a 0.5 s sample in 1.3 was rough | one `usleep` interval with scheduling jitter | measured seven runs of 15 s with the Pi's own clock around each read; the result agreed within 1% |
| Busy loops started on the Pi for the load test | background jobs could be left running | killed them in the same session and confirmed with `top` and `ps` that none remained |

## Task 3: FIQ routing, entry and exit (DONE, 2026-11-06)

Method: source (`rpi3_can_fiq.c`, `platform_config.h`, TF-A `opteed_main.c`, `rpi3_common.c`, `interrupt_mgmt.c`, `context_mgmt.c`), plus `nm` and `objdump` on the built OP-TEE image, plus read-only runtime facts from the Pi. **FIQ was not enabled or armed, and no FIQ was generated.** Facts from the earlier tasks used: GPIO 25 reads HIGH (inactive), `GPEDS0 = 0`, `FIQ_CONTROL = 0`, FIQ count 0.

### The signal chain, stage by stage

| # | Stage | What happens | Who sets it, and its state now |
|---|---|---|---|
| 1 | CAN chip | the MCP2518FD pulls its `/INT` pin low when a receive flag is set and enabled | chip state; pin currently HIGH (inactive) |
| 2 | GPIO 25 | level-detect on GPIO 25 (`GPLEN0` bit 25) raises the GPIO bank interrupt | armed at boot by `rpi3_can_fiq_arm_gpio()` (`driver_init`), and also by Linux's own driver; Linux shows it as IRQ 186, "pinctrl-bcm2835 25 Level spi0.0" |
| 3 | VideoCore interrupt controller | `FIQ_CONTROL` (`0x3f00b20c`) = enable bit `0x80` OR source number **49**. When enabled, source 49 is **diverted from the normal IRQ path to the FIQ line** | **disabled** (`0x0`) until PTA cmd 3 or `can0_secure_transmit()` writes it. One-shot: the FIQ handler writes it back to 0 |
| 4 | ARM-local routing | `GPU_INT_ROUTING` (ARM-local base `+0x0c`) bits [3:2] choose which core receives the FIQ: `RPI3_PRIMARY_CORE_POS = 1`, so **core 1** | written at boot by `rpi3_can_fiq_init()` through `rpi3_route_fiq_to_primary_core()`. Live value **not read** (no PTA command reads it) |
| 5 | Core 1 | takes the FIQ **at S-EL1, directly**, into OP-TEE's own exception vector (see below) | |
| 6 | OP-TEE | `el1_fiq_sp0` -> `elx_fiq` -> `thread_check_canaries` -> `interrupt_main_handler()` | image: `0x10102100`, `0x10104464`, `0x1010f0ac` |
| 7 | Handler | if the SPI bus is idle, reads the received frame into the single capture slot and sets it `PENDING`; clears the chip's interrupt flags while keeping its enable bits; writes `FIQ_CONTROL = 0`; increments the FIQ count | `rpi3_can_fiq.c` |
| 8 | Exit | `elx_fiq` restores the registers and executes `eret` back to the interrupted instruction | disassembly |

Linux's side of the same line: `IRQ 186 ... 25 Level spi0.0` has a total of **1** interrupt since boot (on CPU 2), which is the loopback test from 1.2. Secure World saw none. This agrees with `FIQ_CONTROL = 0`: while the FIQ is not armed, the pin interrupt goes only to Linux.

### Is EL3 (TF-A) involved on Core 1? No, according to the code

| Fact | Source |
|---|---|
| The S-EL1 interrupt handler is registered with `flags = 0; set_interrupt_rm_flag(flags, NON_SECURE)`, i.e. `0x2` | `opteed_main.c:346-350` |
| `0x2` is routing model 0 (`INTR_SEL1_VALID_RM0`): an S-EL1 interrupt traps to EL3 **only if generated while Non-Secure code runs**; while Secure code runs it is delivered to S-EL1 | `interrupt_mgmt.h:41-47,62-67`; per-state SCR_EL3 built in `get_scr_el3_from_routing_model()` |
| Core 1 never runs Non-Secure | Task 1 |
| So the FIQ goes straight to OP-TEE's vector table and TF-A never sees it | |

Consequences:
- The branch in `opteed_sel1_interrupt_handler` for "this FIQ arrived while Core 1's Secure context was already active" (`opteed_main.c:78-92`, with `saved_elr_el3` / `saved_spsr_el3`) is **not reachable under this routing model**. It looks like defensive code written for the case where the FIQ is trapped to EL3 on Core 1. This is derived from the source; `SCR_EL3` cannot be read from Linux, so it is not confirmed at run time.
- The EL3 path (`plat_ic_get_pending_interrupt_type()` returning `INTR_TYPE_S_EL1`, then `vector_fiq_entry`, then `smc FIQ_DONE`) is what the FIQ used when the routing pointed at core 0 (Step 15 / Phase 2 on hardware). It is not used now.
- Two documents already disagree on this point: the TF-A comment describes the EL3 branch for Core 1, while the plan's audit appendix describes the direct S-EL1 vector. The routing model supports the audit appendix.

### The S-EL1 vector table (`thread_excp_vect` = `0x10102000`)

| Offset | Meaning | Code |
|---|---|---|
| `+0x100` | FIQ taken at EL1 while using `SP_EL0` (EL1t) | `el1_fiq_sp0`: `stp x0,x1,[sp]; stp x2,x3,[sp,#16]; b elx_fiq` |
| `+0x300` | FIQ taken at EL1 while using `SP_EL1` (EL1h) | `el1_fiq_sp1`: **`b .` (an endless loop)** |

On any exception into EL1 the hardware selects `SP_EL1`, which OP-TEE uses as a pointer to the core's `thread_core_local` data; the two `stp` lines save x0 to x3 there. **Which vector Core 1 uses depends on whether it runs on `SP_EL0` or `SP_EL1`**; if it were on `SP_EL1`, the first FIQ would hang the core. That is a Task 4 question.

### What `elx_fiq` does (disassembly, `0x10104464`)

```
flags = (flags << 4) | THREAD_CLF_FIQ | THREAD_CLF_TMP          core-local +52
x1 = core_local.tmp_stack_va_end - 0x90                          core-local +32, minus 144 bytes
save  x4..x18 at x1+0..+112,  x30 and the interrupted SP_EL0 at x1+128 / x1+136
msr spsel,#0 ; mov sp, x1                                        SP_EL0 := (tmp_stack_va_end - 144)
bl thread_check_canaries                                         stack canary check
bl interrupt_main_handler                                        the project's handler
restore x4..x18, x30, SP_EL0 := the saved interrupted value
msr spsel,#1 ; flags >>= 4 ; restore x2,x3 ; ldp x0,x1 ; eret   straight back to the interrupted instruction
```
Registers x19 to x29 are not saved here; they are preserved by the compiled C code of the handler and its callees, as the AArch64 calling convention requires.

### Central finding of Task 3 (F11): the FIQ stack frame is placed at the top of the tmp stack, whatever the interrupted code's stack pointer is

`elx_fiq` does **not** continue on the interrupted `SP_EL0`. It computes `tmp_stack_va_end - 0x90`, saves the interrupted `SP_EL0` inside the record, and **sets `SP_EL0` to that address**. The 144-byte record and then the handler's own C frames occupy the top of the tmp stack, growing downward.

Core 1's loop frames also live at the top of that stack if Core 1 entered with `SP_EL0` at `tmp_stack_va_end` (the plan's audit says it did; Task 4 verifies). With the function prologues seen in the disassembly:

```
[end -  32, end)        rpi3_can_server_loop        frame 32 B  (saved x29, x30, x19)
[end -  96, end - 32)   rpi3_can_fiq_process_pending_frame  frame 64 B  (saved x29, x30, x19..x23)
below that              the stack-check hook's frames, etc.
FIQ record:  [end - 144, end)   <- overlaps ALL of the above
```

If a FIQ lands while Core 1 is inside `process_pending_frame` or the hook (which is almost all of the 2.435 us of every pass, Task 2), the record overwrites the saved registers and return address of those live frames. The handler then returns correctly to the interrupted instruction with the right register values, but when the interrupted function later returns it loads its saved `x29`/`x30` from memory that now holds record contents, so it can jump to a wrong address. A FIQ landing only in the loop's own few instructions would overwrite frames that are never used again (the loop never returns), so it would survive.

This is the mechanism behind risk **O-01**, derived independently here from the machine code, and it agrees with the plan's audit. It is **an analysis, not a measured failure**. Not yet established: Core 1's actual `SP_EL0` and frame addresses (Task 4); how many passes would be hit; whether the repository's recorded "HMAC in the FIQ hung the board" history was this mechanism or another. **No FIQ was generated to test it, as instructed, and it should not be armed until O-01 has a decision.**

### What the FIQ path costs and risks, collected

| # | Finding | Why it matters |
|---|---|---|
| F11 | FIQ record placed at the top of the tmp stack, over the live frames of the interrupted call chain | O-01; do not arm FIQ in the Monitor design on this stack arrangement without one of the plan's fixes (lower `SP_EL0` by a reserve, or mask FIQ for the whole loop) |
| F12 | a FIQ on `SP_EL1` would hit `b .` at `+0x300` | a second way to hang; depends on Task 4's answer |
| F13 | while the FIQ is armed, source 49 is taken from Linux's IRQ path. Whether source 49 covers GPIO pins other than 25 was **not checked** | arming could also stop Linux seeing other pins on the same source for the armed time |
| F14 | the FIQ handler reads the chip over SPI0, shared with Linux's driver, and skips the event if the bus is busy (the source says so) | a frame can be skipped, not lost: it stays in the chip's receive FIFO (source comment) |
| F15 | the one-shot design disarms `FIQ_CONTROL` after each capture; Core 1's loop then does one bounded read to drain the backlog | continuous capture is not a property of the current design |
| F16 | on Core 1 the dead EL3 branch in `opteed_sel1_interrupt_handler` is unreachable under routing model 0 | a reader of the TF-A file would expect TF-A to be in the path on Core 1; it is not |

### Not established in Task 3

The live `SCR_EL3` value and the live ARM-local routing register (not readable from Linux and no PTA command); whether the Core 1 FIQ path has ever completed cleanly on hardware in the current image (the Step 15 hardware result was obtained with the FIQ routed to core 0, i.e. the EL3 path); how long the handler runs.

### Problems or confusion avoided

| Item | What happened | How it was handled |
|---|---|---|
| Two descriptions of the FIQ path | the TF-A comment describes an EL3 branch for Core 1; the plan's audit describes OP-TEE's own vector | decided by the routing flags (`0x2`, model 0) and checked the S-EL1 vector in the image |
| "The hardware test passed" could be misread as covering Core 1 | the Step 15 result was with the FIQ routed to core 0 | recorded that the Core 1 path is a different path and has no hardware result in these records |
| A shell command to print the routing function failed | the function name matched two lines | re-ran with an exact pattern |
| Temptation to arm the FIQ to "see" it | the instruction was not to enable FIQ, and F11 shows a plausible reason | analysed code and disassembly only |

## Task 4: exception stack and register preservation (DONE, 2026-11-06)

Method: source and disassembly only (`entry_a64.S`, `thread.c`, `thread_private_arch.h`, `thread_arch.h`, TF-A `context.S`, `opteed_main.c`), plus a **static stack-depth estimate** built for this task (`tools/stackdepth/stackdepth.py`, disassembly of the Sep-20 `tee.elf`, 2,710 functions). Nothing was run on the Pi for this task; no FIQ was armed.

### Core 1's stack state when it enters the loop (this answers the open question from Task 3)

| Item | Value | Evidence |
|---|---|---|
| Exception level and stack selector | **EL1t: `SPSel = 0`, so the code runs on `SP_EL0`** | `cpu_on_handler` ends with `msr spsel,#0` (`entry_a64.S:788-847`); the SMC that ends the first CPU_ON saves that state in `SPSR_EL3` (`runtime_exceptions.S:497`), and TF-A's second entry (`opteed_enter_secure_server_if_core1`) changes only `ELR_EL3` |
| `SP_EL0` | **`tmp_stack_va_end` of core 1**, the top of its tmp stack | `CFG_DYN_CONFIG=y` branch of `cpu_on_handler` (`SP_EL0 := core_local.tmp_stack_va_end`); TF-A saves `SP_EL0` with the general registers on every EL3 entry and restores it at `eret` (`context.S:706, 833`) |
| `SP_EL1` | pointer to core 1's `thread_core_local` struct | `cpu_on_handler`; saved and restored with the EL1 context (`context.S:403-405, 502-503`) |
| Which FIQ vector therefore applies | `+0x100` `el1_fiq_sp0` (real code), **not** `+0x300` (the endless loop) | Task 3 disassembly. F12 is resolved: the endless-loop slot is not the path |
| Run-time support | Core 1 has run for hours; had it been on `SP_EL1` its frames would have been written over the core-local struct and failed quickly | supportive, not proof |

Not read directly: the numeric address of `tmp_stack_va_end` (the stack is allocated at boot, so the address is not in the image) and the live `SP_EL0`. They are derived from the code.

### The tmp stack that Core 1 uses

| Item | Value | Source |
|---|---|---|
| Size | **4096 bytes** (`STACK_TMP_SIZE = 4096 + 0 + 0`: ARM64 with `CFG_CORE_DEBUG_CHECK_STACKS`, `STACK_TMP_OFFS = 0` with TF-A, `CFG_STACK_TMP_EXTRA = 0`) | `thread_private_arch.h:36-45`, `out/arm/conf.mk` |
| Top | `end_va = base + alloc_size - STACK_CANARY_SIZE/2`; canary words (`CFG_WITH_STACK_CANARIES=y`, 32 bytes total) sit at both ends | `alloc_stack()`, `thread.c:506-523` |
| Core-local layout | `x[0..3]` at +0 to +31, `tmp_stack_va_end` +32, `kcode_offset` +40, `curr_thread` +48, `flags` +52, `abt_stack_va_end` +56 | `thread_arch.h`, matches the offsets in the disassembly |
| Who shares it | Core 1's loop frames **and** the FIQ record and handler frames. Both start at the top | Tasks 1 to 3 |

### F18: the design assumption Core 1 breaks

`elx_fiq` builds its frame at the top of the tmp stack regardless of where the interrupted code's stack pointer is. That is correct only if the tmp stack is **free** whenever a FIQ arrives. In standard OP-TEE, code that runs on the tmp stack runs with native interrupts masked, and the secondary-core entry leaves them masked "on the assumption that unmasking happens later, on the way into Non-Secure" (comment in `rpi3_can_server.c`). Core 1 unmasks FIQ (`rpi3_can_server_loop`) **while running on the tmp stack** and never leaves it. So the FIQ handler and the interrupted loop use the same memory at the same time. (This is an inference from the structure of `elx_fiq` and the comment, not a statement written in OP-TEE's documentation.)

### Numbers: how much of those 4096 bytes is used (static estimate)

Method and its limits (details in `tools/stackdepth/README.md`): frame sizes from the prologues; deepest chain over **direct** calls. Calls through function pointers cannot be followed (this makes a chain too short), and every call is assumed to run the stack-check hook at its deepest (this makes it too long). The result is an **estimate in both directions, not a bound**.

| Scenario | Chain (from the top of the stack) | Estimate |
|---|---|---|
| Idle pass | loop 32 + `process_pending_frame` 64 + stack-check hook (deepest 448) | **up to about 544 B** |
| Pass that handles a frame | loop 32 + `process_pending_frame` chain (validation 160, SPI read and address-translation chain about 1.1 KB) | **about 1.9 KB** (direct calls only; HMAC library calls go through function pointers, so this is probably low) |
| `can0_compute_hmac` alone | 112 + `crypto_mac_*` chain | 672 B direct calls only (a **lower bound**; SHA-256 itself is reached through pointers) |
| FIQ handler | 144-byte record + `interrupt_main_handler` 80 + deepest of the SPI read path (1776 total) and the `IMSG` print path (1712 total, measured as `trace_printf` 1632 + 80) | **about 1.9 KB** (144 + 1776) |
| `thread_check_canaries` (runs first in `elx_fiq`) | 64 + chain | about 0.4 KB, not nested with the handler |

Both the interrupted chain and the FIQ handler start from the same top address. The budget is 4096 B. **The problem is overlap, not exhaustion**: an idle pass uses at most about 0.5 KB, but the FIQ record (144 B) plus handler frames (up to about 1.8 KB) are laid over exactly that region.

### What the overlap does (consequence of Task 3's F11 with these numbers)

```
top = tmp_stack_va_end
[top-32 , top)                 loop frame                       (never reused: the loop never returns)
[top-96 , top-32)              process_pending_frame frame      (reused when it returns)
[top-96-hook , top-96)         stack-check hook and its helpers (reused)
[top-144 , top)                FIQ record                       <- written by elx_fiq
[top-144-1.8K , top-144)       FIQ handler frames               <- written by the C code
```
Any FIQ that interrupts Core 1 inside `process_pending_frame` or the hook (almost the whole of every pass, Task 2) lands its record and handler frames on top of the live frames and their saved registers and return addresses. The handler returns to the right instruction with the right registers, but the interrupted function's saved `x29`/`x30`, and with them its return, can then be wrong. A FIQ landing in the loop's own few instructions only damages the loop frame, which is never reused. This remains an **analysis, not a measured failure**.

### What would fix it, using the same numbers

| Option (from the plan) | What the numbers say |
|---|---|
| (a) lower `SP_EL0` for the loop by a reserve; `CFG_STACK_TMP_EXTRA = 2048` | the reserve must cover the record plus the deepest handler chain: about 144 + 1.8 KB, **about 2 KB**. That matches the plan's figure. The loop's own chain (up to about 1.9 KB while handling a frame) must also fit: roughly 2 KB + 1.9 KB = 3.9 KB, close to 4096, so `STACK_TMP_SIZE` would need the extra 2048 as well (about 6 KB). Static estimates are too rough to cut it finer; the run-time watermark (A1.6) is required |
| (b) keep FIQ masked for the whole loop in the Monitor profile | removes the problem; the CAN FIQ would not be used (the monitor reads chip B by polling and chip A's FIQ path is already dormant, R13) |

### Register preservation across the FIQ

| State | Where it is kept | Result |
|---|---|---|
| `x0` to `x3` | the vector (`el1_fiq_sp0`) stores them in the core-local struct at +0 to +31 through `SP_EL1` | saved and restored |
| `x4` to `x18` | `elx_fiq` stores them in the 144-byte record at `top-144` | saved and restored |
| `x30` (LR) and the interrupted `SP_EL0` | same record (+128, +136) | saved and restored |
| `x19` to `x29` | not saved by `elx_fiq`; they are callee-saved, so the compiled C functions of the handler preserve them | preserved by the calling convention |
| FP and SIMD (`v0` to `v31`) | not saved | **nothing to preserve**: the core is built with `-mgeneral-regs-only` (`arm.mk:261`) and none of the 117 functions reachable from the handler or the 125 reachable from the loop uses an FP/SIMD register (checked in the disassembly) |
| `ELR_EL1`, `SPSR_EL1` | saved by hardware on exception entry; **not copied to memory** by `elx_fiq` | a second exception inside the handler would overwrite them. FIQ is masked on entry. SError is unmasked on Core 1 (`msr daifclr,#DAIFBIT_ABT` in `cpu_on_handler`), so a rare SError during the handler would be fatal |
| `PSTATE.F` and the rest of the mask | set by hardware on entry, restored by `eret` from `SPSR_EL1` | correct |
| Core-local `flags` | shifted left 4 and tagged `FIQ|TMP` on entry, shifted back on exit | correct. The stack-check hook inside the handler therefore still finds `TMP` and uses the core-local flag |
| Normal World registers | not involved: no world switch happens on Core 1 | n/a |
| Shared memory used by the handler and the loop | `can0_frame_state`, `can0_last_frame_*`: `volatile` plus `dsb` handshake | discussed in Task 5 |

### Findings

| # | Finding | Why it matters |
|---|---|---|
| F17 | Core 1 runs at EL1t on `SP_EL0 = tmp_stack_va_end`; the FIQ goes to the real vector at `+0x100`; the endless-loop slot (`+0x300`) is not the path | resolves F12 |
| F18 | the FIQ handler assumes a free tmp stack; Core 1 runs its loop on the tmp stack with FIQ unmasked | root of O-01 |
| F19 | idle chain up to about 0.5 KB, frame-handling chain about 1.9 KB, FIQ record plus handler about 1.9 KB, stack 4096 B: overlap is the problem, not size | sizing for the fix |
| F20 | a reserve of about 2 KB (plan option a) is the right order of magnitude; the sum with the loop chain is close to 4096, so `STACK_TMP_SIZE` must grow too | do not apply option (a) without the run-time watermark test |
| F21 | general-purpose registers are fully preserved; no FP/SIMD state; `ELR_EL1`/`SPSR_EL1` are not backed up in memory | only a nested exception inside the handler is exposed |

### Not established in Task 4

The live `SP_EL0` and address of `tmp_stack_va_end`; a measured stack high-water mark; the true HMAC stack depth (function-pointer calls); whether the handler's `IMSG` output can run at all while FIQ is masked on the serial console (UART output with exceptions masked takes milliseconds; not measured); the stack depth when an SError occurs.

### Problems or confusion avoided

| Item | What happened | How it was handled |
|---|---|---|
| First stack-depth script printed 0-byte frames | `objdump` prints `sub sp, sp, #0x40` in hex and my pattern read decimal only | fixed the parser to read hex, re-ran |
| First "deepest chain" was dominated by panic and assert paths | the worst call graph includes error handlers that do not run normally | added a "normal" mode that leaves them out, and reported both |
| "Normal" mode also hid the `IMSG` print path | the handler prints on every FIQ, so tracing is normal for it | measured `trace_printf` on its own (1632 B) and added it to the handler's figure |
| A chain through `check_stack_limits` pulled in an error print | its failure branch calls `trace_printf` | excluded `check_stack_limits` when measuring `trace_printf` alone |
| Static numbers can look precise | they come from the disassembly | labelled as estimates with the error directions stated, and the script and its limits saved in `tools/stackdepth/` |

## Task 5: concurrency and the critical risks O-01, O-05, O-06 (DONE, 2026-11-06)

Method: source and disassembly only. Searched all Secure World code of this project for lock primitives; listed the shared variables and who touches them; read OP-TEE's spinlock, console and `thread_mask_exceptions` code; used the numbers from Tasks 2 to 4. **Nothing was run on the Pi and no FIQ was armed.**

### Who runs concurrently

| Context | Core | Runs |
|---|---|---|
| Linux's `mcp251xfd` + `spi_bcm2835` drivers | 0, 2, 3 | SPI0 and the chip-select GPIO 8 (Linux IRQ 186 on CPU 2 so far) |
| A PTA call from Linux (`spi_poc_test`, any debug command) | whichever core Linux used | its own polled SPI code (cmds 0, 6, 7, 14, 16) and the HMAC context (cmd 7) |
| Core 1's loop | 1 | heartbeat, frame validation (HMAC), one bounded catch-up SPI read |
| Core 1's FIQ handler | 1 (interrupts the loop) | SPI read of the frame, `IMSG` prints |

### The concurrency facts

| # | Fact | Evidence | Consequence |
|---|---|---|---|
| C1 | **There is no lock of any kind** in `rpi3_can_fiq.c`, `rpi3_can_server.c` or the PTA file | `grep` for `cpu_spin`, `spinlock`, `mutex`, `atomic_`, `lock(` found only a comment | all protection is by convention and `volatile` |
| C2 | Linux and Secure World share **SPI0 and chip-select GPIO 8**. The only guard is `wait_for_spi_idle()`: poll the controller's transfer-active bit for a bounded time, then go (`rpi3_can_fiq.c:652-662`) | source | the check is not atomic with the use (a transfer can start in between), and it does not look at the **chip-select pin**: Linux holds chip select low across a message, and the transfer-active bit drops between its transfers, so Secure World can see "idle", drive the pin low and then **release it** (`can0_cs_set(false)`) in the middle of Linux's message. Also `spi_transfer()` rewrites `SPI_CLK` and clears `SPI_CS` |
| C3 | Two independent SPI drivers exist in Secure World: one in `rpi3_can_fiq.c` (used by the FIQ handler, Core 1's catch-up and `rpi3_can_fiq_transmit`) and one in the PTA file (`read_register32`, used by cmds 0, 6, 16) | source | the PTA path and Core 1 can drive the same controller and pin at the same moment; nothing orders them |
| C4 | **One HMAC context** (`static void *can0_hmac_ctx`) is used by Core 1's validation (`:1350`) **and** by the transmit path (`:1017`, reached from PTA cmd 7 on any core) | `grep` | two cores can run `crypto_mac_init/update/final` on the same context at once; the result would be a wrong tag or corrupted state. Reachable only through debug cmd 7 together with traffic, but real. The plan's two MAC contexts (I-10) are the right fix |
| C5 | The FIQ handler and Core 1's loop hand over frames through `can0_frame_state` (EMPTY / PENDING / PROCESSING, `volatile`, with `dsb`) | source | sound **on one core**: the FIQ can only interrupt the loop, and the handler refuses to write unless the slot is EMPTY. The loop's catch-up read masks FIQ around its check-and-capture (code in Task 5 of 1.3) |
| C6 | Debug getters (cmds 5, 8 to 11, 15) read several variables that Core 1 updates at different moments, with no snapshot | source | a reader can see a mixed set (for example a new counter with an old status). Harmless for debugging; **not acceptable for the monitor's state reads** (the plan uses a seqlock) |
| C7 | Linux-side PTA calls are serialised by OP-TEE's busy flag; Core 1 is outside it | Task 3 | no ordering between a PTA handler and Core 1 |
| C8 | Console output takes `puts_lock` and, with `CFG_CONSOLE_MASK_INTERRUPTS=y`, **masks all exceptions (including FIQ) while printing** | `trace_ext.c:17,40-45` | a print cannot be interrupted by a FIQ on the same core, so no self-deadlock on `puts_lock`. But the handler prints on every FIQ (`:1519`, `:1559`): with the 8250 driver flushing before every character, a line of about 75 characters at 115200 baud is **roughly 6 to 7 ms** (estimate from the plan's measured model of 87 us per character, not measured by me), during which that core is deaf to every interrupt |
| C9 | The kernel's deadlock detector (`CFG_TEE_CORE_DEBUG=y`) only prints "possible spinlock deadlock" after 2^32 failed attempts; it does not panic | `spinlock.h:71-92` | a stuck lock shows up as a message much later, not as a crash |
| C10 | OP-TEE's spinlock is an unfair test-and-set (`ldaxr`/`stxr` with `wfe`): no queue, no ordering | `spin_lock_a64.S` | a core can lose the race repeatedly |
| C11 | `cpu_spin_trylock` and `cpu_spin_unlock` assert that foreign interrupts (IRQ) are masked; `cpu_spin_lock_xsave` masks **all** exceptions while it spins and while it holds the lock | `spinlock.h:104,113,121` | Core 1 satisfies the assert only because it never unmasks IRQ (Task 4); a future change that unmasks IRQ there would panic. A PTA caller spinning on a held lock cannot take any interrupt on its core |

### The three critical risks

**O-01: the FIQ can overwrite Core 1's live stack. Severity: critical. Blocks S2a.**
- *Mechanism (Tasks 3 and 4, from the disassembly):* `elx_fiq` places its 144-byte record and the handler's frames at the top of the tmp stack, which is where Core 1's loop already runs; OP-TEE's design assumes the tmp stack is free whenever a FIQ arrives (F18).
- *Size:* idle chain up to about 0.5 KB, frame-handling chain about 1.9 KB, FIQ record plus handler about 1.9 KB, stack 4096 B. The chains overlap; they do not exhaust the stack.
- *Status:* mechanism confirmed in code; **no failure measured, and none should be provoked on the main card.** The FIQ is not armed at boot, which is why the current system runs.
- *Fixes:* (a) lower the loop's `SP_EL0` by about 2 KB and enlarge the tmp stack (`CFG_STACK_TMP_EXTRA = 2048` plus room: about 6 KB total), or (b) keep FIQ masked for the whole loop in the Monitor profile. **Recommendation: (b).**
- *Test that settles it:* for (b), arming is refused in the Monitor profile and the exception mask on Core 1 keeps F set (checked by a read command). For (a), the plan's A1.6: fill the stack with a pattern, take FIQs repeatedly from Core 0 for 10 minutes on a **spare card**, check a sentinel in a live frame, the heartbeat, and the stack watermark during one HMAC plus one print.

**O-05: Core 1 and the PTA handlers compete for the new lock. Severity: high (design risk; the lock does not exist yet). Blocks S3.**
- *Evidence:* the spinlock is unfair (C10). A PTA handler spinning with all exceptions masked cannot be interrupted (C11). Core 1 uses a bounded `trylock` and skips the pass on failure (plan R8, R21), which avoids blocking Core 1 but lets three cores calling the PTA in a loop starve it.
- *What starving Core 1 would do:* deadlines are only checked on Core 1's passes, so the heartbeat or report deadline would be noticed late. The plan's catch-up (PTA handlers check expiry themselves when Core 1's skip count is high) covers this if implemented for every deadline class.
- *Fixes:* keep the critical section tiny (field reads and writes, one fixed-size append; no HMAC, no UART, no copy, no allocation), a catch-up function in the PTA handlers, a call-rate limit that returns BUSY, and a ticket lock or hand-off for Core 1 if measurement shows starvation.
- *Test:* three cores calling the PTA in a loop; record Core 1's trylock failure ratio and the longest gap between passes; repeat for every deadline class (T18i).

**O-06: a FIQ arrives while Core 1 holds the lock. Severity: high (only if FIQ stays unmasked). Blocks S3.**
- *Effect:* the FIQ handler does not take the lock (R8), so there is no self-deadlock, but the lock stays held for the whole handler: a SPI read plus a print of roughly 6 to 7 ms (C8, estimate). Every PTA caller that arrives meanwhile spins with all exceptions masked for that long, delaying Linux's own interrupts on that core.
- *Fixes:* Core 1 wraps trylock, critical section and unlock in `thread_mask_exceptions(THREAD_EXCP_NATIVE_INTR)`; with option (b) of O-01 the FIQ is masked the whole time and this risk disappears. Remove the `IMSG` calls from the handler in any build that keeps it.
- *Test:* a lock-wait histogram on the PTA side under CAN load; the maximum must stay below a stated bound.

### Related risks that these facts raise (new)

| ID | Risk | Severity | Note |
|---|---|---|---|
| N-01 | C2: Secure World can release Linux's chip select mid-message and rewrite its SPI settings | high for the Step 2 profile; irrelevant when Linux and Secure World do not both use SPI0 | the Monitor profile keeps chip A dormant (R13) and reads chip B on other pins, which removes the sharing |
| N-02 | C4: the single HMAC context shared by PTA cmd 7 and Core 1 | medium (debug command) | cmd 7 is on the list to remove or log in the hardened profile |
| N-03 | C8: the handler's `IMSG` blocks the core for milliseconds with all exceptions masked | medium | tied to O-06 and the plan's O-12 (UART) |

### Decision needed from you (blocks Component 3)

| # | Question | My recommendation |
|---|---|---|
| D1 | O-01: option (a) reserve and larger stack, or option (b) keep FIQ masked in the Monitor profile? | **(b)**: smaller change, no use for the CAN FIQ in the Monitor design, also removes O-06 |
| D2 | Do you want a controlled O-01 reproduction on the spare card to prove the failure, or is the code analysis enough for the report? | analysis is enough for the report; a reproduction risks a hung board and can wait until the spare card and UART are available |

### Not established in Task 5

Measured lock hold times, the real duration of the handler (SPI read and print), whether Linux's SPI messages actually leave the chip select asserted across transfers (C2 is derived from the driver's `cs-gpios` design, not observed), the effect of three cores calling the PTA at once on Core 1's pass gap.

### Problems or confusion avoided

| Item | What happened | How it was handled |
|---|---|---|
| Looked for the existing lock to analyse O-05 and O-06 | there is none (C1) | analysed the lock the plan will introduce, using OP-TEE's real primitives |
| "Idle" sounds like "bus free" | `wait_for_spi_idle` reads only the transfer-active bit | read the whole path: it ignores the chip-select pin (C2) |
| Assumed the FIQ handler's print could deadlock on the console lock | the lock is taken with all exceptions masked | read `trace_ext.c`: no self-deadlock, but a multi-millisecond blind spot (C8) |
| Assumed the deadlock detector would panic | | read `spinlock.h`: it only prints after 2^32 retries (C9) |
| Wanted to prove O-01 by firing a FIQ | instructed not to arm FIQ, and a failure could hang the board | wrote the test as a protocol for the spare card instead |

## Subcomponent 1.4 ledger

| Task | Result |
|---|---|
| 1 Core 1 startup | PASS |
| 2 Loop and heartbeat | PASS (2.435 us per pass measured) |
| 3 FIQ routing, entry and exit | PASS |
| 4 Exception stack and register preservation | PASS |
| 5 Concurrency; O-01, O-05, O-06 | PASS (documented, with decisions D1, D2 open) |

Tools produced: `tools/stackdepth/`. Corrections made during 1.4: the stack-check hook was first described as toggling the FIQ mask (it masks IRQ only); my first stack-depth script read hex immediates as zero; the "normal" depth mode first hid the handler's print path; the handler's TF-A "Core 1 secure branch" was taken to be on the path (it is unreachable).
