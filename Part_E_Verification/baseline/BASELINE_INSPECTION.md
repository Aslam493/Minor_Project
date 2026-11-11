# BASELINE_INSPECTION.md (Component 1, Subcomponent 1.1)

Date: 2026-11-06. Read-only inspection. No source, build, flash or board setting was changed.
Rule for later components: everything listed under "Must not change" stays byte-identical unless a stage explicitly allows it.

## 1.1.1 Repositories

14 git repositories. The ones that matter for this project:

| Repo | Path | HEAD | Role |
|---|---|---|---|
| Top level | `/home/aslam/Minor_Project` | branch `phase-a-dummy-secure-timer`, `0259424` | scripts, docs, host apps (`secure_can_demo/`, `spi_poc_test/`), curated binaries |
| OP-TEE OS | `optee_full/optee_os` | `a7ccc63e4` "WIP Step 16/17 - known bug" | Secure World OS, PTA, Core 1 loop, FIQ code |
| TF-A | `optee_full/trusted-firmware-a` | `c60998525` | EL3 monitor, Core 1 divert |
| Build | `optee_full/build` | `f441904` | `rpi3.mk`, `kconfigs/rpi3.conf`, Buildroot overlay |

Other repos (linux, u-boot, buildroot, optee_client, optee_test, optee_examples, optee_ftpm, ms-tpm-20-ref, firmware, optee_project/linux) are pinned upstream trees and are not touched by this work.

## 1.1.2 OP-TEE: existing PTA

- File `core/pta/rpi3_can_spi_poc.c` (15 KB), header `lib/libutee/include/pta_rpi3_can_spi_poc.h`. UUID `PTA_RPI3_CAN_SPI_POC_UUID` (header line 12).
- Commands 0-16 exist: 0 READ_OSC_REG, 1 GET_FIQ_COUNT, 2 READ_FIQ_CONTROL, 3 ARM_FIQ, 4 GET_LAST_FRAME, 5 GET_DEBUG, 6 READ_REG32, 7 TRANSMIT_TEST_FRAME, 8 GET_RXIF_DEBUG, 9 GET_BDIAG1_DEBUG, 10 GET_TX_FAIL_DEBUG, 11 GET_GPIO_DEBUG, 12 GET_CORE1_HEARTBEAT, 13 WAKE_CORE1, 14 WRITE_REG32, 15 GET_VALIDATION_DEBUG, 16 GET_TIMESTAMP_DEBUG. Command 17 and above do not exist.
- Dispatcher layout confirmed (matches plan section 15): `invoke_command` at line 198; per-command `if` blocks with their own `exp_pt` at 209-366; the generic `param_types != exp_pt` check at **372**; `switch (cmd_id)` at **375**. A new block for cmds 17+ must go before line 372.
- The PTA file contains **no lock** (no mutex or spinlock calls).

## 1.1.3 TF-A: boot and platform configuration

- Five modified files, all part of the Core 1 divert: `bl31/aarch64/bl31_entrypoint.S`, `services/spd/opteed/{opteed_main.c, opteed_pm.c, opteed_private.h, teesmc_opteed.h}`.
- `opteed_enter_secure_server_if_core1()` (`opteed_pm.c:243`) is called from the tail of `bl31_entrypoint.S` (line 234). It diverts core 1 on its first CPU_ON, from any source; it sets ELR to `secure_server_entry`.
- `RPI_PRIMARY_CPU` stays 0. TF-A source is not to be changed by the monitor work, but `tf-a` must be rebuilt because OP-TEE is baked into `armstub8.bin`.

## 1.1.4 Core 1 initialization and loop

- File `core/arch/arm/plat-rpi3/rpi3_can_server.c` (431 lines). `rpi3_can_server_loop()` (line 393):
  1. unmask native interrupts (FIQ) with `thread_set_exceptions(... & ~THREAD_EXCP_NATIVE_INTR)`;
  2. `while (true) { heartbeat++; rpi3_can_fiq_process_pending_frame(); dsb(); }`.
- The heartbeat is a plain `volatile uint64_t`, read by PTA cmd 12.
- Nothing in `plat-rpi3` reads the secure timer (`CNTPCT`) today.
- The stack comments in this file and in `rpi3_can_fiq.c` describe **ARM32** (`entry_a32.S`). The build is AArch64 (below), so those comments are obsolete. This is the root of risk O-01 (see 1.4).

## 1.1.5 FIQ implementation

- `rpi3_can_fiq.c` is 71,913 bytes and has about 1,400 uncommitted lines against HEAD. It is **not to be edited** by this work.
- Chip A frame state is a single slot: `static volatile uint32_t can0_frame_state` (EMPTY / PENDING / PROCESSING) plus a plain `static uint32_t secure_last_counter` (strictly-greater policy; counter 0 is rejected). The comments say "single-core"; there is no locking.
- `rpi3_can_fiq_process_pending_frame()` (line 1395) is the HMAC path, called from the Core 1 loop. `rpi3_can_fiq_init` (line 1606, `driver_init`) allocates the HMAC context once.
- Known bug: the Step 16/17 secure frame content is wrong (commit `0259424`). Avoid `--arm-fiq` and `--last-frame`.

## 1.1.6 Linux host applications

- `spi_poc_test` is a TEEC client of the **PTA** (opens a session with `PTA_RPI3_CAN_SPI_POC_UUID`); modes include `--fiq-count`, `--core1-heartbeat`, `--debug`, `--read-reg`, `--transmit`, `--validation-debug`, `--timestamp-debug` and the known-bug modes `--arm-fiq`, `--last-frame`.
- `secure_can_demo/host/main.c` opens a real `PF_CAN` raw socket on `can0`, then calls the **TA** (`TA_SECURE_CAN_CMD_PROCESS_CHALLENGE / CLASSIFY_FRAME / PROCESS_FRAME / VERIFY_FRAME`). It does not call the PTA.

## 1.1.7 Build configuration

- Only one effective profile exists today. `CFG_RPI3_CAN_MONITOR` does not exist yet. The Step 2 profile is the current behaviour.
- `plat-rpi3/conf.mk`: `CFG_RPI3_CAN_SPI_POC_PTA ?= y`, `CFG_WITH_STACK_CANARIES ?= y`.
- `plat-rpi3/sub.mk`: `main.c`, `rpi3_can_fiq.c`, `rpi3_can_server.c`.
- `build/rpi3.mk`: `override COMPILE_S_KERNEL := 64`, `OPTEE_OS_PLATFORM = rpi3`.
- `optee_os/out/arm/conf.mk`: `CFG_ARM64_core=y`, `CFG_CORE_DEBUG_CHECK_STACKS=y`, `CFG_STACK_TMP_EXTRA=0`, `CFG_TEE_CORE_NB_CORE=4`, `CFG_WITH_SOFTWARE_PRNG=y`, `CFG_BOOT_SECONDARY_REQUEST=n`.
- `out/boot/config.txt`: `enable_uart=1`, `kernel_address=0x02000000`, `device_tree_address=0x01000000` (no `dtoverlay`).
- `S12can` brings up `can0` and `can1` with `bitrate 500000` and **no `fd on`**.

## 1.1.8 Git status (recorded)

| Repo | Modified | Untracked |
|---|---|---|
| optee_os | `thread_arch.h`, `teesmc_opteed.h`, `thread_optee_smc_a32.S`, `thread_optee_smc_a64.S`, `platform_config.h`, `rpi3_can_fiq.c`, `sub.mk`, `rpi3_can_spi_poc.c`, `pta_rpi3_can_spi_poc.h` | `rpi3_can_server.c` |
| build | `kconfigs/rpi3.conf`, `rpi3.mk` | `br-ext/board/rpi3/` |
| trusted-firmware-a | `bl31_entrypoint.S`, `opteed_main.c`, `opteed_pm.c`, `opteed_private.h`, `teesmc_opteed.h` | none |
| top level | `rootfs.cpio.gz`, `armstub8.bin`, `spi_poc_test/main.c`, `spi_poc_test/spi_poc_test`; tracked file deleted: `Minor_Project_5_Phase_Implementation_BACKUP_2026-09-08.pptx` | many presentation files |

These diffs are saved in `SD_Card_Backups/pre-S0-sep20/diffs/`.

## What works and what must not change

Works (evidence in later subcomponents): boot of the Sep-20 image; Core 1 diverted (online `0,2-3`); Core 1 heartbeat rising; `can0` initialised.
Must not change: `rpi3_can_fiq.c`; PTA commands 0-16 and their blocks; the TF-A divert; `secure_can_demo` and the TA; the Step 2 profile behaviour.

## Findings for the next subcomponents

1. **Data path (feeds 1.3.6):** Linux receives frames via SocketCAN and sends them to the **TA**, not the PTA. There is no Linux-to-PTA frame path today (cmd 17 does not exist). Secure World also reads chip A itself through the FIQ path, which has the known bug.
2. **O-05 / O-06 (feeds 1.4):** neither the PTA nor the FIQ code takes any lock. `mon_lock` will be new.
3. **O-01 (feeds 1.4):** stack comments are ARM32 while the core is AArch64. Not resolved by reading; needs a hardware test.
4. **can0 mode (feeds 1.2):** `S12can` sets a classic 500 kbit/s interface with no `fd on`. The monitor frames are CAN-FD (DLC 14), so Component 4 will need `fd on` and a real sender.
5. The `.ta` file installed on the Pi has a different hash (`8a3ad20f...`) than the `.ta` in the repo (`ba619ee8...`); check in 1.5/1.7 whether this is signing or a different build.

## Ledger for 1.1

| Step | Result |
|---|---|
| 1.1.1 | PASS |
| 1.1.2 | PASS |
| 1.1.3 | PASS |
| 1.1.4 | PASS |
| 1.1.5 | PASS |
| 1.1.6 | PASS |
| 1.1.7 | PASS |
| 1.1.8 | PASS |
