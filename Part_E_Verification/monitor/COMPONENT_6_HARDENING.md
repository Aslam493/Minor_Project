# COMPONENT 6: Hardening (plan S4), started 2026-11-12

Plan tasks: mode leases and caps; cmds 3/6/7/13/14 refused or logged in the Monitor profile; cmds 25-29 compiled out of release; own non-blocking UART drain (O-12) and `ALIVE seq=N poll_age=...` every 1 s; negative tests; lock-wait histogram and FIQ behaviour. Acceptance: negative tests return the expected errors; events visible on UART. **Milestone M1 = C1-C6 complete.**

## 1. Inventory (read-only, against the source in `optee_full/optee_os` and the earlier records)
| C6 task | State today | Evidence |
|---|---|---|
| Mode leases and caps | **Implemented; now board-verified** | `rpi3_mon_cmd21`: lease capped at 60000 ms (oversize request is clamped, not refused), 16 uses per boot then `REFUSED_CAP` + `LEASE_REFUSED`, renewal replaces (never adds). Board: T21 (C5) and N1-N5 below |
| Legacy cmds 3/6/7/13/14 refused or logged | **Implemented, board-verified in C2** | hardened profile returns NOT_SUPPORTED before any parameter check plus a best-effort `LEGACY_CMD_REFUSED` event; debug profile allows them with `LEGACY_CMD_USED` (C2 gate G4, board 2026-11-09). **Not repeated now**: cmd 3 arms the FIQ if it were ever accepted |
| Cmds 25-29 compiled out of release | **Implemented as build flag; static check only** | only 25 and 26 exist, both under `CFG_RPI3_CAN_MONITOR_TEST_STATS` (default n); static check TESTSTATS (56 static checks). Cmds 27-29 do not exist |
| Own non-blocking UART drain and `ALIVE` line every 1 s | **NOT implemented** (no code, no UART output from the monitor) | needs a source change in Secure World, a rebuild, a card swap, and a UART adapter wired to see anything |
| Negative tests | **Partly done** | C2 V14 (error returns deliver outputs), C5 T2-T4/T12-T15 (bad frames, bad answers); command 21 caps now N1-N5 below |
| Lock-wait histogram | **Exists only in the TEST_STATS build** (C2 H3: longest PTA hold 7.24 us); not in the production image |
| FIQ behaviour | **Decided and checked in C3**: FIQ masked for the whole Core 1 loop (D1 Option B), runtime assertion at loop entry |

## 2. Board negative tests, command 21 (2026-11-12, hardened c16384 image, SSH only)
Tool: `c5_board_tests --c6-negative`; log `baseline/c6_negative_idle_caps_20261112.txt`. **6 PASS, 0 FAIL.**
| Test | Result |
|---|---|
| N1 bad parameter combinations (mode 2; on with 0 ms; off with a non-zero time) | all `BAD_PARAMETERS` (0xffff0006), idle_uses unchanged, no lease |
| N2 off with no lease | `NO_CHANGE` |
| N3 request 120000 ms | `GRANTED`, remaining 60000 ms: **clamped to the cap**, not refused |
| N4 lease active, request 5000 then 1000 ms | `RENEWED`, `RENEWED`, remaining 1000 ms: replaced, not added (not 6000) |
| N4b off with an active lease | `RETURNED` |
| N5 per-boot use cap | 13 more uses granted (3 -> 16), then 7 requests `REFUSED_CAP`, `LEASE_REFUSED` +7 |
After N5 the cap is exhausted until a reboot; the Pi was rebooted afterwards (`verify_boot` 16 pass / 0 FAIL / 2 skipped, no panic, /tmp clean).
Note: an oversize request is clamped to 60 s, so a lease cannot be made longer than the cap, but a caller is not told it was shortened except through the returned remaining time.

## 3. Open items for C6
1. **UART `ALIVE` line and non-blocking drain (O-12):** needs your decision, a Secure World source change, a rebuild and a card swap; verification needs the UART adapter connected (header pins 6/8/10). T23 (UART blocked) depends on it.
2. **Carried from C5, undecided:** the `IDLE_EXPIRED` event (accept the deviation or add it) and F27, which turned out to be a misreading of mine: the limiter exists (COMPONENT_6_DESIGN.md section 1).
3. F24 mitigation made permanent in the image build (the `test` account in `teeclnt`).
4. Cmd 25/26 on a release image: add a board probe that confirms NOT_SUPPORTED (needs a raw-invoke tool; today static check only).

## 4. Design for the remaining C6 work
See `COMPONENT_6_DESIGN.md`: IDLE_EXPIRED as `MODE_CHANGED detail 2`, the F27 correction (no code change), and the UART `ALIVE` line behind a build flag that defaults to off. Awaiting approval; nothing built.
