# COMMUNICATION_BASELINE.md (Component 1, Subcomponent 1.3)

Date: 2026-11-06. Target: Pi at 10.237.27.187 running the Sep-20 image. Read-only commands over SSH. Nothing changed.
Objective: establish how Linux (Normal World) talks to OP-TEE (Secure World), and exactly what that channel carries today.

## Summary table

| Task | Work | Status |
|---|---|---|
| 1 | Identify the Linux-to-OP-TEE communication mechanism | DONE (below) |
| 2 | Identify the TA and its commands | DONE (below) |
| 3 | Identify the PTA and its commands | DONE (below) |
| 4 | Verify a Linux-to-Secure-World request and response | DONE (below) |
| 5 | Verify shared-memory communication | DONE (below) |
| 6 | Document the path and limitations | DONE (below) |

## Task 1: the communication mechanism (DONE)

Commands and output:

| Command | Output | Meaning |
|---|---|---|
| `ls -l /dev/tee*` | `crw-rw---- root teeclnt 248,0 /dev/tee0` and `crw-rw---- root tee 248,16 /dev/teepriv0` | the Linux TEE subsystem created its two device nodes. `tee0` is the **client** node (applications use it through `libteec`); `teepriv0` is the **supplicant** node, used only by `tee-supplicant` |
| `lsmod \| grep -i tee` | no output (grep exit 1) | **not a failure.** The TEE core and the OP-TEE driver are built into the kernel, not loadable modules. Proof is in the next two rows. `/proc/config.gz` is absent, so the kernel config could not be read directly; the README records `CONFIG_TEE=y`, `CONFIG_OPTEE=y` |
| `dmesg \| grep -iE 'optee\|tee'` | `optee: probing for conduit method.` / `optee: revision 4.10 (a7ccc63e41c4acc5)` / `optee: initialized driver` (at about 7.3 s) | OP-TEE answered the kernel's probe, version **4.10, commit `a7ccc63e4`**, which equals the optee_os HEAD saved in the backup. No errors |
| `find /lib/optee_armtz -maxdepth 1 -type f` | **37** `.ta` files | the Trusted Applications Linux can load into Secure World |

Extra read-only checks:

| Check | Output | Meaning |
|---|---|---|
| `/sys/class/tee` | `tee0`, `teepriv0` (major 248, minors 0 and 16) | confirms the two devices are registered with the TEE class |
| `/sys/bus/tee/devices` | `optee-ta-d96a5b40-c3e5-21e3-8794-1002a5d5c61b`, `optee-ta-f04a0fe7-1f5d-4b9b-abf7-619b85b4ce8c` | the kernel itself found two Secure World services and put them on the TEE bus; `/sys/bus/tee/drivers` lists `optee-rng`, so the kernel's random-number driver is attached to one |
| `ps` | `127 tee /usr/sbin/tee-supplicant -d /dev/teepriv0` | the supplicant daemon is running, started by `/etc/init.d/S30optee` (it also sets the node permissions and creates `/var/lib/tee`) |
| our TA | `/lib/optee_armtz/abecf637-ca20-467e-8e6c-0b8779ba6b2f.ta`, 114,096 bytes, dated Sep 16 | the project's `secure_can_demo` TA is installed |
| libraries | `libteec.so`, `libckteec.so` present | the client library applications link against |

#### What this tells us

1. The path is the standard OP-TEE one: **application -> `libteec` -> `/dev/tee0` -> kernel OP-TEE driver -> SMC into Secure World**. The supplicant (`/dev/teepriv0`) is a separate helper that serves requests from Secure World back to Linux, such as loading a TA file from `/lib/optee_armtz`.
2. OP-TEE initialised cleanly, so Linux-to-Secure-World calls are possible on this boot.
3. **The PTA does not appear in `/lib/optee_armtz`.** A PTA is compiled into OP-TEE itself (`tee.bin`), not loaded from a file. So `spi_poc_test` reaches the PTA through the same `/dev/tee0` path, but the PTA is not one of the 37 files. Task 3 will therefore read the PTA from source and prove it by a call, not by looking for a file.
4. Only `tee0` is group `teeclnt` and readable by clients; the demo and `spi_poc_test` run as root here, so permissions did not matter in practice. A non-root caller would need the `teeclnt` group. Relevant later for the hardened Monitor profile (PTA has no caller authorisation: any process allowed to open `/dev/tee0` can call it).

#### Problem or confusion avoided

| Item | Check | Conclusion |
|---|---|---|
| `lsmod \| grep tee` is empty, which looks like "driver not loaded" | `dmesg` shows `optee: initialized driver`; `/dev/tee0` exists | empty `lsmod` is expected for a built-in driver. Judge OP-TEE by `dmesg` and the device nodes, not `lsmod` |
| `/proc/config.gz` missing | compared with README and dmesg | cannot confirm built-in from the config file; the behavioural evidence is enough |

## Task 2: the Trusted Application and its commands (DONE, 2026-11-06)

Method: read `secure_can_demo/ta/include/secure_can_ta.h`, `user_ta_header_defines.h`, `secure_can_ta.c` (246 lines); mapped all 37 installed TA files to their source by UUID; compared the two `.ta` builds byte by byte.

### The project TA

| Property | Value |
|---|---|
| UUID | `abecf637-ca20-467e-8e6c-0b8779ba6b2f` (installed at `/lib/optee_armtz/`, 114,096 bytes) |
| Flags | `TA_FLAGS 0`: not single-instance, not keep-alive, so **each session gets a fresh TA instance and nothing persists between sessions** |
| Memory | stack 8 KB (raised from 2 KB after Incident 3), data 32 KB |
| Open session | requires all four parameters `NONE` |

| Cmd | Name | Parameters (type: content) | What it does |
|---|---|---|---|
| 0 | PROCESS_CHALLENGE | `VALUE_INOUT`: a = challenge in, a = response out; b = marker out | `a ^= 0xa5a5a5a5`, `b = 0x5343414e` ("SCAN"). The demo sends `0x12345678` |
| 1 | PROCESS_FRAME | `MEMREF_INPUT` = frame struct (12 bytes: `can_id` u32 + 8 data); `MEMREF_OUTPUT` >= 8 bytes | HMAC-SHA256 over `can_id(4) ‖ data(8)` with the compiled-in key; returns the **first 8 bytes** of the tag |
| 2 | VERIFY_FRAME | `MEMREF_INPUT` frame; `MEMREF_INPUT` exactly 8 bytes (claimed tag); `VALUE_OUTPUT` | recomputes the tag, constant-time compares 8 bytes, `a = 1` authentic / `0` rejected |
| 3 | CLASSIFY_FRAME | `MEMREF_INPUT` frame; `VALUE_OUTPUT` | `a = 1` (SECURE) if `can_id <= 0x102`, else `0` (NORMAL) |

Every command checks `param_types` exactly and rejects wrong sizes (`BAD_PARAMETERS` / `SHORT_BUFFER`); an unknown command returns `NOT_SUPPORTED`.

#### What this TA does not have (limits to carry forward)

| Gap | Evidence in the source | Why it matters |
|---|---|---|
| **No replay protection** | the tag covers only `can_id ‖ data`; there is no counter in the message and the TA keeps no state | a recorded valid frame verifies again. The PTA/FIQ path *does* have a counter (`secure_last_counter`); the TA path does not |
| **Tag-minting oracle** | cmd 1 returns a valid tag for any frame the caller submits | any process that can open the TA can obtain a valid tag for any frame it invents. The monitor design avoids this: `K_frame` stays in Secure World and the Linux consumer only relays bytes |
| **Truncated 8-byte tag** | `SECURE_CAN_FRAME_TAG_LEN 8U` | the monitor path uses the full 32-byte tag (decision R1) |
| **Hard-coded key** | `secure_can_demo_key` is a compiled-in array | one of three copies of the key in the project; prototype only |
| **Linux decides** | the TA returns a verdict *to the caller* | a compromised Linux can ignore or fake the use of it. This is the reason for the Secure-World-first design |

### Which of the 37 TAs belong to whom

Every UUID was matched to a source header by searching the local trees.

| Owner | Count | Examples |
|---|---|---|
| **This project** | **1** | `abecf637-...` (secure_can_demo) |
| OP-TEE OS built-in extra TAs (`optee_os/ta`) | 5 | `fd02c9da` PKCS#11, `f04a0fe7` trusted_keys, `023f8f1a` avb, `5ca4d9d9` app_secrets, `80a4c275` remoteproc |
| OP-TEE examples (`optee_examples`) | 11 | `8aaaf200` hello_world, `5dbac793` aes, `1dc6a16b` sha, `b6c53aba` random, `f4e750bb` secure_storage, and six more |
| OP-TEE test suite (`optee_test`, for `xtest`) | 20 | `cb3e5ba0` crypt, `5b9e0e40` os_test, `ffd2bded` os_test_lib, `b3091a65` os_test_lib_dl, `731e279e` storage, and fifteen more |

1 + 5 + 11 + 20 = 37, the number found on the Pi. None of the other 36 is project code; they are the standard OP-TEE software that passed 36,114 `xtest` subtests earlier. `f04a0fe7` (trusted_keys) is also one of the two services the kernel enumerated on the TEE bus in Task 1.

### The `.ta` hash difference, resolved

Open since 1.1: the TA on the Pi (`8a3ad20f...`) differs from the one in `secure_can_demo/ta/` (`ba619ee8...`). Two builds exist: the image overlay copy dated Sep 17 00:39 (this is what the Pi runs) and the repo copy dated Sep 17 01:02. Parsing both files with the OP-TEE signed-header layout (`magic 0x4f545348`, 20-byte header, then hash, then signature, then payload):

| Field | Overlay (Pi) | Repo | Same? |
|---|---|---|---|
| header magic, image type, image size (113,512), algorithm (`0x70414930`), hash size (32), signature size (512) | identical | identical | yes |
| image hash | `ef9179fa...` | `ef9179fa...` | yes |
| **payload (program) SHA-256** | `7e872e32...09cb` | `7e872e32...09cb` | **yes** |
| signature bytes | `0556733e...` | `d7c9d280...` | **no** |

Conclusion: **same code, different signature.** The algorithm `0x70414930` is `TEE_ALG_RSASSA_PKCS1_PSS_MGF1_SHA256` (checked in `tee_api_defines.h`). I did not test why the signature differs between two signings; the likely reason is the random salt in PSS signing, and that is an inference, not a measurement. What is measured is that the program the Pi runs is byte-identical to the repo's.

### Problems or confusion avoided

| Item | Check | Conclusion |
|---|---|---|
| Two `.ta` files with different hashes looked like two different builds | parsed the header and compared hash, payload and signature separately | only the signature differs; code is identical. Compare the payload, not the whole file, when checking a signed TA |
| Two UUIDs not found by a UUID-prefix search | searched the full UUID string | both are `optee_test` library TAs (`os_test_lib`, `os_test_lib_dl`) whose UUID is written in a Makefile, not a header |

## Task 3: the Pseudo Trusted Application and its commands (DONE, 2026-11-06)

Method: read `lib/libutee/include/pta_rpi3_can_spi_poc.h` and `core/pta/rpi3_can_spi_poc.c` (dispatcher, lines 197 to 473) and the getter functions in `rpi3_can_fiq.c`; read OP-TEE's `pseudo_ta.h` and `tee_ta_manager.c`; then called the nine read-only commands on the Pi.

### What the PTA is

| Property | Value | Source |
|---|---|---|
| UUID | `9a986af1-048a-44a8-b984-1e902290d63c` | header line 12 |
| Name | `rpi3_can_spi_poc.pta` | `PTA_NAME` |
| Where it lives | compiled into OP-TEE (`tee.bin`) when `CFG_RPI3_CAN_SPI_POC_PTA=y` (default `?= y`); **not** a file in `/lib/optee_armtz` | `core/pta/sub.mk:17`, `conf.mk` |
| Flags | `PTA_DEFAULT_FLAGS` = `SINGLE_INSTANCE \| MULTI_SESSION \| INSTANCE_KEEP_ALIVE` | `pseudo_ta.h:18-27` |
| Reached from Linux | the same way as a TA: `libteec` -> `/dev/tee0`, opening a session with this UUID (`spi_poc_test` does exactly that) | `spi_poc_test/main.c:52,67` |
| Caller authorisation | **none.** Any process allowed to open `/dev/tee0` (group `teeclnt`) can call any command | dispatcher has no check |

What the flags mean (from `tee_ta_try_set_busy`, `tee_ta_manager.c:127-150`): there is **one** instance with state that persists across sessions, and because `TA_FLAG_CONCURRENT` is not set, OP-TEE marks the instance busy during a call and a second caller **waits** until the first finishes. So calls into this PTA from Linux are serialised against each other. This does **not** serialise them against Core 1: Core 1 runs its own loop (`rpi3_can_server_loop`) and takes no part in this mechanism, and the PTA file contains no lock. Any shared state between a PTA handler and Core 1 therefore needs its own lock (the planned `mon_lock`).

### Command table (all 17, from the dispatcher)

Parameter shapes are the exact `param_types` each command demands; anything else returns `BAD_PARAMETERS`. Class: **M** = returns stored values or reads a memory-mapped register (no SPI); **H** = performs SPI reads on the CAN chip (shares SPI0 with Linux's driver); **W** = changes hardware or system state; **K** = known-bug path.

| Cmd | Name | Params | Does | Class | Called on the Pi? |
|---|---|---|---|---|---|
| 0 | READ_OSC_REG | `VALUE_OUTPUT` (a = register) | SPI read of chip register 0xE00 | H | no |
| 1 | GET_FIQ_COUNT | `VALUE_OUTPUT` (a = count) | returns `can0_fiq_count` | M | yes: 0 |
| 2 | READ_FIQ_CONTROL | `VALUE_OUTPUT` (a) | MMIO read of the interrupt controller's FIQ control register | M | yes: `0x00000000` |
| 3 | ARM_FIQ | `VALUE_OUTPUT` (a = FIQ_CONTROL after) | routes FIQ to the primary core, arms GPIO 25 level detect, **writes** FIQ_CONTROL | W, K | no |
| 4 | GET_LAST_FRAME | `VALUE_OUTPUT` (a = valid, b = id) + `MEMREF_OUTPUT` >= 8 B | copies the last secure-captured frame | K | no |
| 5 | GET_DEBUG | `VALUE_OUTPUT` (a = fifo index, b = fifo addr) | stored values | M | yes: 0 / 0 |
| 6 | READ_REG32 | `VALUE_INOUT` (in a = address; out a, b = value) | SPI read of **any** chip register | H | no |
| 7 | TRANSMIT_TEST_FRAME | `VALUE_INOUT` (a = id in, b = ok out) + `MEMREF_INPUT` >= 8 B | **transmits** a CAN frame through the chip from Secure World | W | no |
| 8 | GET_RXIF_DEBUG | `VALUE_OUTPUT` (a = RXIF mask, b = INT reg) | stored values | M | yes: 0 / 0 |
| 9 | GET_BDIAG1_DEBUG | `VALUE_OUTPUT` (a) | stored value | M | yes: 0 |
| 10 | GET_TX_FAIL_DEBUG | `VALUE_OUTPUT` (a) | stored value | M | yes: 0 |
| 11 | GET_GPIO_DEBUG | `VALUE_OUTPUT` (a = GPIO 25 level, b = GPEDS0) | MMIO reads of GPLEV0 and GPEDS0 | M | yes: HIGH, 0 |
| 12 | GET_CORE1_HEARTBEAT | `VALUE_OUTPUT` (a = low 32, b = high 32) | returns Core 1's free-running counter | M | yes: rising |
| 13 | WAKE_CORE1 | `VALUE_OUTPUT` (a = PSCI status) | SMC to EL3, calls `psci_cpu_on(1)`; dead code in practice (Core 1 is already diverted) | W | no |
| 14 | WRITE_REG32 | `VALUE_INOUT` (a = address, b = value in; a = ok out) | SPI **write** of **any** chip register | W | no |
| 15 | GET_VALIDATION_DEBUG | `VALUE_OUTPUT` (a = status, b = last counter) + `MEMREF_OUTPUT` >= 12 B | stored values (id, counter, dropped count) | M | yes: NONE |
| 16 | GET_TIMESTAMP_DEBUG | `VALUE_OUTPUT` (a, b) + `MEMREF_OUTPUT` >= 12 B | **two SPI reads** (CiTBC, CiTSCON) plus stored values | H | no (SPI) |

Anything else returns `NOT_SUPPORTED`. There is no command 17 or above.

How the dispatcher is laid out (confirms the plan): commands 4, 15, 16, 6, 14 and 7 each have their own `if` block with its own `param_types` check (lines 209 to 370); every other command falls through to the generic `if (param_types != exp_pt)` at **line 372** (only `VALUE_OUTPUT` allowed) and the `switch` at **line 375**. A new command with any other parameter shape must therefore be added in an early block before line 372, as the plan says.

### Live calls on the Pi (nine commands, all class M)

| Client option | Cmd | Result |
|---|---|---|
| `--fiq-count` | 1 | 0 |
| `--fiq-control` | 2 | `0x00000000` |
| `--debug` | 5 | `fifo_index=0 fifo_addr=0x00000000` |
| `--rxif-debug` | 8 | `RXIF mask=0x00000000 INT reg=0x00000000` |
| `--bdiag1-debug` | 9 | `BDIAG1=0x00000000` |
| `--tx-fail-debug` | 10 | `0` |
| `--gpio-debug` | 11 | `GPIO 25 live level: HIGH (inactive), GPEDS0=0x00000000` |
| `--core1-heartbeat` | 12 | `1552976060` (earlier readings 236468224, 242159354, 242998875, 1338452966: always rising) |
| `--validation-debug` | 15 | status `NONE`, `secure_last_counter` 0, dropped 0 |

No new `dmesg` lines. The outputs agree with each other: no frame has gone through the secure path since boot, and **the secure FIQ is not armed**.

#### What this tells us

1. **The channel works end to end** for a real, non-trivial Secure World component: nine different request shapes returned correct, mutually consistent answers.
2. **The secure CAN path is dormant at boot.** `FIQ_CONTROL = 0` means the CAN interrupt is not routed to Secure World until command 3 (`ARM_FIQ`, the known-bug path) is called. That explains why the loopback frames in 1.2 produced `FIQ count` 0: Linux handled them and Secure World never saw them.
3. Several commands give any `/dev/tee0` user direct power over the chip: 6 (read any register), 14 (write any register), 7 (transmit), 3 (re-route interrupts), 13 (SMC). The plan's hardened Monitor profile removes or logs exactly 3, 6, 7, 13 and 14 (rule R10). This confirms that list against the code.

### Problems or confusion avoided

| Item | What happened | How it was handled |
|---|---|---|
| Command 16 looks like a "debug getter" like its neighbours | reading `rpi3_can_fiq_get_timestamp_debug()` shows two `mcp251xfd_read32` SPI calls | classified H and not called: Linux's `mcp251xfd` driver shares SPI0, and Secure World has no arbitration with it. Rule: read the function body before calling any "debug" command |
| My first search for the getter bodies returned prototypes, not definitions | the pattern matched the declaration block | re-read by line number (1100 to 1165, 1473 to 1482) |
| The `rc=0` lines after each call looked like the tool's exit status | they are the exit status of `head`, which the output was piped through | the real evidence is the printed values, not those `rc` lines |
| "Secure World doesn't touch the CAN controller" (task text) versus cmds 0, 6, 7, 14 | read the dispatcher | true only for the Monitor design; the current PTA can read, write and transmit on the chip (see the alignment note) |

## Task 4: Linux-to-Secure-World request and response (DONE, 2026-11-06)

Tool: `Part_E_Verification/tools/ta_roundtrip/ta_roundtrip.c` (new, read-only). Built with the image's own Buildroot cross compiler and sysroot (`-Wall -Wextra -Werror`, dynamic link to `libteec`), copied to `/tmp/ta_roundtrip` on the Pi (RAM only), sha256 `3ea132b2...73147` on laptop and Pi (match).
It touches no CAN hardware and writes nothing. Commands used: TA cmd 0 (challenge), PTA cmd 12 (heartbeat), plus two deliberately invalid probes (TA cmd 99, PTA cmd 17). None of the write, transmit or SPI commands is used. Expected values are taken from the project headers, not typed into the tool.

Because the nonce heartbeat does not exist yet (PTA commands stop at 16), this task verifies two **real existing** request/response pairs and four error cases.

| # | Part | Request | Response | Expected | Result |
|---|---|---|---|---|---|
| 1 | TA | open session | result `0x00000000`, origin TRUSTED_APP | success | PASS |
| 2 | TA | challenge `0x12345678` | `0xb791f3dd`, marker `0x5343414e` ("SCAN") | `CHALLENGE ^ MASK` = `0xb791f3dd`, marker from header | PASS |
| 3 | TA | challenge `0x00000000` | `0xa5a5a5a5` | the mask itself (so the answer is not a constant) | PASS |
| 4 | TA | cmd 0 with `VALUE_INPUT` (wrong type) | `0xffff0006` BAD_PARAMETERS, origin TRUSTED_APP | rejected | PASS |
| 5 | TA | unknown cmd 99 | `0xffff000a` NOT_SUPPORTED | rejected | PASS |
| 6 | PTA | open session | success | success | PASS |
| 7 | PTA | heartbeat (cmd 12) twice, 0.5 s apart | 1616877997 then 1617084599 (+206,602) | second > first | PASS |
| 8 | PTA | cmd 12 with `VALUE_INPUT` (wrong type) | `0xffff0006` BAD_PARAMETERS | rejected | PASS |
| 9 | PTA | cmd 17 | `0xffff000a` NOT_SUPPORTED | does not exist yet | PASS |

Summary: 14 of 14 checks passed. Before and after the run: `can0` unchanged (`ctrlmode <>`, ERROR-ACTIVE, UP), CPU online `0,2-3`, no new `dmesg` lines, `tee-supplicant` still running.

What this establishes:
1. **Round trip is correct and deterministic.** Linux sent a value, Secure World transformed it, Linux received the exact expected result and marker, for two different inputs.
2. **Input validation works on both sides.** A wrong parameter shape and an unknown command are refused with the right code, and the error **origin** is reported as the Trusted Application, so Linux can tell a Secure World refusal from a transport failure.
3. **The PTA is live and Core 1 is running.** The counter rises about 413,000 per second, so one pass of Core 1's loop takes roughly **2.4 microseconds**. This is a rough figure: it comes from two reads separated by `usleep(500000)` and Linux scheduling jitter, so Component 4 must measure it properly with the secure timer.
4. **Command 17 is free.** `NOT_SUPPORTED` today confirms the Monitor commands can start at 17 without colliding with anything.

What it does **not** establish: that a nonce heartbeat works (not built), that any CAN frame reaches Secure World (shared memory and the frame path are Task 5 and the data-path question in Task 6), or behaviour under load or concurrent callers.

### Problems or confusion avoided

| Item | What happened | Fix |
|---|---|---|
| Wrong expected value in my own message | I told you the response would be `0xb7b7b7dd`. The correct value is `0x12345678 ^ 0xa5a5a5a5 = 0xb791f3dd`. I caught it by computing it in the shell before writing the tool | the tool derives the expected value from `SECURE_CAN_CHALLENGE ^ SECURE_CAN_MASK` in the header, so a mental-arithmetic slip cannot reach a test. Rule: never type an expected value by hand when the code can compute it |
| Running `secure_can_demo` for the challenge would also drive `can0` | it opens a CAN socket first and then sends frames; in normal mode nobody ACKs them and the controller can drift toward ERROR-PASSIVE | wrote a client that only does the challenge |
| Which compiler matches the Pi | the laptop's Arm 14.3 toolchain has a newer C library than the image | used the Buildroot host compiler and sysroot that built the image, and checked the Pi has `/lib/ld-linux-aarch64.so.1` and `libteec.so.2` |

## Task 5: shared-memory communication (DONE, 2026-11-06)

Tool: `Part_E_Verification/tools/shm_roundtrip/shm_roundtrip.c` (new, read-only). Buildroot cross compiler, `-Werror`, run from `/tmp` on the Pi (sha256 `9ac7646c...e1989bcd`, laptop and Pi match). Commands used: TA classify, TA process_frame, TA verify_frame (all pure computation) and PTA cmd 15 (stored values). No CAN hardware, no writes. Buffers are filled with a `0xAA` canary so the test shows exactly which bytes Secure World wrote.

### How a buffer reaches Secure World (from the source)

| Step | What happens | Source |
|---|---|---|
| 1 | The client passes a memory reference (`TEEC_MEMREF_TEMP_*`, or a part of an allocated block). `libteec` puts it in a shared block obtained from the kernel | `tee_client_api.c` |
| 2 | The kernel driver hands OP-TEE the physical location of that block | `optee/smc_abi.c` |
| 3 | For the **PTA**, OP-TEE checks the memory is Non-Secure (`mobj_is_nonsec`) and maps it; the handler receives a **direct pointer into the live Normal World buffer, no copy** | `pseudo_ta.c:30-46, 73-93` |
| 4 | The TA (`secure_can_ta.c`) copies the input once into a local struct (`memcpy(&frame, params[0].memref.buffer, ...)`) and then works on the copy | `secure_can_ta.c:135,167,211` |

Consequence: while a handler runs, Linux (another core) could change the shared buffer under it. The TA is safe because it copies first. A PTA handler reading `params[n].memref.buffer` repeatedly would not be. This is the reason the plan's rule R8 says "memrefs copied once with byte reads into a local buffer".

### Which shared-memory mode this system uses

| Evidence | Finding |
|---|---|
| `CFG_CORE_DYN_SHM=y`, `CFG_CORE_RESERVED_SHM=y`, `CFG_SHMEM_START=0x08000000`, size 4 MiB (`out/arm/conf.mk`) | both modes are compiled in |
| OP-TEE advertises dynamic shared memory only if `core_mmu_nsec_ddr_is_defined()` (`entry_fast.c:118-121`); `plat-rpi3` never calls `register_ddr()` | by the code, dynamic shared memory is **not** offered |
| Kernel prints `dynamic shared memory is enabled` only if it is offered (`smc_abi.c:1781`); the line is **absent** from `dmesg` | consistent with "not offered" |
| Kernel sets the capability `TEE_GEN_CAP_REG_MEM` only if dynamic shared memory is offered (`smc_abi.c:1192`) | |
| **Measured on the Pi:** `ctx.reg_mem` = NO; `TEEC_RegisterSharedMemory` succeeded but with `shadow_buffer` = YES | libteec fell back to allocating a block and copying |

Conclusion: **static pool only.** Every shared buffer comes from the 4 MiB reserved region at physical `0x08000000`. Ordinary user memory cannot be registered by reference; `libteec` copies it. `memref_null` = YES, so a NULL memory reference is accepted by this kernel.

I was wrong once on the way: my first version of the test treated "registration returned SUCCESS" as proof of dynamic shared memory. The `libteec` source has a fallback that returns SUCCESS without it. The final version decides from the capability flag and the shadow-buffer flag.

### Memory map and what it means (CORRECTED 2026-11-06)

**Correction.** The first version of this section drew the map from a comment in `platform_config.h` (secure RAM at `0x08400000`). That comment is stale. The map below is built from the build configuration, the compiled image, TF-A's own definitions, and Linux's own view of the Pi (all measured on 2026-11-06).

```
physical address        what                                     who says so
0x08000000-0x083fffff   shared memory pool, 4 MiB                OP-TEE conf.mk (CFG_SHMEM_START/SIZE)
0x08000000-0x09ffffff   reserved from Linux (32 MiB)             the device tree Linux runs with (memreserve), kpageflags: 100% RESERVED
0x10100000-0x10ffffff   OP-TEE secure DRAM, 15 MiB               OP-TEE CFG_TZDRAM_START/SIZE, TF-A SEC_DRAM0_BASE/SIZE, tee.elf load address
                        (TEE RAM, TA RAM, Core 1's stack, all monitor state)
0x10000000-0x100fffff   TF-A secure SRAM, 1 MiB                  TF-A SEC_SRAM_BASE/SIZE; the only range TF-A adds to a DT (memreserve)
0x00000000-0x3b3fffff   what Linux treats as System RAM          /proc/iomem
```

The two reservations do not match. **Linux reserves `0x08000000-0x09ffffff`, but OP-TEE runs in `0x10100000-0x10ffffff`.** Linux's per-page flags (`/proc/kpageflags`) show that range as ordinary free memory:

| Range read | Pages | Linux's flag |
|---|---|---|
| `0x08000000-0x09ffffff` | 8,192 | 100% RESERVED |
| `0x10100000-0x10ffffff` (OP-TEE secure DRAM) | 3,840 | **100% BUDDY (free)** |
| `0x20000000-0x20ffffff` (reference, ordinary RAM) | 4,096 | 100% BUDDY (free) |

Why: TF-A edits only the device tree at `RPI3_PRELOADED_DTB_BASE` (`0x00010000`), and only adds the 1 MiB secure SRAM (`rpi3_bl31_setup.c:159-215`; its own comment says "this isn't actually secure memory ... It doesn't guarantee [the OS] won't use it"). The tree Linux actually runs with is the file on the boot partition (loaded by U-Boot, Incident 5), whose reservation (`0x08000000 + 32 MiB`) was written for an older, different layout.

What this means:
1. **Today the secure DRAM is not protected even from Linux's own allocator.** If Linux ever allocates pages in `0x10100000-0x10ffffff` and writes them, it overwrites OP-TEE's code, data, TA memory, Core 1's stack or the monitor's state, without any malice. It has not happened so far: Linux uses about 6 percent of its RAM, none of those 3,840 pages is allocated now, and OP-TEE has run for hours. That is luck of allocation order, not a guarantee; a memory-hungry Linux job could break it.
2. The inference that OP-TEE physically sits at `0x10100000` rests on four configuration facts that agree (OP-TEE config, TF-A map, the image's load address, TF-A's BL32 base). It was **not** read directly: `/dev/mem` refuses RAM reads (`mmap: Operation not permitted`, Linux's `STRICT_DEVMEM`), and no UART is attached to see OP-TEE's own boot message.
3. Because of that `STRICT_DEVMEM` policy, a root process in Linux **cannot read Secure World RAM through `/dev/mem` on this kernel**. This is a property of this kernel's configuration, not of the hardware: a kernel module or a DMA-capable device is not stopped (there is no TZASC).
4. All code addresses quoted in this project from `tee.elf` (for example `0x101b2f30` for the heartbeat) are **link-time** virtual addresses. `CFG_CORE_ASLR=y` is set, so a running core may be mapped at an offset; use them for reading the code, not as runtime facts.

New risk **N-04 (high): the secure DRAM is not reserved from Linux.** Not fixed here (it needs a change to the device tree that U-Boot loads, which is a boot-affecting change and, without a UART, needs the two-card protocol). Candidate fix for a later decision: add a `/memreserve/` (or a `reserved-memory` node with `no-map`) for `0x10000000-0x10ffffff` in the device tree built by `update_bootfs`.

The original statement stays true in spirit: the Pi 3 has no TZASC, so the separation between Linux and Secure World memory is by convention only, and here even the convention is incomplete.

### Test results (33 checks, 33 pass, 0 fail)

| Part | What was tested | Result |
|---|---|---|
| A (5) | Classify through a temporary buffer: ids 0x000, 0x100, 0x102 -> SECURE; 0x103, 0x7ff -> NORMAL. The boundary (0x102 / 0x103) is exact | PASS |
| B (6) | Tag a frame: 8 bytes returned (`27f09240f7c46cbd` for id 0x100); same frame gives the same tag; authentic frame accepted; one flipped tag bit, one flipped data bit, and a changed CAN id are each rejected | PASS |
| C (9) | TA size edges: input 11 B or 13 B (frame is 12) -> BAD_PARAMETERS; output 7 B -> SHORT_BUFFER and buffer untouched; output exactly 8 B -> success; output 64 B -> success, reported size 8, bytes 8..63 untouched; verify with a 7 B or 9 B tag -> BAD_PARAMETERS | PASS |
| D (6) | PTA cmd 15 (returns 12 bytes): buffer 11 B -> SHORT_BUFFER, untouched; 12 B -> success; 64 B -> success, bytes beyond 12 untouched | PASS |
| E (7) | A 4096-byte allocated block, input window at offset 100, output window at offset 300 (length 32). Tag written at offset 300 equals the temporary-buffer result; **every byte outside the 8 bytes written is unchanged**, including the input window and the unused 24 bytes of the output window | PASS |
| F | Mode determination (observation, not a pass/fail): see above | observation |

Run state: before and after, `can0` unchanged (ERROR-ACTIVE, `<>`), CPU mask `0,2-3`, Core 1 heartbeat rising, `tee-supplicant` running, 0 new `dmesg` lines.

#### What this tells us for the monitor design

1. **Exact-size checking is how this system works, and it works.** The TA demands exactly 12 B in and exactly 8 B for a claimed tag, and refuses 11, 13, 7 and 9. The monitor's `MON_SUBMIT_FRAME` (exactly 44 B) and `MON_SUBMIT_REPORT` (16 + 24n, at most 784 B) can follow the same pattern.
2. **Secure World writes only what it reports.** In E and D no byte outside the written window changed, so a canary test is a valid way to test the new commands' output windows.
3. **Short and oversize buffers behave safely** (SHORT_BUFFER or a clean partial write), but the TA does **not** report the needed size back on SHORT_BUFFER; the monitor should (the plan's cmd 18 and cmd 20 define their sizes up front).
4. **A 784-byte report fits easily** in the 4 MiB pool; the pool is shared by all callers, so many large buffers at once could exhaust it (a possible denial of service by any `/dev/tee0` user; not tested).
5. **Copy once.** A PTA handler receives a pointer into live Normal World memory. Report and frame handlers must copy into a local buffer first, then validate the copy.

### Problems or confusion avoided

| Item | What happened | How it was handled |
|---|---|---|
| "Dynamic shared memory is active", my first conclusion | `TEEC_RegisterSharedMemory` returned SUCCESS | contradicted by the missing `dmesg` line; read `libteec` and found a fallback path; made the tool print `ctx.reg_mem` and the shadow-buffer flag. Result: static pool. Lesson: a success code can come from a fallback; test the cause, not the symptom |
| `CFG_CORE_DYN_SHM=y` looked like "dynamic shared memory is on" | the build flag only compiles the support in | the capability is advertised only when normal DRAM is registered, which `plat-rpi3` never does. Config enabled is not feature active |
| A check that could never fail | I had added `check(..., 1)` to part F | removed it; the pass count (33) now contains only real tests |
| `exit=${PIPESTATUS}` printed `?` in one run | my shell snippet, not the tool | the tool's exit status was checked separately (exit 0) |
| OP-TEE's own boot messages "Dynamic shared memory is disabled" | printed on the secure console only (UART), which is not attached | not observed directly; the conclusion rests on code, `dmesg` and the measured capability flag |

## Task 6: the communication path and its limitations (DONE, 2026-11-06)

This task adds no new test on the Pi. It consolidates Tasks 1 to 5 with a read of the frame paths in the source, and answers the question 1.3 set out to settle (1.3.6): **do CAN frames reach Secure World today, and how?**

### The channel (verified in Tasks 1 to 5)

```
Linux application
  -> libteec  ->  /dev/tee0 (client node)  ->  kernel OP-TEE driver  ->  SMC
  ->  OP-TEE (Secure World)  ->  TA  abecf637-...  (user mode, loaded from /lib/optee_armtz)
                             ->  PTA 9a986af1-...  (kernel mode, compiled into tee.bin)
Buffers:  static 4 MiB shared pool at 0x08000000 (no dynamic shared memory)
Helper:   tee-supplicant via /dev/teepriv0 (serves Secure World's requests back to Linux)
Core 1:   runs separately in Secure World; not part of the call path above
```

### The paths a CAN frame can take today

| # | Path | Who decides to pass the frame on | State on the running Pi | Evidence |
|---|---|---|---|---|
| P1 | CAN chip -> Linux driver -> `can0` socket -> `secure_can_demo` -> **TA** (classify, tag, verify) | **Linux** | works when the demo runs; `secure_can_demo` was verified end to end (Sep 18). In the demo, Linux **transmits the frame itself**, reads it back from `can0` in loopback, and only then calls the TA | `host/main.c:316-394`; TA tests in Tasks 4 and 5 |
| P2 | CAN chip -> GPIO 25 interrupt -> **FIQ in Secure World** -> single capture slot -> **Core 1 validates** (ID, counter, freshness, HMAC) | the hardware interrupt; **no Linux call** | **dormant.** The platform routes the FIQ and arms the GPIO at boot, but the interrupt controller's FIQ enable (`FIQ_CONTROL`) stays 0 until PTA cmd 3. Measured: `FIQ_CONTROL 0x0`, FIQ count 0, validation status NONE | `rpi3_can_fiq.c:1583-1642`; Task 3 calls |
| P3 | Linux -> PTA cmds 0, 6, 7, 14, 16 -> Secure World drives the chip over SPI0 | **Linux** (a debug interface) | available, not used in normal operation | dispatcher, Task 3 |
| P4 | Linux -> PTA status reads (cmds 1, 2, 5, 8 to 12, 15) | Linux | works; used throughout Tasks 3 to 5 | Task 3 |
| none | Linux -> PTA with a frame to check | | **does not exist**: no PTA command accepts a frame (no cmd 17) | Task 4: cmd 17 NOT_SUPPORTED |

Details of P2, read from the source today:
- One interrupt captures **one** frame and then disarms itself (`FIQ_CONTROL` written back to 0, count incremented). The design hands the line back to Linux after each capture.
- The capture reads the frame's bytes from the chip over SPI0, which Linux's driver also uses. If the SPI controller is busy it skips the event rather than corrupt Linux's transaction.
- Core 1's loop then validates the captured frame in this order: (1) CAN ID equals the allowed ID, (2) counter strictly greater than the last accepted, (3) timestamp fresh, (4) HMAC over `CAN_ID ‖ counter ‖ 4-byte payload` against the 32-byte tag, compared in constant time. The counter is advanced **only if all checks pass**, so a forged frame cannot use up a counter value.
- It expects CAN-FD frames (counter, payload, 32-byte tag in 48 data bytes). `can0` is classic CAN today and no sender produces such frames, so P2 has nothing real to receive.
- Known bug (Step 16/17): the frame content captured over this path is wrong. Not fixed; the path is not used.

### The answer to 1.3.6

The two situations in the plan do not describe the system exactly:

| Situation in the plan | Reality |
|---|---|
| "Linux receives CAN frames but does not forward them to Secure World" | **True in normal operation.** Frames received on `can0` go nowhere in Secure World unless something forwards them. The loopback frame in 1.2 never reached Secure World (FIQ count 0, validation NONE, checked afterwards in Task 3) |
| "Linux receives CAN frames and forwards them through the existing PTA" | **False.** The PTA has no frame input. When Linux forwards frames at all (the demo), it forwards them to the **TA**, not the PTA, and it chose to do so |

So today: **Linux-to-Secure-World frame forwarding exists only in the demo, goes to the TA, and is Linux's choice.** A direct Secure World capture exists (P2) but is dormant and buggy. The Monitor design (Mode F) adds the missing piece: a PTA command (cmd 17) that accepts a frame from Linux.

### Limitations of the current channel (consolidated)

| # | Limitation | Source |
|---|---|---|
| L1 | No caller authorisation: any process allowed to open `/dev/tee0` can call any TA or PTA command | Tasks 1 and 3 |
| L2 | In P1 Linux decides what to forward and receives the verdict; a compromised Linux can skip, fake or ignore it | Task 2 |
| L3 | The TA has no replay protection, hands out valid tags for any frame (tag oracle), uses an 8-byte tag, and a hard-coded key | Task 2 |
| L4 | Shared memory is a static 4 MiB pool shared by all callers; the PTA reads Linux's live buffer through a direct pointer (copy once) | Task 5 |
| L5 | TEE RAM, TA RAM and shared memory are ordinary DRAM with no hardware protection, and **Linux does not even reserve OP-TEE's secure DRAM from its allocator** (`0x10100000-0x10ffffff` is free memory to Linux; N-04); separation is by convention only | Task 5 (corrected) |
| L6 | Linux's driver and Secure World share SPI0 and one chip with no arbitration beyond a busy check | P2 code |
| L7 | Debug commands 3, 6, 7, 13 and 14 let any `/dev/tee0` user read or write any chip register, transmit, re-route the interrupt, or issue an SMC | Task 3 |
| L8 | No lock between PTA handlers and Core 1; Linux-side PTA calls are serialised by OP-TEE but Core 1 is outside that | Task 3 |
| L9 | P2 is one-shot per arm, single-slot, and has a known content bug; it cannot carry classic CAN | P2 code, 1.2 |

Not tested, so no claim is made: behaviour under load or concurrent callers, exhaustion of the shared pool, calls from a non-root user, and OP-TEE's own console output (no UART adapter).

### What each later component takes from this

| Component | Takes from 1.3 |
|---|---|
| C2 Foundation | new commands start at 17 (free); put them in one early block before the generic check at line 372 of `rpi3_can_spi_poc.c`; demand exact buffer sizes (44 B frame; 16 + 24n report) as the TA does; copy the buffer once before validating; report the needed size on SHORT_BUFFER; test output windows with a canary; keep the key out of any caller-visible output (no tag oracle) |
| C3 Core 1 integration | Core 1 is outside OP-TEE's call serialisation, so a new lock is needed between it and the PTA handlers; P2's FIQ stays untouched |
| C4 Timing | Core 1 loop pass is roughly 2.4 us (rough, to be measured with the secure timer) |
| C5 Heartbeat | request/response mechanics are proven (Task 4); the nonce commands 22 and 23 are new |
| C6 Hardening | remove or log cmds 3, 6, 7, 13, 14 (confirmed as the dangerous set); no caller authorisation exists to rely on |
| C7 Independent CAN | P2 and its known bug are not a foundation; chip B needs its own reader |

### Ledger for 1.3

| Task | Result |
|---|---|
| 1 Mechanism | PASS |
| 2 TA and its commands | PASS |
| 3 PTA and its commands | PASS |
| 4 Request and response | PASS (14/14) |
| 5 Shared memory | PASS (33/33) |
| 6 Path and limitations | PASS |

Tools produced: `tools/ta_roundtrip/`, `tools/shm_roundtrip/`. Corrections made during 1.3: a wrongly typed expected value (`0xb7b7b7dd`, correct `0xb791f3dd`); a wrong first conclusion about dynamic shared memory (a `libteec` fallback returned success); an always-pass check removed from the shared-memory tool.

## Open questions for Task 2 onward

- ~~Which of the 37 TAs belong to this project~~ resolved in Task 2: one.
- ~~`.ta` hash difference~~ resolved in Task 2 (same code, different signature).
- **Architecture statement to align (see below).**

## Alignment note: what the current system does versus the monitor design

The task description says: "Secure World does not directly access the CAN controller", "Secure World generates a heartbeat nonce", "Linux requests the nonce and sends its response", "Secure World verifies the response".

That describes the **Monitor design, Mode F** (components 2 to 6). It is not what the current Sep-20 image does:

| Statement | Current Step 2 image | Monitor design (Mode F) |
|---|---|---|
| Secure World does not access the CAN controller | **False today.** The PTA reads the chip over SPI0 (cmd 0, 6, 7, 14) and Core 1's loop runs the secure frame path (`rpi3_can_fiq_process_pending_frame`) fed by the CAN FIQ | True for chip A: the chip-A path stays dormant and is refused in the Monitor profile (R13); Linux forwards frames through new cmd 17 |
| Secure World generates a heartbeat nonce | **No nonce command exists** (PTA commands stop at 16). Core 1 has only a free-running counter (cmd 12) | Core 1 issues the request; cmd 22 returns the nonce; cmd 23 verifies the answer |
| Linux requests, answers, Secure World verifies | Only a challenge/response exists in the **TA** (`PROCESS_CHALLENGE`, `0x12345678 ^ 0xa5a5a5a5`) | cmds 22 and 23 |

Consequence for Task 4: it can verify a **real existing** request and response (TA challenge, PTA cmd 12 heartbeat), which proves the channel. It cannot verify the nonce heartbeat, because that does not exist yet; building it is Component 5.
