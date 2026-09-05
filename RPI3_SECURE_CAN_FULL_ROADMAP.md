# Minor Project Roadmap

## Secure Automotive Event Logging Using ARM TrustZone and OP-TEE on Raspberry Pi 3

**Roadmap date:** 30 August 2026  
**Target:** Raspberry Pi 3 Model B / Model B+  
**Design status:** Professor-approved secure-first packet flow; the packet movement must be preserved.

---

## 1. Final Project Objective

The project will demonstrate a TrustZone/OP-TEE-based secure CAN reception, validation, classification, and tamper-evident logging pipeline.

The required packet movement is:

```text
Sender ECU
    ↓
CAN bus
    ↓
External CAN controller + transceiver
    ↓
Secure-designated GPIO interrupt
    ↓
OP-TEE secure interrupt handler
    ↓
Secure SPI/CAN receive driver
    ↓
Secure RAM
    ↓
Packet validation
    ├── CAN-ID policy
    ├── HMAC authentication
    ├── Counter/freshness check
    ├── Replay detection
    └── Rate/flood detection
    ↓
Packet classification
    ├── Critical → secure log/secure handling
    └── Normal   → shared memory → Normal World
    ↓
Tamper-evident secure event log
```

Linux must not own or read the selected CAN controller before Secure World validation. Only accepted normal packets or their approved result may be released to Linux.

## 2. Platform Qualification

The Raspberry Pi 3 implementation is an educational/research prototype. It can demonstrate the approved control flow, TrustZone world switches, OP-TEE execution, protected key use, validation policy, and hash-chained logging. It must not be described as production-grade automotive hardware isolation because the Raspberry Pi 3 OP-TEE port lacks the secure peripheral, secure memory, and secure-boot protections of STM32MP157.

This qualification changes the strength of the security claim, not the approved packet movement.

---

# PART A — COMPLETED WORK

## Step 1 — Build OP-TEE OS: COMPLETE

Verified outputs include:

```text
optee_os/out/arm/core/tee.bin
optee_os/out/arm/core/tee.elf
optee_os/out/arm/core/tee-header_v2.bin
optee_os/out/arm/core/tee-pager_v2.bin
optee_os/out/arm/core/tee-pageable_v2.bin
```

OP-TEE is configured as TF-A's BL32 Secure World payload.

**What this proves:** OP-TEE compiled successfully.  
**What it does not prove:** OP-TEE has run successfully on the physical Pi.

## Step 2 — Build U-Boot and TF-A: COMPLETE

Verified outputs include:

```text
u-boot/u-boot.bin
trusted-firmware-a/build/rpi3/debug/fip.bin
trusted-firmware-a/build/rpi3/debug/armstub8.bin
```

The packaging relationship is:

```text
armstub8.bin = padded TF-A BL1 + fip.bin

fip.bin contains:
├── BL32: OP-TEE
└── BL33: U-Boot
```

**What this proves:** TF-A, OP-TEE, and U-Boot were compiled and packaged.  
**What it does not prove:** The complete chain boots on the board.

## Step 3 — Build ARM64 Linux and DTBs: COMPLETE

Verified outputs include:

```text
linux/.config
linux/arch/arm64/boot/Image
linux/arch/arm64/boot/dts/broadcom/bcm2710-rpi-3-b.dtb
linux/arch/arm64/boot/dts/broadcom/bcm2710-rpi-3-b-plus.dtb
```

Verified kernel settings:

```text
CONFIG_TEE=y
CONFIG_OPTEE=y
```

**What this proves:** Linux contains OP-TEE client-driver support.  
**What it does not prove:** A functioning Secure World exists at runtime.

## Current Completion Boundary

```text
OP-TEE build       ✅
TF-A build         ✅
U-Boot build       ✅
Linux build        ✅
Pi 3 DTBs          ✅
Root filesystem    ✅
OP-TEE user space  ✅
Boot packaging     ✅
SD-card image      ✅
Physical boot      ❌ ← START HERE
OP-TEE runtime     ❌
Secure CAN path    ❌
Validation         ❌
Secure logging     ❌
Testing            ❌
```

Do not rebuild Linux unless a later driver, configuration, or device-tree change requires it.

---

# PART B — PLATFORM BOOT AND OP-TEE VERIFICATION

## Step 4 — Confirm the Authoritative Build Tree: COMPLETE

Before packaging, determine which machine contains the complete checkout. The existing notes identify the complete tree as:

```text
/home/aslam/Minor/optee_project
```

The laptop directory currently documents/copied the Linux build under:

```text
/home/aslam/Minor_Project/optee_project
```

### Tasks

1. Confirm that the authoritative tree contains `build`, `buildroot`, `firmware`, `optee_os`, `optee_client`, `optee_test`, `optee_examples`, `trusted-firmware-a`, `u-boot`, and `linux`.
2. Record Git revisions of every component.
3. Back up the existing successful binaries and their SHA-256 checksums.
4. Inspect `build/rpi3.mk` and use its supported packaging targets.

### Completion test

- [x] One complete source tree is selected and documented.
- [x] All required component outputs are present and backed up.
- [x] Component revisions and artifact checksums are recorded.
- [x] Supported packaging targets were verified from `build/rpi3.mk`.

Completion evidence is in `Step4_Verification/STEP4_COMPLETION.md`.

## Step 5 — Build Root Filesystem and User-Space OP-TEE Components: COMPLETE

### Required contents

```text
Linux root filesystem
├── tee-supplicant
├── libteec
├── xtest
├── OP-TEE example host programs
└── matching Trusted Applications
```

### Tasks

1. [x] Build the Buildroot/root filesystem using the RPi3 build configuration.
2. [x] Confirm that `tee-supplicant`, `xtest`, client libraries, and TAs are installed.
3. [x] Verify that the Linux kernel modules or built-in drivers match the kernel Image.

### Completion test

- [x] Root filesystem exists.
- [x] `tee-supplicant`, `xtest`, and example TAs exist in their expected locations.
- [x] No missing shared-library dependency is reported.

Completion evidence is in `Step5_Verification/STEP5_COMPLETION.md`. Continue with Step 6.

## Step 6 — Assemble the Raspberry Pi Boot Files: COMPLETE

Use the project's `rpi3.mk` target to assemble the boot directory. Expected categories include:

```text
boot/
├── Raspberry Pi firmware files
├── config.txt
├── armstub8.bin
├── Linux kernel image
├── Raspberry Pi 3 DTBs
├── U-Boot environment/configuration
└── required overlays or boot scripts
```

Do not copy `fip.bin` separately if the selected TF-A build already combines it into `armstub8.bin`.

### Completion test

- [x] Build target finishes without error.
- [x] Boot directory and root filesystem are complete.
- [x] File checksums are recorded.

Completion evidence is in `Step6_Verification/STEP6_COMPLETION.md`. Continue with Step 7.

## Step 7 — Generate and Write the SD-Card Image: COMPLETE ON 8 GB TEST CARD

### Safety tasks

1. Generate `rpi3-sdcard.img` through the supported build target.
2. Insert the target SD card.
3. Identify the exact block device using read-only checks.
4. Confirm the device and capacity before writing.
5. Write the image and flush it safely.

### Completion test

- [x] Boot and root partitions are readable after writing.
- [x] Required files exist on the SD card.
- [x] The SD card can be cleanly unmounted.

Completion evidence is in `Step7_Verification/STEP7_COMPLETION.md`. Continue with Step 8.

## Step 8 — Capture the First Serial Boot

A USB-to-TTL UART adapter is strongly recommended because early TF-A and OP-TEE messages may not appear over HDMI.

### Required evidence

Capture a complete log showing:

```text
Raspberry Pi firmware
    ↓
TF-A
    ↓
OP-TEE initialization
    ↓
U-Boot
    ↓
Linux
```

### Completion test

- Linux reaches a shell.
- `uname -m` reports `aarch64`.
- TF-A and OP-TEE initialization are visible in the serial log.
- Boot log is saved as project evidence.

## Step 9 — Verify Linux-to-OP-TEE Communication

Run and record:

```bash
uname -a
uname -m
ls -l /dev/tee*
dmesg | grep -i -E 'optee|tee'
ps | grep tee-supplicant
xtest
```

### Completion test

- `/dev/tee0` and the expected privileged TEE device exist.
- `tee-supplicant` runs.
- OP-TEE driver successfully exchanges SMC calls with Secure World.
- Core `xtest` tests pass or every failure is documented and understood.

## Step 10 — Build and Run a Minimal Custom TA

Create a project-specific Hello World pair:

```text
Normal-world host application
        ↓ TEEC_InvokeCommand
OP-TEE Trusted Application
        ↓
returns known response
```

### Completion test

- Session opens successfully.
- Request and response cross the world boundary correctly.
- TA debug output and client output are recorded.

This is the gate before any CAN-specific secure driver development.

---

# PART C — CAN HARDWARE AND SECURE-FIRST RECEPTION

## Step 11 — Freeze the CAN Hardware Specification

Select and document:

1. External CAN controller model.
2. CAN transceiver model.
3. Classical CAN or CAN FD.
4. SPI bus, chip-select, clock rate, and mode.
5. GPIO interrupt pin and electrical polarity.
6. Sender/test ECU hardware.
7. Bus bitrate and required 120-ohm termination.

### Recommended direction

Use an SPI CAN-FD controller such as MCP2518FD when the authenticated payload must contain application data, counter, timestamp, and a useful HMAC tag. If MCP2515 is used, define a safe multi-frame protocol or an explicitly justified truncated tag.

### Deliverable

A one-page hardware specification and wiring diagram.

## Step 12 — Define the Exact Authenticated Packet Format

Before implementing validation, freeze the bytes included in the authentication calculation.

Example logical structure:

```text
protocol_version
sender_id
can_id
message_type
counter
timestamp_or_freshness_value
payload_length
payload
hmac_tag
```

Define:

- Byte order.
- Field sizes.
- HMAC algorithm, preferably HMAC-SHA-256.
- Full or truncated tag length.
- Exact bytes covered by HMAC.
- Maximum payload.
- Counter rollover behavior.
- Malformed-frame behavior.
- Multi-frame reassembly rules, if applicable.

### Completion test

- A test vector has fixed input bytes, key, and expected HMAC.
- Sender and validator independently produce the same HMAC.
- No field has an ambiguous encoding.

## Step 13 — Bring Up CAN Temporarily in Linux

This is a hardware-only diagnostic stage, not the final architecture.

### Tasks

1. Connect the controller, transceiver, termination, and sender.
2. Temporarily use the Linux SPI/SocketCAN driver.
3. Verify receive, transmit, bitrate, interrupt, error counters, and bus termination.
4. Record SPI controller addresses, GPIO pin, interrupt identity, and controller register behavior.

### Completion test

- Known CAN frames are transmitted and received reliably.
- No unexplained bus-off, CRC, overflow, or termination errors remain.

After this test, disable Linux ownership of the selected CAN device for the final secure-first architecture.

## Step 14 — Define Secure Peripheral Ownership

The final ownership policy must be:

```text
Resource                     Owner
External CAN controller      Secure World
Selected SPI controller      Secure World
CAN interrupt GPIO           Secure World
Secure RX buffer             Secure World
HMAC keys                    Secure World
Validation state             Secure World
Shared result buffer         Controlled shared memory
Linux SocketCAN driver       Disabled for this controller
```

### Tasks

1. Remove/disable the normal-world CAN device-tree node.
2. Prevent Linux from binding the selected SPI controller or CAN device.
3. Reserve required MMIO and memory areas.
4. Document which limitations are enforced by hardware and which are prototype software policy on RPi3.

### Completion test

- Linux cannot bind to or directly read the selected CAN controller.
- OP-TEE retains access to the necessary registers.

## Step 15 — Implement Secure GPIO Interrupt Handling

### Tasks

1. Identify the interrupt generated by the selected GPIO.
2. Configure its routing/handling in the RPi3 TF-A/OP-TEE platform code.
3. Register an OP-TEE interrupt handler.
4. Acknowledge the GPIO and CAN-controller interrupt correctly.
5. Count interrupts in Secure World.
6. Test repeated interrupts, simultaneous frames, and interrupt storms.

### Completion test

- An external CAN interrupt produces an OP-TEE trace before Linux sees any frame.
- No continuous unacknowledged interrupt loop occurs.
- Interrupt count matches controlled test traffic.

## Step 16 — Implement a Secure SPI Controller Driver

### Minimum functions

```text
secure_spi_init()
secure_spi_select()
secure_spi_transfer()
secure_spi_deselect()
secure_spi_recover()
```

### Tasks

1. Map SPI MMIO into OP-TEE.
2. Configure pins, clock, SPI mode, and chip select.
3. Implement bounded polling/timeouts.
4. Read a fixed CAN-controller identification/configuration register.
5. Ensure Linux cannot simultaneously access the controller.

### Completion test

- OP-TEE can repeatedly read and write known CAN-controller registers.
- Timeout and device-absent cases do not hang Secure World.

## Step 17 — Implement the Secure CAN Receive Driver

### Minimum functions

```text
can_secure_init()
can_secure_read_status()
can_secure_read_frame()
can_secure_clear_interrupt()
can_secure_read_errors()
can_secure_recover_overflow()
```

### Secure interrupt path

Keep the top half short:

```text
Interrupt
   ↓
Acknowledge/capture minimum state
   ↓
Queue secure processing
   ↓
Read complete frame
   ↓
Validate
```

### Completion test

- Secure World reads correct ID, flags, length, and payload.
- Multiple queued frames are drained correctly.
- RX overflow and malformed frame conditions are detected and logged.
- Linux still cannot receive unvalidated frames.

## Step 18 — Implement Secure RAM Queues

Use a bounded ring buffer for received frames and a separate controlled shared buffer for released results.

Define:

- Queue capacity.
- Maximum frame size.
- Producer/consumer rules.
- Overflow policy.
- Locking/concurrency rules.
- Maximum processing time.

### Completion test

- Normal load causes no loss.
- Deliberate overload triggers a controlled overflow event rather than memory corruption.
- Bounds tests reject oversized or malformed input.

---

# PART D — SECURE VALIDATION

## Step 19 — Key Provisioning and Secure Key Storage

Define whether the prototype uses one key per sender ECU or one shared demonstration key. Per-ECU keys are preferred.

### Tasks

1. Create a provisioning operation separate from normal validation.
2. Generate/import the HMAC key.
3. Store it through OP-TEE secure storage.
4. Associate key identifiers with sender/ECU identities.
5. Ensure normal Linux never receives plaintext key material during normal operation.
6. Define reprovisioning and key-replacement behavior.

### Completion test

- TA can reopen the stored key after reboot.
- Linux client cannot retrieve the plaintext key through the normal API.
- Wrong-key test vectors fail.

## Step 20 — CAN-ID and Sender Policy

Create a secure policy table containing:

```text
sender/key ID
allowed CAN IDs
criticality class
allowed rate
payload length/range policy
```

### Completion test

- Allowed ID passes.
- Unknown ID fails.
- A known sender using an unauthorized ID fails.
- Every rejection produces a reason code and log entry.

## Step 21 — HMAC Authentication and Integrity

Canonicalize the packet exactly as defined in Step 12, calculate HMAC inside Secure World, and compare tags in constant time.

HMAC provides both sender authentication under the shared-key model and packet integrity. Do not implement a duplicate undefined packet-tamper check.

### Completion test

- Correct tag passes.
- Modified ID, counter, timestamp, length, or payload fails.
- Modified tag fails.
- Malformed or truncated packet fails safely.

## Step 22 — Counter and Replay Detection

Maintain replay state per sender/key and, where necessary, per CAN-ID.

Define:

- Initial counter.
- Strict increment or replay window.
- Counter rollover.
- Reboot recovery.
- Resynchronization policy.
- Persistence frequency.

### Completion test

Test sequences such as:

```text
10, 11, 12       → accept
12 again         → replay reject
11 after 12      → stale reject
large jump       → policy-dependent reject/resync
```

## Step 23 — Timestamp/Freshness Validation

If the approved design requires timestamps, explicitly define the time source and its trust limitation. Because Raspberry Pi 3 normally has no battery-backed trusted RTC, the secure counter remains the primary replay defence unless trusted time hardware is added.

### Completion test

- Current timestamp within the permitted window passes.
- Stale and far-future timestamps fail.
- Clock-unavailable and clock-reset cases have defined behavior.

## Step 24 — Rate and Flood Detection

Maintain rate state per sender and CAN-ID using bounded Secure World memory.

### Tasks

1. Define fixed-window, sliding-window, or token-bucket logic.
2. Configure allowed rates per message class.
3. Detect excessive traffic.
4. Rate-limit repetitive attack logging.
5. Ensure malicious traffic cannot cause unbounded secure allocation.

### Completion test

- Normal rate passes.
- Threshold crossing is detected.
- Sustained flooding does not crash or exhaust Secure World.

The report should claim flood detection and controlled response, not prevention of physical CAN-bus occupation.

## Step 25 — Classification and Decision Engine

Produce a single structured decision:

```text
decision: ACCEPT / REJECT
reason: VALID / ID / HMAC / COUNTER / REPLAY / TIME / RATE / FORMAT
class: CRITICAL / NORMAL
secure_sequence_number
```

### Policy

- Invalid frames: discard and securely log.
- Valid critical frames: securely log and execute the defined secure handling policy.
- Valid normal frames: securely log as required, then release through controlled shared memory.

### Completion test

Every possible validation outcome maps to one deterministic decision and log reason.

---

# PART E — NORMAL-WORLD RELEASE AND SECURE LOGGING

## Step 26 — Implement the Secure-to-Normal Release Path

Define a sanitized message structure containing only approved data and the secure decision metadata.

### Tasks

1. Allocate controlled shared memory.
2. Copy only validated normal frames into it.
3. Add sequence number and length checks.
4. Notify the Normal World.
5. Prevent Linux from modifying Secure World policy/state through shared memory.

### Completion test

- Accepted normal packet reaches the Linux application.
- Invalid packet never reaches the normal application as an accepted frame.
- Modifying shared memory cannot change Secure World validation state.

Normal-world rechecking may be included as diagnostic redundancy, but it must not be called trusted ground-truth enforcement when Linux is inside the threat model.

## Step 27 — Define the Canonical Secure Log Entry

Example entry:

```text
log_version
secure_sequence_number
sender_id
can_id
counter
timestamp/freshness
payload_digest
decision
reason
previous_hash
entry_hash
```

Use one consistent formula:

```text
H_i = SHA-256(H_(i-1) || canonical_entry_i)
```

Define byte order, field sizes, initial hash, and serialization rules.

## Step 28 — Implement Tamper-Evident Secure Logging

### Tasks

1. Create the initial chain state.
2. Append each security event.
3. Store log data through the selected OP-TEE storage backend.
4. Protect the latest chain head and sequence number.
5. Verify the complete chain.
6. Handle power loss and partial writes.
7. Define storage-full behavior.

### Completion test

- Clean chain verifies.
- Modified historical entry fails verification.
- Deleted middle entry fails verification.
- Reordered entries fail verification.
- Truncated/rolled-back log is detected where the available storage design permits it.

The result must be called tamper-evident, not absolutely tamper-proof.

---

# PART F — TESTING, MEASUREMENT, AND DEMONSTRATION

## Step 29 — Functional Test Matrix

| Test | Input | Expected result |
|---|---|---|
| Valid frame | Correct ID, HMAC, counter, freshness, rate | Accept |
| Unknown ID | ID absent from policy | Reject: ID |
| Wrong HMAC | Modified data or tag | Reject: HMAC |
| Replay | Previously accepted counter/tag | Reject: REPLAY |
| Old counter | Counter below permitted state | Reject: COUNTER |
| Stale time | Outside freshness window | Reject: TIME |
| Flood | Rate above policy | Reject/flag: RATE |
| Malformed frame | Invalid length/encoding | Reject: FORMAT |
| Log modification | Change stored entry | Chain verification failure |
| RX overflow | More frames than queue capacity | Controlled overflow log |

Record inputs, expected outputs, actual outputs, timestamps, logs, and pass/fail status.

## Step 30 — Security and Fault Tests

Test:

- Linux attempt to access secure CAN SPI device.
- Linux attempt to retrieve HMAC key.
- Shared-memory modification.
- Reboot followed by replay.
- Power loss during log update.
- Controller disconnection.
- Stuck interrupt.
- Corrupted CAN-controller status.
- Secure queue exhaustion.
- Storage exhaustion.

Document which protections are architectural and which cannot be hardware-enforced by Raspberry Pi 3.

## Step 31 — Performance Evaluation

Measure at minimum:

```text
Interrupt-to-secure-read latency
HMAC verification time
Complete validation time
Hash-chain update time
Secure storage write time
End-to-end accepted-packet latency
Maximum sustainable frame rate
Dropped/overflowed frames
CPU utilization
Secure memory usage
```

Compare:

```text
Baseline diagnostic Linux CAN reception
                 versus
Approved secure-first OP-TEE pipeline
```

Do not claim “small” or “acceptable” overhead until these measurements exist.

## Step 32 — Final Demonstration

The demonstration should show:

1. Complete TF-A → OP-TEE → U-Boot → Linux boot log.
2. Working OP-TEE client/TA communication.
3. External CAN controller generating a Secure World interrupt.
4. Linux unable to see the raw unvalidated controller path.
5. Valid packet accepted and released according to classification.
6. Invalid ID rejected.
7. Modified packet/HMAC rejected.
8. Replay rejected.
9. Flood detected.
10. Tampered log detected by hash-chain verification.
11. Performance results displayed in a table or graph.

---

# PART G — REPORT REVISION PLAN

## Step 33 — Update the Existing Report Without Changing Its Approved Core Design

Revise these items:

1. Change the implementation platform from STM32MP157 to Raspberry Pi 3 where describing actual experiments.
2. Add the external CAN controller, SPI connection, and GPIO interrupt to every architecture diagram.
3. Preserve the secure-first packet movement.
4. State that secure GPIO/SPI/CAN handling is custom implementation work.
5. Qualify Raspberry Pi 3 as an educational prototype, not production-secure hardware.
6. Replace unsupported Secure Boot claims with measured/implemented facts.
7. Define the actual OP-TEE storage backend instead of saying only “Secure Flash.”
8. Use “tamper-evident,” not “tamper-proof.”
9. Remove duplicate undefined packet-tamper validation because HMAC supplies packet integrity.
10. Define the exact CAN/CAN-FD authenticated packet layout.
11. Clarify that Normal World rechecking is diagnostic redundancy, not trusted enforcement.
12. Use one canonical hash-chain formula.
13. Replace predicted performance claims with measured results.
14. Clearly separate the approved ideal security architecture from Raspberry Pi 3 prototype limitations.

---

# Immediate Work Queue

Do these tasks next, in this exact order:

```text
NOW  1. Confirm the complete authoritative OP-TEE build tree
     2. Inspect the supported RPi3 packaging targets
     3. Build the root filesystem and OP-TEE user-space tools
     4. Generate the complete SD-card image
     5. Boot through serial UART
     6. Verify /dev/tee*, tee-supplicant, and xtest
     7. Run a custom Hello World TA
     8. Freeze the external CAN hardware selection
     9. Freeze the authenticated packet format
    10. Begin secure interrupt/SPI/CAN driver development
```

Do not begin HMAC, replay, classification, or logging code until Steps 4–10 prove that OP-TEE boots and a custom TA communicates successfully.

## Next Milestone

The next milestone is not CAN validation. It is:

```text
Physical Raspberry Pi boot
          ↓
OP-TEE runtime confirmed
          ↓
/dev/tee0 available
          ↓
tee-supplicant running
          ↓
xtest operational
          ↓
custom Hello World TA passes
```

After that milestone, the project moves to the secure-first external CAN reception path.

