# COMPONENT_1_RECORD.md: what we did in each subcomponent

Component 1 = Baseline and hardware readiness. One section per subcomponent, same layout every time, so the record can be read in order or looked up by number.
Detail files hold the full evidence; this file says what was done, how, and what came out. Updated after each subcomponent.

Layout of every section: **Purpose** / **What we did (steps and commands)** / **Evidence** / **Problems met and how they were solved** / **Result** / **Carry forward** / **Files**.

Target system: Raspberry Pi 3 at `10.237.27.187` (SSH as root, key login), running the Sep-20 image. Laptop: `/home/aslam/Minor_Project`.
Working rules used throughout: read-only unless a change was approved first; one step at a time; every change reversible and read back; no `git stash`; nothing flashed so far.

## Status board

| Sub | Title | Status | Detail file |
|---|---|---|---|
| 1.1 | Existing system inspection | DONE (8/8 steps) | `BASELINE_INSPECTION.md` |
| 1.2 | CAN Controller 1 verification | DONE (6/6 tasks, with limits) | `CAN_CONTROLLER1_BASELINE.md` |
| 1.3 | Linux and Secure World communication | DONE (6/6 tasks) | `COMMUNICATION_BASELINE.md` |
| 1.4 | Core 1 and FIQ verification | DONE (5/5 tasks; decisions D1, D2 open) | `CORE1_FIQ_BASELINE.md` |
| 1.5 | Backup and rollback | DONE (1.5.1 to 1.5.8; the restore itself not run, by rule) | `SD_Card_Backups/pre-S0-sep20/` |
| 1.6 | Hardware readiness | DONE for what can be measured (1.6.1, 1.6.3, 1.6.8); UART left open by the user; 1.6.4-1.6.7 need the user's answers | `HARDWARE_READINESS.md` |
| 1.7 | Rebuild, boot and acceptance | DONE (12 steps: 10 PASS, 1.7.1 and 1.7.4 not applicable; a real cold reboot replaced the flash) | `BASELINE_VERIFICATION.md` |

---

## 1.1 Existing system inspection (DONE)

**Purpose.** Know what already exists, and what must not be changed, before adding anything.

**What we did.**
- 1.1.1 Listed the git repositories: `find . -maxdepth 3 -name .git`. Found 14; four matter (top level, `optee_os`, `trusted-firmware-a`, `build`).
- 1.1.2 Read the PTA: UUID and command list in `lib/libutee/include/pta_rpi3_can_spi_poc.h`, dispatcher in `core/pta/rpi3_can_spi_poc.c` (`grep` for `cmd_id`, `param_types`, `switch`).
- 1.1.3 Read the TF-A divert: `services/spd/opteed/opteed_pm.c`, `bl31/aarch64/bl31_entrypoint.S`.
- 1.1.4 Read the Core 1 loop: `core/arch/arm/plat-rpi3/rpi3_can_server.c`.
- 1.1.5 Read the FIQ code: `rpi3_can_fiq.c` (state variables, processing function, init).
- 1.1.6 Read the Linux host apps: `secure_can_demo/host/main.c` and `spi_poc_test/main.c`.
- 1.1.7 Read the build configuration: `conf.mk`, `sub.mk`, `build/rpi3.mk`, `out/arm/conf.mk`, `config.txt`, `S12can`.
- 1.1.8 Ran `git status --short` in each repository.

**Evidence.** PTA commands 0 to 16 exist, none above; generic `param_types` check at line 372 and `switch` at line 375; Core 1 loop is `heartbeat++` plus `rpi3_can_fiq_process_pending_frame()`; build is AArch64 (`CFG_ARM64_core=y`); no lock anywhere in the PTA or FIQ code.

**Problems met.**
- My earlier summary said the OP-TEE core was ARM32. The saved plan's audit says every image is AArch64. Checked `out/arm/conf.mk` (`CFG_ARM64_core=y`) and `build/rpi3.mk` (`COMPILE_S_KERNEL := 64`): AArch64 is right. Lesson: check the build output, not a remembered note.

**Result.** 8/8 PASS. Frame path today: Linux receives frames and sends them to the **TA**; the PTA has no Linux-to-PTA frame path.

**Carry forward.** Stack comments in the FIQ code are ARM32 and obsolete (risk O-01); no lock exists (O-05, O-06); `can0` is classic CAN; the `.ta` on the Pi has a different hash from the repo's `.ta` (explain in 1.5/1.7).

**Files.** `BASELINE_INSPECTION.md`.

---

## 1.2 CAN Controller 1 verification (DONE)

**Purpose.** Prove that Linux sees the CAN controller and receives frames, without changing anything permanently.

**What we did.**
- Task 1, controller identity: `ls -l /sys/class/net/can0/device`, `readlink -f .../driver`, `cat .../uevent`, `dmesg`.
- Task 2, SPI and driver: `/sys/bus/spi/devices`, `/proc/device-tree/soc/spi@7e204000/`, `lsmod`, `/etc/modules-load.d/`.
- Task 3, interface: `ip link show can0`, sysfs `flags`, `mtu`, `operstate`, statistics.
- Task 4, packet reception, in five steps:
  1. Recorded the state: `ip link show can0`, `ip -V`.
  2. Resolved "can BusyBox `ip` set and show loopback?": `ip link set can0 type can help`, `which ip`, `cat /etc/init.d/S12can`. No iproute2 on the image.
  3. Built **`canmode`**, a read-only netlink tool (one `RTM_GETLINK` request), cross-compiled on the laptop, copied to `/tmp` on the Pi. Read bitrate and control mode.
  4. Enabled loopback: `ip link set can0 down`, `ip link set can0 type can bitrate 500000 loopback on`, `ip link set can0 up`; read back with `canmode`.
  5. Sent one frame: `candump -t A can0` in the background, `cansend can0 123#1122334455667788`; counters before and after.
  6. Restored: `down`, `type can bitrate 500000 loopback off`, `up`; read back with `canmode` and compared with the original.
- Task 5, errors: `dmesg` filter, error counters, re-checked after traffic.
- Task 6, data path written down.

**Evidence.** MCP2518FD on `spi0.0`, driver `mcp251xfd`; 500000 bit/s, 87.5% sample point, 40 MHz clock, mtu 16 (classic CAN); supported modes `0x1b3` (LOOPBACK, LISTEN-ONLY, FD, ...). One transmit in loopback: `tx_packets` +1, `rx_packets` +2, two identical `candump` lines 42 us apart, no errors, no new `dmesg` lines. After restore: identical to the original state (counters keep history: rx 2, tx 1).

**Problems met and how they were solved.** (full table A1 to A8 in the detail file)
- BusyBox `ip` cannot show CAN details -> read sysfs, `/proc`, `dmesg`, then built `canmode`.
- `timeout`, `pgrep`, `python3` missing on the Pi -> used `candump -T`, avoided the others.
- My first `canmode` said "supported modes unavailable", contradicting the kernel source -> tested my own code first; the toolchain header lacked `IFLA_CAN_CTRLMODE_EXT` and my hand-written fallback (18) was wrong (17) -> vendored the kernel's header, removed the fallback, added a compile-time assert.
- My `#ifndef` guard could not see an enum member -> relied on the assert.
- I mis-decoded flag `0x40081` (it contains ECHO `0x40000`, not LOWER_UP) -> corrected.
- "Software echo or real reception?" -> read the driver: echo is created only after the chip reports the transmit done, and the CAN core's own loopback is off because `can0` has `IFF_ECHO`; two frames for one transmit means the second came from the chip's receive FIFO.
- An earlier note said the Sep-20 image had never booted -> hashed the boot partition on the card against the saved image: identical, so it is the running image.

**Result.** 6/6 PASS with limits.

**Carry forward.** Physical bus and transceiver not proven (needs an external sender, 1.6); no Secure World path yet (1.3); FD needs `fd on` plus a data bit timing and BusyBox may not set the latter (Component 4); `can1` dead.

**Files.** `CAN_CONTROLLER1_BASELINE.md`; tool `Part_E_Verification/tools/canmode/` (`canmode.c`, vendored `include/uapi/linux/can/netlink.h`); on the Pi, RAM only: `/tmp/canmode`.

---

## 1.3 Linux and Secure World communication (DONE: 6/6 tasks)

**Purpose.** Know exactly how Linux talks to OP-TEE and whether CAN frames are forwarded.

**Planned steps** (from the plan): 1.3.1 PTA UUID; 1.3.2 PTA dispatcher; 1.3.3 Linux host application and PTA invocation; 1.3.4 input and output parameters; 1.3.5 shared-memory use; 1.3.6 are CAN frames forwarded, and through which path.

**Facts already found in 1.1** (to confirm and document here): `spi_poc_test` opens a session on the PTA UUID; `secure_can_demo` calls the TA, not the PTA; PTA commands stop at 16; no Linux-to-PTA frame path. Distinction to prove: "Linux receives frames but does not forward them to the PTA" versus "forwards them through the PTA".

**Done so far (Task 1, 2026-11-06).** Ran `ls -l /dev/tee*`, `lsmod | grep -i tee`, `dmesg | grep -iE 'optee|tee'`, `find /lib/optee_armtz`, plus `/sys/class/tee`, `/sys/bus/tee`, `ps`. Result: `/dev/tee0` (client) and `/dev/teepriv0` (supplicant) exist; OP-TEE 4.10 `a7ccc63e4` initialised; driver is built in (empty `lsmod` is expected); 37 TA files installed including the project TA `abecf637-...`; `tee-supplicant` running. The PTA is inside `tee.bin`, not in `/lib/optee_armtz`. Alignment note recorded: "Secure World does not access the CAN controller" and the nonce heartbeat describe the Monitor design, not the current image.

**Done so far (Task 2, 2026-11-06).** Read the TA header, properties and source; matched all 37 installed TAs to source by UUID (1 project, 5 OP-TEE built-in extras, 11 examples, 20 test-suite); parsed both `.ta` builds. Project TA `abecf637-...`: cmds 0 PROCESS_CHALLENGE, 1 PROCESS_FRAME, 2 VERIFY_FRAME, 3 CLASSIFY_FRAME; `TA_FLAGS 0` (fresh instance per session, no persistent state). Limits recorded: no replay protection, tag-minting oracle (cmd 1), 8-byte truncated tag, hard-coded key. The `.ta` hash difference from 1.1 is resolved: identical program payload (`7e872e32...`), only the RSA-PSS signature differs.

**Done so far (Task 3, 2026-11-06).** Read the PTA header, dispatcher, getters, `pseudo_ta.h`, `tee_ta_manager.c`. PTA `9a986af1-048a-44a8-b984-1e902290d63c` is compiled into `tee.bin`; flags SINGLE_INSTANCE|MULTI_SESSION|INSTANCE_KEEP_ALIVE, no CONCURRENT, so Linux calls into it are serialised but Core 1 is not part of that and no lock exists; no caller authorisation. All 17 commands (0-16) tabulated with exact parameter shapes and a class (M memory/MMIO, H SPI read, W write, K known-bug). Called the nine class-M commands on the Pi: all correct and consistent; heartbeat rising; FIQ count 0 and FIQ_CONTROL 0 (secure CAN path dormant until cmd 3). Cmd 16 found to do SPI reads and not called. Cmds 3, 6, 7, 13, 14 confirmed as the ones the hardened profile must remove or log.

**Done so far (Task 4, 2026-11-06).** Built `Part_E_Verification/tools/ta_roundtrip/` (read-only; Buildroot cross compiler; `/tmp` on the Pi). 14/14 checks passed: TA challenge `0x12345678 -> 0xb791f3dd` + marker SCAN, second value `0 -> 0xa5a5a5a5`, wrong parameter types and unknown command both rejected with the right code and origin TRUSTED_APP; PTA session, Core 1 heartbeat rising (+206,602 in ~0.5 s, about 2.4 us per loop pass, rough), PTA wrong-type rejected, command 17 NOT_SUPPORTED (free for the monitor). No change to can0, CPU mask or dmesg. My earlier typed expected value was wrong (`0xb7b7b7dd`); correct is `0xb791f3dd`, and the tool now computes it from the header.

**Done so far (Task 5, 2026-11-06).** Built `Part_E_Verification/tools/shm_roundtrip/` (read-only, canary-guarded). 33/33 checks pass: classify boundary (0x102 SECURE / 0x103 NORMAL), tag/verify round trip with tamper cases, TA and PTA buffer-size edges (11/13/7/9 B refused, exact sizes accepted, bytes beyond the written window untouched), and an allocated 4096-byte block with offsets 100/300 where Secure World changed only its 8 output bytes. Findings: static shared-memory pool only (4 MiB at 0x08000000; `ctx.reg_mem` NO, libteec shadow buffer YES; `plat-rpi3` never registers NS DRAM); the PTA gets a direct pointer into live Normal World memory (copy once, rule R8); TEE RAM, TA RAM and shared memory are ordinary DRAM (no hardware protection). Corrected my own first conclusion (RegisterSharedMemory SUCCESS was a libteec fallback) and removed an always-pass check.

**Done (Task 6, 2026-11-06).** Consolidated path and limits; read the frame paths in the source. Four paths: P1 Linux -> TA (works, demo only, Linux's choice, Linux even transmits the frame itself), P2 chip -> FIQ -> Secure World capture -> Core 1 validation (dormant at boot, one-shot per arm, known content bug, needs CAN-FD frames nobody sends), P3 PTA debug SPI access, P4 PTA status reads; no PTA command accepts a frame. Answer to 1.3.6: in normal operation frames received on can0 go nowhere in Secure World; the existing forwarding goes to the TA, not the PTA. Nine consolidated limitations (L1-L9) and a table of what Components 2-7 take from 1.3.

**Result.** 6/6 PASS. Tools: `tools/ta_roundtrip/` (14 checks), `tools/shm_roundtrip/` (33 checks). Corrections made: wrong typed expected value, wrong first shared-memory conclusion, an always-pass check removed.

**Files.** `COMMUNICATION_BASELINE.md`, `tools/ta_roundtrip/`, `tools/shm_roundtrip/`.

---

## 1.4 Core 1 and FIQ verification (DONE: 5/5 tasks)

**Purpose.** Understand the secure execution environment and record the critical risks O-01, O-05, O-06.

**Planned steps:** 1.4.1 Core 1 startup; 1.4.2 loop; 1.4.3 heartbeat counter; 1.4.4 FIQ entry and exit; 1.4.5 exception stack; 1.4.6 shared variables and locks; 1.4.7 document O-01, O-05, O-06. `rpi3_can_fiq.c` is read, never edited.

**Already seen:** heartbeat rose 236468224 -> 242159354 -> 242998875 (and 1338452966 later); CPU online `0,2-3`; `--fiq-count` = 0; no lock in PTA or FIQ code; stack comments are ARM32 while the core is AArch64 (O-01 not settled by reading, needs a hardware test).

**Done so far (Task 1, 2026-11-06).** Scoped searches (no `rg`; project root also holds Linux/Buildroot) plus `nm` and `objdump` on the built `tee.elf`. Startup chain: Linux PSCI CPU_ON -> TF-A warm entry -> normal OP-TEE per-core init (`vector_cpu_on_entry` 0x1010181c) -> return to EL3 -> `opteed_enter_secure_server_if_core1()` (bl31_entrypoint.S:234, opteed_pm.c:243) -> ELR = 10th vector slot 0x10101924 -> `vector_secure_server_entry` 0x10101830 -> `rpi3_can_server_loop` 0x1010f300 (loop 0x1010f330-0x1010f344). Heartbeat: `volatile uint64_t` at 0x101b2f30, incremented only by Core 1, read by PTA cmd 12. Machine code matches the source. Findings: stale 'core 0' comments in thread_optee_smc_a64.S (F1); every OP-TEE function runs a stack-check hook that also toggles the exception mask (F5, for Tasks 4-5); loop starts with FIQ unmasked (F4).

**Done (Task 2, 2026-11-06).** One idle pass = `heartbeat++`, one call to `process_pending_frame` (which returns at once when nothing is pending), `dsb sy`. Measured 7 runs x 15 s with the Pi's own clock: **2.435 us per pass (410,474 to 410,925 per second), stable to 0.1% and unchanged with cores 0, 2, 3 busy** (register-only load). Almost all the time is the `CFG_CORE_DEBUG_CHECK_STACKS` hook (masks IRQ only and restores it; Core 1 always uses the core-local flag because its flags start as TMP and curr_thread INVALID, so no stray write). Heartbeat counts passes, not time, proves progress not validation, cannot wrap in practice. Inference F8: each instrumented call in the loop costs about a whole pass (~2.4 us). Open: cost of a pass with a frame (HMAC), effect of memory/SPI/PTA/FIQ load, Core 1 cache state.

**Done (Task 3, 2026-11-06).** Read-only; no FIQ generated. Chain: MCP2518FD /INT -> GPIO 25 level detect -> VC interrupt controller source 49 (FIQ_CONTROL, one-shot, currently 0) -> ARM-local routing to core 1 -> Core 1 takes it at S-EL1 directly (TF-A routing model 0: EL3 only if Non-Secure was running, so TF-A is NOT in the path on Core 1; the 'core-1-already-secure' branch in opteed_main.c is unreachable) -> `el1_fiq_sp0` (0x10102100) -> `elx_fiq` -> `interrupt_main_handler` -> `eret`. The SP_EL1 slot (`el1_fiq_sp1`, +0x300) is an endless loop. **F11 (O-01 mechanism, from disassembly):** `elx_fiq` sets SP_EL0 to tmp_stack_va_end-0x90 whatever the interrupted SP_EL0 is, so the 144-byte record and handler frames overwrite the live frames of the interrupted call chain (loop 32 B, process_pending_frame 64 B, hook) - almost all of every pass. Analysis not measurement; Core 1's actual SP_EL0 is Task 4; do not arm FIQ until O-01 is decided. Linux IRQ 186 (GPIO25) shows 1 interrupt since boot (the 1.2 loopback test); Secure World saw none.

**Done (Task 4, 2026-11-06).** Core 1 enters the loop at EL1t on SP_EL0 = tmp_stack_va_end (TF-A saves SP_EL0 with the GP regs and SPSR_EL3 at the first CPU_ON smc and changes only ELR_EL3 for the divert); SP_EL1 = core-local. FIQ therefore uses the real vector (+0x100), not the endless-loop slot (F12 resolved). tmp stack is 4096 B (ARM64 + CHECK_STACKS, canaries at both ends). Static stack-depth estimate (new `tools/stackdepth/`, estimate not bound): idle chain <= ~0.5 KB, frame-handling chain ~1.9 KB, FIQ record 144 B + handler ~1.8 KB; overlap, not exhaustion, is the problem (F18: elx_fiq assumes a free tmp stack, Core 1 loops on it with FIQ unmasked). Plan option (a) needs ~2 KB reserve and a larger STACK_TMP_SIZE (~6 KB total); option (b) = keep FIQ masked. GP regs fully preserved; no FP/SIMD (-mgeneral-regs-only, checked); ELR_EL1/SPSR_EL1 not backed up in memory (nested SError exposure). Nothing run on the Pi.

**Done (Task 5, 2026-11-06).** Concurrency: there is **no lock anywhere** in the project's Secure World code; SPI0 and chip-select GPIO 8 are shared by Linux, the PTA's own SPI driver, and Core 1 with only a non-atomic 'transfer-active bit' check that ignores the chip-select pin (Secure World can release Linux's CS mid-message); one HMAC context is shared by PTA cmd 7 (any core) and Core 1; the handler/loop frame hand-over is sound on one core; console prints mask all exceptions for ~6-7 ms per line (estimate); OP-TEE's spinlock is unfair, `cpu_spin_lock_xsave` masks everything while spinning, the deadlock detector only prints after 2^32 retries. Risks: O-01 critical (mechanism confirmed, blocks S2a), O-05 high (starvation of Core 1 or PTA callers; blocks S3), O-06 high (FIQ while holding the lock extends the hold by the handler's ms; vanishes with option b). New: N-01 shared SPI0/CS, N-02 shared HMAC context, N-03 blocking prints in the handler. Open decisions: D1 FIQ policy (recommend b: keep FIQ masked in the Monitor profile), D2 whether to reproduce O-01 on the spare card (recommend not now).

**Result.** 5/5 PASS. Tool: `tools/stackdepth/`. Corrections: hook mask description, script hex parsing, normal-mode trace path, the unreachable TF-A Core-1 branch.

**Files.** `CORE1_FIQ_BASELINE.md`, `tools/stackdepth/`.

---

## 1.5 Backup and rollback (DONE)

**Purpose.** Keep the complete working system recoverable.

**What we did.**
- 1.5.1 Identified the image: `optee_full/out/rpi3-sdcard.img` (Sep 20, 336,592,896 bytes).
- 1.5.2 `sha256sum`: image `014bb983...`, `armstub8.bin` `f203d095...`, host binaries and TA; written to `SD_Card_Backups/pre-S0-sep20/SHA256SUMS`.
- 1.5.3 and 1.5.4 `git diff` and untracked-file tarballs for `optee_os`, `build`, `trusted-firmware-a`, plus `spi_poc_test/main.c`; HEAD hashes saved. No `git stash`.
- 1.5.5 Build configuration included in the `build` diff (`rpi3.mk`, `kconfigs/rpi3.conf`) and the untracked overlay tarball.
- 1.5.6 and 1.5.7 Copied the image and `armstub8.bin` into `SD_Card_Backups/pre-S0-sep20/`; `cmp` reported identical.
- Extra: proved the card in the Pi runs this image by hashing partition 1 on both sides (`6cef8675...`).

**Done (1.5.8, 2026-11-06): rollback procedure, `baseline/ROLLBACK_PROCEDURE.md`.** Six steps R1-R6 as specified: identify the backup (path, size, sha256), verify it (`sha256sum -c`), restore **only to the spare card** (with `lsblk` + `file /dev/sdX` = "block special" checks, Incident 2), read back with `cmp`, boot-verify, recovery rule (stop; never overwrite the original; hardware first; next older backup; UART). Verified against the real backups: 7 images, all 336,592,896 bytes, `sha256sum -c` 7/7 OK (3 match hashes recorded at backup time; 4 had no earlier record so today's value is the reference); firmware fingerprint of each read; staged `out/rpi3-sdcard.img` is byte-identical to `pre-S0-sep20`; the saved source diffs apply to the pinned HEADs and reproduce all 23 live changed/untracked files (0 mismatches, live repos untouched). New files: `SD_Card_Backups/SHA256SUMS` (master), `tools/verify_boot/verify_boot.sh` (read-only boot check; 18/18 PASS on the live Sep-20 image, FAILs on a wrong expectation, exit 2 when unreachable) and `baseline/verify_boot_sep20_reference.txt`. Two script bugs found and fixed on the way (a missing quote; a CRC check that matched the driver banner). The `.ta` hash difference was explained in 1.3 (same code, different signature).

**Not done, by design or lack of means:** the restore itself (no spare card attached to the laptop; never test on the working card); the sudoers rule's exact allowed commands (needs the user's `sudo -n -l`); booting the older images. **Open:** which card is in the Pi (original or spare) is unknown; the backups exist only on the laptop's internal disk (2.2 GB, untracked by git): copy to an external drive.

**Files.** `SD_Card_Backups/pre-S0-sep20/` (diffs, tarballs, hashes, image copy).

---

## 1.6 Hardware readiness (PARTLY DONE: measurable items done; UART left open; user answers needed)

**Purpose.** Know what hardware exists for later components.

**Planned steps:** 1.6.1 Pi model; 1.6.2 UART adapter; 1.6.3 CAN Controller 1; 1.6.4 sender hardware; 1.6.5 50 ms CAN-FD sender capability; 1.6.6 second CAN board; 1.6.7 3.3 V compatibility; 1.6.8 SPI and GPIO pins.

**Known so far:** CAN Controller 1 (`can0`) works (1.2); `can1` is dead; no UART adapter seen on the laptop (`/dev/ttyUSB*`, `/dev/ttyACM*` absent); second board and sender unknown; the shield covers header pins 29/31/32/33 (plan finding).

**Done (2026-11-06; read-only; decision by the user: no UART adapter, item left open).**
- 1.6.1 board: Raspberry Pi 3 Model B (BCM2837), 897 MB visible, serial `00000000111e9802`. The card in the Pi: SanDisk `SD32G`, serial `0x3224d710`, made 07/2025, 29.7 GiB (original or spare? unknown, user to compare labels).
- 1.6.2 UART: none attached. Measured that the header UART is the **mini UART on GPIO14/15 (pins 8/10), ALT5**, and that TF-A, OP-TEE and Linux all share it at 115200 (TF-A picks it because GPIO14 is ALT5). Wiring recorded (adapter RX to pin 8, TX to pin 10, GND to pin 6, no VCC, 3.3 V logic). Consequences listed: no TF-A/early-boot changes, Component 3 option (a) not recommended, no secure-console output.
- 1.6.3 CAN controller 1: from 1.2 (`can0` works, `can1` dead; shield Seeed 2-ch CAN-BUS(FD) v2.0, 40 MHz oscillator). A lone node never gets an ACK in normal mode.
- 1.6.4-1.6.6 sender and second board: unknown. Requirements written down (CAN-FD, ID 0x123, DLC 14, counter/payload/32-byte tag/pad, 50 ms period, 3.3 V, crystal).
- 1.6.7 3.3 V: checklist only (needs the board).
- 1.6.8 pins: measured by read-only `devmem` of GPFSEL0-2, GPLEV0, GPLEN0 and decoded by script: SPI0 on GPIO7-11, UART on GPIO14/15, can0 /INT GPIO25 with low-level detect enabled (confirms the interrupt chain on live hardware), can1 /INT GPIO24 not enabled; all other GPIO 0-27 free inputs. Options for chip B listed with the shared-register constraints (GPIO5/6 share GPFSEL0 and GPIO12/13 share GPFSEL1 with pins in use; GPIO20-23, 26, 27 sit in GPFSEL2 which has no pin in use). `/dev/mem` readable by root Linux (a compromised Linux can touch these pins).
- Needed from the user: second CAN board (model, crystal)? CAN-FD sender? which card? are header pins 6/8/10 and 29/31/32/33 reachable with the shield on? the CAN-FD data bit rate?

**Files.** `HARDWARE_READINESS.md`.

---

## 1.7 Rebuild, boot and acceptance (DONE)

**Purpose.** Confirm the existing system boots and passes the objective acceptance list.

**Planned steps:** 1.7.1 spare card; 1.7.2 flash permission; 1.7.3 reference image; 1.7.4 flash; 1.7.5 boot; 1.7.6 boot log; 1.7.7 demo PASS lines; 1.7.8 `--core1-heartbeat` twice; 1.7.9 `--fiq-count`; 1.7.10 `dmesg` CRC check; 1.7.11 CPU mask `0,2-3`; 1.7.12 save results.

**Note.** The Pi is already running the Sep-20 image (identity proven), so 1.7.4 and 1.7.5 may not need a flash; decide with you. The demo needs `can0` in loopback (restore afterward with the same commands as 1.2).

**Done (2026-11-06).** The Pi already ran the Sep-20 image (boot partition byte-identical; 8 of 9 key rootfs files identical, the exception being a newer `spi_poc_test` copied to the Pi earlier), so no flash was made; a **real cold reboot** was done instead (back in 50 s; same boot as before: `CPU1: failed to come online` 5.116 s, OP-TEE initialised 7.37 s, `can0` 9.90 s; 289-line log saved). `verify_boot.sh` 18/18 PASS after the reboot and again after the demo. Demo (`can0` in loopback, restored and confirmed by `canmode`): round trip `0x12345678 -> 0xb791f3dd`, 5 frames (3 SECURE, 2 NORMAL), authentic accepted, tampered rejected, exit 0; counters tx 5 / rx 10 (echo plus receive-FIFO copy, as derived in 1.2), errors 0. Heartbeat rising, FIQ count 0, CPU mask `0,2-3`, no CRC errors, dmesg clean. Regression values: deterministic tags for the three SECURE frames. `sudo -n -l` read: two exact passwordless rules (now in `ROLLBACK_PROCEDURE.md`). Observed: the demo's CPU-1 worker cannot pin (CPU 1 is Secure World) and prints a misleading `pthread_setaffinity_np: Success`; the cross-core display no longer applies.

**Files.** `BASELINE_VERIFICATION.md`, `boot_log_sep20_image.txt`, `demo_run_sep20_image.txt`, `verify_boot_sep20_reference.txt`, `verify_boot_sep20_after_reboot_and_demo.txt`.

---



---

## Component 1 completion gate (assessed 2026-11-06)

| Subcomponent | Verified and documented? | Detail |
|---|---|---|
| 1.1 Inspection | yes | `BASELINE_INSPECTION.md` |
| 1.2 CAN controller | yes, with limits | physical bus and FD not proven |
| 1.3 Communication | yes | `COMMUNICATION_BASELINE.md` |
| 1.4 Core 1 and FIQ | yes | `CORE1_FIQ_BASELINE.md`; risks O-01, O-05, O-06 recorded |
| 1.5 Backup and rollback | yes | `ROLLBACK_PROCEDURE.md`; the restore itself not run (no spare card attached, by rule) |
| 1.6 Hardware readiness | the measurable part yes; **hardware items open** | `HARDWARE_READINESS.md` |
| 1.7 Boot and acceptance | yes | `BASELINE_VERIFICATION.md` |

**Verdict: the gate for the software work (Components 2 to 6, Mode F, milestone M1) is MET.** There is a verified, reproducible, backed-up baseline; the objective acceptance list passes; the rollback is documented and its checkable parts verified.

**Not met, and recorded as open (they do not block Components 2 to 6 but they block the real-bus tests and Component 7):**
- no UART adapter (left open by the user): early-boot and TF-A work stay off limits;
- no second CAN board and no CAN-FD sender known (user to answer);
- the restore to a card has never been run;
- which SD card is in the Pi (original or spare) is not known;
- `SD_Card_Backups/` exists only on this laptop's internal disk.

**Decisions waiting for the user (D1 blocks Component 3):** D1 the FIQ policy for the Monitor profile (recommended: keep FIQ masked for the whole Core 1 loop); D2 whether to reproduce O-01 on a spare card (recommended: not now). Also: the CAN-FD data bit rate, and whether to fix the stale "core 0" comments and the demo's CPU-1 worker pin once source edits are approved.

## Files created for Component 1

| File or folder | Purpose |
|---|---|
| `Secure_CAN_Monitor_Plan_8_Components.md` | the 8-component plan, status board, rollback section |
| `Part_E_Verification/baseline/BASELINE_INSPECTION.md` | 1.1 |
| `Part_E_Verification/baseline/CAN_CONTROLLER1_BASELINE.md` | 1.2, with the problem and solution log |
| `Part_E_Verification/baseline/COMMUNICATION_BASELINE.md` | 1.3 |
| `Part_E_Verification/baseline/CORE1_FIQ_BASELINE.md` | 1.4 |
| `Part_E_Verification/baseline/ROLLBACK_PROCEDURE.md` | 1.5 |
| `Part_E_Verification/baseline/HARDWARE_READINESS.md` | 1.6 |
| `Part_E_Verification/baseline/BASELINE_VERIFICATION.md` | 1.7 |
| `Part_E_Verification/baseline/boot_log_sep20_image.txt`, `demo_run_sep20_image.txt`, `verify_boot_*.txt` | raw evidence |
| `Part_E_Verification/baseline/COMPONENT_1_RECORD.md` | this file |
| `Part_E_Verification/tools/` | `canmode`, `ta_roundtrip`, `shm_roundtrip`, `stackdepth`, `verify_boot` |
| `SD_Card_Backups/pre-S0-sep20/`, `SD_Card_Backups/SHA256SUMS` | verified backup, master checksums |
