# Secure CAN Gateway — Detection-Only Design (improved)

**Status:** design only, nothing in this document is implemented yet.
Items marked **(unverified)** have not been checked against the code or hardware.

## 1. Goal

Time is limited, so the requirement is **detect, not prevent**.

Secure World (OP-TEE on Core 1) must notice:

- a CAN frame that was modified, forged or replayed; and
- evidence that Linux (Normal World) is hiding, altering or ignoring frames, or has stopped behaving normally.

Linux keeps receiving and using every frame normally. Secure World only observes, verifies and logs.

### Cut from scope (future work)

- Locking Linux out of `can0`.
- Gating or releasing frames to Linux.
- Secure RAM queues (Step 18).
- Key provisioning.
- Counter wraparound and restart resync.
- Persistent or anti-rollback log.
- Any further TF-A / BL31 change. TF-A stays frozen at the verified state.

## 2. Related work used

| Paper | What it gives this design |
|---|---|
| iCORE (SAC 2019) | A core diverted into Secure World at boot runs as "master" and does not wait for Linux to call it. It hashes Linux kernel memory continuously. Core 1 already uses this mechanism. |
| EmLog (2017) | OP-TEE tamper-resistant logging. Requirements worth copying: forward integrity, truncation and re-ordering resistance, power-loss resilience. |
| TEECheck (RTNS 2020) | TEE vets outgoing CAN frames at the source (prevention, Cortex-M). Related work only. |
| SPROBES, TZ-RKP | Prevention oriented, invasive to Linux. Related work only. |
| Schneier and Kelsey (1998) | HMAC hash chain for logs. Plain chains do not catch tail truncation. |
| SecOC / BusCount | Counter-based freshness for CAN replay protection. |

No paper was found that pairs OP-TEE with CAN detection of a compromised Linux. State that as a gap.

## 3. Flow diagram

```
 Sender ECU / attacker on bus
   frame = ID | counter | data | HMAC tag
        │
        ▼
 ┌──────────────────────────────┐
 │ MCP2518FD CAN chip           │
 │ SPI0, /INT = GPIO25          │
 └───────┬──────────────┬───────┘
         │              │ FIQ (re-armed every frame)
         ▼              ▼
 ┌──────────────┐  ┌────────────────────────────────────────────────┐
 │ CORE 0       │  │ CORE 1 – SECURE WORLD (permanent loop)         │
 │ LINUX        │  │                                                │
 │ driver → app │  │ 1 CAPTURE  bounded SPI read, retry if bus busy │
 │ uses frame   │  │ 2 VALIDATE HMAC, counter, freshness            │
 │ at once      │  │ 3 GAP CHECK sender counter k, k+2 → k+1 missing│
 │              │  │ 4 REMEMBER counter → hash(frame)               │
 │ every T s:   │  │                                                │
 │  asks Core 1 │──►  5 CHALLENGE  fresh random nonce per window   │
 │  for nonce   │◄──                                               │
 │  sends       │  │                                                │
 │  report:     │  │ 6 CROSS-CHECK report vs table (step 4)         │
 │  nonce +     │──►   • content differs     → ALTERED              │
 │  range [a,b] │  │   • seen, not reported  → SUPPRESSED           │
 │  + (counter, │  │   • wrong/old nonce     → REPLAYED REPORT      │
 │  hash) list  │  │   • no report by T      → SILENT               │
 └──────────────┘  │   • gap, Core 1 missed  → CAPTURE GAP (no blame)│
                   │                                                │
                   │ 7 WATCHDOGS (every loop)                       │
                   │   • canary: send own test frame, expect it back│
                   │   • read back chip config registers            │
                   │   • interrupt-silence timer                    │
                   │   • optional: kernel text hash (iCORE)         │
                   └───────────────────────┬────────────────────────┘
                                           ▼
                   ┌────────────────────────────────────────────────┐
                   │ 8 LOG (Secure RAM)                             │
                   │   entry_n = HMAC(key, entry_n-1 ‖ record)      │
                   │   publishes (length, tip, epoch)               │
                   └───────────────────────┬────────────────────────┘
                                           ▼
                   PTA: GET_VIOLATIONS / GET_LOG_TIP / SIGNED_CHECKPOINT
                   → operator machine stores each checkpoint
```

## 4. How it works

1. **Capture.** A frame arrives and the interrupt goes to Core 1. Core 1 reads it over SPI, retries briefly if Linux is using the bus, and re-arms the interrupt. Linux receives and uses the same frame as normal.
2. **Validate.** Core 1 checks the HMAC tag, that the counter has increased, and the timestamp. A forged, edited or replayed frame is a violation.
3. **Gap check.** The sender's counter increases by one per frame. A jump from 56 to 58 shows frame 57 is missing, whoever dropped it.
4. **Remember.** Core 1 stores `counter → hash(frame)` for every frame it saw.
5. **Challenge.** At each report window Core 1 gives Linux a fresh random nonce, so an old clean report cannot be reused.
6. **Cross-check.** Linux sends its nonce, a counter range, and the `(counter, hash)` of every frame it consumed. Core 1 compares that with step 4.
7. **Watchdogs.**
   - **Canary:** Core 1 transmits its own authenticated test frame and expects to capture it back within a set time. If not, the capture path is broken or blinded.
   - **Config readback:** Core 1 re-reads the chip's configuration registers and alerts on any change.
   - **Silence timer:** alert if no interrupt arrives for far longer than normal.
   - **Optional kernel hash:** Core 1 hashes Linux kernel text at boot and again periodically (iCORE idea).
8. **Log.** Every violation is appended to an HMAC hash chain. The `(length, tip)` value is exported as a signed checkpoint to an outside machine, so later truncation no longer matches the saved copy.

## 5. What each attack produces

| Attack | Caught by step | Log entry |
|---|---|---|
| Forged or edited frame on the bus | 2 | HMAC violation |
| Replayed frame | 2, 3 | Replay or gap |
| Linux edits a frame | 6 | ALTERED |
| Linux hides a frame | 6 | SUPPRESSED |
| Linux sends an old clean report | 5, 6 | REPLAYED REPORT |
| Linux stops reporting | 6 | SILENT |
| Linux blinds the chip or interrupt | 7 | Canary failure, config change or silence |
| Log truncated afterward | 8 | Checkpoint mismatch |

If Linux sends nothing at all, that silence is itself the alarm. Secure World does not depend on Linux handing it packets, because Core 1 captures frames from the chip on its own.

## 6. Flaws that remain

| # | Flaw | Why it remains / handling |
|---|---|---|
| 1 | Capture is best-effort. Both worlds read the same chip FIFO. | Counter gaps and the "capture gap" label avoid false accusations. A separate receive FIFO would need chip behaviour not verified here. |
| 2 | A careful attacker can answer the canary and restore chip registers just before each check. | No TZASC on the Pi 3. Checks only raise the attacker's cost. |
| 3 | Linux can lie about frames Core 1 also missed. | Nothing else saw those frames. |
| 4 | Replay counter and log are lost on reboot. | Persistent storage needs the normal-world supplicant. Out of scope. After a reboot Core 1 enters an explicit "resync" state and logs it, so the reset is visible. |
| 5 | One shared prototype HMAC key (currently hard-coded in three files). | Use one shared header and per-purpose subkeys `HMAC(key, "label")`. Real provisioning is future work. |
| 6 | Secure World shows evidence that Linux is suspect. It cannot prove Linux is compromised. | True of every monitor of this kind, including iCORE. |
| 7 | The freshness check may only measure receive-to-processing delay, not sender freshness. **(unverified)** | Read `secure_can_validate_frame` before claiming replay protection from it. |
| 8 | The canary transmits on the bus. On a real bus it uses bandwidth and needs another node to acknowledge it. **(unverified)** | Works in the current loopback setup. |
| 9 | No TZASC: Secure World registers and memory are protected by software convention only. | Hardware limit. A root Linux can reach them through `/dev/mem`. State it in the report. |

## 7. Files likely touched (no TF-A changes)

- `optee_full/optee_os/core/arch/arm/plat-rpi3/rpi3_can_server.c` — Core 1 loop: frame table, report comparison, deadlines, watchdogs, log append.
- `optee_full/optee_os/core/arch/arm/plat-rpi3/rpi3_can_fiq.c` — re-arm FIQ, gap detection, hook validation outcomes into the log. Reuse `secure_can_validate_frame()`, `can0_hmac_key`, and the transmit function for the canary.
- `optee_full/optee_os/core/pta/rpi3_can_spi_poc.c` and `lib/libutee/include/pta_rpi3_can_spi_poc.h` — new commands: GET_CHALLENGE, SUBMIT_LINUX_REPORT, GET_VIOLATIONS, GET_LOG_TIP, SIGNED_CHECKPOINT.
- `secure_can_demo/host/main.c` — send the Linux report and add attack scenarios for the demo.
- `KNOWLEDGE.md` — one addendum.

## 8. Order of work

1. Take a fresh SD image backup. Confirm the working tree builds the image last verified.
2. Read-only check: does Core 1's capture see frames while Linux's driver is active? Does the freshness check measure what the comments claim?
3. Hash-chained violation log and PTA readout.
4. Sender-counter gap detection and FIQ re-arm.
5. Linux report with nonce and range, cross-check, deadline.
6. Canary, config readback, silence timer.
7. Signed checkpoint export.
8. Demo scenarios, then report text. Kernel hash only if time remains.

Rules for every flash: two-card protocol, `PLATFORM=rpi3` on every OP-TEE build, rerun the full buildroot, update_bootfs, sdcard-image pipeline after edits, never flash TF-A changes without UART.

## 9. Verification (on hardware, extended `secure_can_demo`)

- Authentic frame: no violation. Tag flipped: HMAC violation logged.
- Same counter replayed: replay violation. Skipped counter: gap logged.
- Linux report omits a frame Core 1 saw: SUPPRESSED after T.
- Linux report with altered content: ALTERED.
- Old report replayed with a stale nonce: REPLAYED REPORT.
- Reports stopped: SILENT after T.
- Canary frame blocked or chip config changed: watchdog alert.
- Log chain verifies end to end. Deleting the last entry no longer matches the saved checkpoint.
- No regression: original `secure_can_demo` PASS, zero CRC errors, board stays responsive.

## 10. Honest limits for the report

No TZASC on the Pi 3, so isolation is a software boundary. Secure World detects evidence that Linux is suspect and cannot prove compromise. Capture is best-effort. The log and replay counter are RAM-only. The key is a shared prototype key. This is an educational prototype, not production automotive hardware isolation.
