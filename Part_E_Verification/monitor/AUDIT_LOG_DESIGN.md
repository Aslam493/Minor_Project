# Audit log: a record of every check Secure World performs, kept for later investigation (design note, 2026-11-18)

**Requirement (user):** log what Secure World checked, and store it in a log that survives, so it can be used for further investigation. "Log into flash memory." Flash on the Pi 3 = the SD card, which Linux owns.

**Gap today:** the event log (64 slots, RAM) records only anomalies, rate-limited, and is lost at reboot. An ACCEPTED frame, an on-time heartbeat or an on-time reply leaves no record, only counters. There is no record of "what was checked and what the result was".

## 1. What is recorded (the audit log, separate from the alarm event log)
One record per check, in the order the checks happened:
| Check | Record |
|---|---|
| each frame verdict (command 17, and each packet of command 31 in Mode P) | kind FRAME, verdict (ACCEPTED, GAP, RESYNC, MALFORMED, AUTH_FAIL, REPLAY, DUPLICATE, CONFLICT), packet counter, detail |
| each heartbeat answer verdict (command 23) | kind HEARTBEAT, verdict, request number |
| each packet-request reply verdict (Mode P, command 31) | kind PACKET_REQUEST, verdict, request number |
| each deadline decision by the clock (miss, timeout, recovery) | kind DEADLINE, which one, detail |
Record = 24 bytes (same shape as an event record): `seq u32 | kind u16 | verdict u16 | ticks u64 (Secure World timer) | counter u32 | detail u32`. Sequence numbers are contiguous, so a gap in `seq` means records were lost.

## 2. Where it lives in Secure World
A ring of 128 records (3 KB) in the monitor state, written **inside the lock hold that already makes the check** (a few volatile stores, no loop over data, no memcpy, no HMAC). When the ring is full the oldest record is overwritten and `audit_lost` counts it. At 20 checks per second the ring holds about 6 s, so the logger must read it at least once a second.

## 3. How it reaches the SD card (flash)
Secure World cannot write the SD card. A Linux logger (`mon_logger`) reads the audit ring with a new command (**32 GET_AUDIT**: from_seq, up to 5 records per call, returns the records and the oldest sequence still held) and appends each record, with the Linux wall-clock time, to a file on the SD card, with `fsync`. It asks again from the next sequence. After a reboot it starts from the new boot's first sequence and writes the boot id, so boots are separate in the file. The file is later read offline.

## 4. What this log can and cannot prove
- It records every check and its result, in order, with Secure World timestamps. That is what an investigation needs.
- **Linux writes the file, so Linux could drop or edit it.** The records are not authenticated yet. Stage B (later): Secure World adds a chain tag to each record, `tag_i = HMAC(K_log, tag_(i-1) | record_i)`, computed lazily outside the lock when records are read. Then edits, deletions in the middle and reordering are detectable offline. Deleting the **tail** or the whole file is only detectable if the latest chain value is also kept somewhere else: the UART line on the laptop is that copy.
- The Pi 3 has no secure counter and no secure storage (the OP-TEE hardware key is all zeros), so a rolled-back SD card cannot be detected without an external anchor.
- A stalled logger shows up as growing `audit_lost` and a record gap, and (stage B) as an event.

## 5. Work units
- A1: host tests first (harness target for the audit ring, records for each check kind, lock-hold rules, loss counting, command 32 parameters and BUSY rules).
- A2: Secure World code behind a flag `CFG_RPI3_CAN_MONITOR_AUDIT` (default n; off = identical to today), command 32; compose with the Mode P flag so ONE image can carry both and be flashed once.
- A3: host / static / mutation / scratch builds (0 warnings), as for Mode P.
- A4: Normal World: client function, `mon_logger` (append + fsync, boot markers), an offline reader on the laptop.
- A5: image, flash to the 8 GB spare with the user's approval, board test (log survives a reboot, matches the monitor counters, shows a forced tampered frame and a stopped sender).
- Stage B: the chain tag and the offline verifier.

## 6. Decisions taken by me (change on request)
Separate audit ring (the alarm log keeps its limiter); 128 records; command 32; the SD card file written by Linux; chain tag later. Nothing is built yet.

## Execution plan
The multi-agent execution plan (agents, file ownership, waves, gates, risks, decisions D1-D4, time) is in `AUDIT_LOG_PLAN.md` (2026-11-18). Execution starts 2026-11-19 after the Mode P timing runs.

## 7. Protection requirement (user, 2026-11-18): see `AUDIT_LOG_PLAN.md` section 8
The log should live where an attacker cannot get it. On the Pi 3 that cannot be guaranteed (no TZASC, no usable secure storage). Level 1 (records sealed in Secure World: encrypted and chained, newest chain value also on the UART) is part of stage A; Level 2 (a chip only Secure World uses) is optional hardware; Level 3 (a board with a memory firewall) is the real fix. The honest claim: tamper-evident, not tamper-proof.
