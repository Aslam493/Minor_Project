# Audit log: execution plan for problem 4 (written 2026-11-18, to be executed from 2026-11-19; NOTHING is built yet)

**Problem 4:** Secure World must keep a persistent record of everything it checked, in flash (the SD card, the only flash on a Pi 3), for later investigation. Design: `AUDIT_LOG_DESIGN.md` (read it first). This file says **who does what, in which order, how it is proved, and what the user must decide.**

Honest scope: Secure World cannot write the SD card (Linux owns the SD controller; OP-TEE secure storage is a file on the same SD, written through Linux). So the path is Secure World ring -> Linux logger -> SD file. Linux can drop or edit the file; stage B (chain tag + a copy of the newest tag on UART) makes edits and middle deletions detectable. Deleting the tail or rolling back the card stays undetectable without an outside copy. Detection only; nothing here prevents tampering.

## 1. Decisions needed from the user before wave 1 (ask once, in the morning)
| # | Decision | My recommendation |
|---|---|---|
| D1 | Ring size: the design says 128 records. In Mode P the load is about 31 checks per second (20 request verdicts + 10 packets + 1 heartbeat), so 128 records hold about 4 s. | 256 records (6 KB). The logger reads every 250 ms. |
| D2 | One image with Mode P and the audit log together (one flash), or two separate images. | One image; flag `CFG_RPI3_CAN_MONITOR_AUDIT` composes with `..._PULL`. |
| D3 | Where on the SD card: the rootfs may be read-only or small. I check this on the Pi first (read-only: `mount`, `df`). | A directory on a writable partition, size-capped, rotated at 4 MB, 8 files kept. |
| D4 | Stage B (chain tag) in the same week, or only stage A first. | Stage A first, then B after the board test of A. |

## 2. Agents and file ownership (no two agents edit the same file)
| Agent | Role | Owns (only these files) | Must not touch |
|---|---|---|---|
| **LEAD** (me) | freezes the interface, merges, runs every gate, commits after each merge, builds images, talks to the user | `AUDIT_LOG_ABI.md`, commits, builds | - |
| **T** tests | writes the host tests FIRST from `AUDIT_LOG_DESIGN.md` and the ABI note, without reading the implementation | `source_tests/harness.c` (new `t_audit`), `Makefile` target `test_audit` | the monitor sources |
| **S** secure code | implements the ring, the record hooks and command 32 behind the flag | `rpi3_can_monitor.c/.h` (audit parts only, inside `#ifdef CFG_RPI3_CAN_MONITOR_AUDIT`) | tests, glue |
| **G** glue + client | PTA glue for command 32, libutee header, `mon_client` function, `status` print | `rpi3_can_monitor_pta.c`, `pta_rpi3_can_spi_poc.h`, `tools/monitor_client/mon_client.[ch]`, `mon_cli.c` | the monitor logic |
| **N** normal-world logger | `mon_logger` (read command 32 every 250 ms, append, batched fsync, boot markers, rotation, loss markers) and `audit_read.py` (offline reader/verifier), with host tests for the file format, rotation and a simulated stalled/restarted logger | `tools/monitor_client/mon_logger.c`, `audit_read.py`, their tests | the Secure World files |
| **R** reviewer (independent, read-only) | tries to break the finished code: lock-hold rules, ring wrap, loss counting, BUSY paths, ABI mismatch between the two sides; reports findings, edits nothing | none (reports) | everything |
| **V** verification | static-check additions, mutation script for the audit code, object-code checks | `static_checks.sh`, `mutation_audit.py` | the monitor sources |
| **P** paper | records, plan status board, `RESULTS.md`, `PROJECT_EXPLANATION.md`, the corrected key wording, PDFs | docs only | code |

Rules for all agents: standing project rules apply (detection only; no TF-A change; do not edit `rpi3_can_fiq.c`; commands 0-16 unchanged; no memcpy/memset/HMAC inside a lock hold; keys never readable; scratch builds with `PLATFORM=rpi3` and 0 warnings; never touch the Pi or any card; never push nested repos; never leave a test edited to make it pass). Nobody edits the optee_os tree or `harness.c` while a build or a mutation run reads them (this broke a result earlier today).

## 3. Waves
**Wave 0: LEAD, about 30 min.** Snapshot the monitor files; write `AUDIT_LOG_ABI.md` (record layout 24 bytes, kinds, verdict codes, command 32 parameters, ring size from D1, loss rules, boot id handling); commit. The ABI note is the contract all agents code against.

**Wave 1 (parallel): T, N and G start together; S starts when T has committed its tests.** About 1.5 h.
- T: tests for every check kind (frame verdicts in both Mode F and Mode P, heartbeat answer, packet-request verdict, deadline decisions), contiguous `seq`, ring wrap and `audit_lost`, one hold per check with no memcpy/HMAC, command 32 parameter types, BUSY (no change), `from_seq` before the oldest and after the newest, empty log, flag-off build identical to today (440/1499/457 and 613 stay unchanged).
- S: the ring and hooks, written so the tests pass; the hook is plain stores inside the existing hold.
- G: glue and client function against the ABI.
- N: logger and reader with a fake source (a file standing in for command 32), so it needs no board.

**Wave 2: LEAD merge gate, about 30 min.** Run all four host suites, then the Mode P+audit build, fix nothing silently, record the numbers.

**Wave 3 (parallel): R and V, about 1 h.** V adds static checks (audit code only inside the flag, one write site, no call in the hook, ring index bound) and a mutation script (~20 mutants: wrong wrap, missing loss count, off-by-one `from_seq`, hook missing on one check kind, BUSY changing state...). R reviews adversarially. Any survivor or finding goes back to S/T, then wave 2 again.

**Wave 4: LEAD builds, about 40 min.** Scratch builds OFF/ON/PULL/AUDIT/PULL+AUDIT/DEBUG with 0 warnings, object-code checks on the disassembly, the combined armstub and image, read-back and partition comparisons, sha256. Then `card_helper.sh check`, and **the user runs the sudo dd** (never the 32 GB card).

**Wave 5: board test, with the user's go, about 1 h.** `verify_boot`; run `mon_logger` and the Mode P test; then check: (1) the file on the SD card exists and has a record for every check the monitor counters report; (2) `seq` contiguous, `audit_lost` 0; (3) a forced altered packet and a stopped sender appear in the file with correct verdicts; (4) the log survives a clean reboot with a boot marker; (5) kill the logger for 10 s: the file shows a gap marker and `audit_lost` counts it; (6) hold time with the audit hook (test-statistics build) is not worse than today's by more than the measured cost; (7) flash wear/size: bytes per hour measured and extrapolated.

**Wave 6 (stage B, only after D4): about 2 h.** Chain tag `HMAC(K_log, tag_(i-1) | record_i)` computed lazily outside the lock, tag in the reply, UART copy of the newest tag every N records, offline verifier. Same T/S/G/N/R/V cycle.

**Wave 7: P, about 45 min.** Records, status board, results, explanation, key-wording correction, PDFs; backup bundle refresh.

## 4. Gates (nothing moves on without all of them)
- Host: flag-off suites unchanged; audit suite 0 failed; static checks 0 FAIL with object code; mutation: all caught or explained as equivalent, 0 skipped (check the skip count; anchors break after refactors).
- Builds: 0 warnings in every configuration; disassemblies non-empty.
- Board: only with the user's approval; fresh boot per section; every FAIL explained from the source, never by editing the test to pass.
- Every wave ends with a commit by the lead; the nested optee_os repo is committed locally and never pushed.

## 5. Risks and how the plan handles them
| Risk | Handling |
|---|---|
| Ring overwritten before the logger reads | ring 256 + 250 ms read; `audit_lost` and a gap marker; test (5) above |
| Hook lengthens the lock hold | plain stores only; static + object check; hold time measured in the test-statistics build |
| Flash wear and SD filling | batched fsync (about once per second), 4 MB rotation, 8 files, bytes-per-hour measured |
| Logger killed or file edited by hostile Linux | detected as gaps (stage A) and chain breaks (stage B); tail deletion needs the UART copy; stated in the report |
| Two sides disagree on the ABI | one ABI note; reviewer R checks both sides; a host test links the real client structs against the real Secure World layout |
| Agents collide | file ownership table; no edits during builds or mutation runs; lead merges |

## 6. Time estimate
Stage A: about 6 to 7 h of work (host about 4 h with agents in parallel, build and flash about 1 h, board about 1 h, papers 45 min). Stage B: about 2 h more. The user is needed for D1-D4, the flash, and the go for board runs.

## 7. Order against the other open work
Tomorrow's first hour (Mode P timing runs, `board_full_check --mode-p`, key-wording fix) comes first, because the audit image is built on the same Mode P base and the timing result may change constants.

## 8. Requirement added by the user, 2026-11-18: "log the events carefully into secure memory where the attacker cannot get that part"
**What cannot be promised on the Pi 3.** There is no TZASC, so Secure World RAM is not protected from a root attacker or from DMA; OP-TEE secure storage is a file on the same SD card written through Linux, encrypted with a hardware key that is all zeros on this board (public), and the eMMC RPMB that OP-TEE normally relies on does not exist here. So "the attacker cannot get that part" cannot be guaranteed. The report must say: **tamper-evident, not tamper-proof.**

**Protection levels (what to build, in this order):**
| Level | What | Cost | What it gives | What it does not give |
|---|---|---|---|---|
| 1 (moved from stage B into stage A) | Each record is sealed in Secure World BEFORE it leaves: encrypted and chained with a log key `K_log` held only in Secure World (computed lazily outside the lock, never inside a hold); the newest chain value is also printed on the UART (outside copy) | software only, about 2 more hours than stage B alone | the file on the SD card is unreadable, and edits, middle deletions and reordering are detectable offline | a root attacker who reads Secure World RAM can extract `K_log`; tail deletion without the UART copy; rollback of the card |
| 2 (needs the user to add hardware) | Secure World writes the records straight to a small SPI FRAM/flash on a bus Linux does not use (Secure World already owns SPI0 from the earlier peripheral-ownership phase); Linux is not in the path at all | one small chip + wiring | Linux cannot drop or edit what never passes through it; survives a Linux compromise that stays in software | without a peripheral firewall a kernel-level attacker could still drive that bus; kernel hardening (no `/dev/mem`) raises the bar |
| 3 | A board with a memory firewall (STM32MP1, i.MX8) | different hardware | real protection of Secure World memory | not available now; named in the report as the path to a guarantee |

**Changes to the work units above:** (a) the chain tag and the sealing move from wave 6 into waves 1 to 5 (unit S computes the seal lazily when command 32 reads records; unit N stores the sealed form; the offline reader decrypts and verifies); (b) new decision **D5**: does the user want Level 2 (needs the chip)? default: no, Level 1 only; (c) new decision **D6**: kernel hardening (no `/dev/mem`, lockdown) in the same image, because Level 1 and 2 are only as strong as Linux's ability to reach Secure World memory; (d) the memory-isolation measurement (`/dev/mem` read test, read-only) is done first, so the claim in the report rests on a measurement; (e) the report states the limits above in the user's own terms.
