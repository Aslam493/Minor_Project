# Secure Automotive Event Logging — Adapted for Raspberry Pi 3 (BCM2837) + OP-TEE

Adapted from a framework originally written for STM32MP157 (Cortex-A7 + Cortex-M4).
Every "M4" reference below is replaced by **Core2** (a Cortex-A53 core permanently
diverted into Secure World, the same way Core1 already is). Every ST-specific
mechanism (OpenAMP/RPMsg, ETZPC) is replaced by this board's actual equivalent,
or marked **N/A — not available on this hardware**.

---

## 1. Hardware and System Background (adapted)

| Original (STM32MP157) | This board (BCM2837 / Raspberry Pi 3) |
|---|---|
| Dual Cortex-A7 | Quad Cortex-A53 |
| Dedicated Cortex-M4 coprocessor | **None.** No separate microcontroller exists. The "independent monitor" role is played by **Core2**, an A53 core diverted permanently into Secure World — a software/boot-time decision, not a separate physical chip |
| TrustZone on Cortex-A7 | TrustZone on Cortex-A53 — same architectural guarantee (NS bit enforced by the CPU itself) |
| OP-TEE | OP-TEE — same |
| ETZPC (peripheral isolation controller) | **N/A.** BCM2837 has no TZASC/TZC-class memory/peripheral controller. Secure/Non-secure peripheral separation here is enforced only by which core's software touches a given MMIO region, not by a hardware-level lock |
| OpenAMP/RPMsg (A7↔M4) | **N/A.** Core1↔Core2 and Core1↔Core0 communication uses this project's own existing mailbox-doorbell protocol (`rpi3_can_server_push_result()`), not RPMsg |

**Correction carried over from the original prompt's own warning:** "TrustZone separates security states rather than assigning a permanent world to each physical core" is true in general — but this project does not rely on that being automatic. It engineered a specific, already-verified mechanism: `opteed_enter_secure_server_if_core1()` intercepts the PSCI `CPU_ON` call for Core1 at `bl31_warm_entrypoint`'s tail, before the Non-Secure `eret`, permanently diverting that core before Linux ever gets it. This is confirmed working on real hardware (dmesg shows Core1 excluded from Linux SMP). Core2's diversion (not yet built) will reuse the exact same proven mechanism, just targeting Core2's MPIDR position.

---

## 2. Security Objectives — Threat Checklist, Applied to This System

| # | Threat | Status in this design |
|---|---|---|
| 1 | Replay attacks | **Detected** — `secure_last_counter` continuity check on Core2 |
| 2 | Duplicate event reports | **Detected** — same counter check rejects duplicates |
| 3 | Modified event payloads | **Detected** — HMAC-SHA256 recomputation on Core2 |
| 4 | Forged event reports | **Detected** — HMAC verification; a forged tag won't match |
| 5 | Missing events | **Detected/prevented-from-being-invisible** — Core1 captures first-hand off the wire; Linux never gets a chance to hide a frame Core1 already has |
| 6 | Missing/delayed Linux responses | **Not applicable to this design's core guarantee** — Core1 never waits for or depends on a Linux response for anything. See note below |
| 7 | Linux application crashes | **Out of current scope** — this design detects CAN-frame tampering/omission, not general Linux liveness. Not addressed |
| 8 | Linux kernel hangs | **Out of current scope**, same as above |
| 9 | Linux sending fake heartbeats | **Not applicable** — this design has no Linux→Core1 heartbeat channel at all. Linux never reports anything to Core1, so it has nothing to fake |
| 10 | Linux acknowledging events without processing them | **Not applicable**, same reason as #9 — there is no Linux acknowledgement path in this design |
| 11 | Linux suppressing events before forwarding | **Structurally closed** — Linux is never the one who "forwards" a frame to the secure side in the first place; Core1 captures independently and sends its own copy to both Linux and Core2 in parallel |
| 12 | Unauthorized modification of monitoring firmware | **Partially addressed** — Core2's code is part of the same OP-TEE/TF-A image already covered by this project's existing secure boot chain. No additional M4-specific firmware-signing step is needed, since there is no separate M4 firmware |
| 13 | Attempts to modify protected monitor state | **Detected within CPU-architecture limits** — Core2's working memory is Secure-World-only RAM, unreachable from Linux's page tables by ARM's NS-bit enforcement. Not protected against non-CPU bus masters (see #16/DMA note) |
| 14 | Tampering with persistent logs | **Detected for in-place modification** (hash chain breaks) — **not yet addressed for deletion/truncation** of the log (see Section 9) |
| 15 | Rollback to an older valid log | **Not yet addressed** — requires a persistent, protected "last known chain length/tip" independent of the log itself, which this design does not yet have (ties to the open reboot-persistence gap) |
| 16 | Denial-of-service against the monitoring system | **Partially addressed** — the 64-slot ring buffer with a drop-oldest-and-log-the-drop policy absorbs bursts; a sustained flood exceeding that is explicitly a known, accepted limit |
| 17 | Reset and recovery attacks | **Not yet addressed** — no defined counter-resynchronization procedure after a Core2 restart (see Section 9) |

**Note on #6, #9, #10:** this is a genuine structural strength worth stating plainly in your report — many of the hardest problems in the original framework (detecting fake heartbeats, fake acknowledgements) exist *because* that architecture has the monitor relying on the Normal World to report things. This design's one-way push (Core1 → Linux, Core1 → Core2, nothing flows back) sidesteps that entire category for the omission-detection guarantee. It does **not** give you Linux-liveness monitoring (#7, #8) — that would be a separate, additional feature if the professor wants it, not something this design currently does.

---

## 3. Adapted Architecture Components

### A. Core0 (Linux, Normal World)
Same role as "Cortex-A7/Linux" in the original — normal CAN consumption, no trusted logging requests needed in this design (unlike the original's "TEE Client API" calls), since Core1 pushes to Linux directly via shared memory, not via a client-invoked TEE session.

### B. Core2 (replaces "Cortex-M4 Independent Monitor")
Feasible on this hardware, with adjusted scope:

| Original M4 responsibility | Feasible on Core2? |
|---|---|
| Monitor independently observable CAN events | **Yes** — via Core1's parallel push, not independent observation of the bus itself |
| Maintain independent event sequence state | **Yes** — `secure_last_counter`, moved to Core2 |
| Detect duplicate/replayed reports | **Yes** — counter check |
| Detect missing event acknowledgements | **N/A** — no ack protocol exists in this design |
| Maintain an independent timer / detect heartbeat timeouts | **Not built** — would require new work if Linux-liveness monitoring becomes in-scope |
| Compare Linux reports against independently captured events | **N/A in the omission sense** — Core2 never needs to compare against Linux, since it gets its own independent copy directly from Core1, not from Linux |
| Maintain protected monitoring state | **Yes** — Secure-World-only RAM |
| Record security violations | **Yes** — hash-chained tamper log |
| Notify the trusted logging component | **N/A as a separate step** — Core2 *is* the trusted logging component in this design; there's no third component to notify |
| Apply a defined recovery policy | **Not yet defined** — see Section 10 |

### C. OP-TEE Trusted Logging (merged into Core2 in this design)
Unlike the original framework's three-way split (M4 / OP-TEE logging app / Linux), this design keeps crypto and logging together in Core2 — both already run in OP-TEE's Secure World, so there's no meaningful boundary to split them across. Key management, HMAC computation, and the hash-chained log all live in the same OP-TEE core code.

### D. Trusted Event-Capture Component — already resolved
This is the exact comparison this session already worked through (Designs 2–8). Mapping the original's four alternatives onto what was actually decided:

| Alternative | Feasible here? | Chosen? |
|---|---|---|
| Assign CAN peripheral to the monitor directly | **Yes — this is what Core1 already does** (SPI0/GPIO25 owned by Secure World) | **Yes — this is the design** |
| Independently observable hardware event path (e.g., CS-line snooping) | Feasible but insufficient (Design 3 — proves activity, not content) | No |
| External trusted CAN gateway | Would work, but no second chip is available/needed given option 1 already works | No |
| Restrict guarantees to events that reach the monitor | N/A — not needed since option 1 gives full first-hand capture | No |

### E. Protected Persistent Storage — current state vs. gaps
| Property | Status |
|---|---|
| Integrity protection | **Yes** — hash chain |
| Authentication tags | **Yes** — HMAC per entry |
| Crash recovery | **Not yet defined** |
| Counter persistence | **No — RAM-only, explicit known gap** |
| Anti-rollback | **No — not yet addressed** |
| Log deletion detection | **No — not yet addressed** |
| Storage corruption/exhaustion | **Not yet addressed** |

---

## 4. Replay Protection — already matches the recommended design

Your event format (CAN_ID + counter + payload + HMAC tag) and verification order (ID check → counter-monotonic check → freshness → HMAC compare → only then advance `secure_last_counter`) already satisfies nearly everything the original framework asks for in Section 4, with two explicit gaps:

- **Counter wraparound:** your counter is 32-bit (`CAN0_FD_COUNTER_LEN = 4` bytes). Not yet defined: what happens when it wraps back to 0. At realistic CAN bus frame rates this takes a very long time to occur, but it should be an explicit, documented policy (e.g., treat wraparound as a special case requiring a defined reset procedure), not silently relied upon never happening.
- **8-byte truncated tag question:** already resolved correctly earlier this session — the active format uses the full 32-byte HMAC-SHA256 tag, not the dead 8-byte-truncated code path (`RPI3_CAN_SERVER_CLASSIFY_ENABLED`, never compiled in).

---

## 5–6. Liveness/Heartbeat/Acknowledgement Problems — explicitly out of current scope

As noted in Section 2's table (#6–#10), this design does not implement Linux liveness monitoring or an acknowledgement protocol, because the professor's stated requirement is CAN-frame tamper/omission detection, not general Linux-health monitoring. **If this is wanted, it is new scope**, not an extension of the current plan — flagging rather than building it speculatively.

---

## 7. Missing-Event Problem — already solved, matches the framework's own recommendation

Already covered under Section 3D above: the framework's own comparison (Linux-owned vs. monitor-owned vs. independent observation vs. external gateway) independently arrives at "give the monitor direct ownership" as the answer — which is exactly Core1's role in this design.

---

## 8. Firmware and Memory Tampering — adapted

| Original concern | This board's answer |
|---|---|
| Secure boot | Already covered by this project's existing TF-A/OP-TEE boot chain |
| M4 firmware authentication | **N/A** — no separate M4 firmware; Core2's code is part of the same signed OP-TEE image |
| ETZPC | **N/A** — not available on this SoC |
| Protected M4 memory | Replaced by: Secure-World RAM carve-out, enforced by ARM's NS-bit, already relied on by OP-TEE's existing heap |
| DMA attack surface | **Real, unresolved exposure** — without a TZASC-class controller, a compromised Linux could in principle program the GPU/DMA/USB controllers to reach Secure physical memory directly, bypassing the CPU's own NS-bit check. This is an accepted, pre-existing exposure of this board (also true of OP-TEE's existing secure heap), not something this specific design change introduces or fixes |

---

## 9. Secure Logging and Rollback — real open items, concretely defined

Using the original's suggested `LogEntry` structure as the actual target:
```c
struct LogEntry {
    uint32_t event_sequence;
    uint8_t  event_data[...];
    uint32_t verification_result;
    uint8_t  previous_entry_hash[32];
    uint8_t  authentication_tag[32];
};
```
Open items to design before this is complete:
1. **Deletion/truncation detection** — the hash chain alone detects in-place modification of an entry, but not "the log was truncated and replaced with an older, internally-consistent, shorter chain." This requires a separately protected record of "the chain's last known length/tip," independent of the log file itself.
2. **Rollback resistance** — needs the same kind of protected, persistent anchor as above. On this hardware, without RPMB (which this board's eMMC/SD path doesn't give you the same guarantees as a proper RPMB-equipped device), this likely means an external secure element (e.g., ATECC608 over SPI) if real rollback resistance is required — already flagged as future work in the main design plan.
3. **Storage exhaustion policy** — not yet defined. Options: wrap and overwrite oldest (with the overwrite itself logged under a new "log full" witness entry), or halt and alert. Needs an explicit choice.
4. **Crash/restart recovery and counter resynchronization** — directly inherited from the AUTOSAR SecOC discussion earlier: if Core2 restarts, `secure_last_counter` resets. A defined resync procedure (e.g., requiring the first N post-restart frames to re-establish trust before resuming normal rejection behavior) is needed, not yet designed.

---

## 10. Failure-Response Policy — not yet formalized, proposed table

| Failure | Proposed response |
|---|---|
| Replay detected | Reject the frame silently to Linux (Linux still gets its fast copy regardless — detection, not prevention); log the violation |
| Invalid HMAC | Same — log, do not block Linux's copy |
| Ring buffer overflow | Drop oldest, log the drop explicitly (not a silent loss) |
| Log storage exhaustion | **Decision needed** — wrap-and-log vs. halt-and-alert |
| Core2 restart | **Decision needed** — defined resync procedure before resuming full rejection behavior |
| Core1/Core2 queue failure | Not yet designed — needs its own detection (e.g., a dead-man's check that Core2 is still consuming) |

Per the original framework's own caution: **do not default to resetting Linux or disabling CAN communication** without separately analyzing automotive safety implications — that decision is out of this project's current scope and would need explicit instructor/safety sign-off, not an assumed default.

---

## 11. What This Adaptation Changes in the Existing Implementation Plan

No change to the already-approved build steps (Core2 TF-A diversion, SPI collapse, ring buffer, Core1→Core2 queue, Core2 validation+log). This adaptation adds **new, explicitly scoped follow-on items**, not replacements:

1. Define and implement counter-wraparound handling.
2. Define and implement log deletion/truncation detection (requires a protected length/tip anchor).
3. Define and implement a storage-exhaustion policy for the log.
4. Define and implement a Core2-restart counter-resynchronization procedure.
5. Formalize the failure-response policy table above into actual code paths.
6. Explicitly document Linux-liveness monitoring (heartbeats, crash/hang detection) as **out of scope** unless the professor requests it as new scope.

---

## 12. Testing Additions

Add to the existing verification plan:
- Inject a replay at counter wraparound boundary (or simulate it, since reaching 2^32 frames naturally isn't practical in testing).
- Truncate the log file / delete an intermediate entry and confirm this is detected (currently: **it would not be**, with the current single-hash-chain-only design — this is the concrete test that proves item 11.2 above is needed).
- Simulate Core2 restart mid-stream and confirm defined resync behavior (once designed).
- Fill log storage to capacity and confirm the chosen exhaustion policy triggers correctly.

---

## 13. Summary — what to tell the professor

This session's independent design work (Core1+Core2 split) already satisfies the hardest part of a much larger, more general framework for this class of problem (CAN-frame integrity/omission under a potentially compromised Linux), and does so with a genuine structural advantage over the general case: because there is no Linux→secure-core reporting channel at all, an entire category of attacks (fake heartbeats, fake acknowledgements) doesn't apply here. What remains open — counter wraparound, log deletion/rollback detection, storage exhaustion, restart resynchronization — are real, concrete, scoped engineering tasks, not open design questions, and Linux-liveness monitoring (crash/hang detection) is explicitly flagged as new scope, not assumed to be part of the current deliverable.
