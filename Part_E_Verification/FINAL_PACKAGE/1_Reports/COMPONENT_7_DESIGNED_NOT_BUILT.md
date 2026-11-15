# COMPONENT 7: Independent CAN and cross-checking (plan S5 + S6): DESIGNED, NOT BUILT (2026-11-12)

**Status: NOT STARTED, BLOCKED. Nothing of Design 2 is implemented or tested.** This is deliberate: the plan itself says "without a second board and a sender the report says Design 2 designed, not tested".

## 1. Why it is not built
| Need (plan C7) | State |
|---|---|
| A second working CAN controller ("chip B") that Core 1 reads directly | **None.** `can1` on the shield is dead hardware (`ENODEV`, `baseline/CAN_CONTROLLER1_BASELINE.md`). |
| An external CAN sender (CAN-FD frames, ID 0x123, 50 ms) | **None.** The user has no second Pi. A Waveshare RS485 CAN HAT (MCP2515) exists but is classic-CAN only, so it could be a sender or listener on a second Pi, **not** a chip B for FD frames, and the Secure World driver would be new code (different register set from the MCP2518FD). |
| Header access for chip B wiring (M5.1) | Not possible: the shield's pass-through header takes only male pins; no suitable jumpers. |

## 2. What exists (design only)
The design is in `monitor/design/ORIGINAL_PLAN_v3.4_adaptive-doodling-iverson.md`: rules R5 (report intake, cmd 24), R6 (cross-check, matching by `(ctr, tag16)`), R7 (diagnosis table), O-09 (rejected-frame ring, `rec_seq`), O-22 (record ring sizing), the gates M5.1 to M5.7, and the tests T18c/d/f/g/h/j and T26 to T47. **None of these tests has been run.** Implemented and tested instead is Design 1 (Mode F): Linux forwards frames to Secure World and Core 1 watches the deadlines and the heartbeat.

## 3. What this means for the claims
- There is **no independent evidence of what is on the CAN bus**. Every frame the monitor has judged was either loopback or injected through the PTA. A compromised Linux can withhold or alter what it forwards; Mode F cannot see that.
- The cross-check outcomes (`FRAME_ALTERED`, `FRAME_UNACCOUNTED`, `CHIPB_FAULT`, `B_MISS`) exist as event types in the ABI but are never raised.
- Rules (b) and (c) of `derive_params.py` are Design-2 rules about the 128-record chip-B ring (which does not exist). With the plan's provisional numbers (R = 500 ms, grace = 1100 ms, T = 50 ms) rule (b) **fails** (`32T = 1600 ms < 1.25 x (1100 + 500) = 2000 ms`, `monitor/derived/derive_params_20261112_plan_values.txt`). That is an open design question, to be settled only when R and grace can be measured.

## 4. What would be needed to build it
A second MCP2518FD board (or a replacement shield) wired to the Pi with a free chip select and interrupt line; a CAN-FD sender with an absolute-deadline timer; the UART adapter for Core 1 diagnostics; then the plan's order: M5.1 to M5.7 each recorded before the next, then the record ring, report intake, matching and diagnosis, with the D2 tests. Estimated by the plan as the largest and riskiest part (effort "L, risky").
