# COMPONENT_3_CORE1_CALL_LIST_AND_SYMBOL_DIFF.md (plan C3: allowed/forbidden Core 1 call list O-13, `nm` comparison)

Date: 2026-11-08. Method: `callgraph.py` on the disassembly of the Monitor-ON `tee.elf` (`build_scratch/final_on_h2`, hardened profile, stack checks on), root `rpi3_can_server_loop`; direct `bl`/tail-`b` edges only. Evidence files: `source_tests/callgraph_core1_h2_20261108.txt`, `nm_only_in_on_20261108.txt`, `nm_only_in_off_20261108.txt`.

## 1. Forbidden on Core 1 (design F27, D1 Option B)
No HMAC or MAC, no `crypto_*`, no heap allocation or free (`malloc`, `calloc`, `realloc`, `free`, `mempool_*`, `nex_*`), no waiting spin on a lock (`cpu_spin_lock*`), no FIQ unmask. **Result: none of these is reachable** (72 reachable functions; negative control: `rpi3_mon_cmd17` reaches six forbidden symbols and is detected).

## 2. Allowed on Core 1: the complete set reachable today (72 functions)

**Monitor code (new) (5):** `rpi3_mon_core1_attempt`, `rpi3_mon_core1_entry_check`, `rpi3_mon_ev_append.constprop.0`, `rpi3_mon_evaluate.constprop.0`, `rpi3_mon_init_ok`

**Loop itself (1):** `rpi3_can_server_loop`

**Lock and exception primitives (20):** `__cpu_spin_lock`, `__cpu_spin_trylock`, `__cpu_spin_unlock`, `__get_core_pos`, `cpu_mmu_enabled`, `cpu_spin_lock_no_dldetect`, `cpu_spin_trylock`, `cpu_spin_unlock`, `get_core_local`, `get_core_pos`, `get_core_pos_mpidr`, `have_spinlock`, `spinlock_count_decr`, `spinlock_count_incr`, `thread_get_core_local`, `thread_get_exceptions`, `thread_get_id_may_fail`, `thread_mask_exceptions`, `thread_set_exceptions`, `thread_unmask_exceptions`

**Stack-check hooks (-finstrument-functions, DEC-BUILD-1) (13):** `__cyg_profile_func_enter`, `__cyg_profile_func_enter.constprop.0`, `__cyg_profile_func_enter.part.0`, `__cyg_profile_func_exit`, `check_stack_limits`, `ftrace_map_lr`, `get_stack_limits`, `get_stackcheck_recursion_flag`, `memtag_strip_tag_vaddr`, `pauth_strip_pac`, `stack_end_va_to_top_hard`, `stack_end_va_to_top_soft`, `stack_size_to_alloc_size`

**Terminal failure path only (panic, assertion, trace output) (33):** `__do_panic`, `__stack_chk_fail`, `__vsnprintf`, `_assert_break`, `_assert_log`, `_assert_trap`, `console_flush`, `console_putc`, `copy_in_reg`, `cpu_idle`, `kprintf`, `memchr`, `memcpy`, `plat_trace_ext_puts`, `print_core_id`, `print_guest_id`, `print_kernel_stack`, `print_stack_arm64`, `print_stack_limits`, `print_thread_id`, `release_trace_contention`, `snprintk`, `strlen`, `trace_ext_get_core_id`, `trace_ext_get_thread_id`, `trace_ext_puts`, `trace_level_to_string`, `trace_printf`, `trace_vprintf`, `unwind_stack_arm64`, `uuid2str`, `vsnprintk`, `wait_if_trace_contended`

**Limits of this audit:** direct calls only. Indirect calls (`blr`/`br`) exist in the reachable set and are not resolved: console_flush : 1011cd44: d63f0020  blr x1; console_putc : 1011cc58: d63f0060  blr x3; console_putc : 1011cc6c: d63f0040  blr x2; kprintf : 1018c224: d61f0000  br x0; kprintf : 1018c288: d61f0000  br x0. The terminal failure path (`__do_panic`, assertion, stack-check failure) reaches `trace_printf`/console output by design; it does not return.

## 3. Symbol comparison, Monitor OFF vs Monitor ON (plan C3: "`nm` diff shows only new symbols")

- Only in ON: **43** symbols (all monitor code and data: `rpi3_mon`, `rpi3_mon_*` functions, the glue `rpi3_mon_pta_*`, the two prototype key arrays, one compiler switch table `CSWTCH.*`).
- Only in OFF: **3** symbols: `can0_check_timestamp_fresh`, `rpi3_can_fiq_process_pending_frame`, `secure_can_validate_frame`.

**Deviation from the plan's wording, intended:** the plan expected only new symbols. Three symbols disappear in the Monitor build: they are the legacy chip-A frame processing (`rpi3_can_fiq_process_pending_frame` and what only it calls). Decision D1 Option B removes that call from the Monitor loop (design F27: Core 1 must not reach HMAC or the heap), so the linker drops the functions. Everything else in the OFF image is present in the ON image. The Monitor-OFF build itself is unchanged against the baseline (no instruction differs except trace line numbers, `COMPONENT_2_2_FINAL_CLOSURE.md` section 13). The FIQ handler file `rpi3_can_fiq.c` is byte-identical to the pre-change snapshot.

## 4. Decision recorded: the hook flag
The plan names the hook flag `CFG_RPI3_CAN_MONITOR_POLL`. The implementation guards the hook with the single flag `CFG_RPI3_CAN_MONITOR` (which also gates the PTA commands). Kept as is: a second flag would allow a build with the commands but without the loop, a configuration no one needs and no test covers. If a separate flag is wanted later it is a three-line change plus a build-matrix test.

## 5. Still open for C3
The plan's A1.6 stack-watermark test (SPSel=0, 4096-byte tmp stack, an HMAC at depth plus a FIQ) has not been run. Under D1 Option B no FIQ is ever taken on Core 1 and Core 1 reaches no HMAC, so the combination cannot occur in the Monitor profile; a watermark measurement of the real loop still needs a test image (NOT RUN).

## A1.6 stack analysis (added 2026-11-09)
**Margin (my proposal, needs the user's confirmation):** the plan states no number; I use headroom >= 2048 bytes of the 4096-byte AArch64 tmp stack (`STACK_TMP_SIZE` = 4096 with `CFG_WITH_ARM_TRUSTED_FW`, `CFG_STACK_TMP_EXTRA=0`), the same 2 KB reserve the plan assumed in decision O-01 option (a).
**Static (OBJECT, no board):** `source_tests/stackdepth.py` on the hardened Monitor-ON disassembly (`stackdepth_hardened_20261109.txt`): frame sizes from the prologues, deepest direct-call path from `rpi3_can_server_loop`.
- NORMAL path (fatal-path functions removed): **640 bytes**, headroom 3456: OK. Path: loop 64 + `rpi3_mon_core1_attempt` 48 + the stack-check/spinlock helpers (`__cyg_profile_func_enter` 32, `check_stack_limits` 64, `get_stack_limits` 64, ... `thread_mask_exceptions`, `thread_set_exceptions`, `have_spinlock`, `get_core_local`).
- Including the fatal path (stack-check failure, then panic, then trace printing): **2000 bytes**, headroom 2096: still >= 2048. That path only runs after a failure.
- Limits of the figure: sums of prologue allocations along DIRECT calls; indirect calls exist only in the console/printing code (`console_flush`, `console_putc`, `kprintf`); no dynamic stack adjustment; the "recursion" found is OP-TEE's own guard in the stack-check hook. FIQ/IRQ are masked for the whole loop (D1), so no interrupt frame is ever pushed on this stack.
**Board watermark (test image, built, NOT run):** `rpi3-sdcard-monitor-teststats-stack.img`. Core 1 fills the unused part of its tmp stack with a pattern at its first attempt; `tstats_probe stack` reports the deepest point touched (conservative: the region between the attempt frame and the fill point counts as used). HMAC-at-depth plus FIQ stays unreachable by design (Core 1 reaches no crypto; FIQ masked).

### A1.6 board watermark result (2026-11-09, BOARD, test-statistics + stack image)
`baseline/board_C3_stack_watermark_20261109.txt` (image `rpi3-sdcard-monitor-teststats-stack.img`, sha256 50038db6...; Pi 3 B at 10.237.27.187; flashed by the user, the cmp output of this flash was not shown to me; the test image is identified because the stack command works). Core 1 tmp stack 4096 bytes. In all five phases (idle baseline; cadence 64 with 40 s of NO heartbeat answers = miss and TIMEOUT events; a 60 s soak with answered heartbeats, frames and recovery; 5 s of traffic silence = traffic-deadline timeout; events seen: COUNTER_RESYNC, HEARTBEAT_RECOVERED, TIMEOUT detail 1 and 2, 1685 skips) the deepest point touched stayed at **688 bytes** = the fill point, i.e. nothing below it was ever touched. Headroom **3408 bytes** against the proposed 2048 margin: OK. The figure is a conservative upper bound (the region between the attempt frame and the fill point counts as used), and it agrees with the static normal-path figure of 640 bytes. Not covered: a path never exercised (e.g. a MON_STARVED episode, which needs the PTA to hold the lock for K=8 attempts and cannot happen at 6-7 us holds); the margin of 2048 is still my proposal, pending your confirmation. HMAC-at-depth plus FIQ remains unreachable by design.
**C3.3: COMPLETE (static + board, with the margin awaiting confirmation).**
