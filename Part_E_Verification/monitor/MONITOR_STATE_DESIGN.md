# MONITOR_STATE_DESIGN.md (Component 2, Subcomponent 2.1)

Status: **v3 (2026-11-06).** G5, G6, G8 rules (section 4c), G9 v3 (section 4b) and access rules R-A1 to R-A10 (section 5) are **APPROVED by the user and recorded**. The section 6 additions and the N-04 report wording are **APPROVED**. **Component 2.1 checklist complete.** **D1 was decided on 2026-11-07 (Option B).** (Header status as of 2026-11-06; later approvals are listed in the checklist, section 10.) Started 2026-11-06.
Scope of 2.1: decide what state the Secure World monitor holds, who owns each item, how it is accessed and what it may never expose, before any code is written.

## 0. Decisions recorded (2026-11-06, from the user)

| # | Decision | Effect |
|---|---|---|
| A | **Prefix `rpi3_mon_`** for every new file-scope monitor identifier | no clash with existing code (14 planned names had 0 hits; the only `mon_` identifiers in OP-TEE are struct members of unrelated 32-bit monitor-mode code) |
| B | **No chip A variable is reused** | the monitor has its own counter state and its own MAC contexts (table in section 2) |
| C | **N-04 install deferred**; keep the original DTB unchanged; copy the verified backups to an external drive before any SD-card write; arrange a UART adapter if possible | the state lives in memory that Linux's allocator does not know about (limit L-N04 below); recorded as a known limitation until the fix is installed |

### Review outcome (2026-11-06)

| Item | Outcome |
|---|---|
| Struct or separate variables (3.1) | **Approved: one instance `rpi3_mon`.** Note on the example given in review: the frame counter on the wire is a `u32` and `hb_seq` is a `u32` in the plan, so `ctr.last_counter` and `heartbeat.hb_seq` stay 32-bit (a 64-bit field would be harmless but would break the 44-byte canonical form comparison if copied from it) |
| G1, G3, G4 | approved |
| G2 | approved **with conditions**: keys immutable (`static const`, in read-only data); each MAC context has exactly one owner and is never shared across contexts |
| G5 | approved with conditions; **conditions defined in section 4c, APPROVED (2026-11-06)**: limits `IDLE_MAX` and `IDLE_USES_MAX` stay parameters fixed in Component 4 |
| G6 | approved with conditions; **conditions defined in section 4c, APPROVED (2026-11-06)**: a late valid answer does **not** reset `hb_miss_count` |
| G8 | approved with conditions; **conditions defined in section 4c, APPROVED (2026-11-06)**: 4 pinned entries per class x 6 classes = 24 pinned + 40 ordinary |
| **G7** | **APPROVED (2026-11-06), as specified in section 4a (G7 v3)** |
| **G9** | **APPROVED (2026-11-06) as G9 v3, section 4b** (lock only, no seqlock; Core 1 one non-spinning attempt per 256 passes; cmd 18 at most 5 records (120 B) per call, amended 2026-11-06; PTA bounded acquire with 1 ms initial budget) |
| N-04 install | deferred; no SD-card changes |

The conditions on G2, G5, G6, G8 become the content of the next design step (interfaces and transitions); they are listed here so they are not lost.

**D1 (FIQ policy) DECIDED 2026-11-07: Option B**, FIQ stays masked throughout the Core 1 loop in Monitor builds (section 4g C7, rule R-A6).

## 1. What already exists (read-only search, 2026-11-06)

`rg` is not installed and a whole-tree search returns 1,007 lines, almost all from the crypto libraries. The project's own hits are in three files.

| Existing variable | Where | Decision |
|---|---|---|
| `secure_last_counter` | `rpi3_can_fiq.c:221`, plain `u32`, strictly-greater rule, counter 0 rejected | **not reused** (A and B). The monitor's counter policy is deliberately different (plan R2) |
| `can0_frame_state` | `:93`, `volatile u32` | **not shared**; chip A handshake, dormant in the Monitor profile (R13) |
| `can0_frame_dropped_count`, `can0_last_validation_*`, `can0_last_frame_*`, `can0_last_fifo_*`, `can0_last_rxif_mask`, `can0_last_int_reg`, `can0_last_bdiag1`, `can0_last_tx_fail_point`, `can0_fiq_count`, `can0_tbc_epoch` | `rpi3_can_fiq.c` | **not shared**; chip A debug and capture state. Chip timestamps come from the chip's own 40 MHz counter and are never compared with a secure-timer tick |
| `can0_hmac_ctx` | `:312`, one `void *` used by Core 1 and by PTA cmd 7 (N-02) | **not shared**; two new contexts |
| `rpi3_can_server_heartbeat` | `rpi3_can_server.c:355`, `volatile u64` | **left untouched and not used by the monitor**; it stays visible only through cmd 12. The monitor keeps its own `poll_count` and `last_poll_ticks` |
| nonce, monitor status, any secure-timer read | none | all new. `CFG_SECURE_TIME_SOURCE_CNTPCT=y` is forced in `conf.mk` and the inline `barrier_read_counter_timer()` exists (`arm.h:144`), but no project code reads the timer, and `CNTPCT` on Core 1 is untested (Component 4 prerequisite) |

## 2. Where the state lives and what that guarantees

- Static storage in a new OP-TEE core file (planned: `core/arch/arm/plat-rpi3/rpi3_can_monitor.c`), in TEE RAM.
- Touched by two kinds of context that no existing mechanism orders: **PTA handlers** (any core; OP-TEE serialises them against each other with its busy flag) and **Core 1's loop** (outside that mechanism, running on a 4096-byte tmp stack with FIQ policy still undecided). One lock, `rpi3_mon_lock`, is therefore required (1.3, 1.4).
- "Persistent" means it survives between calls. **It is lost at every reboot by design** (`boot_id` changes, the counter state re-synchronises, and the replay window after a reboot is a documented limit). Nothing here is anti-rollback.
- **Limit L-N04:** Linux's allocator treats `0x10000000-0x10FFFFFF` as free memory, because the device tree it runs with reserves a different range (`N04_SECURE_MEMORY_RESERVATION.md`). Until the fix is installed, monitor state integrity against a Linux allocation is not guaranteed, even without any attacker. No claim of state integrity may be made in the report before then.
- **Limit L-TZ:** the Pi 3 has no TZASC; even with N-04 fixed, a compromised kernel or a DMA device can read or write this memory. `/dev/mem` refuses RAM reads on this kernel, but that is a kernel setting. State must never be described as confidential or tamper-proof against Linux.
- **N-04 report wording (APPROVED 2026-11-06).** "The device tree Linux boots with does not reserve the OP-TEE secure DRAM range (`0x10100000-0x10FFFFFF`) or TF-A's secure SRAM; Linux's page allocator therefore regards them as free memory, so Linux awareness of the monitor's memory is incomplete. The Raspberry Pi 3 has no TZASC, so Secure World memory is protected only by software policy and cooperation, not by hardware isolation: a compromised kernel or a DMA-capable device can read or write it. A fix (one reserved-memory node) was prepared and verified offline but is not installed. The monitor therefore detects evidence of tampering by Linux; it does not prevent it, and its state is neither confidential nor tamper-proof against Linux."
- Memory cost is small: about 2 KB for the Mode F state and its event ring, about 5 KB more for the Mode D2 record ring (Component 7), all in `.bss`. **None of it is on a stack**, which matters because Core 1's tmp stack is shared with the FIQ frame (O-01).

## 3. Conventions

### 3.1 One struct or separate variables (new point for confirmation)

Proposal: **one file-scope instance of one struct**, `static struct rpi3_mon_state rpi3_mon;`, so the only file-scope identifier is `rpi3_mon` plus the lock and the constants (all prefixed `rpi3_mon_`), and members use short names (`rpi3_mon.ctr_f.last_counter`). Reasons: one place to take a snapshot for cmd 20, one place to apply alignment and cache-line grouping (section 5), a `_Static_assert` on its size, and no namespace growth. If you prefer separate variables literally named `rpi3_mon_last_counter`, `rpi3_mon_last_nonce`, `rpi3_mon_poll_count` and so on, the content of the table does not change, only the spelling.

### 3.2 Notation in the table

- **Writer**: who may write. **Lock**: `L` = only under `rpi3_mon_lock`; `1W` = single writer, lock-free (see section 4a); `A` = lock-free atomic access (section 4d); `C` = constant after init.
- **Phase**: the component that first needs it (C2 foundation, C3 Core 1 hook, C5 heartbeat and deadline, C6 hardening, C7 Mode D2).

## 4. The state table

### G1. Identity and configuration (set once)

| Member | Type | Writer | Lock | Phase | Purpose and rule |
|---|---|---|---|---|---|
| `boot_id` | u32 | init | C | C5 | uniqueness tag (not a secret): low 32 bits of `HMAC(K_hb, init_ticks64 ‖ prng16)` (R17); its uniqueness across boots is tested, not assumed |
| `init_ticks` | u64 | init | C | C5 | secure-timer value at init; `boot_ticks` for the grace period |
| `mode` | u8 {F, D2} | init, one-way latch | L | C2 | mutually exclusive per boot; changes logged (R10) |
| `profile` | u8 flags | init | C | C2 | Step 2 / Monitor profile, hardened or debug |
| parameters (T, R, H, D_req, k, G, grace, ring sizes) | constants | build | C | C4 | from the generated `mon_params.h`; provisional until measured; `_Static_assert` on the invariants |

### G2. Authentication material

| Member | Type | Writer | Lock | Phase | Purpose and rule |
|---|---|---|---|---|---|
| `rpi3_mon_key_frame[32]` | const bytes | build | C | C2 | `K_frame`: sender and Secure World only. Compiled constant (no HUK on the Pi 3); readable by root Linux from the boot partition (R12) |
| `rpi3_mon_key_hb[32]` | const bytes | build | C | C5 | `K_hb`: Secure World only |
| `rpi3_mon_key_test[32]` | const bytes | build | C | C2 | `K_test`: Linux test tools only; never used by the Monitor profile for real decisions |
| `mac_pta` | `void *` | init | C | C2 | HMAC context for PTA handlers. PTA calls are serialised by OP-TEE, so one context is safe for the PTA side; **used only outside `rpi3_mon_lock`** |
| `mac_core1` | `void *` | init | C | C2 | HMAC context for Core 1 (Mode D2 chip B validation). **Never shared with `mac_pta` or with `can0_hmac_ctx`** |

### G3. Frame validation (one instance per mode, same layout)

`struct rpi3_mon_ctr` is declared once and instantiated twice (`ctr_f`, later `ctr_d2`) so that Mode F and Mode D2 never share counter state. `rpi3_mon_counter_step(ctr, ...)` is the only writer.

| Member | Type | Writer | Lock | Phase | Purpose and rule |
|---|---|---|---|---|---|
| `have_last` | bool | PTA handler (cmd 17) | L | C2 | false until the first authenticated frame: that frame is RESYNC, including counter 0 (R2) |
| `last_counter` | u32 | same | L | C2 | **separate from `secure_last_counter`**. Compared only with direct unsigned comparisons in the order of R2 (no `delta` variable) |
| `last_frame44[44]` | bytes | same | L | C2 | canonical authenticated bytes (`can_id`, counter, payload, tag), to tell DUPLICATE from CONFLICT. The wire frame is exactly 44 bytes with no padding (corrected 2026-11-06): nothing beyond byte 43 exists, is stored or is compared |
| `last_accept_ticks` | u64 | same | L | C5 | refreshes the traffic deadline |

Rule: the counter state is updated **only after** format, stream and HMAC checks have passed. A frame with a bad tag never reaches the counter rules and never advances `last_counter`.

### G4. Verdicts and exact counters (three verdicts, never merged: plan R4b)

| Member | Type | Writer | Lock | Phase | Purpose and rule |
|---|---|---|---|---|---|
| `ev_count[N_EVENT_TYPES]` | u32 array | PTA handler, Core 1 | L | C2 | **exact** per-class counters, kept even when the event ring overflows or rate-limits |
| `verdict_frame` | `{last_type, last_ticks, total}` | PTA handler | L | C2 | latest frame-level finding (MALFORMED, INVALID_AUTHENTICATION, REPLAY, GAP, RESYNC, DUPLICATE, CONFLICT) |
| `verdict_hb` | same | PTA handler, Core 1 | L | C5 | heartbeat finding (MISS, LATE, TIMEOUT, RECOVERED, BAD_NONCE, STALE ...) |
| `verdict_xcheck` | same | Core 1 | L | C7 | report cross-check finding (ALTERED, UNACCOUNTED, FABRICATED, B_MISS ...); unused in Mode F |

There is deliberately **no combined "monitor status"** field. Command 20 returns the three verdicts side by side. A good heartbeat never hides a frame or cross-check finding.

### G5. Traffic deadline and idle mode

| Member | Type | Writer | Lock | Phase | Purpose and rule |
|---|---|---|---|---|---|
| `tr_armed` | bool | PTA handler | L | C5 | arms on the first valid frame (R3) |
| `tr_last_valid_ticks` | u64 | PTA handler | L | C5 | refreshed by accept, GAP and RESYNC only; REPLAY, DUPLICATE, CONFLICT, MALFORMED and AUTH_FAIL do not refresh it |
| `tr_timeout_raised` | bool | set: `rpi3_mon_evaluate` (Core 1, PTA); cleared: cmd 17 | L | C5 | raised once, cleared by recovery (section 4d, S4) |
| `idle_until_ticks`, `idle_uses` | u64, u32 | PTA handler (cmd 21) | L | C6 | leased and capped per boot; every use is an event (R3, R10) |
| `last_eval_ticks` | u64 | Core 1, on each successful lock attempt | L | C3 | **evaluation heartbeat** (rule O-3): proves Core 1 actually got the lock and evaluated deadlines, unlike `last_poll_ticks` |

### G6. Heartbeat (Component 5)

| Member | Type | Writer | Lock | Phase | Purpose and rule |
|---|---|---|---|---|---|
| `hb_seq` | u32 | Core 1 | L | C5 | 0 at boot, first request is 1, never decremented; no wrap (stops at `0xFFFFFFFF`) |
| `hb_issue_ticks` | u64 | Core 1 | L | C5 | **the "nonce timestamp"** |
| `hb_nonce`, `hb_nonce_valid` | u64, bool | PTA handler (lazy) | L | C5 | **derived, not a stored random**: first 8 bytes of `HMAC(K_hb, boot_id ‖ hb_seq ‖ issue_ticks)`. Computed outside the lock (copy, unlock, HMAC, lock, store only if `hb_seq` is unchanged). **Not a secret**: cmd 22 hands it to Linux by design |
| `hb_state` | enum {NONE, OPEN, ANSWERED, MISSED, LATE_ANSWERED} | Core 1, PTA handler | L | C5 | one outstanding request at a time; every transition is guarded by its current value so a miss is logged exactly once (R21) |
| `hb_last_ok_ticks` | u64 | PTA handler | L | C5 | **the "last response time"**: last on-time answer; a late answer does not reset it |
| `hb_miss_count`, `hb_timeout_raised` | u32, bool | set: `rpi3_mon_evaluate` (Core 1, PTA); cleared: on-time cmd 23 | L | C5 | k consecutive misses after the boot grace G (R17, O-10) |
| `hb_next_issue` | u64 | Core 1 | L | C5 | `issue_ticks + H`; never a catch-up burst (O-11) |

### G7. Health

Superseded by **section 4a (G7 v3, approved)**. The v2 table (single-writer `1W` words) was removed on 2026-11-06 to avoid confusion; the earlier text is in `MONITOR_STATE_DESIGN.md.bak`.

### G8. Event log

| Member | Type | Writer | Lock | Phase | Purpose and rule |
|---|---|---|---|---|---|
| `ev_ring[64]` | 24-byte entries `{seq u32, type u16, flags u16, ticks u64, counter u32, detail u32}` | PTA handler, Core 1 | L | C2 | **evidence classes only** (AUTH_FAIL, REPLAY, TIMEOUT, ALTERED, UNACCOUNTED, CHIPB_FAULT): first-N per class pinned, never freed by an acknowledgement, overflow marker (O-07) |
| `mis_count[]`, `diag_last_ticks[]` | u32 array, u64 array (one entry per diagnostic class) | PTA handler, Core 1 | L | C2 | **API-misuse and diagnostic classes** (BAD_NONCE, STALE, DUPLICATE_*, UNISSUED, REPORT_MALFORMED, LOCK_TIMEOUT, MON_STARVED ...) collapse into exact counters plus at most one event per second **per class** (rule G8-6, approved 2026-11-07), so a caller with no key cannot fill the evidence ring (O-07) |
| `ev_seq`, `ev_ack`, `ev_lost` | u32 | writers; `ev_ack` by cmd 19 | L | C2 | `ev_ack` is a **bookmark only**: it never affects overwriting or `ev_lost` (DEC-IF-4); `ev_lost` counts ordinary-ring overwrites |
| `pinned_used` | u32 | PTA handler, Core 1 | L | C2 | (approved 2026-11-06) number of pinned ring slots in use. Initialised to 0 in `rpi3_mon_init`; incremented **only** when an evidence event takes a pinned slot; never decreased in normal operation (G8-4); reset only by reboot or an authorised, logged reset (none exists in Component 2). Invariants: `pinned_used <= 24` (6 evidence classes x 4) and at most 4 pinned slots per class. Reported only by cmd 20, never by cmd 18 |

### G9. Synchronisation

Superseded by **section 4b (G9 v3, approved)**: one member, `rpi3_mon_lock`. The v2 `snap_seq` seqlock counter was removed from Component 2 and is deferred to Component 7 (rule L11).

### G10. Mode D2 only (Component 7, not part of the first cut)

`rep[W]` (deadline outcome, receipt and evaluated flag per request), report staging slots (one per `hb_seq mod W`), the 128-record chip B ring, the rejected-frame ring, `ctr_d2`. Layout to be designed in Component 7.

## 4a. G7 v3 (APPROVED 2026-11-06; replaces v2)

Review questions and the evidence for each (everything below was read or compiled without touching the repository or the Pi, except where marked):

| Question | Answer | Evidence and confidence |
|---|---|---|
| Is an aligned 64-bit access atomic on this platform? | **Yes**: on ARMv8-A (AArch64) a naturally aligned 64-bit `LDR`/`STR` to Normal memory is single-copy atomic | architecture rule (ARM ARM, "Atomicity in the Arm architecture"), **not tested on this board**. The build is AArch64 (`CFG_ARM64_core=y`, checked in 1.5), and the generated code is a single `ldr x` / `str x` (below). On an ARM32 build this would **not** hold, so the design adds a compile-time guard |
| Is alignment or `volatile` enough? | **No, not by themselves** | probe compiled with the project's GCC 14.3 at `-O2 -mgeneral-regs-only`: a *plain* read in a loop became `ldr; cbnz; b .` (the value is loaded once and the loop never reloads: a reader on another core would never see an update). The `volatile` and the `__atomic_*_n(..., __ATOMIC_RELAXED)` versions reload every iteration. For the **writer**, plain, volatile and relaxed-atomic code are identical (`ldr / add / str`) |
| Does the compiler provide suitable atomic operations? | **Yes for loads and stores; avoid read-modify-write** | `__atomic_load_n`/`__atomic_store_n` with `__ATOMIC_RELAXED` compile to one `ldr`/`str` (re-checked with OP-TEE's exact compile flags, not only my approximate ones). **OP-TEE's `atomic.h` has helpers only for 32-bit and smaller types, none for 64-bit**, so two local wrappers are needed (rule 2). *Correction:* an earlier version of this row said `__atomic_fetch_add` on 8 bytes calls a libgcc helper; that was true only for the default flags. OP-TEE builds with `-mno-outline-atomics`, so it becomes an inline `ldxr / add / stxr` loop. The single-writer design still needs no read-modify-write |
| Can the PTA read a consistent snapshot? | **Each word, yes. The pair, no (by design)** | two separate 64-bit words can come from two different passes. This is acceptable because the stall decision uses `last_poll_ticks` alone and `poll_count` is diagnostic only. If a consistent pair were ever needed, it must be read under the lock or under a validated seqlock |
| Are cache visibility and ordering sufficient? | **Yes for this use, by hardware coherence** | OP-TEE maps cached normal memory as **Write-Back, Inner Shareable** (`ATTR_IWBWA_OWBWA_NTR_INDEX \| ISH`, `core_mmu_lpae.c:497-499`) with `TCR` set to inner-shareable write-back walks (`:1254-1256`), through the same `boot_mmu_config` (`TCR`, `MAIR`) on every core (`enable_mmu`, `entry_a64.S`); TF-A's Cortex-A53 reset function sets the **SMP bit** (`CPUECTLR_EL1.SMPEN`, `cortex_a53.S:283-288`) on each core's reset path, which a coherent inner-shareable cache requires; OP-TEE sets `SCTLR.M` and `SCTLR.C` (`entry_a64.S:751,768`). So a store by Core 1 becomes visible to other cores without cache maintenance. **Static reading, not measured on Core 1** (this answers the static part of open question F9 of 1.4) |
| Is a barrier needed? | **No** | G7 guards no other data (nothing is "published after" it), so relaxed ordering is enough; a barrier would only add cost. Anything that does publish data after a flag needs release/acquire (`stlr`/`ldar`) and does not belong in G7 |

### G7 v3 specification

All of G7 lives in one struct, `struct rpi3_mon_health`, a member of `rpi3_mon`, **aligned to 64 bytes and padded to a multiple of 64 bytes**, so that it shares no cache line with lock-protected members (the Cortex-A53 data cache line is 64 bytes: Cortex-A53 manual, not read from this board).

| Member | Type | Written by | Read by | Access | Rule |
|---|---|---|---|---|---|
| `poll_count` | `uint64_t` | Core 1 only | PTA (cmd 20) | relaxed atomic load, then relaxed atomic store of `value + 1` (**not** an atomic add) | diagnostic only; no decision uses it |
| `last_poll_ticks` | `uint64_t` | Core 1 only | PTA, Core 1 | relaxed atomic store of the secure-timer value | the only input to `CORE1_POLL_STALL`; the reader computes `age = now - last_poll_ticks` with **its own** `CNTPCT` read |
| `skip_count` | `uint32_t` | Core 1 only | PTA | `atomic_store_u32` / `atomic_load_u32` (existing OP-TEE helpers) | consecutive failed `trylock`; set to 0 on success |
| `skip_total` | `uint32_t` | Core 1 only | PTA | same | wraps; diagnostic |
| `starved` | `uint32_t` (0 or 1, not `bool`) | Core 1 only | PTA | same | set when `skip_count >= K`, cleared on success |

Rules:
1. **One writer.** Only Core 1's loop writes G7. The PTA never does, and **`MON_RESET` does not reset G7** (health counters are cleared only at boot). This resolves a conflict found in review: a debug reset that clears them would be a second writer.
2. **Accessors only, forced inline (amendment ACCEPTED 2026-11-06).** Every access goes through `rpi3_mon_load_u64()` / `rpi3_mon_store_u64()`, declared **`static inline __attribute__((always_inline)) __nostackcheck`** over `__atomic_load_n` / `__atomic_store_n` with `__ATOMIC_RELAXED`, or through the existing 32-bit helpers. A plain `x = h->poll_count` is a review failure (the probe shows why). **Why the attributes are mandatory:** OP-TEE compiles with `-Os -finstrument-functions -mstrict-align -mno-outline-atomics -fpie`; with a plain `static inline` GCC emitted real calls to the wrappers, and each wrapper was itself instrumented, which would have added about two hook calls (roughly 5 us) per counter update, more than twice the 2.4 us pass. With the attributes the accesses compile to a single `ldr` / `str` with no call. The function that contains the accessors (the Core 1 hook) must also be `__nostackcheck`, and the update sits in the loop body, so a pass gains no hook call.
3. **No atomic read-modify-write** anywhere in the monitor's lock-free paths, **except the one multi-writer counter `pta_lock_timeouts`** (section 4d, S1), which is not part of G7.
4. **Compile-time guards**, so a future change cannot silently lose the guarantee: `#ifndef ARM64` -> `#error`; `_Static_assert(sizeof(long) == 8)`; `_Static_assert(_Alignof(uint64_t) == 8)`; `_Static_assert(offsetof(struct rpi3_mon_state, health) % 64 == 0)`; `_Static_assert(sizeof(struct rpi3_mon_health) % 64 == 0)`; `_Static_assert(offsetof(struct rpi3_mon_health, poll_count) % 8 == 0)` and the same for `last_poll_ticks`.
5. **Update cadence (proposal; the final value is a Component 4 measurement):** `poll_count` every pass (3 instructions); `last_poll_ticks` every 256th pass (about 0.6 ms at the measured 2.435 us per pass), which is far finer than any deadline (H is about 500 ms). The cost of `barrier_read_counter_timer()` (`isb; mrs cntpct_el0`) per read is not measured yet.
6. **Code placement:** the G7 update is written **inline** in the loop body (or in a `__nostackcheck` static inline), never as a separate instrumented function, because each instrumented call costs about a whole loop pass (1.4, F8).
7. **Timer prerequisite:** `last_poll_ticks` depends on `CNTPCT` being valid and consistent on Core 1; it is untested and is a Component 4 prerequisite. Until it is shown, G7's stall detection is a design, not a verified feature.
8. **Memory ordering:** relaxed. A reader may see a value some tens of nanoseconds old (coherence latency, not measured) but never a torn or invented value.
9. **Initialisation:** G7 is zeroed at monitor init, which runs on the primary core during boot, before Core 1 is diverted (Linux brings Core 1 up about 5 s into boot), so there is no concurrent writer at that moment.

### G7 torn-read verification (2026-11-06)

Three independent routes, because none is enough alone:

| Route | Result |
|---|---|
| **Architecture rule** (Arm ARM DDI0487, B2.8.2; read from the search-result text, not the full page) | single-register loads and stores are single-copy atomic when **all the bytes lie inside one 16-byte-aligned block** and the memory is **Normal, Inner and Outer Write-Back Cacheable, Shareable**. An 8-byte-aligned doubleword never crosses a 16-byte boundary, so it qualifies **on shareable write-back memory only** (outside that, only single bytes are guaranteed). OP-TEE maps its cached RAM as exactly that (section 4a), so the rule applies. |
| **Layout guard under OP-TEE's exact flags** | the structure as designed (`health` aligned and padded to 64 bytes, `u64` members at multiples of 8) compiles with all the guards of rule 4; a deliberately broken layout (a `u32` first, packed) is **rejected** (the alignment asserts fire); an ARM32 compiler hits the `#error`. So a future edit cannot silently break the rule |
| **Direct hardware test** (`tools/torn_read`, raw numbers in `torn_read_results.txt`): a writer on one core flips a 64-bit location between 0 and all-ones, a reader on another core loads it as fast as it can, and any other value is a torn read | **aligned 64-bit relaxed atomics: 0 torn in 568.6 million reads and 1.2 billion writes**, across four core pairs, with the reader seeing both patterns (about 79 million zeros, 62 million ones). The **controls prove the detector works**: two separate 32-bit stores tore 43,161 times in 15.5 million reads; a 64-bit access straddling a cache line tore in 4.97 million of 7.3 million reads; a misaligned access staying inside one 16-byte block did not tear, as the rule predicts |

What this does and does not establish:
- Zero torn reads in this test is **evidence, not proof**; the proof is the architecture rule, and the test shows the rule holds on this SoC and that the test would have caught a violation.
- The test ran in **Linux user memory on cores 0, 2 and 3**. It could not run on Core 1 (Secure World) or in Secure World memory. The architecture rule depends only on the memory attributes, which are the same (static reading); a test on Core 1 itself needs code running there (Component 3, S2a).
- **Placement rule (new):** the guarantee exists only for Normal, Write-Back, Shareable memory. G7 must therefore live in the ordinary data of the core image (`.bss`/`.data`, cached, inner shareable). It must **never** be moved to a Device, non-cacheable or non-shareable mapping (for example a future "dedicated uncached monitor region"); if the placement ever changes, G7 must be re-reviewed. A compile-time assert cannot check memory attributes, so this is a review rule.

### G7 ordering review (2026-11-06)

Question: must any other monitor state be observed in a particular order relative to the G7 counters? Answer: **no. No G7 value publishes other data, so relaxed ordering is correct.**

| G7 value | Used by | Ordering requirement |
|---|---|---|
| `poll_count` | cmd 20 projection (diagnostic) | none |
| `last_poll_ticks` | PTA computes an age; a `CORE1_POLL_STALL` event is appended **under the lock** | none for memory; one timestamp hazard (rule O-1 below) |
| `skip_count`, `starved` | a hint that a PTA handler should run the deadline catch-up | none: a stale hint causes a needless or one-call-late catch-up, and every catch-up transition is guarded by its current value under the lock, so events still happen exactly once |
| the pair `poll_count` / `last_poll_ticks` | nobody uses them together | none |
| initialisation | zeroed on the primary core before Core 1 is diverted; the memory is also zero from load | none |

Rules that come out of the review (mandatory for every reader and writer of G7):

| # | Rule | Why |
|---|---|---|
| O-1 | **Saturating age.** A reader loads `last_poll_ticks` **first**, then reads its own clock (`barrier_read_counter_timer()`), and computes `age = (now >= t) ? now - t : 0`. A bare `now - t` is forbidden. | Core 1 may update `last_poll_ticks` just after the reader took `now`; the unsigned subtraction would then wrap to a huge age and report a false `CORE1_POLL_STALL` for a healthy core. This is the plan's O-18 rule extended to G7 |
| O-2 | **No G7 value may be used as the condition for reading lock-protected state without taking the lock.** | G7 publishes nothing; treating it as a "data ready" flag would need release/acquire ordering that G7 deliberately does not have. Component 3 code review item |
| O-3 | **`last_poll_ticks` means "Core 1 is spinning", not "Core 1 is evaluating deadlines".** Core 1 updates it every 256th pass whether or not its lock attempt succeeded. A starved Core 1 would look healthy. The evaluation heartbeat is a separate member, **`last_eval_ticks` (u64, lock-protected, written by Core 1 on each *successful* lock attempt)**, added to the deadline design (G5/G6). Fresh `last_poll_ticks` with stale `last_eval_ticks` = alive but starved; both stale = stalled. | otherwise a hostile PTA caller could starve the lock while the health words keep advancing |

Still open for G7: (a) the ordering result above is a **design-level** review; it is re-checked against the real code in Component 3 (rules O-1 to O-3 become review items); (b) `CNTPCT` working consistently on Core 1 needs code running on Core 1 (Component 4 prerequisite). Everything else about G7 is closed.

Not covered by G7 (and why): anything that needs two or more words to be seen together (use the lock), anything that guards other data (needs release/acquire), anything with more than one writer (needs the lock or an atomic read-modify-write, which is not allowed here).

## 4b. G9 v3 (APPROVED 2026-11-06, replaces v2)

### What the review found

| Question | Finding | Evidence |
|---|---|---|
| Does a sequence counter make lock-free reads safe? | **No.** Written naively it does not even survive the compiler | probe (GCC 14.3, `-O2`, AArch64): in the naive version GCC merged `seq++ ... seq++` into one `add #2; str` (the odd "write in progress" value is **never stored**) and moved the data stores before it; the naive reader **dropped the second read of `seq`**, so it validates nothing. The correct C11 form needs relaxed atomics for every shared word plus fences: the writer compiled to `str` (odd), `dmb ish`, data stores, `stlr` (even); the reader to `ldar`, data loads, `dmb ishld`, re-read |
| Is OP-TEE's lock a real acquire/release lock? | **Yes** | `spin_lock_a64.S`: lock and trylock use `ldaxr` then `stxr`; unlock is `stlr wzr`. They are out-of-line assembly, so the compiler must treat them as opaque calls: they also act as a compiler barrier. Therefore **plain loads and stores to a member are correct inside the critical section**, provided *every* access to that member (reads too) is under the lock |
| Are exclusive accesses valid on this memory? | **Yes** | the data is mapped Normal, Write-Back, Inner Shareable on every core (section 4a evidence); this is the memory type exclusives need. OP-TEE's own console lock uses the same primitives |
| Is a seqlock needed in Component 2? | **No** | the readers are only PTA handlers (cmd 18 and 20). OP-TEE serialises calls into this PTA (single instance, no `TA_FLAG_CONCURRENT`; measured, below), so at most one PTA handler contends with Core 1 for the lock. A PTA call took at least **0.86 ms** in the best contended case and **2.84 ms** alone, while the lock is held for microseconds, so the share of time a PTA handler holds the lock is about 1 percent or less (estimate; to be measured in Components 4 and 5). The seqlock would add subtle memory-ordering code to remove a contention that is already small |
| What does taking the lock cost on this debug build? | **about four instrumented calls per lock cycle** (corrected 2026-11-06; v3 draft said two) | code-level check of the bounded-acquire sequence (compiled with OP-TEE's exact flags, probe deleted): `cpu_spin_trylock` and `cpu_spin_unlock` are **not inlined**, so each is an instrumented call, and so are `spinlock_count_incr` and `spinlock_count_decr`. `thread_mask_exceptions`, `thread_unmask_exceptions`, `__cpu_spin_trylock`, `__cpu_spin_unlock` and the assertion trap are hook-free. With about 2.4 us per instrumented call (1.4, F8) that is **about 10 us per lock cycle (an inference from F8, to be measured), several Core 1 passes**. The hold itself (three of the four calls lie between taking and releasing) has a floor of about 5 to 7 us plus the critical section. **[CORRECTION 2026-11-07, see 4b.1: the "about four" counts the OP-TEE primitives only and is right for that scope; the monitor's PTA lock cycle is 7 instrumented entries, a Core 1 attempt is 4 lock entries plus the attempt function itself, and the "three of the four inside the hold" statement does not match the compiled call order (Core 1: 2 of 4, PTA: 4 of 7). The microsecond figures above are unmeasured inferences and are unchanged.]** |

### 4b.1 Lock-cost correction (2026-11-07; document only, no design decision changed)

The original text mixed three different things under "the lock costs about four instrumented calls (about 10 us)". They are now kept apart.

**1. Verified function-entry count.** Method: the Monitor-ON `tee.elf` built in a scratch directory with OP-TEE's real flags and `CFG_CORE_DEBUG_CHECK_STACKS=y` was disassembled, and each function on the lock path was checked for a call to `__cyg_profile_func_enter`; the call order inside each function was read from the same disassembly. Commands and output are in `COMPONENT_2_2_FINAL_CLOSURE.md` (section "Lock-cost evidence").

| Scope | Instrumented entries | Which |
|---|---|---|
| OP-TEE lock primitives alone (take and release) | **4** | `cpu_spin_trylock`, `spinlock_count_incr`, `cpu_spin_unlock`, `spinlock_count_decr`. This is the original "about four", and it is correct for this scope. |
| Core 1, successful attempt | 4 lock entries, **5** with `rpi3_mon_core1_attempt` itself | the four above plus the attempt function |
| Core 1, failed attempt | 1 lock entry, **2** with the attempt function | only `cpu_spin_trylock`; `spinlock_count_incr` runs only after a successful trylock (OP-TEE `spinlock.h`) |
| PTA command, one lock cycle (`rpi3_mon_acquire` + `rpi3_mon_release`) | **7** | `rpi3_mon_acquire`, `cpu_spin_trylock`, `spinlock_count_incr`, `rpi3_mon_release`, `cpu_spin_unlock_xrestore`, `cpu_spin_unlock`, `spinlock_count_decr` |
| PTA command including the wrappers every command uses | **9** | the seven plus `rpi3_mon_hold` and `rpi3_mon_wait_init` |

Not instrumented (no hook call in the compiled code): `thread_mask_exceptions`, `thread_unmask_exceptions`, `thread_get_exceptions`, `thread_set_exceptions`, `atomic_inc32`, `have_spinlock`, `thread_get_core_local`, `__cpu_spin_trylock`, `__cpu_spin_unlock`, and the critical-section helpers (`rpi3_mon_evaluate`, `rpi3_mon_ev_append`).

Entries while the lock is actually held (from the call order in the compiled code): `spinlock_count_incr` runs after the lock is taken; `spinlock_count_decr` runs **after** `__cpu_spin_unlock` has released it. So **Core 1: 2 of its 4 lock entries** (`spinlock_count_incr` and the entry of `cpu_spin_unlock`), and **PTA: 4 of its 7** (`spinlock_count_incr`, `rpi3_mon_release`, `cpu_spin_unlock_xrestore`, `cpu_spin_unlock`). The original statement "three of the four calls lie between taking and releasing" is therefore also inexact. The compiler additionally emits `__cyg_profile_func_exit` calls in some of these functions; the verified count above is of entries, and the cost of the exit hooks has not been separated.

**2. Estimated lock overhead (unmeasured).** The figures "about 2.4 us per instrumented call", "about 10 us per lock cycle", "about 1.6 percent of Core 1 passes" and "a floor of about 5 to 7 us" are the inferences recorded under F8 and L4. They have **not** been recomputed with the corrected counts, because the per-entry cost itself was never measured and a recomputation would present arithmetic as a measurement. They stay as originally written and stay flagged as inferences.

**3. Actual hardware hold time: PENDING.** Maximum and 99th-percentile hold time (check H3), measured wait for a simulated holder (X9) and the Core 1 pass duration with monitor logic and collision rate (Y10) have not been measured and need the Raspberry Pi. The CS5 value "under 100 us" remains a design target, not a measurement.

**What does not change:** the 256-pass cadence (L4), the 1 ms budget and 2,000-attempt cap (4f), the lock design (one lock, no seqlock), and every approved decision. If the hardware measurement shows the real cost is different, the cadence and budget are revisited in Components 4 and 5, as already stated in CS5.

### 4b.2 Measured Core 1 loop on the board (2026-11-07; document only, nothing approved is changed)

Measured on the Pi (Pi 3 B, 600 MHz powersave, hardened monitor image, `poll_count` over the secure counter, two 5-second runs that agree):

| Quantity | Design assumption (4b, L4, C9, F8) | Measured |
|---|---|---|
| Core 1 pass | 2.435 us (measured on the old loop, which called an instrumented function every pass) | **75.2 ns** (13.29 million passes per second) |
| Lock attempt interval at cadence 256 | about 0.62 ms | **about 19 us** (about 51,900 attempts per second) |
| Collisions with PTA holds | not estimated | 1 per 5 s idle; 0.61 per status call during a PTA latency run (60,582 over 100,000 calls) |
| PTA hold per status call | about 10 us per lock cycle (inference) | about 11.6 us (**inference** from the collision rate: 0.61 x 19.2 us; real hold time still pending H3) |
| Starvation threshold K = 8 | 8 x 0.62 ms = about 5 ms of denied lock | 8 x 19 us = about 154 us of denied lock |

Meaning: the loop is lighter than the design assumed because the monitor loop no longer calls an instrumented function on every pass, so a pass-count cadence of 256 now fires about 32 times more often than intended. Effects seen: nothing broke (0 BUSY and 0 lock timeouts in 120,000 PTA calls, no starvation in a 60-second soak), and the best-effort legacy-refusal log dropped one event in three because it makes a single lock attempt. Not measured: the cost of one attempt and the share of Core 1 time spent in the lock.
Open decision (not applied): raise `RPI3_MON_CORE1_CADENCE` to about 8192 (restores the 0.62 ms intent), or keep 256 and accept about 52,000 attempts per second after the hold time is measured (H3). A time-based gate would remove the dependency on pass duration but costs a timer read per pass, which the design rejected (4g C1); it is mentioned only so the choice is explicit. Evidence: `baseline/board_checks_monitor_candidate_20261107.txt`, `baseline/pta_latency_*_monitor_20261107.txt`.

### G9 v3 specification

G9 consists of **one member**, `rpi3_mon_lock` (an OP-TEE spinlock, `unsigned int`, 4-byte aligned). **`snap_seq` and the seqlock are removed from Component 2.**

| # | Rule |
|---|---|
| L1 | Every member marked `L` is read and written **only** under `rpi3_mon_lock`. Inside the critical section, plain loads and stores are correct. |
| L2 | G7 is the **only** lock-free group, and only through the approved atomic accessors. No other member is read without the lock, so the two access styles never mix on one member. |
| L3 | PTA handlers take the lock with the **bounded** sequence of rule R-A2 (not the unbounded `cpu_spin_lock_xsave`); Core 1 takes it with a single `cpu_spin_trylock()` per attempt, **never a retry loop**, and skips on failure (`skip_count`, G7). |
| L4 | **Core 1 attempts the lock at most once per 256 passes** (about 0.62 ms at 600 MHz; the same cadence as `last_poll_ticks`), not every pass: a lock cycle costs more than a pass. At that cadence the lock overhead is about 1.6 percent (about 10 us per 256 passes of 2.435 us = 623 us; corrected from 1 percent), and a skipped attempt delays a deadline check by well under a millisecond, far below T (300 ms) and H (1 s). **[2026-11-07, see 4b.1: the cadence of 256 passes is unchanged. The "about four instrumented calls" is verified for the lock primitives of a Core 1 attempt (a successful attempt is 5 entries including the attempt function itself, a failed one is 2); the 10 us and 1.6 percent figures remain unmeasured inferences, to be confirmed by check Y10.]** |
| L5 | Inside the lock: field reads and writes, comparisons, one fixed-size (24-byte) append, and a copy of at most 128 bytes into a local buffer. Written as `static inline` or `__nostackcheck` helpers, so **no instrumented call** is made inside (except the lock wrappers' own). No printing, no HMAC, no caller-buffer copy, no allocation, no call that can sleep. |
| L6 | Copy-out is two-step: under the lock copy into a **local buffer on the PTA thread stack** (never Core 1's tmp stack), unlock, then copy to the caller's memory. **cmd 18 returns at most 5 records per call** (5 x 24 B = 120 B, within the 128-byte copy limit CS3 and check H5; amended 2026-11-06 from 16 entries / 384 B, which exceeded CS3; a caller loops, and the scan visits at most 64 ring slots in the same hold). cmd 20 copies one fixed projection of at most 128 bytes. |
| L7 | Every exit path unlocks (single-exit structure). A handler never returns to Normal World holding the lock (OP-TEE's debug assertion `assert_have_no_spinlock` would trip). |
| L8 | One lock, no nesting, no lock ordering. |
| L9 | FIQ handling follows decision D1 = Option B (rule R-A6): in Monitor builds Core 1 never unmasks FIQ. The FIQ handler never takes the lock. |
| L10 | Correctness of the **lock-protected state** does not depend on OP-TEE serialising PTA calls: PTA handlers still take the lock, and serialisation only keeps contention low. The shared HMAC context `mac_pta` **does** depend on it and is guarded separately (section 4d, S3). |
| L11 | **Deferred to Component 7** (the chip B record ring, where Core 1 is a high-rate single writer): a seqlock, with these recorded conditions: every shared word accessed through relaxed-atomic accessors; writer `store seq odd; release fence (dmb ish); data stores; store-release seq even`; reader `load-acquire seq; if odd retry; data loads; acquire fence (dmb ishld); re-read seq; compare`; exactly one writer at a time; bounded retries then BUSY; and a unit test against the **naive** version to show the compiler difference. |
| L12 | **Bounded PTA acquire** (approved 2026-11-06; exact sequence in R-A2). Initial budget 1 ms (19,200 ticks of the 19.2 MHz counter). **The budget is not evidence that the critical section is safe**: the real hold time must be measured in Components 4 and 5, and the budget set to at least ten times the longest measured hold. |
| L13 | A timed-out acquire touches **no** `L` member; it only adds 1 to the lock-free counter `pta_lock_timeouts` (a multi-writer counter, the one atomic add allowed) and returns `BUSY`. The `BUSY` reply also carries the lock-free health words, so a consumer can tell "Core 1 hung holding the lock" from "just busy". |

### Preliminary measurements behind this (`pta_latency_preliminary.txt`; idle board, not the Component 4 matrix)

| Run | Result |
|---|---|
| One caller alone | 311 to 329 calls/s; per call min **2.84 ms**, p50 2.87 to 3.29 ms, p99 3.29 to 3.72 ms, max up to 6.1 ms |
| Three callers at once (cores 0, 2, 3) | aggregate **466 calls/s**; fastest call **0.857 ms**, p50 about 0.87 ms, but **p90 about 23 ms, p99 66 to 76 ms, max 123 to 131 ms** (calls queue behind each other) |
| Environment | CPU governor `powersave`, **all CPUs at 600 MHz** (max 1200 MHz), cluster-wide |

Consequences recorded (new items F22 to F26):

| # | Finding | Where it matters |
|---|---|---|
| F22 | A Linux to PTA call takes about 3 ms when alone (mostly the stack-check hook: a call traverses on the order of a thousand instrumented functions at about 2.4 us each; **an inference**, not measured) | Component 4: L99.9 for the heartbeat timing; the build setting `CFG_CORE_DEBUG_CHECK_STACKS=y` is the largest single cost |
| F23 | **Unexplained:** the fastest call under contention (0.857 ms) is 3.3 times faster than the lone-caller minimum (2.84 ms) with the clock constant at 600 MHz | Component 4 must explain this before any latency number is used for a deadline |
| F24 | **Any process that can open `/dev/tee0` can delay every other PTA caller by 100 ms or more** just by calling the PTA in a loop (measured: 3 callers, max 131 ms). A hostile Linux can therefore make the legitimate consumer's heartbeat answer late without touching the consumer | Components 5 and 6: false `HEARTBEAT_MISS` or `HEARTBEAT_LATE` is an availability attack the monitor cannot prevent; Secure World's own clock still records when each call arrived, so the late answer is visible and attributable, but the report must say so |
| F25 | CPU frequency is Linux-controlled and cluster-wide (`powersave` = 600 MHz now). Every Core 1 timing (2.435 us per pass = about 1,460 cycles) depends on it; a root user can change it | record the frequency with every timing; the secure timer (`CNTPCT`, 19.2 MHz) is independent of it; the earlier puzzle about the high cycle count per pass (1.4, F9) is partly explained |
| F26 | The lock cycle costs about four instrumented calls (about 10 us, corrected 2026-11-06 from two and 5 us; an inference from F8, to be measured); most other low-level helpers are `__nostackcheck`. **[CORRECTION 2026-11-07, see 4b.1: about four is the count of OP-TEE primitives only. A PTA command's lock cycle through `rpi3_mon_acquire` and `rpi3_mon_release` is 7 instrumented entries (9 with `rpi3_mon_hold` and `rpi3_mon_wait_init`); the 10 us figure remains an unmeasured inference.]** | rule L4; the monitor's hot helpers should use `__nostackcheck` where they need no stack-limit check |
| F27 | OP-TEE's heap lock is taken with `cpu_spin_lock_xsave`, which spins without a limit and with every exception masked (`bget_malloc.c:143-150`); HMAC allocates and frees through it (`hmac_init.c:45`, `hmac_done.c:40-41`). The monitor's bounded lock-wait budget does **not** bound this separate lock. A panic or assertion while the heap lock is held could leave later allocating calls spinning forever on a Linux core. **A design-level availability risk, not a measured failure**; the monitor must not claim that all PTA calls are bounded | Mode F proceeds with the risk recorded (Core 1's monitor loop performs no HMAC; the legacy chip A path is reachable in code and dormant only because the FIQ is not armed, see Z7). **Component 7 hard requirement:** in the Monitor profile Core 1 must not invoke HMAC or any function that can allocate or free from the shared heap, directly or indirectly, checked on the Core 1 call graph; a separate `mac_core1` context does not make HMAC heap-free. D2 must be revisited before Core 1 chip B validation is enabled |

## 4c. G5, G6, G8 rules (APPROVED 2026-11-06)

User decisions: G5 limits stay Component 4 parameters; a late answer does not reset `hb_miss_count`; 4 pinned entries per evidence class. Design rules only; no code.

### G5. Idle lease and traffic timeout

A lease is an **exemption, not evidence that the CAN network is healthy.**

| # | Rule |
|---|---|
| G5-1 | A lease is `idle_until_ticks`, set by cmd 21 to `now + requested`, with `requested` capped at `IDLE_MAX`. While `now <= idle_until_ticks` the traffic deadline is disarmed and no `TRAFFIC_TIMEOUT` is raised. |
| G5-2 | Every lease request is counted (`idle_uses`) and limited: at `IDLE_USES_MAX` per boot further requests are refused. **Event naming (clarification from review):** a grant or renewal is logged as `LEASE_GRANTED` or `LEASE_RENEWED`; `MODE_CHANGED` is logged **only** when the mode actually changes (EXPECTED to IDLE, IDLE to EXPECTED, including expiry). A refused request is also an event. |
| G5-3 | The lease **expires by itself**: when `now > idle_until_ticks` the next evaluation (Core 1 or PTA catch-up, under the lock, using `expired()`) returns to EXPECTED and logs `MODE_CHANGED`. |
| G5-4 | On return to EXPECTED the traffic window restarts from that moment (`tr_last_valid_ticks = now`), never from the stale value, so no false timeout follows. |
| G5-5 | Each transition is guarded by its current value and happens exactly once (R21): in EXPECTED with `tr_armed`, `now - tr_last_valid_ticks > T` and not `tr_timeout_raised` raises one `TRAFFIC_TIMEOUT` and sets `tr_timeout_raised`; the next frame that refreshes the deadline clears it and logs one `TRAFFIC_RECOVERED`. |
| G5-6 | Granting a lease does **not** clear an existing timeout; only a frame that refreshes the deadline does. |
| G5-7 | The lease state (EXPECTED, IDLE by lease, remaining time, `idle_uses`) is part of the exposed 128-byte projection (cmd 20), so a consumer can tell "idle by lease" from "silent". |
| Open | `IDLE_MAX` and `IDLE_USES_MAX` values: Component 4. |

### G6. Heartbeat nonce, replay and rollover

| # | Rule |
|---|---|
| G6-1 | A nonce lives from `hb_issue_ticks` to `hb_issue_ticks + D_req` and accepts **exactly one** answer. A second answer for the same `hb_seq`, or one for a closed request, is API misuse (`mis_count`) and changes nothing else. |
| G6-2 | An answer is **on time** only if `hb_state == OPEN`, its sequence equals the outstanding `hb_seq`, and its nonce equals the **stored** nonce (derived once by cmd 22; the nonce is public, so a plain compare is correct and no HMAC is needed in cmd 23). In `MISSED` a matching answer is recorded as late (G6-3). Anything else is refused and changes no state (DEC-IF-7). |
| G6-3 | **Late answer (decided: no reset).** `hb_miss_count` is incremented **once**, at the guarded transition OPEN to MISSED, and never again for that request. A later valid answer moves MISSED to LATE_ANSWERED, is recorded as an event, and does **not** reset `hb_miss_count` and does **not** set `hb_last_ok_ticks`. Duplicate late answers are API misuse (G6-1). Only an on-time answer (state ANSWERED) resets the consecutive-miss count. A late answer shows that Linux eventually responded, not that it met the deadline. |
| G6-4 | A new request is issued only at `hb_next_issue = issue_ticks + H` (no catch-up burst, O-11) and invalidates the previous nonce (`hb_nonce_valid = false`). |
| G6-5 | `hb_seq` never wraps: at `0xFFFFFFFF` Core 1 stops issuing and logs `HB_SEQ_EXHAUSTED` (about 136 years at H = 1 s, so unreachable in practice but defined). `boot_id` changes every boot, so a nonce from an earlier boot is invalid. |

### G8. Event ring

| # | Rule |
|---|---|
| G8-1 | 64 slots: **6 evidence classes x 4 pinned = 24 pinned**, plus **40 ordinary** slots. The first 4 events of each class are pinned. |
| G8-2 | After a class has used its 4 pinned slots, its further events go to the ordinary ring. **When all 40 ordinary slots are occupied, a new event overwrites the oldest unpinned entry** and `ev_lost` is incremented (rule preserved explicitly after review). |
| G8-3 | Every overwrite or suppression increments `ev_lost`, and the next record carries an overflow flag, so the consumer sees the gap. |
| G8-4 | A pinned entry is **never** overwritten or freed by `ev_ack` during normal operation; nothing replaces it. Only a reboot, or an explicitly authorised reset that is itself logged, clears it. |
| G8-5 | Cmd 18 returns at most 5 records per call in `seq` order (24 bytes each, 120 B), with a `more` flag and `ev_lost`; the pinned count is **not** returned by cmd 18 and is reported only by cmd 20 (`pinned_used`). Amended 2026-11-06. |
| G8-6 | **Per-class rate limit (approved 2026-11-07, DEC-RATE-1).** Every diagnostic or API-misuse class has its own limit of **one event per second**, kept in its own timestamp `diag_last_ticks[class]` (`L`, initialised to "never"). The first event of a class is always appended. A later event of the same class is appended only if at least one full second (19,200,000 ticks) has passed since that class's last appended event, using `now >= last` with a strict `<` test for suppression (exactly one second later is allowed). **A suppressed event still increments the exact per-class counter** (`ev_count[class]`); no class ever suppresses another. **Evidence classes (amended 2026-11-07, DEC-IF-10 option Y):** the first four events of each evidence class are always kept (pinned) and never limited; after its pinned quota an evidence class is subject to the same one-per-second per-class limit, with the exact counter always kept, so a caller with no key cannot flood the ordinary ring with AUTH_FAIL or REPLAY events. Each event type is its own class (list in section 4k, K10). |

## 4d. Shared-state ownership and access (Component 2.2.1; APPROVED 2026-11-06)

Class: **L** = only under `rpi3_mon_lock`; **A** = lock-free atomic access; **C** = constant after init (init runs before Linux boots and before Core 1 is diverted).

| Group | Members | Writers | Readers | Class |
|---|---|---|---|---|
| G1 | `boot_id`, `init_ticks`, `profile`, parameters | init | cmd 20, 22, Core 1 | C |
| G1 | `mode` | init only in Component 2 (S5) | all | L |
| G2 | keys | build | HMAC code | C (`const`) |
| G2 | `mac_pta`, `mac_core1` (pointers) | init | PTA / Core 1 | C; context use is outside the lock; `mac_pta` is guarded (S3) |
| G3 | `have_last`, `last_counter`, `last_frame44`, `last_accept_ticks` | cmd 17 | cmd 17, 20 | L |
| G4 | `ev_count[]`, `verdict_*` | PTA, Core 1 | cmd 20 | L |
| G5 | `tr_*`, `idle_until_ticks`, `idle_uses` | cmd 17, 21, `rpi3_mon_evaluate` | cmd 20, Core 1 | L |
| G5 | `last_eval_ticks` | Core 1 | cmd 20 | L |
| G6 | `hb_seq`, `hb_issue_ticks`, `hb_next_issue`, `hb_state`, `hb_miss_count`, `hb_timeout_raised` | Core 1, cmd 23, `rpi3_mon_evaluate` | cmd 20, 22 | L |
| G6 | `hb_nonce`, `hb_nonce_valid`, `hb_last_ok_ticks` | cmd 22 (lazy), cmd 23 | cmd 20, 22 | L |
| G7 | `poll_count`, `last_poll_ticks`, `skip_count`, `skip_total`, `starved` | **Core 1 only** | cmd 20, Core 1 | A (section 4a) |
| G8 | `ev_ring`, `mis_count[]`, `diag_last_ticks[]`, `ev_seq`, `ev_ack`, `ev_lost` | PTA, Core 1; `ev_ack` by cmd 19 | cmd 18, 20 | L |
| G9 | `rpi3_mon_lock` | n/a | n/a | OP-TEE spinlock |
| new | `pta_lock_timeouts` | any timed-out PTA call | cmd 20, Core 1 | **A, multi-writer** (S1) |
| new | `pta_timeouts_seen` | Core 1 | Core 1, cmd 20 | L |

### S1. `pta_lock_timeouts`

- A separate, naturally aligned `uint32_t` on its **own 64-byte-aligned, padded cache line**, apart from G7 and every lock-protected member (G7 is written about 410,000 times a second; sharing a line could make the exclusive store fail repeatedly; not measured).
- Initialised to 0 at boot before any concurrent execution. **Not reset by `MON_RESET`**; resets on reboot.
- Incremented **once per timed-out PTA acquisition and only on that path**, with OP-TEE's existing `atomic_inc32()` (`ldaxr`/`stxr` retry loop in `atomic_a64.S`, linked into `tee.elf`). The return value is ignored. This is the monitor's only lock-free read-modify-write.
- Readers use an atomic 32-bit load, relaxed ordering (the counter publishes no other data).
- Wraps modulo 2^32. Consumers compute changes as `(uint32_t)(new - old)`, correct as long as fewer than 2^32 timeouts occur between reads (about 50 days at the physical ceiling of roughly 1,000 per second).
- It measures **contention**. It is neither proof of an attack nor of a Core 1 fault, because a caller with no key can drive it (F24).
- Today PTA calls are serialised, so concurrent writers cannot occur; the atomic form is kept so that correctness does not depend on that.

### S2. Core 1 observation rule

1. Only inside Core 1's scheduled attempt (once per 256 passes): `trylock`. If it fails, nothing is read or consumed.
2. Under the lock: load `pta_lock_timeouts` atomically; `delta = (uint32_t)(count - pta_timeouts_seen)`.
3. If `delta != 0`: set `pta_timeouts_seen = count`; add `delta` to `ev_count[LOCK_TIMEOUT]` (exact); append **at most one `LOCK_TIMEOUT` event per second** (the per-class limit of rule G8-6, independent of every other class) with `delta` in the detail field.
4. `LOCK_TIMEOUT` is an **API-misuse/diagnostic class**, not one of the six pinned evidence classes (O-07). There is no `lock_timeout_ever` flag; `ev_count[LOCK_TIMEOUT] > 0` carries that fact.

### S3. `mac_pta` concurrency guard

- Compile time: the PTA's flags come from one named macro used for both the registration and an assertion that `!(flags & TA_FLAG_CONCURRENT)`. Current flags: `PTA_DEFAULT_FLAGS` (`SINGLE_INSTANCE | MULTI_SESSION | INSTANCE_KEEP_ALIVE`, `rpi3_can_spi_poc.c:471`). OP-TEE enforces the serialisation with a busy flag and condition variable (`tee_ta_manager.c:127-165, 800`), held for the whole call including trips to Normal World.
- Run time: an atomic in-use flag brackets every use of `mac_pta`, released on every exit path. If already set, the call returns `BUSY` and does not touch the context. **It is not counted** (decision 2026-11-06): the situation cannot occur while the compile-time check holds, and counting would need a lock or a new atomic counter. The HMAC stays outside `rpi3_mon_lock`.

### S4. Exactly-once flags

- All deadline evaluation is one function, `rpi3_mon_evaluate(now)`, called **under the lock** by Core 1 (its scheduled attempt) and by every PTA handler after it takes the lock. One code path means one set of guards.
- The setter returns the old value; an event is logged only if the old value was false (R21).
- `tr_timeout_raised`: set by `evaluate`, cleared by cmd 17. `hb_timeout_raised`: set by `evaluate`, cleared by an on-time cmd 23. A late answer clears neither (G6-3).
- If a timeout was already raised when a lease expires, the window restarts and the flag stays set until a valid frame arrives; no second event.

### S5. `mode`

Set to F once at init. **No command writes it in Component 2.** The D2 latch (a guarded F to D2 transition under the lock, logged `MODE_CHANGED`) is defined in Component 7.

### S6. BUSY and output parameters (static trace, not yet run on the board)

Value outputs reach Linux even when the PTA returns an error: `pseudo_ta.c:188-216` (`update_out_param` runs before `res` is checked), `entry_std.c:447-452` (`copy_out_param` ignores `res`), Linux `optee/call.c:436-447` (`from_msg_param` unconditional), `libteec` `tee_client_api.c:736-738` (post-process after a successful ioctl). Memref outputs return only their size, and a failed `copy_in_param` (bad parameters) skips the copy. The "BUSY plus lock-free health words" reply is therefore supported, pending V14.

### Verification V1 to V14

| ID | Check | Kind |
|---|---|---|
| V1 | `pta_lock_timeouts` is 32-bit, naturally aligned, on its own 64-byte padded line | compile-time assert |
| V2 | No lost increments with concurrent writers | on-Pi user-space test (`atomic_a64.S` assembled, cores 0, 2, 3; proves the instruction sequence on real A53, **not** Secure-World memory) plus a host test of the accounting wrapper |
| V3 | Every read of the counter is an atomic load | review/grep |
| V4 | `atomic_inc32` is the helper used; its assembly and call path inspected | inspection |
| V5 | Builds with `PLATFORM=rpi3` and the real flags | build |
| V6, V7 | One increment per timed-out acquisition; none on success | unit test |
| V8 | No ordinary write to the counter; not reset by `MON_RESET` | grep |
| V9 | Wrap to zero behaves as modulo 2^32 | test |
| V10 | Core 1 rule: a failed trylock consumes nothing; one event per observed increase, rate limited | unit test |
| V11 | `mac_pta` guard: occupied returns `BUSY`, released on every path; the build fails if `CONCURRENT` is added | test and compile-time |
| V12 | Exactly-once: interleaved Core 1 / PTA call orders with a simulated clock raise and recover each flag once | host-side unit test |
| V13 | Exactly one assignment to `mode` outside init | grep |
| V14 | An error-return command delivers its value outputs to Linux | on-board test (Component 2 acceptance) |

## 4e. Lock initialisation and ownership (Component 2.2.2; APPROVED 2026-11-06)

### I1. Declaration and initial state

`static unsigned int rpi3_mon_lock;` lives in `.bss`; the loader clears `.bss` (`entry_a64.S:246-266`) and `SPINLOCK_UNLOCK` is 0 (`spinlock.h:11`). `rpi3_mon_init()` also assigns `rpi3_mon_lock = SPINLOCK_UNLOCK` explicitly, to document the invariant. This is not a substitute for check W1.

### I2. Init owner and timing

`rpi3_mon_init()` is registered with `driver_init(...)` and runs on the **primary core** inside `boot_init_primary_final()` (`boot.c:1096-1105`), before OP-TEE hands over to the Normal World bootloader. It therefore runs before Linux and before Core 1 can exist, so no lock is contended during init. It must not depend on the order of other same-level `driver_init` functions (link order), including `rpi3_can_fiq_init`. Crypto is ready by then (`call_service_initcalls()`, `boot.c:853`).

### I3. Fail-closed init

`init_ok` is set **last**. Until it is set, every monitor command returns an error and Core 1 does not touch monitor state. If `crypto_mac_alloc_ctx` or any other step fails, the monitor stays unavailable for that boot, with an `EMSG` giving the reason.

### I4. Runtime ownership

| Context | Acquire | Release |
|---|---|---|
| PTA handler | bounded sequence (R-A2) | `cpu_spin_unlock_xrestore` |
| Core 1 | one `cpu_spin_trylock` per 256 passes (G9 L3, L4) | `cpu_spin_unlock` |

One lock, no nesting (L8). Core 1 is started only once, by Linux's PSCI `CPU_ON` through TF-A (`vector_cpu_on_entry`, then `vector_secure_server_entry`, then `rpi3_can_server_loop()`, which is `__noreturn`), so there is no second execution.

### I5. Dead-holder policy

- `panic()` stops only the panicking core: `CFG_HALT_CORES_ON_PANIC=n` and `CFG_MULTI_CORE_HALTING=n` in this build (`out/arm/conf.mk`). `__do_panic` masks every exception, prints, and spins in `cpu_idle()` (`panic.c`). Linux and the PTA path keep running.
- **Core 1 panics holding the lock:** the lock stays held. PTA callers time out after the budget and get `BUSY` with the lock-free health words (value outputs reach Linux on an error return: section 4d, S6). A stale `last_poll_ticks` plus a rising `pta_lock_timeouts` suggests Core 1 failure or starvation; it does not prove a crash with the lock held.
- **A PTA handler panics holding the lock:** Core 1's trylock fails every time, `skip_count` rises and `starved` is set.
- **The lock is never force-released** by Linux, a watchdog or any other context. If the holder is only delayed, a forced release lets two contexts modify state at once and corrupts verdicts.
- **Recovery** is a reboot decided by the operator. The monitor cannot recover itself.
- **Consumer rule (Component 6):** `BUSY` or stale health is never treated as "all is well". N consecutive `BUSY` replies, or a stale `last_poll_ticks`, make the Linux-side consumer raise its own "monitor unavailable" alarm, because Secure World cannot report its own death.
- The stack-check hook (`thread.c:348-357`) can panic at any instrumented function entry, including the instrumented lock wrappers (`cpu_spin_trylock`, `cpu_spin_unlock`, `spinlock_count_incr`, `spinlock_count_decr`), so a stack violation or an assertion can fire inside a lock hold. A FIQ frame overlapping Core 1's stack (O-01) made that more likely under D1 Option A, which was rejected: D1 = Option B (2026-11-07) masks FIQ throughout the Core 1 loop, so this cause does not arise.

### I6. Debug wake command

In the Monitor profile, `PTA_RPI3_CAN_SPI_POC_CMD_WAKE_CORE1` (`rpi3_can_spi_poc.c:457`) returns `TEE_ERROR_NOT_SUPPORTED` and logs one rate-limited misuse-class event; it joins cmd 3/6/7/13/14 on the refuse-or-log list. A second wake would only return `PSCI_E_ALREADY_ON` (`psci_on.c:40`; TF-A handler `opteed_main.c:490-495` calls `psci_cpu_on(1, 0, 0)`), and by the structure of `opteed_main.c` (the wake case lies after the non-secure-caller branch at `:257`) Normal World cannot send that SMC directly; this was read, not tested. The refusal removes the only route and an unnecessary startup path. It changes an existing command, so it is designed in the interface subcomponent and not implemented now.

### Acceptance checks W1 to W7

| ID | Check | Kind |
|---|---|---|
| W1 | The lock's memory is cached, inner-shareable normal memory on the primary core and on Core 1 | static trace now (same `boot_mmu_config` on every core, section 4a); on-board check later |
| W2 | Init runs before Core 1 can exist | boot-log check |
| W3 | `init_ok` fail-closed: a forced init failure leaves every command returning an error | test |
| W4 | A repeated wake returns `ALREADY_ON`; the Monitor profile refuses the command | on-board test |
| W5 | No code path force-releases `rpi3_mon_lock` | grep/review |
| W6 | A stalled Core 1 leaves PTA returning `BUSY` plus health words within the budget | test with a simulated holder |
| W7 | After a forced Core 1 panic, Linux and the PTA stay up | on-board test; **only by a separate decision** (no UART, no spare card) |

## 4f. PTA bounded lock acquisition (Component 2.2.3; APPROVED 2026-11-06)

Amends R-A2 and L12: the timeout test fails closed and has a second, timer-independent bound; the budget is per command and counts lock-wait time only.

### B1. Per-command wait state

Each PTA command that takes the lock creates, on the PTA thread stack, `{ wait_ticks_used = 0, attempts_used = 0 }`. It is shared by every acquisition in that command and never reset between them. The budget measures **time spent waiting for the lock**, not time since the command started: otherwise the HMAC of a two-acquisition command (about 3 ms per PTA call on this debug build, F22, an inference) would consume it.

### B2. Parameters (Component 4 fixes the final values)

| Parameter | Initial | Meaning |
|---|---|---|
| `budget_ticks` | **19,200** (1 ms at 19.2 MHz) | total lock-wait allowed per command |
| `attempt_cap` | **2,000 (provisional)** | total trylock attempts per command; about 5 times the roughly 300 to 400 attempts expected in 1 ms (an estimate), so a broken timer is caught within a few milliseconds |

### B3. Acquire sequence (exact order)

1. **Pre-check, before masking:** if `wait_ticks_used >= budget_ticks` or `attempts_used >= attempt_cap`, go to EXHAUSTED (step 7).
2. `e = thread_mask_exceptions(THREAD_EXCP_ALL)`; `t0 = barrier_read_counter_timer()`.
3. Attempt: `cpu_spin_trylock(&rpi3_mon_lock)`. On success go to step 5.
4. On failure: `attempts_used++`; `now = barrier_read_counter_timer()`. EXHAUSTED (step 6) if **any** holds, tested in this order: `now < t0` (fail closed); `wait_ticks_used + (now - t0) > budget_ticks`; `attempts_used >= attempt_cap`. Otherwise repeat from step 3.
5. **SUCCESS:** `wait_ticks_used += (timer read after success) - t0`; keep `e`; return with exceptions still masked. Release is `cpu_spin_unlock_xrestore(&rpi3_mon_lock, e)`: unlock first, unmask second (the spinlock-count assertion requires this order).
6. **EXHAUSTED after masking:** `thread_unmask_exceptions(e)` first (the lock was never taken, so no spinlock count is outstanding), then `atomic_inc32(&pta_lock_timeouts)`, then return `BUSY`.
7. **EXHAUSTED at the pre-check:** nothing was masked; increment `pta_lock_timeouts` and return `BUSY`.

A command that fails returns `BUSY` immediately, so it adds at most one to `pta_lock_timeouts`.

### B4. Fail-closed helper

The spin bound uses its own helper, `rpi3_mon_wait_exhausted()`, where `now < t0` means **exhausted**. The deadline helper `expired()` (R-A4; `now < t0` means not expired, because a false alarm is the bad direction there) is **never** used in the acquire loop (check X8). The two safe directions are opposite, so the helpers have different names.

### B5. Two acquisitions in one command (cmd 22)

Acquire, read state, release (exceptions restored); HMAC outside the lock; acquire again using the remaining budget (HMAC time is not counted); if that fails, return `BUSY`. The nonce is derived, so discarding the work is harmless. It does not get a fresh 1 ms.

### B6. BUSY response

Result `TEE_ERROR_BUSY`. No lock-protected member is read and no caller buffer is copied. The value outputs carry the lock-free health words (G7 and `pta_lock_timeouts`, read with the approved atomic accessors after unmasking); their layout is defined in the interface subcomponent. Delivery with an error return is supported by the trace in section 4d, S6, pending V14.

### B7. Masked window

For a PTA core: `T_masked <= T_budget + T_critical_section`. This is a **design target, not a measured guarantee**; lock-hold time must be measured in Components 4 and 5 before the budget is final.

### B8. Timer assumptions

The counter is the 64-bit `CNTPCT` at 19.2 MHz, read by `barrier_read_counter_timer()` (`static inline __noprof`, `isb; mrs cntpct_el0`, no compiler hook). It is read on PTA-handling cores (xtest `TEE Wait` passed there). A frozen or backward timer is covered by the attempt cap and the `now < t0` rule. Consistency of `CNTPCT` across cores is read from the architecture and not measured here.

### B9. Assumptions to verify

- **A1.** `__cpu_spin_trylock` retries internally while its exclusive store keeps failing (`spin_lock_a64.S`); it returns "held" only when it sees the lock held. The attempt cap bounds the outer loop, not that inner retry. The counter and G7 are on separate cache lines, so it should be negligible; not measured.
- **A2.** `tee_ta_set_busy()` waits on a condition variable, with no timeout, for an earlier PTA invocation (`tee_ta_manager.c:127-165`). That is a sleeping serialisation wait, not a spin, and never happens while `rpi3_mon_lock` is held. The existing SPI debug commands (up to 1,000,000 poll iterations per wait, `rpi3_can_spi_poc.c:86,122`) can delay later monitor calls; the Monitor-profile refuse-or-log policy (interface subcomponent) handles that, and 2.2.3 does not bound those loops.

### Acceptance checks X1 to X9

| ID | Check | Kind |
|---|---|---|
| X1 | Free lock: acquired on the first attempt, no timeout increment | unit test |
| X2 | Lock held by a simulated holder: `BUSY` after at most the budget plus one iteration; exceptions restored; counter +1 | unit test, simulated clock |
| X3 | Frozen timer (mock returns a constant): exits via the attempt cap with `BUSY` | unit test |
| X4 | Backward timer (`now < t0`): exits at once with `BUSY` | unit test |
| X5 | Two acquisitions in one command: the second gets only the remainder; non-wait time (HMAC) is not counted | unit test |
| X6 | Exception state before and after is identical on every exit path; no spinlock count left outstanding | test; debug assertions |
| X7 | The `BUSY` path touches no lock-protected member and copies no caller buffer | review plus test |
| X8 | `rpi3_mon_lock` is never taken with `cpu_spin_lock` or `cpu_spin_lock_xsave`, and the acquire loop never calls `expired()` | grep/review |
| X9 | On the board: measured wait for a simulated holder is about the budget; attempts per millisecond recorded (calibrates the cap); error-with-outputs delivery seen (V14) | on-board, deferred to implementation and Component 4 |

X1 to X8 are host-side tests of the pure logic with a mocked timer and lock; X9 is the hardware item and stays tracked with W1 to W7 and V1 to V14.

## 4g. Core 1 lock acquisition (Component 2.2.4; APPROVED 2026-11-06)

**D1 is decided (2026-11-07): Option B.** Option A is kept below only as the record of the alternative that was rejected.

### C1. Cadence

Core 1 keeps a **local** `uint32_t pass` (never shared) and increments it each loop pass. A lock attempt happens only when `(pass & 255) == 0`: one attempt per 256 passes, about 0.62 ms at the measured 2.435 us per pass. The counter wraps harmlessly. No timer is read on non-attempt passes. A time-based trigger is an alternative Component 4 may choose if the pass duration proves unstable once monitor logic lengthens a pass.

### C2. No spinning

Each attempt is exactly one `cpu_spin_trylock()` call with no retry loop around it. Core 1 never calls `cpu_spin_lock`, `cpu_spin_lock_xsave` or the bounded PTA sequence of section 4f. The inner retry inside `__cpu_spin_trylock` on a failed exclusive store (assumption A1, section 4f) is unchanged.

### C3. Failed attempt (touches only G7, never a lock-protected member)

1. `skip_count++` and `skip_total++`, through the G7 accessors.
2. If `skip_count >= K`, set `starved = 1`.
3. Continue the loop. A skipped attempt delays evaluation by about 0.62 ms, far below T (300 ms) and H (1 s).

### C4. Successful attempt (inside the lock)

1. `rpi3_mon_evaluate(now)` and the `pta_lock_timeouts` observation (section 4d, S2).
2. If `skip_count >= K` (this episode reached K), append **one** `MON_STARVED` event with the episode length (`skip_count`) in the detail field.
3. Set `skip_count = 0` and `starved = 0`, then unlock.
4. **`skip_total` is cumulative and is never reset.**

### C5. Starvation episode

An episode begins at the first failed attempt after a success and ends at the next success. One `MON_STARVED` event is logged per episode that reached K, never one per failed attempt; shorter episodes log nothing. If the lock never becomes available, `starved` stays 1 and is visible through the lock-free G7 state; no event can be appended because Core 1 cannot take the lock, which agrees with the dead-holder policy (section 4e, I5). `K` is a Component 4 parameter.

### C6. Event class

`MON_STARVED` is a diagnostic class with its own one-event-per-second limit (rule G8-6; it is not shared with `LOCK_TIMEOUT` or any other class), **not** one of the six pinned evidence classes: a caller can influence contention, so it must not fill the evidence ring (O-07). Approved 2026-11-06.

### C7. D1: FIQ policy (DECIDED 2026-11-07, Option B)

- **Option A:** `m = thread_mask_exceptions(THREAD_EXCP_NATIVE_INTR)` before the attempt. On failure `thread_unmask_exceptions(m)` at once; on success unlock first, then unmask. The FIQ handler never takes the lock, so a FIQ arriving inside a hold only lengthens it.
- **Option B (CHOSEN):** FIQ stays masked for the whole loop. In Monitor builds the existing `thread_set_exceptions(thread_get_exceptions() & ~THREAD_EXCP_NATIVE_INTR)` at `rpi3_can_server.c:395` is not executed (it stays in the non-Monitor build, which is unchanged), so every exception stays masked on Core 1. Requirements: (1) FIQ is never unmasked anywhere in the Core 1 monitor loop; (2) a **runtime assertion at loop entry** checks that FIQ and IRQ are masked; (3) a **static check** verifies that the loop never unmasks FIQ; (4) the legacy CAN FIQ path is not enabled in Monitor mode; (5) the non-Monitor build behaves exactly as before. **Effect:** this removes the identified O-01 stack-overlap risk by construction (a FIQ can no longer be taken on top of the live loop frames, so there is no overlap, no FIQ-extended lock hold and no stack-limit panic from it) and needs no stack resize. The change to `rpi3_can_server.c` belongs to the Core 1 integration stage and has NOT been made yet.
- Option A (rejected): would have kept the unmask, wrapped each attempt in a FIQ mask, raised `CFG_STACK_TMP_EXTRA` to at least 2,048 and required the on-board stack-watermark test.

### C8. IRQ rule (both options)

**Core 1 never unmasks IRQ**, because `cpu_spin_trylock()` asserts that foreign interrupts are disabled (`thread_foreign_intr_disabled()`). IRQ is masked at entry (`init_secondary_helper`) and the loop clears only the FIQ bit. `thread_set_exceptions` asserts "no spinlock held" only when the new mask leaves the foreign interrupt unmasked (`thread.c:74, 99`), so FIQ-mask changes around a hold never trip it.

### C9. Cost

A successful attempt is about four instrumented calls (about 10 us, an inference from F8); a failed one about one (about 2.4 us, since the count is bumped only on success). Overhead is about 1.6 percent of passes on success (G9 rule L4). **[2026-11-07, see 4b.1: verified entry counts are 4 lock entries on success (5 with the attempt function) and 1 on failure (2 with the attempt function); the microsecond and percentage figures are unmeasured inferences.]**

### C10. Assumptions to verify

- Pass duration stays near 2.435 us once monitor work is added.
- `thread_core_local->locked_count` is valid on Core 1 (`spin_lock_debug.c:10-24`); it is already used there by the stack-check hook.

### Acceptance checks Y1 to Y10

| ID | Check | Kind |
|---|---|---|
| Y1 | Every Core 1 attempt is non-spinning and occurs only at the approved cadence: exactly one attempt per 256 passes, no loop around `cpu_spin_trylock` | review plus unit test counting attempts |
| Y2 | A failed attempt changes only G7 lock-free bookkeeping, never a lock-protected member | review plus test with sentinel values in the lock-protected region |
| Y3 | `K-1` failures leave `starved = 0`; the `K`th sets it; a success resets `skip_count` and `starved`; `skip_total` never resets | unit test |
| Y4 | Exactly one `MON_STARVED` per episode that reached K; none for shorter episodes; detail equals the episode length | unit test |
| Y5 | Lock never available: `starved` stays 1, readable lock-free, no event appended | unit test with a held mock lock |
| Y6 | Core 1 never unmasks IRQ after the monitor starts; a runtime assertion at loop entry confirms IRQ masked | grep/review plus debug assertion |
| Y7 | D1 = Option B (decided 2026-11-07): Core 1 never unmasks FIQ in Monitor builds (static check on the loop source) and a runtime assertion at loop entry confirms FIQ and IRQ are masked | review plus debug assertion |
| Y8 | G7 words are accessed only through the approved accessors | grep/review |
| Y9 | The pass counter is local and wraps harmlessly | review |
| Y10 | On the board: measured pass duration with monitor logic, collision rate and lock-hold time; confirms the 256-pass cadence or triggers the time-based alternative | on-board, Component 4 |

Y1 to Y9 are host-side tests or reviews of pure logic; Y10 is the hardware item, tracked with W1 to W7, V1 to V14 and X9.

## 4h. Critical-section rules (Component 2.2.5; APPROVED 2026-11-06)

Design baseline, not implementation acceptance.

| Rule | Text |
|---|---|
| CS1 | No HMAC, allocation or free, UART or trace printing, or **copying to or from caller memory** while holding the monitor lock. (Copying monitor state into a local buffer on the PTA stack is allowed; see CS3.) |
| CS2 | Only small, fixed, bounded operations: no unbounded loops, waits, polling, RPC or sleeping, and nothing whose duration depends on external input. No new `assert` beyond cheap invariants that cannot fail, because each is a potential panic site (section 4e, I5). |
| CS3 | Where a state copy is needed, use the verified `always_inline` word or byte loop (16 x u64 for the 128-byte projection; field-wise or a 24-byte struct assignment for events; an explicit loop for the 44-byte compare). **Never** `memcpy`, `memcmp`, `memset` or a large struct assignment under the lock: probe 2026-11-06 (OP-TEE's exact flags) showed they compile to calls, and `memcpy`, `memcmp` and `memset` in `tee.elf` are instrumented (`__cyg_profile_func_enter/exit`). Maximum copy 128 bytes. Every function whose body runs inside a hold is `always_inline` or `__nostackcheck`; the lock wrappers' own hooks (`spinlock_count_incr` and `_decr` run inside the hold) are a known, accepted floor. |
| CS4 | No nested locks: the monitor lock is never acquired while another lock is held, and no other lock is taken while it is held. HMAC and its heap operations stay outside. |
| CS5 | Initial design target: hold time **under 100 us** (one tenth of the 1 ms PTA budget), **not a measured guarantee**. Hold time runs from the successful trylock return to the start of the unlock call, measured with `CNTPCT` in a test build (52 ns resolution), as maximum and 99th percentile. Components 4 and 5 measure it and revise the budget if the evidence requires. Under D1 Option A a FIQ inside a hold extends it. The wrapper-bypass option (calling the hook-free `__cpu_spin_trylock` and `__cpu_spin_unlock` directly) is left to Component 4 and not recommended now. |

| ID | Check | Kind |
|---|---|---|
| H1 | The compiled critical-section code has no `bl` to `memcpy`, `memcmp`, `memset` or `__cyg_profile_func_*`, apart from the lock wrappers | disassembly check |
| H2 | No forbidden call (CS1, CS2) appears inside any hold | grep/review |
| H3 | Measured worst-case and 99th-percentile hold time recorded | on-board, Components 4 and 5 |
| H4 | No nested lock anywhere on the monitor paths | review |
| H5 | Copy sizes never exceed 128 bytes | static assert and review |

## 4i. Shared-state integration matrix (Component 2.2.6; APPROVED 2026-11-06)

Command numbers follow this document. **Direct access** is what the command itself reads and writes; evaluation is described once below.

| Cmd | Direct reads (lock) | Direct writes (lock) | Lock-free (A) | Holds | HMAC and buffers |
|---|---|---|---|---|---|
| 17 submit frame | G3 | G3, G4, G5 (`tr_*`), G8 | none | one, after HMAC | 44-byte frame copied in before the lock; HMAC (`mac_pta`) outside |
| 18 read events | G8 | none | none | one | at most 5 records (120 B) via one bounded scan of at most 64 slots into a local buffer, copied to the caller after unlock |
| 19 acknowledge | G8 | G8 (`ev_ack`) | none | one | none |
| 20 status | G1 to G6, G8 counters | none | G7 words, `pta_lock_timeouts` | one | projection of at most 128 B into a local buffer, copied to the caller after unlock |
| 21 idle lease | G5 | G5, G8 | none | one | none |
| 22 get nonce | G6 | G6 (`hb_nonce`) | none | two | HMAC between the holds; shared budget (section 4f, B5) |
| 23 heartbeat answer | G6 | G6, G4, G8 | none | one | nonce and `hb_seq` arrive in value parameters (no buffer); compared with the stored nonce under the hold; **no HMAC and no `mac_pta`** (DEC-IF-7) |

**Evaluation side effects (every command above, under the lock).** After taking the lock a handler reads the clock once and calls `rpi3_mon_evaluate(now)`, **before** its direct access, so it sees current state. Evaluation may set or clear the timeout flags, move `hb_state` to MISSED, bump `hb_miss_count`, expire a lease (restarting the traffic window), and append `TRAFFIC_TIMEOUT`, `HEARTBEAT_MISS` or `MODE_CHANGED` events, each guarded to happen once. **Commands 18 and 20 are therefore not purely read-only.** The exactly-once guards prevent duplicate events; they do not make the operation read-only.

**Lock-free column.** G7 and `pta_lock_timeouts` are read only through their approved accessors and are **not** protected by the monitor lock.

**BUSY rule (unchanged).** On lock-acquisition failure, return `TEE_ERROR_BUSY` with only the lock-free health words; touch no lock-protected state and copy no lock-protected projection to the caller.

**Out of scope.** The chip A debug variables (commands 4, 15, 16) and the SPI0 accesses by commands 6, 7 and 14 are outside this matrix. The Monitor profile's refuse-or-log policy handles those legacy commands (interface subcomponent).

| ID | Check | Kind |
|---|---|---|
| Z1 | Every access to a lock-protected member happens inside a hold (a mock accessor fails if the lock flag is not set) | host test plus review |
| Z2 | G7 and `pta_lock_timeouts` are accessed only through the approved accessors | grep/review |
| Z3 | Caller-buffer copies happen before acquisition or after release | review plus test with a lock-held flag in the mock copy function |
| Z4 | The `BUSY` path touches no lock-protected member and returns only G7 and the counter | test with sentinels |
| Z5 | Evaluation runs once per command, before direct access, and its transitions are exactly-once | host test (with V12) |
| Z6 | `mac_pta` use is guarded and outside the lock (with V11) | test and review |
| Z7 | Core 1 call-graph audit: no reachable heap allocation or HMAC in the Monitor profile; the chip A path (`rpi3_can_fiq_process_pending_frame`) is recorded as reachable but dormant (FIQ not armed), or compiled out (F27) | static call-graph audit |
| Z8 | Commands 4, 6, 7, 14, 15 and 16 are refused or logged in the Monitor profile | interface subcomponent |

## 4j. Synchronization acceptance and review (Component 2.2.7; APPROVED 2026-11-06)

This section closes the synchronization **design**. It does not close implementation or verification: **none of the 53 checks below has been run or passed**; inventorying a check is not passing it.

### Consistency review (2026-11-06)

The document was scanned for contradictory normative wording. One defect was found and fixed: R-A2 still contained the obsolete fail-open condition `now >= t0 && now - t0 > budget` and lacked the attempt cap and shared budget. R-A2 was rewritten so that section 4f (B1 to B3) is the only normative description of the bounded acquisition. The remaining `now >= t0` forms are the G7 saturating age (rule O-1) and the deadline helper `expired()` (R-A4); they serve different purposes, and section 4f B4 requires that the acquire loop never uses `expired()`.

### Check inventory (53 checks)

| ID | Check | Kind | Status |
|---|---|---|---|
| V6 | One increment of `pta_lock_timeouts` per timed-out PTA acquisition | host-side logic test | NOT RUN |
| V7 | No increment on a successful acquisition | host-side logic test | NOT RUN |
| V9 | The counter wraps modulo 2^32 and consumers' `(uint32_t)(new - old)` stays correct | host-side logic test | NOT RUN |
| V10 | Core 1 observation rule: failed trylock consumes nothing; one rate-limited event per observed increase | host-side logic test | NOT RUN |
| V11 | `mac_pta` guard: occupied returns `BUSY`, released on every path; build fails if `CONCURRENT` is added | host-side logic test | NOT RUN |
| V12 | Exactly-once timeout/heartbeat flags under interleaved Core 1 and PTA call orders (simulated clock) | host-side logic test | NOT RUN |
| W3 | `init_ok` fail-closed: a forced init failure leaves every command returning an error | host-side logic test | NOT RUN |
| W6 | A stalled Core 1 leaves PTA returning `BUSY` plus health words within the budget (simulated holder) | host-side logic test | NOT RUN |
| X1 | Free lock: acquired on the first attempt, no timeout increment | host-side logic test | NOT RUN |
| X2 | Lock held: `BUSY` after at most the budget plus one iteration; exceptions restored; counter +1 | host-side logic test | NOT RUN |
| X3 | Frozen timer: exits via the attempt cap with `BUSY` | host-side logic test | NOT RUN |
| X4 | Backward timer (`now < t0`): exits at once with `BUSY` | host-side logic test | NOT RUN |
| X5 | Two acquisitions in one command share the budget; non-wait time (HMAC) not counted | host-side logic test | NOT RUN |
| X6 | Exception state identical before and after on every exit path; no spinlock count left | host-side logic test | NOT RUN |
| X7 | `BUSY` path touches no lock-protected member and copies no caller buffer (review plus test) | host-side logic test | NOT RUN |
| Y1 | Core 1 attempts are non-spinning and at the approved cadence: one per 256 passes (review plus test) | host-side logic test | NOT RUN |
| Y2 | A failed Core 1 attempt changes only G7 lock-free bookkeeping (sentinel test) | host-side logic test | NOT RUN |
| Y3 | `skip_count`, `skip_total`, `starved` transitions around K | host-side logic test | NOT RUN |
| Y4 | Exactly one `MON_STARVED` per episode that reached K; detail equals episode length | host-side logic test | NOT RUN |
| Y5 | Lock never available: `starved` stays 1, no event appended | host-side logic test | NOT RUN |
| Z1 | Every access to a lock-protected member happens inside a hold (mock accessor checks the lock flag) | host-side logic test | NOT RUN |
| Z3 | Caller-buffer copies happen before acquisition or after release (mock copy checks the lock flag) | host-side logic test | NOT RUN |
| Z4 | `BUSY` path touches no lock-protected member and returns only G7 and the counter (sentinels) | host-side logic test | NOT RUN |
| Z5 | Evaluation runs once per command, before direct access, with exactly-once transitions | host-side logic test | NOT RUN |
| Z6 | `mac_pta` use is guarded and outside the lock (test and review) | host-side logic test | NOT RUN |
| V1 | `pta_lock_timeouts` is 32-bit, naturally aligned, on its own 64-byte padded line (static assert) | static / review / build / disassembly | NOT RUN |
| V3 | Every read of `pta_lock_timeouts` is an atomic load | static / review / build / disassembly | NOT RUN |
| V4 | `atomic_inc32` is the helper used; assembly and call path inspected | static / review / build / disassembly | NOT RUN |
| V5 | Builds with `PLATFORM=rpi3` and the real flags | static / review / build / disassembly | NOT RUN |
| V8 | No ordinary write to the counter; not reset by `MON_RESET` | static / review / build / disassembly | NOT RUN |
| V13 | Exactly one assignment to `mode` outside init | static / review / build / disassembly | NOT RUN |
| W5 | No code path force-releases `rpi3_mon_lock` | static / review / build / disassembly | NOT RUN |
| X8 | `rpi3_mon_lock` never taken with `cpu_spin_lock` or `cpu_spin_lock_xsave`; the acquire loop never calls `expired()` | static / review / build / disassembly | NOT RUN |
| Y6 | Core 1 never unmasks IRQ after the monitor starts (runtime assertion at loop entry) | static / review / build / disassembly | NOT RUN |
| Y7 | D1 = Option B (decided 2026-11-07): Core 1 never unmasks FIQ in Monitor builds (static check) and a runtime assertion at loop entry confirms FIQ and IRQ are masked; pending until the Core 1 integration stage | static / review / build / disassembly | NOT RUN |
| Y8 | G7 words accessed only through the approved accessors | static / review / build / disassembly | NOT RUN |
| Y9 | The Core 1 pass counter is local and wraps harmlessly | static / review / build / disassembly | NOT RUN |
| H1 | Compiled critical-section code has no `bl` to `memcpy`, `memcmp`, `memset` or `__cyg_profile_func_*` apart from the lock wrappers (disassembly) | static / review / build / disassembly | NOT RUN |
| H2 | No forbidden call (CS1, CS2) appears inside any hold | static / review / build / disassembly | NOT RUN |
| H4 | No nested lock on the monitor paths | static / review / build / disassembly | NOT RUN |
| H5 | Copy sizes never exceed 128 bytes (static assert and review) | static / review / build / disassembly | NOT RUN |
| Z2 | G7 and `pta_lock_timeouts` accessed only through the approved accessors | static / review / build / disassembly | NOT RUN |
| Z7 | Core 1 call-graph audit: no reachable heap allocation or HMAC in the Monitor profile; chip A path recorded as reachable but dormant, or compiled out (F27) | static / review / build / disassembly | NOT RUN |
| V2 | No lost increments with concurrent writers: user-space `atomic_a64.S` test on the Pi, cores 0, 2, 3 (Normal-World memory) plus host wrapper test | on-board | NOT RUN |
| V14 | An error-return command delivers its value outputs to Linux | on-board | NOT RUN |
| W1 | Lock memory is cached, inner-shareable normal memory on the primary core and on Core 1 (static trace done; on-board check pending) | on-board | NOT RUN |
| W2 | Init runs before Core 1 can exist (boot log) | on-board | NOT RUN |
| W4 | A repeated wake returns `ALREADY_ON`; the Monitor profile refuses the command | on-board | NOT RUN |
| W7 | After a forced Core 1 panic Linux and the PTA stay up (only by a separate decision; no UART, no spare card) | on-board | NOT RUN |
| X9 | Measured wait for a simulated holder is about the budget; attempts per millisecond recorded (calibrates the cap) | on-board | NOT RUN |
| Y10 | Measured pass duration with monitor logic, collision rate and hold time (confirms the 256-pass cadence) | on-board | NOT RUN |
| H3 | Measured worst-case and 99th-percentile lock-hold time (CS5) | on-board | NOT RUN |
| Z8 | Commands 4, 6, 7, 14, 15 and 16 are refused or logged in the Monitor profile | deferred to interface subcomponent | NOT RUN |

Counts: host-side logic tests 25, static / review / build / disassembly 18, on-board 9, deferred to the interface subcomponent 1; total 53. **[2026-11-07: the Status column above is the design-time status and is intentionally left as written. The per-row evidence mapping after implementation is in `COMPONENT_2_2_INVENTORY.md`.]** The 25 host-side tests need the pure-logic code to exist and are written together with the implementation, using one harness: a controllable mock timer (freezable, able to run backwards), a mock lock that can be held, a recorder of exception-mask state, and a "lock held" flag that mock accessors and mock copy functions check.

### Interface and implementation checks added 2026-11-06 (not part of the original 53)

Contract source: the command 18 and command 20 contracts approved in the 2026-11-06 interface review (Steps 1 and 2, with corrections A to E and the health-pair draft). Their text is written into the document with the interface section, once the remaining interface decisions are closed. **Status is `NOT RUN` for every real-source check.** The last column is the result of the separate host reference model (`sync_model/`) and is **not** evidence about the OP-TEE code.

| ID | Check | Kind | Status (real source) | Host reference model (not real-source evidence) |
|---|---|---|---|---|
| N1 | Cmd 20 page 0 returns exactly 128 bytes, `version` 1, `length` 128, all reserved bytes zero | host-side logic test | NOT RUN | HOST MODEL PASS |
| N2 | Cmd 20 page 2 and higher: `BAD_PARAMETERS` with the health pair zero; page 1 returns the 128-byte counters page (superseded 2026-11-07 by DEC-IF-10: it was `NOT_SUPPORTED`) | host-side logic test | NOT RUN | HOST MODEL PASS |
| N3 | Cmd 20 with a 127-byte buffer: `SHORT_BUFFER`, `size` 128, no data | host-side logic test | NOT RUN | HOST MODEL PASS |
| N4 | Cmd 20 on `BUSY`: `size` 0, no projection, health pair filled | host-side logic test | NOT RUN | HOST MODEL PASS |
| N5 | Projection static asserts: size 128, alignment 8, every field offset | static / build | NOT RUN | model: size, alignment and offsets asserted |
| N6 | Cmd 20 sentinels: not armed or never answered gives all ones; a past lease gives 0 | host-side logic test | NOT RUN | HOST MODEL PASS |
| N7 | Cmd 20 `now_ticks` is read inside the hold and the locked words come from one hold; lock-free fields are read after unlock | host-side logic test | NOT RUN | HOST MODEL PASS |
| N8 | Cmd 20 precedence: parameters (including page 2 and above), then `BAD_STATE`, then short buffer, then `BUSY` (the "unsupported page" step was removed 2026-11-07 when page 1 became supported) | host-side logic test | NOT RUN | HOST MODEL PASS |
| HP1 | Every monitor command's `param_types` ends with `(VALUE_OUTPUT, VALUE_OUTPUT)` in slots 2 and 3 | static / review | NOT RUN | not in model |
| HP2 | Health pair is filled on `SUCCESS` and `BUSY` only, zero for every other result | host-side logic test | NOT RUN | HOST MODEL PASS |
| HP3 | The `BUSY` path reads no lock-protected member | host-side logic test | NOT RUN | HOST MODEL PASS |
| HP4 | Health age sentinel `0xFFFFFFFF` for never polled, saturation, and `skip_count` saturating at 255 | host-side logic test | NOT RUN | HOST MODEL PASS |
| HP5 | Health lock-free reads happen after unlock or unmask | host-side logic test | NOT RUN | HOST MODEL PASS |
| HP6 | Static assert on the `p2.b` bit layout; reserved bits zero | static / build | NOT RUN | not in model |
| Q1 | Cmd 18: `max entries` 1 to 5 else `BAD_PARAMETERS`; buffer at least 24 x max; at most 5 records and 120 B; `more` flag; strict seq order | host-side logic test | NOT RUN | HOST MODEL PASS |
| Q2 | Static assert `5 * sizeof(record) <= 128` (CS3, H5) | static / build | NOT RUN | model: asserted |
| Q3 | `pinned_used` invariants: at most 24, at most 4 per class, equals the pinned slots in use, never decreases in normal operation | host-side logic test | NOT RUN | HOST MODEL PASS |
| Q4 | Cmd 18 scan visits at most 64 slots in one hold; included in the H3 worst-case and 99th-percentile hold-time measurement | host-side test, then on-board (H3) | NOT RUN | model: scan bound asserted; hold time not measured |
| Q5 | Per-class diagnostic rate limit (rule G8-6): one event per second per class; a suppressed event of one class never suppresses another; exact counters always incremented; evidence classes never limited; the one-second boundary is inclusive | host-side logic test | NOT RUN | HOST MODEL PASS (test group Q5, 8 checks; 4 mutants caught) |
| C17-1 | Size not 44 is MALFORMED detail 1 and the buffer is not read | host-side logic test | NOT RUN | not in model |
| C17-2 | `can_id` range and stream list `{0x123}` give MALFORMED details 2 and 3 | host-side logic test | NOT RUN | not in model |
| C17-3 | AUTH_FAIL changes no state | host-side logic test | NOT RUN | not in model |
| C17-4 | Counter vector: last=100: 90 REPLAY, 100 DUPLICATE or CONFLICT, 101 ACCEPTED, 105 GAP of 3, `0xFFFFFFFF` then 0 REPLAY, first frame counter 0 RESYNC | host-side logic test | NOT RUN | not in model |
| C17-5 | The traffic deadline is refreshed only by ACCEPTED, GAP and RESYNC | host-side logic test | NOT RUN | not in model |
| C17-6 | `BUSY` precedes any state change | host-side logic test | NOT RUN | not in model |
| C17-7 | Copy before the lock; HMAC outside the lock | host-side logic test | NOT RUN | not in model |
| C17-8 | Tag comparison constant time over 32 bytes | static / review / build | NOT RUN | not in model |
| C17-9 | NULL buffer with size above 0 is `BAD_PARAMETERS` | host-side logic test | NOT RUN | not in model |
| ACK1 | An acknowledgement never changes `ev_lost` or the overwrite victim | host-side logic test | NOT RUN | HOST MODEL PASS |
| ACK2 | Clamp above `ev_seq`; `out b` reports the effective value | host-side logic test | NOT RUN | HOST MODEL PASS |
| ACK3 | A lower value is a no-op; `ev_ack` never decreases | host-side logic test | NOT RUN | HOST MODEL PASS |
| ACK4 | `b` non-zero is `BAD_PARAMETERS`; health pair on `SUCCESS` and `BUSY` only | host-side logic test | NOT RUN | HOST MODEL PASS |
| L21-1 | Parameter validation (mode, reserved, zero duration) | host-side logic test | NOT RUN | HOST MODEL PASS |
| L21-2 | Clamp to `IDLE_MAX` and reported time | host-side logic test | NOT RUN | HOST MODEL PASS |
| L21-3 | The five verdicts occur in the right states | host-side logic test | NOT RUN | HOST MODEL PASS |
| L21-4 | Only granted and renewed requests count; REFUSED_CAP changes nothing | host-side logic test | NOT RUN | HOST MODEL PASS |
| L21-5 | Renewal sets `now + clamped duration`, not additive, may shorten | host-side logic test | NOT RUN | HOST MODEL PASS |
| L21-6 | Return restarts the window; a grant never clears a raised timeout | host-side logic test | NOT RUN | HOST MODEL PASS |
| L21-7 | Heartbeat evaluation unaffected during IDLE | host-side logic test | NOT RUN | HOST MODEL PASS |
| L21-8 | A lease that expires during evaluation yields GRANTED, not RENEWED | host-side logic test | NOT RUN | HOST MODEL PASS |
| L21-9 | Events follow G5-2 and exact counters survive the per-class limit | host-side logic test | NOT RUN | HOST MODEL PASS |
| N22-1 | Exact `param_types`; `BAD_PARAMETERS` before `BAD_STATE` | host-side logic test | NOT RUN | not in model |
| N22-2 | NONE, ANSWERED and LATE_ANSWERED return a zero nonce with the state | host-side logic test | NOT RUN | not in model |
| N22-3 | OPEN and MISSED return the nonce, matching a known HMAC-SHA256 vector | host-side logic test | NOT RUN | not in model |
| N22-4 | A second call for the same challenge does not recompute | host-side logic test | NOT RUN | not in model |
| N22-5 | `hb_seq` changed between the holds: nothing stored, new state, zero nonce | host-side logic test | NOT RUN | not in model |
| N22-6 | Second hold uses only the remaining budget; exhaustion is `BUSY` | host-side logic test | NOT RUN | not in model |
| N22-7 | `mac_pta` guard busy returns `BUSY` before the HMAC with no state change | host-side logic test | NOT RUN | not in model |
| N22-8 | The HMAC runs with the lock released | host-side logic test | NOT RUN | not in model |
| N22-9 | Input layout and byte order exactly as specified | host-side logic test | NOT RUN | not in model |
| N22-10 | Health pair on `SUCCESS` and `BUSY` only | host-side logic test | NOT RUN | not in model |
| N23-1 | Reserved field non-zero is `BAD_PARAMETERS`; `BAD_STATE` when not initialised | host-side logic test | NOT RUN | HOST MODEL PASS |
| N23-2 | Precedence UNISSUED, STALE, DUPLICATE as specified | host-side logic test | NOT RUN | HOST MODEL PASS |
| N23-3 | BAD_NONCE details 1 and 2 change no state | host-side logic test | NOT RUN | HOST MODEL PASS |
| N23-4 | ON_TIME transitions and recovered event | host-side logic test | NOT RUN | HOST MODEL PASS |
| N23-5 | LATE leaves the miss count and clears nothing | host-side logic test | NOT RUN | HOST MODEL PASS |
| N23-6 | A new challenge invalidates the old nonce | host-side logic test | NOT RUN | HOST MODEL PASS |
| N23-7 | Evaluation runs before the comparison (deadline boundary) | host-side logic test | NOT RUN | HOST MODEL PASS |
| N23-8 | Command 23 contains no HMAC and does not use `mac_pta` | static / review / build | NOT RUN | not in model |
| N23-9 | Health pair on `SUCCESS` and `BUSY` only | host-side logic test | NOT RUN | HOST MODEL PASS |
| N23-10 | Events and exact counters per class | host-side logic test | NOT RUN | HOST MODEL PASS |
| DS1 | An unknown id with any types is `NOT_SUPPORTED` in the enabled build | host-side logic test | NOT RUN | not in model |
| DS2 | The disabled build behaves as today (unknown id with other types `BAD_PARAMETERS`, with `VALUE_OUTPUT` `NOT_SUPPORTED`) | host-side logic test, then board | NOT RUN | not in model |
| DS3 | A refused legacy id is `NOT_SUPPORTED` regardless of the passed types | host-side logic test | NOT RUN | not in model |
| DS4 | Monitor init failure: 17 to 23 `BAD_STATE`, legacy 1, 2, 12 still work | host-side logic test, then board | NOT RUN | not in model |
| LG1 | Refused ids 0, 3, 6, 7, 13, 14 in both profiles | host-side logic test | NOT RUN | not in model |
| LG2 | Debug profile: readers 4, 5, 8, 9, 10, 11, 15, 16 unchanged plus `LEGACY_CMD_USED`; hardened refuses them | host-side logic test | NOT RUN | not in model |
| LG3 | Commands 1, 2, 12 unchanged in all builds | host-side logic test, then board | NOT RUN | not in model |
| LG4 | Refusal logging is one non-spinning trylock and never waits | host-side logic test | NOT RUN | not in model |
| LG5 | Disabled build: legacy behaviour identical to the baseline | static / review / build, then board | NOT RUN | not in model |
| EV1 | The 25-entry event table matches K10 (static asserts) | static / review / build | NOT RUN | not in model |
| EV2 | The first four events of each evidence class are always kept | host-side logic test | NOT RUN | HOST MODEL PASS |
| EV3 | Evidence flood: after the pinned quota AUTH_FAIL and REPLAY are limited per class, exact counters kept, other unread entries not evicted | host-side logic test | NOT RUN | HOST MODEL PASS |
| EV4 | Verdict to event mapping (K10) | host-side logic test | NOT RUN | not in model |
| EV5 | Cmd 20 page 1 layout, offsets and content | host-side logic test | NOT RUN | HOST MODEL PASS |
| EV6 | TIMEOUT detail 3 (never started) | host-side logic test | NOT RUN | not in model |
| TM1 | Provisional constants in one header with static asserts (`D_req < H`) | static / review / build | NOT RUN | not in model |
| TM2 | Constants equal the approved table in K11 (review) | static / review / build | NOT RUN | not in model |
| TM3 | `K` and `k` boundary behaviour | host-side logic test | NOT RUN | HOST MODEL PASS |
| BG1 | `CFG_RPI3_CAN_MONITOR` defaults to `n` and `CFG_RPI3_CAN_MONITOR_DEBUG` to `n` | static / review / build | NOT RUN | not in model |
| BG2 | Build error when the monitor is `y` without `CFG_RPI3_CAN_SPI_POC_PTA` | static / review / build | NOT RUN | not in model |
| BG3 | Monitor-disabled build is functionally identical to the baseline (object comparison) | static / review / build | NOT RUN | not in model |
| BG4 | Monitor-enabled build succeeds with `PLATFORM=rpi3` | static / review / build | NOT RUN | not in model |
| BG5 | The host harness compiles the same logic file that OP-TEE builds | static / review / build | NOT RUN | not in model |
| KY1 | Three distinct keys; none is returned by any command, event or debug read | static / review / build | NOT RUN | not in model |
| KY2 | `boot_id` random-read failure makes init fail with `init_ok` 0 | host-side logic test | NOT RUN | not in model |

Status labels used from here on: `DESIGN APPROVED`, `HOST MODEL PASS`, `SOURCE TEST PASS`, `STATIC REVIEW PASS`, `BUILD PASS`, `BOARD PASS`, `NOT RUN`, `BLOCKED`. A check becomes `SOURCE TEST PASS` only when it is run against the actual OP-TEE source file, never because of a model result or a successful compile.

### Exit criteria for the Component 2.2 design

| Criterion | State |
|---|---|
| Sections 4a to 4i and section 5 approved | done |
| Consistency review passes (R-A2 fixed) | done (2026-11-06) |
| All 53 checks inventoried and classified | done |
| Unresolved decisions and hardware dependencies listed | done (below) |
| Implementation | **not started** |
| Host-side, static and on-board checks | **pending (none run)** |

### Unresolved items (not closed by this design)

| Kind | Items |
|---|---|
| Decisions | D1 is decided (Option B); its code change (Core 1 loop) is not yet made; `K`, `IDLE_MAX`, `IDLE_USES_MAX`, `attempt_cap` and the lock budget (Component 4) |
| Unmeasured | lock-hold time (H3); `CNTPCT` on Core 1; F23 (the 3.3 times PTA latency gap); attempts per millisecond (X9); pass duration with monitor logic (Y10) |
| Implementation-dependent | Z7 (Core 1 call graph); monitor-profile refusal of the legacy debug commands (Z8, interface subcomponent); the bounded-acquire and critical-section code |
| Environment | UART adapter, second CAN board, CAN-FD sender, external copy of `SD_Card_Backups/`, deferred N-04 install |

## 4k. PTA interface, commands 17 to 23 (APPROVED 2026-11-07)

Authority: `INTERFACE_DECISION_REGISTER.md`, DEC-BUILD-1, DEC-RATE-1 and DEC-IF-1 to DEC-IF-15 and D1 (all decided). Design only: nothing here is implemented.

### K1. Dispatch, result codes, health pair

**Dispatch (builds with `CFG_RPI3_CAN_MONITOR=y`).** (1) An id outside the known set (0 to 16 and 17 to 23) returns `NOT_SUPPORTED` before any parameter check. (2) A refused legacy id (K9) returns `NOT_SUPPORTED` before any parameter check, plus a best-effort event. (3) Ids 17 to 23 go to the monitor handler: exact `param_types` (`BAD_PARAMETERS`), then `init_ok` (`BAD_STATE`), then the command sequence. (4) Everything else takes the legacy path unchanged. **The build with the monitor disabled is unchanged**, including its existing error behaviour. If monitor init fails only 17 to 23 return `BAD_STATE`; legacy commands (1, 2, 12 are used by `verify_boot`) keep working.

**Result codes.** `TEE_Result` reports API-level outcomes only: `SUCCESS`, `BAD_PARAMETERS` (wrong types, a non-zero reserved field, an out-of-range value, a NULL buffer with a size above 0), `BAD_STATE` (`init_ok` = 0), `SHORT_BUFFER` (the required size is written back), `NOT_SUPPORTED`, `BUSY` (lock budget or `mac_pta` guard). Frame, lease and heartbeat outcomes are **verdicts** in value outputs. All multi-byte fields are little-endian; reserved input fields must be zero.

**Exact `param_types`** (an unused slot must be `TEE_PARAM_TYPE_NONE`):

| Cmd | p0 | p1 | p2 | p3 |
|---|---|---|---|---|
| 17 submit frame | `VALUE_OUTPUT` | `MEMREF_INPUT` | `VALUE_OUTPUT` | `VALUE_OUTPUT` |
| 18 read events | `VALUE_INOUT` | `MEMREF_OUTPUT` | `VALUE_OUTPUT` | `VALUE_OUTPUT` |
| 19 acknowledge | `VALUE_INOUT` | `NONE` | `VALUE_OUTPUT` | `VALUE_OUTPUT` |
| 20 status | `VALUE_INOUT` | `MEMREF_OUTPUT` | `VALUE_OUTPUT` | `VALUE_OUTPUT` |
| 21 idle lease | `VALUE_INOUT` | `NONE` | `VALUE_OUTPUT` | `VALUE_OUTPUT` |
| 22 get nonce | `VALUE_OUTPUT` | `VALUE_OUTPUT` | `VALUE_OUTPUT` | `VALUE_OUTPUT` |
| 23 heartbeat answer | `VALUE_INOUT` | `VALUE_INPUT` | `VALUE_OUTPUT` | `VALUE_OUTPUT` |

**Health pair (p2, p3), identical in all seven commands.** Lock-free or constant data only, so it can be filled on `BUSY`: `p2.a` = Core 1 poll age in ms (`0xFFFFFFFF` = never polled or 49 days or more, saturating, rule O-1); `p2.b` bit 0 = `starved`, bit 1 = `init_ok`, bits 8 to 15 = `skip_count` saturated at 255, other bits zero; `p3.a` = `pta_lock_timeouts`; `p3.b` = `skip_total`. Filled on `SUCCESS` and `BUSY` only, zero for every other result; read after the final unlock or unmask through the approved accessors.

### K2. Command 17, submit frame

`p0.a` verdict, `p0.b` detail; `p1` the message. Message `can_id(4) | counter(4) | payload(4) | tag(32)` = 44 bytes (the consumer builds it from the bus `can_id` and the first 40 bytes of the 48-byte CAN-FD data field; the 8 padding bytes are dropped). Tag = HMAC-SHA256 over the first 12 bytes with `K_frame`, compared in constant time over all 32 bytes. `payload` is opaque.
Order: (1) `BAD_PARAMETERS` for wrong types or a NULL buffer with size above 0, then `BAD_STATE`; (2) size not 44 is MALFORMED detail 1 and the buffer is not read (**no size cap**); (3) copy the 44 bytes once into a local buffer before any lock (R-A8); (4) `can_id` above `0x1FFFFFFF` is MALFORMED detail 2, an id outside the compile-time stream list `{0x123}` is MALFORMED detail 3 (the counter state is one instance per mode, so a second id needs a per-stream state array); (5) HMAC with the guarded `mac_pta` outside the lock (guard busy returns `BUSY`), mismatch is AUTH_FAIL; (6) one bounded hold: evaluate, counter rules, state update, events, unlock (`BUSY` on exhaustion). Steps 1 to 5 change no state, so **`BUSY` happens only before any state change** and a resubmission is safe.
Verdicts: ACCEPTED 0, GAP 1 (detail = counter - last - 1), RESYNC 2, MALFORMED 3, AUTH_FAIL 4, REPLAY 5, DUPLICATE 6, CONFLICT 7. Counter rules in this order, direct unsigned comparisons, no delta variable: no previous frame is RESYNC (any counter, including 0); `counter < last` REPLAY; `counter == last` DUPLICATE if the 44 bytes equal the stored frame, else CONFLICT; `counter == last + 1` ACCEPTED (guarded against wrap: after `0xFFFFFFFF`, counter 0 is REPLAY); `counter > last + 1` GAP. Only ACCEPTED, GAP and RESYNC refresh the traffic deadline and clear a raised timeout; only the last accepted frame is stored. Known limits: state resets on reboot, so a captured valid frame is accepted again as the first frame (RESYNC); a GAP cannot tell bus loss from a drop by Linux; while a caller hammers the PTA real frames can get `BUSY` and Core 1 can raise a traffic timeout although traffic flows (F24).

### K3. Command 18, read events

In `a` = first seq wanted, in `b` = max entries (**1 to 5**, else `BAD_PARAMETERS`); out `a` = count (bits 0 to 7) | `more` (bit 8), out `b` = `ev_lost`. `p1` receives at least `24 x max` bytes (else `SHORT_BUFFER`, size written back), at most 120 bytes. Record `{seq u32, type u16, flags u16, ticks u64, counter u32, detail u32}`, seq order, seq at or above the requested value. One hold: evaluate, **one scan of at most 64 slots** keeping the five smallest qualifying seq, `more` determined in the same hold, copy to a local buffer with the approved inline loop; copy to the caller after unlock. Pinned count is **not** returned here (it is `pinned_used` in cmd 20). A consumer detects loss from a jump in seq numbers.

### K4. Command 19, acknowledge (bookmark only)

In `a` = ack up to seq, in `b` = 0 (else `BAD_PARAMETERS`); out `a` = 0, out `b` = effective `ev_ack`. Clamped to `ev_seq`; below `ev_ack` is a no-op; `ev_ack` never decreases and **never affects overwriting or `ev_lost`**. One hold, evaluation first.

### K5. Command 20, status

In `a` = page (0 or 1), in `b` = 0; out `a` = 128, out `b` = layout version 1; `p1` at least 128 bytes. **Precedence:** wrong types, `b` not 0 or page 2 and above (`BAD_PARAMETERS`); `init_ok` (`BAD_STATE`); buffer under 128 (`SHORT_BUFFER`, size 128); bounded lock (`BUSY`: size 0, no data, health filled). Locked fields come from one hold (clock read inside it) built with the approved inline word loop; lock-free fields are read after unlocking; reserved bytes are zero.
**Page 0** (16 x u64): 0x00 `u32 version`=1, `u32 length`=128; 0x08 `u32 boot_id`, `u32 flags` (bit0 init_ok, bit1 hardened profile, bit2 tr_armed, bit3 tr_timeout_raised, bit4 hb_timeout_raised, bit5 lease_active, bit6 starved); 0x10 `u64 now_ticks`; 0x18 `u64 core1_poll_age_ticks` (lock-free, saturating, own clock read); 0x20 `u64 poll_count` (lock-free); 0x28 `u32 skip_count`, `u32 skip_total` (lock-free); 0x30 `u32 pta_lock_timeouts` (lock-free), `u32 pta_timeouts_seen` (L); 0x38 `u32 last_counter`, `u32 hb_seq`; 0x40 `u64 traffic_age_ticks` (all ones if not armed); 0x48 `u64 hb_last_ok_age_ticks` (all ones if never); 0x50 `u32 hb_miss_count`, `u32 idle_uses`; 0x58 `u64 lease_remaining_ticks`; 0x60 `u32 ev_seq`, `u32 ev_ack`; 0x68 `u32 ev_lost`, `u32 pinned_used`; 0x70 `u8` last frame, heartbeat and cross-check verdicts, `u8 hb_state`, `u8 mode`, three zero bytes; 0x78 `u64` zero. The six lock-free fields use the approved accessors (`rpi3_mon_load_u64`, `atomic_load_u32`); `pta_timeouts_seen` is read under the lock.
**Page 1** (exact counters, approved with DEC-IF-10): 0x00 `u32 version`=1, `u32 length`=128; 0x08 to 0x6B `u32 ev_count[0..24]` (25 event types, K10); 0x6C `u32` frame-verdict total; 0x70 `u32` heartbeat-verdict total; 0x74 `u32` cross-check total (0 in Mode F); 0x78 `u64` zero. All read in the same hold.

### K6. Command 21, idle lease

In `a` = mode (0 EXPECTED, 1 IDLE, else `BAD_PARAMETERS`), in `b` = requested ms (mode 1 with 0, or mode 0 with non-zero, is `BAD_PARAMETERS`); out `a` = verdict, out `b` = remaining ms (saturating). One hold, evaluation first (a just-expired lease is already EXPECTED). IDLE while EXPECTED: GRANTED 0, expiry `now + min(b, IDLE_MAX)`. IDLE while IDLE: RENEWED 1, new expiry `now + min(b, IDLE_MAX)` (replaces, never additive, may shorten). At `IDLE_USES_MAX`: REFUSED_CAP 3, no change. EXPECTED while IDLE: RETURNED_TO_EXPECTED 2, traffic window restarts. EXPECTED while EXPECTED: NO_CHANGE 4, no event. Only granted and renewed requests count toward `idle_uses`; returning is free; a grant never clears a raised timeout; the heartbeat keeps running. Events follow G5-2 (a grant from EXPECTED logs `LEASE_GRANTED` and `MODE_CHANGED`). Exposure from an unauthenticated caller: at most `IDLE_MAX x IDLE_USES_MAX` of blind traffic deadline per boot, a refused legitimate request once the cap is used up, every use visible.

### K7. Command 22, get nonce

Outputs: `p0.a/p0.b` = nonce low and high (zero when not answerable); `p1.a` = `hb_seq`, `p1.b` = `hb_state`. A nonce is returned in **OPEN and MISSED**. Hold 1: evaluate, read the request and the stored nonce; unlock; if no stored nonce, enter the `mac_pta` guard (busy returns `BUSY`), compute `HMAC-SHA256(K_hb, boot_id(4,LE) || hb_seq(4,LE) || issue_ticks(8,LE))`, nonce = first 8 bytes (low word = bytes 0 to 3 LE, high = bytes 4 to 7 LE), leave the guard; hold 2 with the remaining budget stores it only if `hb_seq` is unchanged. If `hb_seq` changed, return the new `hb_seq` and state with a zero nonce and let the caller retry (**no internal retry**). **Only Core 1 issues challenges; the PTA never does.** The nonce is public, so the heartbeat proves only that some PTA-capable process answered a fresh challenge.

### K8. Command 23, heartbeat answer

In `a` = `hb_seq`, in `b` = 0; `p1` = nonce low and high; out `a` = verdict, out `b` = detail. One hold, evaluation first, **compare with the stored nonce, no HMAC**. Order: state NONE or seq above `hb_seq` UNISSUED 3; seq below `hb_seq` STALE 2; same seq and state ANSWERED or LATE_ANSWERED DUPLICATE 5; state OPEN or MISSED with no stored nonce BAD_NONCE 4 detail 1, with a different nonce BAD_NONCE detail 2; equal and OPEN ON_TIME 0 (state ANSWERED, `hb_last_ok = now`, miss count 0, a raised heartbeat timeout cleared with a recovered event); equal and MISSED LATE 1 (state LATE_ANSWERED, miss count unchanged, nothing cleared). Only ON_TIME and LATE change state. A new challenge makes the old nonce invalid (G6-4).

### K9. Legacy commands in a monitor build

Kept in both profiles: 1, 2, 12. **Refused** (`NOT_SUPPORTED` before any parameter check, plus `LEGACY_CMD_REFUSED`) in both profiles: 0, 3, 6, 7, 13, 14. The chip A debug readers 4, 5, 8, 9, 10, 11, 15, 16 are refused in the hardened profile and allowed with a `LEGACY_CMD_USED` event in the debug profile. The profile is chosen at build time only (`CFG_RPI3_CAN_MONITOR_DEBUG ?= n`); there is no runtime switch. The event path takes **one** non-spinning trylock with exceptions masked and drops the event if the lock is busy; a refusal never waits.

### K10. Event types and classes (25)

Evidence, first four always kept: 0 AUTH_FAIL; 1 REPLAY (detail 1 older counter, 2 duplicate, 3 conflict); 2 TIMEOUT (detail 1 traffic, 2 heartbeat, 3 never started); 3 ALTERED, 4 UNACCOUNTED, 5 CHIPB_FAULT (Mode D2 only). Diagnostic: 6 MALFORMED, 7 COUNTER_GAP, 8 COUNTER_RESYNC, 9 TRAFFIC_RECOVERED, 10 HEARTBEAT_RECOVERED, 11 MODE_CHANGED, 12 LEASE_GRANTED, 13 LEASE_RENEWED, 14 LEASE_REFUSED, 15 LOCK_TIMEOUT, 16 MON_STARVED, 17 HB_SEQ_EXHAUSTED, 18 LEGACY_CMD_REFUSED, 19 LEGACY_CMD_USED, 20 BAD_NONCE, 21 STALE_ANSWER, 22 UNISSUED_ANSWER, 23 DUPLICATE_ANSWER, 24 LATE_ANSWER. Verdict to event: GAP, RESYNC, MALFORMED, AUTH_FAIL, REPLAY, DUPLICATE and CONFLICT (as REPLAY details 2 and 3) raise the event of the same name; ACCEPTED raises none. Rules: G8-6 (per-class limit, evidence after its quota).

### K11. Timing constants (provisional, Component 4 measures them)

Traffic timeout T = **300 ms** (the sender period is 100 ms with 50 ms jitter, so the earlier 50 ms would raise false timeouts); challenge period H = **1 s**; per-request deadline D_req = **500 ms** (must be below H so only one request is outstanding; the design report's 3 s was inconsistent with that); boot grace G = 60 s; misses before a timeout k = 3; starvation threshold K = 8; `IDLE_MAX` = 60 s; `IDLE_USES_MAX` = 16; lock budget 19,200 ticks; attempt cap 2,000; Core 1 cadence 256 passes. A build with `CFG_CORE_DEBUG_CHECK_STACKS=n` needs the cap and cadence re-derived (section 8).

### K12. Build gate, structure, ABI

`CFG_RPI3_CAN_MONITOR ?= n` in `plat-rpi3/conf.mk`, a build error if `y` without `CFG_RPI3_CAN_SPI_POC_PTA=y`, plus `CFG_RPI3_CAN_MONITOR_DEBUG ?= n`. The logic is one file pair, `plat-rpi3/rpi3_can_monitor.[ch]`, with the platform primitives behind a small bindings header (OP-TEE macros in the real build, mocks in the host harness) and plain C signatures, so the **same file compiles on the host and in OP-TEE**; `core/pta/rpi3_can_spi_poc.c` holds only dispatch glue mapping `TEE_Param` to those calls; the Core 1 hook stays in `rpi3_can_server.c` and waits for D1. ABI names in `pta_rpi3_can_spi_poc.h`: `PTA_RPI3_CAN_SPI_POC_CMD_MON_SUBMIT_FRAME` 17, `_MON_GET_EVENTS` 18, `_MON_ACK_EVENTS` 19, `_MON_GET_STATUS` 20, `_MON_SET_IDLE` 21, `_MON_GET_NONCE` 22, `_MON_SUBMIT_HB_ANSWER` 23.

### K13. Keys and `boot_id`

`K_frame`, `K_hb`, `K_test`: three distinct random 32-byte prototype constants in one clearly marked header, committed like the existing `can0_hmac_key`; never returned by any command, event or debug read. Anyone with the repository, or root on the Pi (the keys are in the boot image), can read them; the report states this limit. `boot_id` = low 32 bits of `HMAC-SHA256(K_hb, init_ticks64 || prng16)` with `prng16` from the software PRNG (seed quality unverified, so it is a uniqueness tag, not a secret); if the random read fails, init fails and `init_ok` stays 0. The timer restarts each boot, so `boot_id` is what separates the nonces of different boots.

## 5. Access rules (APPROVED 2026-11-06; R-A6 decided by D1 on 2026-11-07)

| # | Rule | Reason (from Component 1) |
|---|---|---|
| R-A1 | Every **read and write** of an `L` member is under `rpi3_mon_lock`. Exceptions: (i) initialisation in `driver_init`, before Linux boots and before Core 1 is diverted; (ii) the lock-free words (G7 and `pta_lock_timeouts`), accessed only through the approved atomic accessors. | no lock exists anywhere today (1.4, C1) |
| R-A2 | **(rewritten 2026-11-06; the normative sequence is section 4f, B1 to B3, and only there)** PTA handlers acquire `rpi3_mon_lock` with the bounded, fail-closed sequence defined in section 4f: a per-command budget of lock-wait time (initial 1 ms, 19,200 ticks) and a per-command attempt cap (provisional), both shared across every acquisition in the command; a timer reading that is frozen or goes backward (`now < t0`) counts as exhausted. On exhaustion the handler restores exceptions, adds 1 to `pta_lock_timeouts` (the one atomic add allowed) and returns `BUSY` with the lock-free health words, **without touching any `L` member**. Release is `cpu_spin_unlock_xrestore`. Core 1 makes one non-spinning `cpu_spin_trylock` per 256 passes (section 4g) and turns an increase in `pta_lock_timeouts` into one rate-limited `LOCK_TIMEOUT` event the next time it holds the lock (section 4d, S2). | the spinlock is unfair, `xsave` spins with every exception masked and no time limit, and a Core 1 that dies holding the lock would hang Linux cores (1.4, C10, C11). The code-level checks of 2026-11-06 are recorded in sections 4e and 4f |
| R-A3 | **(corrected 2026-11-06)** Critical sections follow CS1 to CS5 (section 4h). Copies use the verified inline word or byte loop; a size limit alone does not make a critical section safe. The measured hold time must stay below one tenth of the PTA lock budget. | a PTA caller spinning with exceptions masked cannot take any interrupt on its core |
| R-A4 | The clock is read **inside** the lock; one helper `expired(now, t0, limit)` (strict `>`, with `now >= t0`) is used everywhere. Lock-free readers of G7 use rule O-1 (saturating age). | exactly-once events and a single boundary (R21, O-18) |
| R-A5 | HMAC, UART and caller-buffer copies happen **outside** the lock. No allocation and no printing while holding the lock. | R21 |
| R-A6 | **FIQ rule (D1 decided 2026-11-07: Option B).** In Monitor builds FIQ is masked for the whole Core 1 loop and is never unmasked there; nothing more is needed around the lock. The FIQ handler never takes the lock. `cpu_spin_trylock` asserts IRQ masked; this holds because Core 1 never unmasks IRQ. | O-01 and O-06 (1.4) |
| R-A7 | G7 members follow the approved G7 v3 specification (section 4a): single writer, atomic accessors, 64-byte aligned and padded. | Core 1 writes them about 410,000 times a second; sharing a line with lock-protected data would make the PTA path and Core 1 slow each other |
| R-A8 | Caller buffers are copied once, with byte reads, into a local buffer, and only the copy is validated. Exact-size and NULL checks come before any copy; copy-out to the caller happens after unlocking. Core 1's tmp stack is not used for any such buffer (the PTA thread stack is). | the PTA receives a pointer into live Normal World memory (1.3); Core 1's stack is shared with the FIQ frame (1.4) |
| R-A9 | Frames are validated against the **44-byte canonical form** only. The HMAC is computed and compared (constant time, all 32 bytes) **before** any counter rule; a bad tag never moves counter state. | R2 |
| R-A10 | Cmd 20 takes the lock (R-A2), copies a fixed projection of at most 128 bytes into a local buffer on the PTA thread stack, releases, then copies out. The lock-free words are read with the atomic accessors under O-1. The result is never the struct. | below |

**Items that must stay explicit (from review).** (1) The 1 ms budget is a starting value, not proof that the critical section is short; the real hold time is measured later. (2) Returning the lock-free health words together with `BUSY` must be checked against the actual OP-TEE client API output semantics (whether output parameters reach Normal World when the PTA returns an error) before it is relied on. (3) The atomic add on `pta_lock_timeouts` needs a verified implementation (a 32-bit relaxed `__atomic_fetch_add`, checked in the compiled code under `-mno-outline-atomics`).

## 6. What may leave the Secure World

| Item | May be returned to a caller? |
|---|---|
| `K_frame`, `K_hb`, `K_test`, `mac_pta`, `mac_core1` | **never**, in any form |
| raw bytes of the state struct | **never** (no dump command, no debug "read memory") |
| `hb_nonce` | **yes, by design**, through cmd 22 (it is not a secret) |
| event records, counters, verdict summaries, `hb_state`, `hb_seq`, ages, health | yes, as a fixed 128-byte projection (cmd 20) and 24-byte records (cmd 18) |
| `last_frame44` | no (it contains authentication tags of frames already seen; not needed by a caller) |
| `boot_id`, `hb_seq` | **yes, by design** (identifiers, not secrets; Linux needs them to answer a heartbeat) *(approved 2026-11-06)* |
| computed or expected authentication tags (frame tags, HMAC results, `hb_nonce` derivation inputs other than the ones above) | **never** in an event record, a counter, a status field or an error reply *(approved 2026-11-06)* |
| keys and MAC contexts (`K_frame`, `K_hb`, `K_test`, `mac_pta`, `mac_core1`) | **never** readable through any PTA command or debug "read memory" interface, in the release and in every profile *(approved 2026-11-06)* |

## 7. Mapping of the six items first proposed

| Proposed | Becomes | Why it changed |
|---|---|---|
| `last_counter` | `ctr_f.have_last`, `ctr_f.last_counter`, `ctr_f.last_frame44` | first frame, duplicate and conflict need more than one number |
| `last_nonce` | `boot_id`, `hb_seq`, `hb_issue_ticks`, `hb_nonce`, `hb_nonce_valid` | the nonce is derived from the others, stored once derived, and compared with the stored value to check an answer (DEC-IF-7) |
| `nonce_timestamp` | `hb_issue_ticks` | same value |
| `last_response_time` | `hb_last_ok_ticks` (plus `hb_miss_count`, `hb_timeout_raised`) | a late answer must not reset the miss count |
| `monitor_status` | **no single field**: `hb_state`, the three verdicts, `mode`, health members, counters | the plan requires three independent verdicts, never merged (R4b) |
| `mon_lock` | `rpi3_mon_lock` (the `snap_seq` seqlock counter was removed in G9 v3) | as proposed |

## 8. Dependencies and risks that touch this design

| Item | Effect on the state design |
|---|---|
| **D1** (FIQ policy) | decided 2026-11-07: Option B (FIQ masked for the whole Core 1 loop in Monitor builds); the `rpi3_can_server.c` change is made in the Core 1 integration stage |
| **N-04** (deferred) | limit L-N04 above; the report must say state integrity against Linux's allocator is not guaranteed until it is installed |
| `CNTPCT` on Core 1 untested | every `*_ticks` member depends on it (Component 4 prerequisite) |
| Core 1 cache and MMU state unknown (1.4, F9) | the lock and the shared lines assume normal inner-shareable cacheable memory; to be checked before relying on the spinlock from Core 1 (S2a) |
| Each instrumented call costs about a whole loop pass (1.4, F8) | Core 1 touches the state through as few calls as possible, or through `__nostackcheck` helpers |
| Stack-check hook panics on a stack-limit violation at function entry | the monitor adds no large stack buffer on Core 1 |
| **F27**: shared OP-TEE heap lock (unbounded spin) used by HMAC | not covered by the bounded monitor-lock acquisition; accepted for Mode F; Component 7 hard requirement (Core 1 heap-free in the Monitor profile) |
| **Build configuration (DEC-BUILD-1, approved 2026-11-07)**: `CFG_CORE_DEBUG_CHECK_STACKS=y` | The shipped Sep-20 image was built with this flag, which no makefile sets (default `n`); it is passed explicitly by `Part_E_Verification/scripts/build_optee_scratch.sh`. **Every cost and panic-site figure in this document (F8 about 2.4 us per instrumented call, the lock cycle, the 2.435 us Core 1 pass, the 256-pass cadence, the attempt cap, CS3 and the hook-free rule) assumes it.** A build with the flag `n` needs those parameters re-derived (for example the attempt cap of 2,000 would be reached in roughly 100 us, long before the 1 ms budget). Evidence: `IMPLEMENTATION_BASELINE.md` |

## 9. What this document does not decide

The final values of the provisional constants in section 4k, K11 (Component 4 measurements), whether an error reply carries output parameters on the board (V14), the atomic-add implementation for `pta_lock_timeouts`. Also: the exact size of each array (derived in Component 4 from measurements), the final layout of Mode D2 state (Component 7), (the byte layout of cmd 20's projection and the event type list are now defined in section 4k).

## 10. Approval checklist

- [x] Section 3.1: one struct, `rpi3_mon` (approved)
- [x] Section 4: G1, G3, G4 accepted; G2 accepted with its conditions; **G5, G6, G8: section 4c APPROVED (2026-11-06)**; **G7: section 4a APPROVED**; G9: see section 4b (G10 deferred to Component 7)
- [x] Section 4b: G9 v3 **APPROVED (2026-11-06)** (lock only, no seqlock in Component 2; Core 1 attempts the lock once per 256 passes; cmd 18 returns at most 5 records per call (amended 2026-11-06); PTA bounded acquire, 1 ms initial budget; lock cost corrected to about four instrumented calls)
- [x] Section 5: access rules R-A1 to R-A10 **APPROVED (2026-11-06)**; R-A6 decided by D1 = Option B (2026-11-07)
- [x] Section 4d: shared-state ownership and access (2.2.1), S1 to S6 and V1 to V14 **APPROVED (2026-11-06)**; verification items V1 to V14 are NOT yet run
- [x] Section 4e: lock initialisation and ownership (2.2.2), I1 to I6 and W1 to W7 **APPROVED (2026-11-06)**; checks W1 to W7 NOT yet run
- [x] Section 4f: PTA bounded lock acquisition (2.2.3), B1 to B9 and X1 to X9 **APPROVED (2026-11-06)**; checks X1 to X9 NOT yet run
- [x] Section 4g: Core 1 lock acquisition (2.2.4), C1 to C10 and Y1 to Y10 **APPROVED (2026-11-06)**; checks Y1 to Y10 NOT yet run; D1 decided 2026-11-07 (Option B)
- [x] Section 4h: critical-section rules (2.2.5), CS1 to CS5 and H1 to H5 **APPROVED (2026-11-06)**; checks H1 to H5 NOT yet run
- [x] Section 4i: shared-state integration matrix (2.2.6), Z1 to Z8 **APPROVED (2026-11-06)**; checks Z1 to Z8 NOT yet run; F27 recorded (Mode F accepted, Component 7 hard requirement)
- [x] Section 4j: synchronization acceptance and review (2.2.7) **APPROVED (2026-11-06)**: the synchronization DESIGN is closed; all 53 checks are inventoried and NONE has been run or passed; implementation not started
- [x] Amendments applied 2026-11-06 (document only): cmd 18 five records and 120 B (decisions table, L6, G8-5, matrix, checklist), G8 `pinned_used`, G3 44-byte frame, 18 interface checks added as `NOT RUN`. No source changed
- [x] Decisions recorded 2026-11-07 (document only): DEC-BUILD-1 (stack checks ON, section 8) and DEC-RATE-1 (per-class one event per second, rule G8-6, check Q5). No source changed
- [x] Section 4k: PTA interface, commands 17 to 23, the 25 event types, legacy policy, timing constants, build gate and keys **APPROVED (2026-11-07)**; 67 interface checks added as `NOT RUN`; D1 decided (Option B)
- [x] Section 6: the list of what may leave Secure World **APPROVED (2026-11-06)**, with the three additions
- [x] N-04 deferral and limits L-N04, L-TZ **APPROVED (2026-11-06)** as report wording
- [x] Lock-cost correction applied 2026-11-07 (document only, section 4b.1; annotations on the G9 evidence row, L4, C9 and F26): the 'about four instrumented calls' is verified for the OP-TEE primitives and for a Core 1 attempt, but a PTA lock cycle is 7 entries (9 with wrappers); 'three of the four inside the hold' corrected to 2 of 4 (Core 1) and 4 of 7 (PTA); microsecond figures stay unmeasured inferences; hold-time maximum and p99 PENDING hardware measurement. Backup: `MONITOR_STATE_DESIGN.md.bak12`. No source changed, no decision changed
- [x] Measured Core 1 loop recorded 2026-11-08 (document only, section 4b.2): 75.2 ns per pass, lock attempt about every 19 us (design intent about 0.62 ms); cadence decision open; hold time still pending. Backup: `MONITOR_STATE_DESIGN.md.bak13`. No source changed, no decision changed

### 4b.3 Cadence changed from 256 to 16384 (2026-11-09; decision by the user, Option A)
Board data (test-statistics image, 2026-11-07, `baseline/teststats_cadence_sweep_20261107.txt`): at 256 passes per attempt the Core 1 attempt interval is 21.5 us and 0.44 of Core 1's attempts collide with a PTA hold per status call; at 8192 it is 313 us and 0.04; at 65536 it is 2.4 ms and 0.004. The measured longest PTA hold is 7.24 us (target "<100 us"), Core 1's own hold 0.21-0.31 us. `RPI3_MON_CORE1_CADENCE` is now 16384, giving an attempt interval of about 0.6 ms (the intent of 4b.1); expected collisions about 0.02 per call. Consequences: the starvation bound K=8 consecutive skips now means about 5 ms of continuous PTA holding, which never happens at 6-7 us per hold, so starvation is a pure fault indicator; Core 1 reacts to a heartbeat deadline or frame within about 0.6 ms, negligible against D_req = 500 ms and the 100 ms frame period. Host tests were updated to the 16384 arithmetic (not weakened): Y1/Y9, held-lock, counter-wrap (now 20000 passes from 0xFFFFC000, 2 attempts) and TS6; the cadence mutant is now 8192. Not yet measured on the board with the new constant until the hardened-c16384 image is flashed.

### 4b.4 Correction of the Core 1 pass time (2026-11-09, from board data)
4b.2 gave 75.2 ns per pass. That figure was measured at cadence 256 and includes the lock attempt: base pass 26.6 ns (hardened) plus one 12.6 us attempt per 256 passes (26.6 + 12600/256 = 75.8 ns). At cadence 16384 the board shows 36.462 M passes/s = 27.4 ns per pass and an attempt interval of about 448 us (not 0.62 ms). See `COMPONENT_4_TIMING_AND_MEASUREMENT.md` C4.1.
