# Mode P (pull) record, 2026-11-18

**Requirement:** packets made outside the Pi reach Linux through the CAN shield; Linux holds them; **Secure World asks**, Linux hands them over, Secure World judges them. Design: `MODE_P_PULL_DESIGN.md`.

## Done (evidence in files, nothing here is a board result)
| Unit | What | Evidence |
|---|---|---|
| P0 | Design note, snapshot of optee_os before the change (clean at `6fe8ffad0`) | `MODE_P_PULL_DESIGN.md`, `source_snapshots/20261118_modeP_pre/` |
| P1 | Host tests written first (PQ1 to PQ10) | `source_tests/harness.c` (the pull build did not compile until P2) |
| P2 | Secure World code, flag `CFG_RPI3_CAN_MONITOR_PULL` (default n): commands 30 and 31, Core 1 request issue, deadline and miss rules, command 17 refused; command 17 split into prepare + apply, behaviour unchanged. optee_os commit `05543140e` (local, not pushed) | `rpi3_can_monitor.[ch]`, `rpi3_can_monitor_pta.c`, `pta_rpi3_can_spi_poc.h`, `conf.mk` |
| P3 | Host: flag off 440 / 1499 / 451 checks (were 434 / 1493 / 451 + 6 new "commands 30 and 31 do not exist" checks; the UART build shows 457), flag on **538**, 0 failed; static checks **70 pass** (F27, F13 and the header boundary updated for the prepare step; PQ1 to PQ8 added, PQ7 and PQ8 on the object code); mutation **27 of 27** caught; scratch builds `PLATFORM=rpi3` for flag off and on: exit 0, 0 warnings, both disassemblies non-empty and checked | `baseline/modeP_host_verification_20261118.txt`, `source_tests/mutation_pull_20261118.txt` |
| P4 | Normal World: client functions for 30 and 31, forwarder queue (drop-oldest, 38 host checks), `can_forwarder --pull` | `tools/monitor_client/` |
| P5a | Candidate image composed: hardened c16384 image with only `armstub8.bin` replaced; read-back, boot-file and rootfs comparisons done | `build_scratch/candidate_image/README.md`, `SHA256SUMS` |

## NOT done
- **Not flashed, not booted, not tested on the Pi.** Whether the image boots and whether the forwarder meets the 40 ms reply time is unknown until a board run. The Pi still runs the proven hardened c16384 image (Mode F).
- The `can_forwarder --pull` decision logic has no host test (it needs the TEE client); only the queue and the packet parsing do.
- Timing of Mode P (request every 50 ms, reply due in 40 ms) is a design value, not a measurement. Under load (F24) late replies would show as `LATE_ANSWER`, missed requests and false traffic TIMEOUTs.
- The plan itself has no Mode P section yet apart from the log entry; the plan status board is unchanged.

## How to try it (needs the user)
1. `bash scripts/card_helper.sh check rpi3-sdcard-monitor-modeP.img` with the 8 GB spare card in the laptop; run the two `sudo` commands it prints; put the card in the Pi.
2. `verify_boot` must show 16 pass, 0 FAIL.
3. `bash scripts/modeP_board_test.sh 10.237.27.187`.
4. Rollback: flash `rpi3-sdcard-monitor-hardened-c16384.img` the same way.
