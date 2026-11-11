# BOARD_VALIDATION_READINESS.md (Component 2.2, nine outstanding on-board checks)

> **STATUS UPDATE 2026-11-09 - the text below is the 2026-11-07 preparation record and is superseded.** The nine checks have since been run on the real Pi (SSH only, no UART): V2, V14, W1, W4 (both halves), W7 (user-approved), X9 and H3 are PASS; W2 (literal boot-log form) and Y10 (real CAN traffic) stay PARTIAL. Current record: `COMPONENT_2_2_INVENTORY.md` (51 PASS, 2 PARTIAL, 0 NOT RUN), `COMPONENT_2_2_REMAINING_WORK.md` (CURRENT STATE section), `IMPLEMENTATION_COMPONENTS_2_TO_5_EVIDENCE.md`. Statements below such as "nothing has been run" describe the situation on 2026-11-07 only.


Date: 2026-11-07. Status: **PREPARATION ONLY. Nothing in this document has been run. The Pi, the SD card, the device tree and the boot configuration were not touched.**
Every command below that is not already part of an existing tool is marked *(proposed, untested)*.

## 1. Preconditions: current state

Board validation needs ALL of these. As of today **none of the first five is met.**

| # | Precondition | State today | Source |
|---|---|---|---|
| P1 | **An external copy of `SD_Card_Backups/`** (verified by sha256 against `SHA256SUMS`) | **NOT DONE.** The 2.2 GB of backups exist only on this laptop's internal disk | `baseline/ROLLBACK_PROCEDURE.md` section 7, item 3 |
| P2 | **A spare microSD card** to flash. Golden rule 1: never write to the original working card | **NOT AVAILABLE** (no spare card). Which card is in the Pi (original or spare) is also unanswered | `ROLLBACK_PROCEDURE.md` sections 1 and 7; `HARDWARE_READINESS.md` "What I need from you", Q3 |
| P3 | **A verified rollback plan** | Written (`ROLLBACK_PROCEDURE.md` R1 to R6, backup table, "which backup to restore") but **a restore has never been executed on a real card** (section 7, item 4) | same |
| P4 | **Recovery access** if the Pi stops booting | **Only SSH exists.** No UART adapter (`HARDWARE_READINESS.md` 1.6.2), no second CAN board, no CAN-FD sender. A non-booting Pi can only be diagnosed by putting its card in this laptop | `HARDWARE_READINESS.md` |
| P5 | **Your separate approval** for each phase below | Not given. The 2.2 permission covered software work only | your 2026-11-07 instructions |
| P6 | A Monitor-enabled **full SD image** (TF-A + OP-TEE + U-Boot + Linux + rootfs) | **Not built.** Only OP-TEE scratch builds exist (`build_scratch/final_on`, `final_on_debug`); the packaging pipeline (buildroot, `update_bootfs`, sdcard image) has not been run with `CFG_RPI3_CAN_MONITOR=y`. Lesson from the project: the whole pipeline must be rerun after a source change, and the build must pass `PLATFORM=rpi3` explicitly | project notes; `scripts/build_optee_scratch.sh` |
| P7 | A Normal-World **test client for commands 17 to 23** | **Does not exist.** `spi_poc_test` only knows the legacy options (`--core1-heartbeat`, `--wake-core1`, `--fiq-count`, ...) | `spi_poc_test/main.c` |
| P8 | The **V2 user-space atomic test program** | **Does not exist** | repo search, 2026-11-07 |

Note on the rollback targets: the image running on the Pi today is `pre-S0-sep20/rpi3-sdcard-sep20.img` (sha256 `014bb983...802d19`); it has **no monitor**. The first rollback choice if a monitor image fails to boot is that image (`ROLLBACK_PROCEDURE.md`, "Which backup to restore"). The passwordless flash rule only allows the exact forms `dd if=.../out/rpi3-sdcard.img of=/dev/sda bs=4M conv=fsync status=progress` and `cmp --bytes=* .../out/rpi3-sdcard.img /dev/sda`; **it writes whatever `out/rpi3-sdcard.img` currently is to `/dev/sda`**, so the device must be identified with `lsblk` before every use (Incident 2 in the project notes).

## 2. The nine checks

Legend for "Needs": **client** = new Linux program only; **test-only Secure World code** = a source change to OP-TEE that would need your explicit approval and a separate test-only image; **UART** = cannot be observed without a console.

| ID | Exact purpose | Hardware / software | Specific preconditions beyond P1 to P6 | Expected output | Failure handling | Separate approval | Without UART or extra hardware? |
|---|---|---|---|---|---|---|---|
| **V2** | No lost increments of `pta_lock_timeouts` with concurrent writers on cores 0, 2, 3 (design: user-space `atomic_a64.S` test on Normal-World memory, plus the host wrapper test) | Pi with any image; a user-space program running three pinned threads, each doing `ldaxr/add/stxr` increments on one shared word *(proposed, untested; P8)* | The program does not exist. It tests the instruction sequence on Normal-World memory; it **cannot** test the Secure World counter itself | Final value = sum of all increments, exactly | A mismatch means the exclusive-access assumption is wrong on this CPU/memory type: stop and report; no state to roll back (user-space only, nothing written to the card) | **Yes** (a run on the Pi) | **Yes**, SSH is enough |
| **V14** | An error-return command (for example BUSY or BAD_STATE) still delivers its value outputs (the health pair) to Linux | Monitor-enabled image (P6); a client calling commands 17 to 23 with the exact parameter types *(proposed, untested; P7)*; a way to force BUSY (a second caller flooding the PTA, or a held lock, which needs test-only code) | Without a forced BUSY the check can only be done with the cheaper error returns (BAD_PARAMETERS, SHORT_BUFFER, BAD_STATE) | Per the interface decision DEC-IF-1: the health pair is filled only for SUCCESS and BUSY and zero for the other errors. What the Linux TEE driver actually returns for a non-SUCCESS call is the thing being measured | Record the observed behavior. If the driver drops outputs on error, the design for BUSY must change (design decision, not a rollback) | **Yes** (needs a flash) | **Yes**, SSH is enough once the image and client exist |
| **W1** | The lock word lives in cached, inner-shareable Normal memory on the primary core and on Core 1 (needed for `ldaxr/stxr`) | Monitor-enabled image; a way to read the translation attributes from Secure World | There is **no existing way** to read them from Linux. Needs **test-only Secure World code** (for example a PTA command returning the descriptor bits) or a UART trace | The page descriptor for `rpi3_mon` shows Normal, Write-Back, Inner Shareable on both cores | A different attribute would invalidate the lock design: stop; no rollback needed (read-only) | **Yes**, for both the flash and the source change | **No**: needs UART or a source change |
| **W2** | Monitor init finishes before Core 1 can run the monitor loop | Monitor-enabled image | The original wording uses the boot log; Secure World log lines cannot be read without UART. **An indirect SSH-only test exists:** Core 1 reads `init_ok` once at loop entry (`mon_on = rpi3_mon_init_ok()`); if `poll_count` in command 20 page 0 keeps rising and the INIT_OK flag is set, init was published before Core 1 entered the loop. This is a consequence, not a timestamp comparison | `INIT_OK` set in the page-0 flags **and** `poll_count` increasing between two reads | `poll_count` stuck at 0 with INIT_OK set means init finished after Core 1 started: report; no rollback | **Yes** | **Partly**: the indirect form needs only SSH plus the client (P7); the literal boot-log form needs UART |
| **W4** | A repeated wake returns ALREADY_ON (legacy path); **in the Monitor profile the wake command (13) is refused** | Monitor-enabled image; existing `spi_poc_test --wake-core1` | The refusal half is already covered by host tests (`LG1`). The ALREADY_ON half applies only to a non-Monitor build | Monitor build: `--wake-core1` fails with `TEE_ERROR_NOT_SUPPORTED` (0xFFFF000A) both times; legacy build: second call reports ALREADY_ON | Unexpected success in a Monitor build means the dispatch order is wrong: stop and report | **Yes** | **Yes**, SSH is enough |
| **W7** | After a forced Core 1 panic, Linux and the PTA stay up | Monitor-enabled image; **a way to force a Core 1 panic** | **There is no such mechanism in the source** (needs test-only Secure World code). A panic in Secure World may stop **every** core (the OP-TEE panic path loops forever), in which case Linux dies too; with no UART the cause cannot be seen and recovery is a power cycle, which risks filesystem damage on the single card. Design note 4e I5 and the project notes say this is "only by a separate decision" | The design wants Linux and PTA to stay up. A realistic outcome on this platform is that they do **not** | Power-cycle; then `verify_boot.sh <pi> --expect-core1-pinned`; if not healthy, restore per `ROLLBACK_PROCEDURE.md` | **Yes: its own, separate approval, never bundled with the others** | **No**: needs source change, and is unsafe without UART and a spare card |
| **X9** | Measured wait for a simulated lock holder is about the 1 ms budget; attempts per millisecond recorded (calibrates the 2,000-attempt cap) | Monitor-enabled image; a holder that keeps the lock for longer than 1 ms | Nothing in the current code holds the lock that long. Needs **test-only Secure World code** (a hold command or a debug delay) | BUSY returned after roughly 1 ms; `pta_lock_timeouts` increased by one per exhausted command; the recorded attempts per ms tell whether 2,000 is a safe cap | If BUSY comes much later than 1 ms, the cap or budget needs revision (Component 4); no rollback | **Yes**, for the flash and for the test-only code | **No**: needs a test-only source change; reading the result needs the client (P7) |
| **Y10** | Measured Core 1 pass duration with monitor logic, the lock-collision rate and the hold time (confirms the 256-pass cadence) | Monitor-enabled image; the client reading command 20 page 0 twice, a known interval apart (`poll_count`, `skip_total`, `now_ticks`) | Pass duration and collision rate can be derived from two reads of page 0 with the existing code. **Hold time cannot** (needs the CNTPCT stamps of H3) | `poll_count` delta over the interval gives passes per second; `skip_total` delta gives collisions; compare with the earlier inference of about 2.4 us per pass | If the pass is far slower than assumed, the 256-pass cadence and the 1.6 percent claim are revisited (design 4b.1) | **Yes** | **Yes** for pass duration and collisions; **no** for hold time |
| **H3** | Worst-case and 99th-percentile lock-hold time (CS5 target: under 100 us, not yet a measurement) | Monitor-enabled **test build** that stamps `CNTPCT` after a successful trylock and before the unlock and keeps max and a histogram | Needs **test-only Secure World code**; the stamps themselves add instrumented-call cost and must be accounted for. Under D1 Option B no FIQ can extend a hold | Two numbers (maximum and p99) in microseconds, recorded with the CPU frequency (the Linux governor sets it; the design noted 600 MHz) | A maximum above 100 us does not break correctness but invalidates the CS5 target and the 1 ms budget assumption: report to the design | **Yes**, for the flash and the source change | **No**: needs a source change |

## 3. Proposed order (nothing scheduled)

| Phase | What | Gate |
|---|---|---|
| B0 | Close P1 (copy `SD_Card_Backups/` to an external disk, verify with `sha256sum -c`), obtain a spare card (P2), decide the UART question, write the V2 program and the 17 to 23 client (P7, P8) | all software and hardware preparation, no board risk. Needs your decisions on P2 and the UART |
| B1 | Build the Monitor-ON full image into a **separate output location** (P6), verify its hash, flash it to the **spare** card only, `cmp` read-back, then `verify_boot.sh <pi> --expect-core1-pinned` | your approval; rollback = swap back to the Sep-20 card or image |
| B2 | SSH-only checks: W4, V14 (non-BUSY error returns), W2 (indirect), Y10 (pass rate and collisions), V2 | your approval |
| B3 | Test-only-code checks: X9, H3, W1 | a separate test image and your approval of the source change |
| B4 | W7 | its own approval; recommended **not** to run without UART and a recovery card |

## 4. Rollback and failure rules (from the project's own documents)

- Stop at the first failed step. Do not repeat a flash with a different image blindly; change one thing at a time and record it (`ROLLBACK_PROCEDURE.md` R6).
- Check the hardware before the software: re-seat the microSD card and the CAN shield on the GPIO header (project lesson of 2026-09-18).
- Restore the card **and** the source together (a later rebuild from a modified tree recreates the broken firmware; Incident 5).
- Restore order: `pre-S0-sep20` first, then `step2-core1-hard-pinning-verified`, then `working-demo-verified-sep18`, then `pre-dual-core`.
- After any restore: `verify_boot.sh <pi-address> --expect-core1-pinned` (or `--expect-stock` for images without the divert). It reads only; it never arms the FIQ.
- Never run `--arm-fiq`, `--last-frame` or write registers during any of these checks.

## 5. What this document does not claim

No timing number, attribute value or boot order is stated as measured. Expected outputs are what the approved design and interface decisions say should happen; they are hypotheses until the board runs them.

## 6. Update 2026-11-07 (evening): P2 and P3 now met, P1 and P4 not

| Precondition | State |
|---|---|
| P1 External copy of `SD_Card_Backups/` | **STILL NOT DONE.** No external drive attached. The spare card's old boot partition was saved to `SD_Card_Backups/spare-8GB-before-restore-2026-11-07/` (laptop disk only) |
| P2 Spare card | **MET.** An 8 GB card (`/dev/sda`, 7,730,102,272 bytes, USB reader) is the spare. It is distinct from the Pi's original 32 GB card (31,914,983,424 bytes, FAT volume ID `A4BC-FC14`, firmware equal to the Sep-20 image) |
| P3 Rollback plan tested | **MET for the restore step.** The Sep-20 image (`014bb983...802d19`) was written to the spare by you (`sudo dd`, 336,592,896 bytes in 75 s) and a read-back `cmp --bytes=336592896` against the image returned identical. Not yet done: booting the restored spare in the Pi (R5 of the rollback procedure) |
| P4 Recovery access | SSH only, no UART. Recovery = move the card to the laptop reader |
| P5 Approval | Given for the restore only. **The flash of the candidate monitor image is not approved yet** |

Events: the Pi's original card was briefly in the laptop reader (auto-mounted read-write by the desktop; no write by this work; unmounted; its first 65 MB differ from the Sep-20 image by metadata, firmware files equal). The Pi was off while it was out. You added a Claude Code permission rule `sudo`, which is broader than the two commands in the sudoers file; consider narrowing it to the exact `dd` and `cmp` lines when the board work is finished. The sudoers flash rule writes to exactly `/dev/sda`: never run it while the 32 GB original is in the reader.

## 7. Update 2026-11-07 (late): the restored spare card boots; P3 fully met

The 8 GB spare, restored with the Sep-20 image, was put in the Pi and booted. The Pi came up at **10.237.27.215** (DHCP address changed from 10.237.27.187; the laptop is on a different network, 10.153.x.x, so the hardware address is not visible). The SSH "host key changed" warning was expected: a fresh copy of the system generates its own host keys at first boot. Only the stale `known_hosts` entry for that address was removed (backups: `~/.ssh/known_hosts.old`, `~/.ssh/known_hosts.bak-before-pi-215-2026-11-07`). Identity was confirmed from inside: Raspberry Pi 3 Model B, kernel 6.7.0-rc6-v8, `CPU1: failed to come online` at 5.116397 s (baselines 5.116407 and 5.116416 s), OP-TEE revision 4.10 `a7ccc63e41c4acc5`, CPUs 0,2-3 online.
`verify_boot.sh 10.237.27.215 --expect-core1-pinned`: **16 pass, 0 FAIL, 2 skipped** (canmode and ta_roundtrip not on the Pi). Log: `baseline/verify_boot_restored_spare_8GB_20261107.txt`. Core 1 heartbeat rose 81,702,973 to 82,834,237.
Meaning: the rollback path (backup image to spare card to a booting Pi) is proven on the real board. Remaining: P1 (external backup copy), P4 (no UART), and your explicit approval to flash the candidate monitor image to the spare.

## 8. Update 2026-11-07 (night): monitor candidate flashed to the 8 GB spare
Approved by the user. Device check before the write: `/dev/sda` = 7,730,102,272 bytes (the 8 GB spare, not the 32 GB original), unmounted, Pi off. `build_scratch/candidate_image/rpi3-sdcard-monitor-hardened.img` (sha256 `135c0473b6c87ab7...`) was copied to `optee_full/out/rpi3-sdcard.img`, written with the permitted `dd`, and a full read-back `cmp --bytes=336592896` was identical. Afterwards `out/rpi3-sdcard.img` was restored to the Sep-20 image (`014bb983...`). Not yet known: whether the Pi boots it. Rollback: rewrite the Sep-20 image to this card (proven, about 2 minutes), or use the original 32 GB card.

## 9. Update 2026-11-07 (night): first board run of the monitor
The candidate booted and the monitor runs (`COMPONENT_2_2_FINAL_CLOSURE.md` section 14). SSH-only checks done: V2 PASS, V14 PASS, W4 and W2 and Y10 partial. Not done: W1, X9, H3 (need test-only Secure World code and your approval), W7. New finding: Core 1 attempts the lock every about 19 us instead of the intended about 0.62 ms; decision needed. The Pi currently runs the monitor image from the 8 GB spare; the original 32 GB card is the fallback; the Sep-20 restore onto the spare is proven.
