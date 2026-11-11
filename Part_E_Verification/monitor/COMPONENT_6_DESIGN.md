# COMPONENT 6 design: IDLE_EXPIRED, the F27 correction, and the UART ALIVE line (2026-11-12, DESIGN ONLY: nothing built, nothing flashed)

## 1. F27 correction (my earlier wording was wrong)
I wrote in the F24 analysis and the C5/C6 records that `TIMEOUT` / `TRAFFIC_RECOVERED` events "are not rate-limited". **They are.** `rpi3_mon_ev_append` (rpi3_can_monitor.c) pins the first four events of each of the six evidence classes (never limited) and limits **every other event to one per second per class**; the exact counter `ev_count[type]` always increments; overwriting an unread ordinary entry is counted in `ev_lost`.
What the F24 flood really showed: AUTH_FAIL (1000 frames) wrote only 15 ring entries (985 suppressed, correct). The flood made the traffic alarm flap about once per second, so `TIMEOUT`, `TRAFFIC_RECOVERED`, `LATE_ANSWER`... each wrote up to 1 entry per second, about 2-4 entries per second in total into a **40-slot ordinary ring that nobody acknowledged**, so after roughly 15 s the ring wrapped and `ev_lost` rose (16 at N = 8, 149 at N = 24). That is the designed overflow signal, not a missing limiter. The pinned slots held (the first evidence survived, T22).
Conclusion: **no code change is needed for F27.** Remedies if wanted later: the consumer acknowledges events (`cmd 19`) regularly, or a hysteresis rule for flapping classes (not proposed here). The statement "F27 open" is withdrawn; F24's real effect stays: delayed frames/answers produce false traffic alarms and late-answer events that wrap the unread ring.

## 2. IDLE_EXPIRED without a new event type
Today the lease expiry in `rpi3_mon_evaluate` appends `MODE_CHANGED` with `detail = 0`, the same value as an explicit "return to normal" (`cmd 21` off, `RETURNED`): indistinguishable. Adding a new event type would change the ABI (25 types, `ev_count[25]` in status page 1, client name table).
**Proposal:** keep the type, give the expiry its own detail: `MODE_CHANGED detail 0 = returned by the consumer, 1 = lease granted, 2 = lease expired`. One-line change in `rpi3_mon_evaluate` (`..., 0, 2, now`), client name/printing update, host test, board test T21 updated to expect detail 2. No ABI size change, no new state.

## 3. UART ALIVE line (plan O-12), behind a build flag
**Why own code:** OP-TEE's console `putc` (drivers/serial8250_uart.c) waits for the whole transmitter to be empty (`LSR_TEMT | LSR_THRE`) before **every** character; one 40-character line costs about 3.5 ms at 115200. Core 1 must never block that long (its attempt interval is 448 us).
**Mechanism (Core 1 only, no lock, no printf):**
- Registers (mini UART, 16550 layout, 4-byte stride, base `CONSOLE_UART_BASE` 0x3f215040): LSR at +0x14, THR at +0x00. The mapping is the one `main.c` already registers (`MEM_AREA_IO_NSEC`); resolve the virtual address **once at init**, never in the Core 1 loop. If the mapping is missing the feature switches itself off (no crash).
- A static 128-byte ring owned by Core 1. Every Core 1 attempt (about every 448 us) it writes **at most 8 bytes**, and only while `LSR & LSR_THRE`; if the transmitter is busy it does nothing and tries next time. The work per call is bounded; it never spins.
- Once per second (checked with the existing timer read) Core 1 takes the lock **once** the way it already does for its attempt, copies a handful of 32-bit counters into locals, releases, then formats outside the lock with a small hand-written integer-to-text routine (no `snprintf`, no calls outside the allowed Core 1 list). If the ring is not empty the line is skipped and counted as `dropped`.
- Line: `ALIVE seq=N poll_age_us=A ev=E lost=L hb=<state>/<miss> tr=<armed|timeout|lease>` plus up to three new events as `EV seq type detail` (oldest unseen first, at most 3 per second; the rest only show as a larger `ev=`).
- Linux owns the same mini UART for its console, and TF-A/OP-TEE use it at boot: lines can interleave with Linux output. Accepted and documented.
- Flag: `CFG_RPI3_CAN_MONITOR_UART ?= n` (needs `CFG_RPI3_CAN_MONITOR=y`). **Off by default**, so the production image is unchanged until it is turned on deliberately.
**Risk and why a flag:** a fault on Core 1 (bad mapping, device not clocked: the AUX block can be disabled by Linux) may panic Secure World, and with no UART attached I could not see it. So: first image with the flag **off** (IDLE_EXPIRED only), proven on the board; second image with the flag **on** only when the adapter is wired and I can watch the boot log.
**Checks before any flash:** host harness (formatter, ring, drain with a mocked LSR including "never ready": bounded work per call, no blocking), static checks (Core 1 call graph: the new functions are `__nostackcheck`/inline and call nothing outside the allowed list; static stack estimate; no store to the UART unless THRE read first), scratch builds flag off/on with 0 warnings, off-build identical to today's except line numbers. Board: T23 (UART stalled or garbled: Core 1 keeps polling, drop counter rises), `ALIVE` seen once per second, boot log unchanged, `verify_boot` 16/0.

## 4. Rollout and rollback
Build in a scratch directory with `PLATFORM=rpi3`; new image written only to the 8 GB spare (your passwordless dd/cmp rule; the 32 GB original is never written); rollback = reflash the proven image `rpi3-sdcard-monitor-hardened-c16384.img` (hash in `SD_Card_Backups/SHA256SUMS`). Each step committed.

## 5. Decisions needed
1. Approve **IDLE_EXPIRED as `MODE_CHANGED detail 2`** (section 2).
2. Accept the **F27 correction: no code change** (section 1).
3. Approve the **UART design with the flag default off** (section 3) and the two-image rollout (section 4).
