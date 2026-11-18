# Mode P: Secure World asks, Linux hands the packets over (design note, 2026-11-18)

**Requirement (user):** packets are created outside the Pi (laptop). They reach Linux through the CAN shield. Linux receives and **holds** them. **When Secure World asks, Linux hands them over**; Secure World checks them and decides. Packets move to Secure World **only when it asks**. The sender and the shield stay as they are.

**Gap this closes:** in Mode F (built) Linux pushes every frame with command 17, unasked. See the gap table in the 2026-11-18 conversation record and `RESULTS.md` section 9 once updated.

## 1. Plan placement
The plan (`Secure_CAN_Monitor_Plan_8_Components.md`, v3.3) has Mode F and Mode D2. Mode P is a **third mode, additive, compile-time, default off** (`CFG_RPI3_CAN_MONITOR_PULL`, default n). With the flag off the build is functionally identical to today (same host checks, same `nm` diff rule). Commands 24 to 29 are taken (24 and 25 test-only, D2 report = 24 in the plan), so Mode P uses **30 and 31**. Commands 0 to 23 keep their numbers; commands 0 to 16 are untouched.

## 2. The flow (the heartbeat pattern, applied to packets)
```
Core 1 (Secure World), every P = 50 ms:  pq_seq++, state OPEN, issue time. Sends nothing.
Linux forwarder, always holding a queue of frames received from can0:
   cmd 30  GET_PACKET_REQUEST   -> (pq_seq, state, max_batch, due_ms)
   cmd 31  SUBMIT_PACKETS(seq, n, n x 44 bytes) -> (request verdict, packets processed)
Secure World, per request:  verdict ON_TIME / LATE / STALE / UNISSUED / DUPLICATE (same order as command 23);
   then EACH packet is judged exactly as command 17 judges a frame (tag, counter, id, length), under its own short lock hold.
Core 1 by the clock: OPEN longer than D_p = 40 ms -> MISSED, miss_count++; 3 misses, or never answered after the 60 s grace -> ONE TIMEOUT (detail 4).
```
- **Command 17 is refused in Mode P** (`NOT_SUPPORTED`): packets enter only through a reply to a request.
- **Empty reply** (n = 0) is valid: Linux is alive, the sender is silent. The traffic deadline (300 ms) still fires if no valid packet is processed in time.
- **One lock, as before.** The HMAC of each packet is computed outside the lock (as command 17 does). The first packet's hold also marks the request, so a BUSY result changes nothing (rule "BUSY comes before any state change"). A BUSY on a later packet stops the batch: `processed` tells Linux how many to drop from its queue; the rest stay queued for the next request.
- **No new event types** (the event table stays at 25): a missed request is `TIMEOUT` detail 4; late, stale, unissued and duplicate replies reuse `LATE_ANSWER`, `STALE_ANSWER`, `UNISSUED_ANSWER`, `DUP_ANSWER` with detail 1 and the request number in the counter field; recovery is `HEARTBEAT_RECOVERED` detail 1.
- **Status page:** `mode` reads 2.
- The heartbeat (commands 22 and 23) is unchanged and still runs.

## 3. Constants (decisions taken by me, change on request)
P = 50 ms (request period), D_p = 40 ms (reply due; D_p < P so one request is outstanding), max batch = 32 packets, K = 3 misses, grace 60 s (existing). Static assert: T_traffic (300 ms) > P + D_p + sender period (100 ms) + jitter (50 ms) = 240 ms.

## 4. What Linux can and cannot do (honest limits)
Linux cannot forge a packet (no key). It can leave packets out or delay them: gaps in the counters, `TIMEOUT`, and missed requests show some of that, but a Linux that withholds the newest packets only shows up when the traffic deadline fires. The **timing is coarser** than reading the bus itself: Secure World learns about a packet when the reply arrives (up to P + D_p late) and cannot trust Linux's timestamps. Only Design 2 (Secure World reads chip B itself) removes that.

## 5. Work units (each: snapshot, host tests first, commit; board only with approval)
- P1: host tests for Mode P (new harness target `test_pull`), written first, expected to fail.
- P2: implementation in `rpi3_can_monitor.[ch]`, `rpi3_can_monitor_pta.c`, `pta_rpi3_can_spi_poc.h`, `conf.mk` (the refactor of command 17 into prepare + apply, behaviour unchanged).
- P3: all host targets green (flag off: 434/1493/451 unchanged; flag on: new), static checks, mutation tests for the new code, scratch builds with `PLATFORM=rpi3`, 0 warnings, both flag settings, object-code checks.
- P4: Normal World: client library functions, `can_forwarder` pull mode (queue + answer requests), host test.
- P5: image build (Mode P), flash **only the 8 GB spare** with your approval, `verify_boot`, regression, board test with the laptop-made packet file. Rollback = the hardened c16384 image.
