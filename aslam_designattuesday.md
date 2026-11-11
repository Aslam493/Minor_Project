# Secure CAN Monitoring — Final Design (v4)

**Status:** design only. Nothing here is implemented. Values marked *(tunable)* are proposals to be fixed by measurement. Items marked **(unverified)** have not been checked in the code. Items marked **(decision)** need the project owner's approval before implementation.
v4 applies eight corrections found by re-checking v3 against the source: counter policy is new (not existing), the monitored CAN ID, the old freshness check's real meaning, two separate validation paths, nonce derivation, the TA's different message format, loop-timing measurement, and PTA concurrency. v3 closed the gaps found when v2 was compared with the Design A+ specification. v4 supersedes v3, v2 and `Secure_CAN_Detection_Only_Design.md` (v1), which assumed independent Core 1 capture. Code inspection showed that is probably NO-GO (section 11).

## 1. Goal and principle

Detect and log, not prevent. Linux keeps receiving and using every frame. Secure World verifies what Linux forwards, checks traffic and consumer liveness on its own clock, and records evidence in a bounded event log.

Build in stages: **A** (Linux-forwarded verification) → **A+** (deadline and heartbeat monitors) → **B** (independent capture), where B proceeds only if a feasibility experiment returns GO.

Hard constraints: Step 2 and the verified Core 1 startup are untouched. No TF-A / BL31 changes. The existing Core 1 heartbeat/FIQ path is untouched, and the FIQ handler stays capture-only and never takes the new lock. Existing boot and `S12can` behaviour is unchanged.

## 2. Architecture

```
 Sender ECU / attacker
   frame = ID | counter | payload | HMAC-SHA256 tag
        │
        ▼
 MCP2518FD ──► Linux mcp251xfd driver (Core 0, unchanged)
                     │
                     ▼
        Linux CONSUMER daemon (new)
          • forwards each frame to Secure World
          • answers heartbeat challenges
                     │  existing OP-TEE client path (TEEC)
                     ▼
 ┌─────────────────────────────────────────────────────────┐
 │ OP-TEE core, S-EL1 (monitor state, one spinlock)        │
 │  0 FORMAT   length / field checks                       │
 │  1 VERIFY   HMAC-SHA256 over the canonical message      │
 │  2 COUNTER  accept / replay / gap / old rules           │
 │  3 LOG      bounded event log in Secure RAM             │
 └───────────────────────────────┬─────────────────────────┘
                                 │ shared monitor state (locked)
 ┌───────────────────────────────▼─────────────────────────┐
 │ CORE 1 existing loop + deadline monitor (new)           │
 │  every iteration, from the secure 64-bit counter:       │
 │  • traffic deadline        → TIMEOUT / RECOVERED        │
 │  • consumer heartbeat      → TIMEOUT / RECOVERED        │
 │  • existing Core 1 heartbeat counter (loop stall check) │
 └─────────────────────────────────────────────────────────┘
        PTA read: GET_EVENTS / GET_MONITOR_STATE / SET_TRAFFIC_MODE
```

**Where validation runs (decision).** Recommended: the consumer calls the existing pseudo-TA (`rpi3_can_spi_poc`) through TEEC, the pattern `spi_poc_test` already uses. HMAC runs in OP-TEE core using the `crypto_mac_*` code already ported in `rpi3_can_server.c`. Reason: the user-mode TA instance is destroyed when its last session closes (`TA_FLAGS 0`), so counter state would be lost on every consumer restart, and a TA cannot share state with Core 1's loop. This deviates from "the TA validates". The alternative is a keep-alive TA that forwards to the PTA, which adds a hop. Whether a user TA may call that PTA is **(unverified)**; the recommended route does not need it. The legacy TA commands stay as they are.

**Where the log comes from (decision).** The repository has no event log, only trace prints. A minimal log is built from scratch (section 9).

**Two validation paths stay separate.** The existing Core 1 path reads the chip itself (in loopback), validates in `secure_can_validate_frame()` and keeps its own `secure_last_counter`. It is left exactly as it is and is not part of this design's event stream. The new consumer path has its own counter state, its own monitor state and its own log. The two never share state, so the same traffic is not counted or logged twice. Both are described by different stages in section 3: the existing path sees the chip directly, the new path sees only what Linux forwards.

## 3. The five stages kept distinct

The monitor only ever observes stages 3 to 5. It does not observe the physical bus.

| # | Stage | Seen by Secure World? |
|---|---|---|
| 1 | Physical CAN frame arrives at the controller | No |
| 2 | Frame becomes available to Linux (driver) | No |
| 3 | Linux consumer forwards a message | Yes, only if it forwards |
| 4 | Message passes format and authentication checks | Yes |
| 5 | Message is accepted (counter rules satisfied) | Yes |

A frame dropped between stages 1 and 3 is invisible directly. It can only show up indirectly as a counter gap or a traffic timeout.

## 4. Canonical message encoding and validation

All integers are little-endian. Fixed-length fields only.

| Field | Size | Notes |
|---|---|---|
| can_id | 4 | |
| counter | 4 | per-stream, increases by 1 |
| payload | 4 | matches Core 1's CAN-FD layout |
| tag | 32 | full HMAC-SHA256 over the first 12 bytes |

HMAC input = `can_id(4) ‖ counter(4) ‖ payload(4)`, the same input Core 1's existing code uses. Total message = 44 bytes.

Check order, each failure a distinct outcome:

1. **Format.** Length must be exactly 44 bytes and `can_id <= 0x1FFFFFFF`. Failure → `MALFORMED_MESSAGE`; no counter or deadline state changes.
2. **Stream.** `can_id` must be in the monitored-stream list. The list is a compile-time constant containing only `0x123`, the ID the existing Core 1 path already accepts (`SECURE_ALLOWED_CAN_ID`). An unlisted ID → `MALFORMED_MESSAGE` with reason "unlisted id"; no state changes. The TA's demo class `<= 0x102` is a different demo and is not used here.
3. **Authentication.** Failure → `INVALID_AUTHENTICATION`; no counter or deadline state changes.
4. **Counter rules** (section 5).

There is deliberately **no timestamp freshness check**. The existing check (`can0_check_timestamp_fresh`) compares the chip's own receive timestamp with the chip's live clock, so it measures only the delay between capture and validation. It cannot catch an old frame replayed later, and forwarded messages carry no chip timestamp. Replay protection in this design rests on the counter alone.

The existing TA commands use a different format: `{can_id, data[8]}` with an 8-byte tag and no counter. This design follows the Core 1 layout instead (above), so the TA's current commands are left as they are and are not reused for the consumer path.

Key handling: one prototype key defined once in a shared header. Per-purpose subkeys derived with `HMAC(key, "label")`. The key is never returned by any PTA command and never logged. It is compiled in, not provisioned, and is stated as a limitation.

## 5. Counter rules

`last` is the highest accepted counter for the stream. **This is a new policy for the consumer path, not an existing one.** The existing Core 1 path accepts any counter greater than the last and silently accepts gaps. It starts at `secure_last_counter = 0`, so it rejects counter 0 as a replay. The consumer path instead raises `COUNTER_GAP` and `COUNTER_RESYNC`, and accepts any counter value, including 0, as the first baseline.

| Received | Event | Action |
|---|---|---|
| counter == last+1 | none | accept, `last = counter`, refresh traffic deadline |
| counter == last | `REPLAY_DETECTED` (duplicate) | reject |
| counter < last | `REPLAY_DETECTED` (old / out-of-order) | reject |
| counter > last+1 | `COUNTER_GAP` (detail = skipped count) | accept, `last = counter`, refresh deadline |
| first frame after boot | `COUNTER_RESYNC` | accept as baseline |

HMAC alone does not prevent replay. The counter rules do, within one boot session, and they are the only replay protection (see section 4: no freshness check). State resets on reboot, so a captured frame can be accepted again after a restart. The 32-bit counter wrapping is out of scope (about 13.6 years at 10 frames per second) and documented. A gap cannot distinguish loss on the bus from a drop by Linux.

## 6. Monitoring state

All times are ticks of the secure 64-bit counter at 19.2 MHz, so 1 ms = 19,200 ticks. Comparisons use unsigned subtraction (`now - then`), and a 64-bit counter will not wrap in practice.

| Field | Type | Init | Reset |
|---|---|---|---|
| traffic_state | DISARMED / OK / TIMEOUT | DISARMED | reboot |
| last_valid_ticks | u64 | 0 | reboot |
| period_ticks, jitter_ticks, timeout_ticks | u64 | constants *(tunable)* | reboot |
| traffic_mode | EXPECTED / IDLE_ALLOWED | EXPECTED | reboot |
| hb_state | DISARMED / OK / TIMEOUT | DISARMED | reboot |
| hb_nonce, hb_issued_ticks | u64 | 0 | reboot |
| hb_last_ok_ticks | u64 | monitor start | reboot |
| last_counter, counter_valid | u32, bool | 0, false | reboot |
| timeout_reported, hb_reported | bool (dedup) | false | cleared on recovery |
| log ring, seq | see section 8 | empty, 0 | reboot |

Nothing survives a reboot. All state is volatile Secure RAM.

## 7. Monitors (A+)

**Timer source.** The secure 64-bit counter, read with `barrier_read_counter_timer()` (`core/arch/arm/include/arm.h:144`, also used by `tee_time_get_sys_time`). TF-A fixes the frequency at 19.2 MHz. `CFG_SECONDARY_INIT_CNTFRQ` is not set for this platform, so the monitor uses the 19.2 MHz constant and never reads the frequency register on Core 1. No existing secure timer exists (the interrupt-based attempt was reverted, Incident 1).

**Context.** Polled in Core 1's existing loop (`rpi3_can_server_loop()`), not an interrupt. It runs whether or not packets arrive and needs no EL3 change. The loop also calls the frame handler. All 5 loops in `rpi3_can_fiq.c` are fixed-bound `for` loops with no `while` loops. The SPI waits allow up to `SPI_POLL_TIMEOUT_LOOPS` = 100000 iterations each, and the crypto routines the handler calls are not covered by that check. The worst-case time of one loop iteration is **not measured**, so the timeout margins below are provisional until that measurement is done (test 16). If the handler hung, both the deadline check and the existing Core 1 heartbeat counter would stop, which is visible.

### 7.1 Traffic deadline

- **Monitored stream:** frames with `can_id` `0x123`, the single configured ID in the stream list (section 4), matching what the existing Core 1 path accepts. Extending the list is a one-line change.
- **Parameters** *(tunable)*: demo period 100 ms (1,920,000 ticks), jitter ±50 ms, scheduling margin; initial timeout 300 ms.
- **Arming:** DISARMED at boot. The first accepted valid frame arms it (state OK). There is no timeout before the first valid frame.
- **Rule:** if state is OK and `now - last_valid_ticks > timeout_ticks`, emit `CAN_TRAFFIC_TIMEOUT` once (state TIMEOUT). The next accepted valid frame emits `CAN_TRAFFIC_RECOVERED` and returns to OK.
- **Legitimate idle periods.** A new PTA command `SET_TRAFFIC_MODE` switches between EXPECTED and IDLE_ALLOWED. In IDLE_ALLOWED the traffic deadline is DISARMED. Switching back to EXPECTED re-arms with a fresh window measured from that moment, never from the stale `last_valid_ticks`, so no false timeout follows. Linux can call this command, so a compromised Linux could switch monitoring off. Every mode change is logged as `MONITOR_MODE_CHANGED` (proposed extra event), so disabling the monitor is itself recorded.
- Invalid or malformed messages never refresh the deadline, so "no valid traffic" is distinct from "traffic that failed checks".

```
            first valid frame
 DISARMED ───────────────────► OK ──deadline passed──► TIMEOUT (one event)
     ▲                         ▲                          │
     │ IDLE_ALLOWED            └───── valid frame ────────┘ (RECOVERED)
     └─────────────────────────┘
```

### 7.2 Consumer heartbeat

- **What exists:** the Core 1 heartbeat counter (`rpi3_can_server_heartbeat`, read by `GET_CORE1_HEARTBEAT`). It is incremented by Core 1's own loop and proves only that Core 1's loop is running. It says nothing about the consumer, and it is reused only to show that the monitor loop itself is alive.
- **New:** Core 1 issues a fresh nonce every H, derived as `HMAC(heartbeat key, hb_seq ‖ ticks)` truncated to 64 bits. It is deliberately not taken from `crypto_rng_read`: the build has `CFG_WITH_SOFTWARE_PRNG=y`, a software generator whose seed quality is unverified, and Linux must not be able to predict the next nonce without the secret key. It is issued every H *(tunable, initial 1 s)*. The consumer fetches it and returns it through a new PTA command within D *(tunable, initial 3 s)*.
- **Arming:** DISARMED until the first correct response. To avoid a blind spot if the consumer never starts, a boot grace period G *(tunable, initial 60 s)* applies: if no response arrives within G of monitor start, emit `CONSUMER_HEARTBEAT_TIMEOUT` with reason "never started".
- **Rule:** a missed deadline emits `CONSUMER_HEARTBEAT_TIMEOUT` once. The next correct response emits `CONSUMER_HEARTBEAT_RECOVERED`.
- **Proves:** a process able to call the PTA answered a fresh challenge recently. A replayed old answer is rejected.
- **Does not prove:** that the Linux kernel is healthy, that the consumer forwards every frame, or that frames were not dropped. A compromised Linux can answer the heartbeat and still drop frames.
- Heartbeat failure is a separate event from a traffic timeout, so the two causes stay distinguishable.

```
 DISARMED ──correct nonce──► OK ──no response by D──► TIMEOUT (one event)
                              ▲                            │
                              └──── correct nonce ─────────┘ (RECOVERED)
```

## 8. Synchronisation between execution contexts

The consumer's PTA calls run in OP-TEE core on whichever core Linux called from (Core 0). The monitor loop runs on Core 1. Both touch the same state, and neither the PTA nor the FIQ file has any lock today.

- One spinlock protects the monitor state and the log append. The primitives exist in `core/include/kernel/spinlock.h`: `cpu_spin_lock_xsave` / `cpu_spin_unlock_xrestore` (which also mask interrupts) for the consumer path, and `cpu_spin_trylock` for Core 1's bounded attempt.
- The PTA is not marked `TA_FLAG_CONCURRENT` (flags are single-instance, multi-session, keep-alive). Simultaneous calls from the consumer, such as frame forwarding and a heartbeat fetch, may be serialised or return busy; which one happens is **unverified**. The consumer is therefore a single sequential loop, and a busy result is retried and not logged as `COMM_ERROR`.
- Take the lock with native interrupts masked on the acquiring core.
- The FIQ handler never takes it. It stays capture-only, which avoids a self-deadlock if the FIQ lands while Core 1's loop holds the lock.
- Core 1 acquires with a bounded attempt. If the lock is busy, it skips that iteration and counts the skip, so a stuck holder cannot wedge the loop.
- HMAC is computed before the lock is taken. Only the counter update, state change and log append happen under it, so the critical section stays short and bounded.

## 9. Event log

Stage 1 (baseline, required): a plain bounded ring with sequence numbers.

| Field | Size |
|---|---|
| sequence number | 4 |
| event id | 1 |
| counter (or 0) | 4 |
| timestamp (ticks) | 8 |
| detail / reason code | 4 |

- **Events:** `INVALID_AUTHENTICATION`, `MALFORMED_MESSAGE`, `REPLAY_DETECTED`, `COUNTER_GAP`, `COUNTER_RESYNC`, `CAN_TRAFFIC_TIMEOUT`, `CAN_TRAFFIC_RECOVERED`, `CONSUMER_HEARTBEAT_TIMEOUT`, `CONSUMER_HEARTBEAT_RECOVERED`, `MONITOR_MODE_CHANGED`, `COMM_ERROR` (SPI or TEEC failure, kept separate from missing frames). Only events backed by implemented validation logic are emitted.
- **Full ring:** the oldest entry is overwritten and an overflow marker records how many were lost.
- **Flood protection:** repeated events of the same type are rate-limited and folded into one entry with a suppressed count, so an attacker spamming bad frames cannot push earlier evidence out of the ring.
- **Non-blocking:** append never waits on anything; it is a bounded copy under the lock.
- **Persistence:** none. The log is volatile Secure RAM and is lost on reboot. It is not tamper-proof persistent storage.

Stage 2 (optional, only after stage 1 is tested): HMAC hash chain over entries and a signed `(length, tip)` checkpoint exported over the PTA, so an outside machine can detect later truncation. Not a prerequisite for the monitors.

## 10. Detection matrix

| Situation | Event | Limit |
|---|---|---|
| Wrong length or bad field | `MALFORMED_MESSAGE` | |
| Forged / edited frame | `INVALID_AUTHENTICATION` | |
| Duplicate / old counter | `REPLAY_DETECTED` | session-only |
| Skipped counters | `COUNTER_GAP` | bus loss and Linux drop look the same |
| Traffic stops | `CAN_TRAFFIC_TIMEOUT` | not proof of an attack |
| Consumer stops answering | `CONSUMER_HEARTBEAT_TIMEOUT` | does not show kernel failure |
| Monitoring switched to idle mode | `MONITOR_MODE_CHANGED` | recorded, not prevented |
| Linux answers the heartbeat but drops frames | only `COUNTER_GAP` or timeout, indirectly | |
| Linux drops frames the TA never sees, with no counter gap | **not detected** | Design A limitation |
| Reboot | counter, state and log reset | volatile state |

## 11. Design B (independent capture): gated experiment, not part of the baseline

Preliminary evidence from the source suggests NO-GO as currently coded:
- Linux's driver configures the chip's FIFOs, filters and mode and claims the shared GPIO25 interrupt.
- Core 1's `can0_secure_init()` overwrites `FIFOCON(0)`, `FIFOCON(1)`, `FLTOBJ0`, `FLTMASK0`, `FLTCON0` and switches the chip into internal loopback. It reads FIFO 1 and issues `UINC`, which removes entries Linux expects.
- Whether a frame can be delivered to two FIFOs is **unverified** (datasheet).

Phase 1 is read-only: the driver's FIFO layout, the datasheet, then hardware experiments with numbered frames. Stop and keep A+ unless it returns GO with measured results.

## 12. Order of work

1. Inspection report (Phase 1): files, functions, execution flow, timer context, risks. No code changes. Take a fresh SD image backup and confirm the working tree builds the image last verified.
2. Byte-level message, shared key header, format checks.
3. Plain bounded event log with PTA readout.
4. Counter state in OP-TEE core under one spinlock, new PTA command, persistent consumer.
5. Measure the worst-case time of one Core 1 loop iteration, then fix the timeout margins. Add the traffic deadline monitor in Core 1's loop, with `SET_TRAFFIC_MODE`.
6. Consumer heartbeat challenge.
7. Full test matrix with measurements.
8. Optional: hash chain and signed checkpoint. Design B Phase 1 only after everything else.
9. Throughout: update the project report, PPT content and learning notes as each step is tested.

Rules for every change: one small reversible change at a time; build, then regression-check boot, Core 1 startup, heartbeat/FIQ and `S12can`; two-card flashing protocol; `PLATFORM=rpi3` on every OP-TEE build; rerun buildroot → update_bootfs → sdcard-image after edits; never flash TF-A changes without UART.

## 13. Test matrix

Record for every test: setup, expected event, actual event, detection latency, false positives, recovery behaviour, relevant logs. Mark clearly which tests ran on hardware and which were only reasoned about.

1. Valid periodic frames: no events.
2. Sender stopped, Linux running: `CAN_TRAFFIC_TIMEOUT` once, then `CAN_TRAFFIC_RECOVERED` on resume.
3. Traffic running, consumer stopped: `CONSUMER_HEARTBEAT_TIMEOUT`, no traffic timeout.
4. Modified payload: `INVALID_AUTHENTICATION`.
5. Invalid tag: `INVALID_AUTHENTICATION`.
6. Malformed length or bad can_id: `MALFORMED_MESSAGE`.
7. Repeated counter: `REPLAY_DETECTED`.
8. Skipped counter then later valid frame: `COUNTER_GAP`.
9. Consumer restart: `CONSUMER_HEARTBEAT_RECOVERED`, counter state intact.
10. Heartbeat never started: timeout after the grace period G.
11. Legitimate idle period (`SET_TRAFFIC_MODE`): no false timeout, and a `MONITOR_MODE_CHANGED` entry.
12. No packet arrivals at all: deadline and heartbeat checks keep executing.
13. Log flood: rate limiting keeps earlier evidence.
14. Long run: no drift or spurious events.
15. Regression: boot, Core 1 heartbeat/FIQ, original `secure_can_demo` PASS, `S12can` tolerates the dead `can1` chip. CAN1 itself has never worked in this project (the chip never detects hardware), so this check only confirms nothing new breaks around it.

16. Worst-case Core 1 loop iteration time under frame load: measured, and the timeout margin checked against it.
17. Consumer sends a frame and fetches a heartbeat nonce back to back: confirm whether the PTA serialises or returns busy.

## 14. Honest limits

No TZASC on the Pi 3, so isolation is a software boundary. Secure World detects evidence, not proof of compromise, and cannot see frames Linux drops before forwarding. A compromised Linux can switch off the idle-mode guard or answer heartbeats without forwarding frames; both leave only indirect traces. Replay protection rests on the counter alone; the chip's timestamp freshness check measures capture delay, not sender freshness, and is not used for forwarded messages. State and log are volatile. The key is a shared prototype key. This is an educational prototype, not production automotive hardware isolation.
