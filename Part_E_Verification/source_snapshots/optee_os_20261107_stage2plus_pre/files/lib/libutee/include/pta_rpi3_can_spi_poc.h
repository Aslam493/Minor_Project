/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Phase 1 proof-of-concept for the project's Secure Peripheral Ownership
 * work (KNOWLEDGE.md, roadmap Step 14): proves OP-TEE core can talk to
 * the MCP2518FD CAN chip directly over real SPI0 hardware, independent
 * of Linux's own mcp251xfd/spi_bcm2835 drivers. No interrupt/GIC changes
 * yet - purely a polled register read.
 */
#ifndef __PTA_RPI3_CAN_SPI_POC_H__
#define __PTA_RPI3_CAN_SPI_POC_H__

#define PTA_RPI3_CAN_SPI_POC_UUID \
	{ 0x9a986af1, 0x048a, 0x44a8, \
		{ 0xb9, 0x84, 0x1e, 0x90, 0x22, 0x90, 0xd6, 0x3c } }

/*
 * Reads the MCP2518FD's Oscillator Configuration Register (0xE00) on
 * SPI0 chip-select 0 (can0's chip) via a from-scratch, polled BCM2835
 * SPI0 driver running entirely in OP-TEE core.
 *
 * params[0].value.a (out): the 32-bit register value read.
 * A non-zero value matches the same success signature Linux's own
 * mcp251xfd driver already proved on can0 (see KNOWLEDGE.md).
 */
#define PTA_RPI3_CAN_SPI_POC_CMD_READ_OSC_REG 0

/*
 * Phase 2 (roadmap Step 15): returns how many times the CAN controller's
 * interrupt has reached Secure World directly (an FIQ handled entirely
 * in OP-TEE core, with zero Normal World involvement). Any non-zero
 * value proves the interrupt path works.
 *
 * params[0].value.a (out): the FIQ count.
 */
#define PTA_RPI3_CAN_SPI_POC_CMD_GET_FIQ_COUNT 1

/*
 * Debug only: reads back the VC interrupt controller's live FIQ_CONTROL
 * register value, to check whether the boot-time arming write in
 * rpi3_can_fiq.c actually reached the hardware.
 *
 * params[0].value.a (out): raw FIQ_CONTROL register value.
 */
#define PTA_RPI3_CAN_SPI_POC_CMD_READ_FIQ_CONTROL 2

/*
 * Arms FIQ routing for the CAN controller's interrupt (see
 * rpi3_can_fiq.c). The boot-time driver_init() arming attempt did not
 * take effect (read back as 0 - see KNOWLEDGE.md's eighth addendum for
 * why); this command performs the same write from a normal PTA
 * invoke_command context instead, which is proven to reach real
 * hardware correctly (same context already used for the SPI0/GPIO code
 * in Phase 1). Call this once after boot, before triggering CAN
 * traffic.
 */
#define PTA_RPI3_CAN_SPI_POC_CMD_ARM_FIQ 3

/*
 * Roadmap Steps 16/17: returns the last CAN frame Secure World read
 * directly from the chip's message RAM via SPI, from within the FIQ
 * handler - proving real frame content (not just an event count)
 * reaches Secure World with zero Normal World involvement.
 *
 * params[0].value.a (out): 1 if a frame was captured, 0 if not yet.
 * params[0].value.b (out): the frame's CAN ID.
 * params[1] (out, memref): 8 bytes of frame data.
 */
#define PTA_RPI3_CAN_SPI_POC_CMD_GET_LAST_FRAME 4

/*
 * Debug only: which FIFO index and raw message-RAM address the last
 * capture used, to diagnose the known Step 17 frame-content bug (see
 * KNOWLEDGE.md's ninth addendum).
 *
 * params[0].value.a (out): FIFO index used.
 * params[0].value.b (out): raw address read from FIFOUA.
 */
#define PTA_RPI3_CAN_SPI_POC_CMD_GET_DEBUG 5

/*
 * Temporary debug-only command (Step 17 planning): reads an arbitrary
 * MCP251XFD register address, used once to capture Linux's live
 * bit-timing/FIFO-config values before disabling its can0 binding, so
 * Secure World's own chip bring-up can reuse them verbatim instead of
 * re-deriving bit-timing math by hand. Not meant to stay long-term.
 *
 * params[0].value.a (in): 16-bit register address to read.
 * params[0].value.b (out): the 32-bit value read.
 */
#define PTA_RPI3_CAN_SPI_POC_CMD_READ_REG32 6

/*
 * Roadmap Steps 16/17: constructs and transmits a CAN frame directly via
 * SPI, entirely from Secure World - brings the chip up from scratch
 * (oscillator/bit-timing/FIFO config/RXIE, all normally done by Linux's
 * probe routine) if not already done, then transmits. Needed once
 * Linux's binding to can0 is removed - nothing else can inject a frame
 * onto the bus anymore.
 *
 * params[0] is VALUE_INOUT, not VALUE_INPUT: value.a (in) is the 32-bit
 * CAN ID, value.b (out) is 1 if transmit succeeded, 0 if not - both
 * directions share this one slot, so INPUT alone silently drops the
 * result across the Normal/Secure World boundary per the GlobalPlatform
 * TEE Client API spec (found the hard way - every earlier test run
 * reported FAIL regardless of what actually happened on the chip).
 * params[1] (in, memref): 8 bytes of frame data.
 */
#define PTA_RPI3_CAN_SPI_POC_CMD_TRANSMIT_TEST_FRAME 7

/*
 * Debug only: the RXIF bitmask and INT register value captured at the
 * moment of the last FIQ, for diagnosing why read_pending_rx_frame()
 * did or didn't find a match (KNOWLEDGE.md's ninth addendum).
 *
 * params[0].value.a (out): RXIF bitmask.
 * params[0].value.b (out): INT register value.
 */
#define PTA_RPI3_CAN_SPI_POC_CMD_GET_RXIF_DEBUG 8

/*
 * Debug only: the Bus Diagnostic Register 1 (BDIAG1) value captured at
 * the moment of the last FIQ - the only register that says *which* bus
 * error (DLCMM controller misconfiguration, NACKERR, form/stuff/CRC
 * error, ...) actually set IVMIF in CiINT, since IVMIF alone never says
 * why (see Linux's mcp251xfd_handle_ivmif()).
 *
 * params[0].value.a (out): raw BDIAG1 register value.
 */
#define PTA_RPI3_CAN_SPI_POC_CMD_GET_BDIAG1_DEBUG 9

/*
 * Debug only: which step inside can0_secure_transmit() last returned
 * false - 0 (last attempt succeeded, or none made yet), 1
 * (can0_secure_init()), 2 (FIFOUA read), 3/4/5/6 (the id/flags/data
 * writes to the TX message object), 7 (FIFOCON read), 8 (the final
 * FIFOCON write that sets TXREQ, issued right after FIQ routing is
 * (re-)armed - see arm_fiq_control()'s comment for why this exact step
 * is under suspicion of self-preemption).
 *
 * params[0].value.a (out): the fail-point code above.
 */
#define PTA_RPI3_CAN_SPI_POC_CMD_GET_TX_FAIL_DEBUG 10

/*
 * Debug only: GPIO 25's live electrical level (can0's /INT pin,
 * active-low) and GPEDS0's latched-event bit for that pin, read directly
 * from the GPIO peripheral - neither goes through the CAN chip/SPI at
 * all, so this isolates "is the pin itself asserted" from "does the chip
 * think it has something to report" (CiINT can show zero flags while
 * FIQs keep firing - see KNOWLEDGE.md's ninth addendum).
 *
 * params[0].value.a (out): raw GPLEV0 bit for GPIO 25 - 1 if the pin
 * currently reads electrically high (inactive), 0 if low (asserted,
 * since this pin is active-low).
 * params[0].value.b (out): raw GPEDS0 register value.
 */
#define PTA_RPI3_CAN_SPI_POC_CMD_GET_GPIO_DEBUG 11

/*
 * Debug only (Plan A hard core-pinning, Test A): reads the free-running
 * heartbeat counter incremented forever by core 1's permanent Secure-World
 * loop (rpi3_can_server_loop(), optee_os/core/arch/arm/plat-rpi3/rpi3_can_server.c).
 * A continuously-increasing value across repeated calls proves core 1 is
 * alive and running Secure World code, rather than hung or never reached.
 *
 * params[0].value.a (out): heartbeat's low 32 bits.
 * params[0].value.b (out): heartbeat's high 32 bits.
 */
#define PTA_RPI3_CAN_SPI_POC_CMD_GET_CORE1_HEARTBEAT 12

/*
 * Plan A hard core-pinning: invoked from Normal World userspace once boot is
 * fully complete, to trigger core 1's one-time, permanent wake-into-Secure-World
 * (rpi3_can_server_wake_core1(), optee_os/core/arch/arm/plat-rpi3/rpi3_can_server.c).
 * By design this must happen only after boot completes, never during the
 * boot chain itself - there is deliberately no automatic trigger for this.
 *
 * params[0].value.a (out): raw PSCI status code from psci_cpu_on() (0 =
 * success). A non-zero value (e.g. PSCI_E_ALREADY_ON if called twice) means
 * core 1 was NOT (re)woken - check this before assuming the heartbeat will
 * start incrementing.
 */
#define PTA_RPI3_CAN_SPI_POC_CMD_WAKE_CORE1 13

/*
 * Diagnostic only (Problem 3I - isolating whether a raw Message-RAM write
 * via the existing mcp251xfd_write32() actually persists, independent of
 * the full can0_secure_init()/can0_secure_transmit() sequence). Not meant
 * to stay long-term - same caveat as PTA_RPI3_CAN_SPI_POC_CMD_READ_REG32.
 *
 * params[0] is VALUE_INOUT, matching PTA_RPI3_CAN_SPI_POC_CMD_TRANSMIT_TEST_FRAME's
 * own rationale: value.a (in) is the 16-bit register/RAM address to write,
 * value.b (in) is the 32-bit value to write - then value.a is overwritten
 * with the result (1 = write succeeded, 0 = failed) before returning, since
 * VALUE_INPUT alone would silently drop that result across the boundary.
 */
#define PTA_RPI3_CAN_SPI_POC_CMD_WRITE_REG32 14

/*
 * Step 4.2 (Secure CAN Packet Validation): debug-only readback of the most
 * recent CAN-ID + per-packet-counter (+ timestamp, + HMAC as of Step
 * 4.3C/4.4C) validation outcome, computed entirely in Secure World in
 * secure_can_validate_frame()
 * (optee_os/core/arch/arm/plat-rpi3/rpi3_can_fiq.c). As of Step 4.4D that
 * function runs from rpi3_can_server_loop() (Core 1's ordinary execution,
 * rpi3_can_server.c), not from interrupt_main_handler()/FIQ context
 * anymore - see rpi3_can_fiq.c's top-of-file comment for why. Read-only -
 * Normal World cannot influence the validation decision through this or any
 * other command; this exists purely so test tooling can observe the
 * result.
 *
 * params[0].value.a (out): last validation status -
 *   0 = CAN0_VALIDATION_NONE (no frame validated yet since boot)
 *   1 = CAN0_VALIDATION_ACCEPTED
 *   2 = CAN0_VALIDATION_INVALID_ID
 *   3 = CAN0_VALIDATION_REPLAY (counter <= secure_last_counter)
 *   4 = CAN0_VALIDATION_STALE (Step 4.3C: timestamp age over policy)
 *   5 = CAN0_VALIDATION_HMAC_FAIL (Step 4.4C: received tag != recomputed tag)
 * params[0].value.b (out): current secure_last_counter (the highest counter
 *   value accepted so far this boot - 0 if none accepted yet since boot;
 *   see rpi3_can_fiq.c's own comment on why this does NOT persist across
 *   reboots).
 * params[1] (MEMREF_OUTPUT, 12 bytes): [0..3] = CAN ID of the most recently
 *   *attempted* validation (accepted or rejected), [4..7] = its counter
 *   value, [8..11] = Step 4.4D's dropped-frame count (FIQ events seen while
 *   the single-slot secure frame buffer was still busy - see
 *   can0_frame_dropped_count's own comment in rpi3_can_fiq.c), all
 *   little-endian uint32_t.
 */
#define PTA_RPI3_CAN_SPI_POC_CMD_GET_VALIDATION_DEBUG 15

/*
 * Step 4.3B (Secure CAN Timestamp Capture): debug-only readback of the
 * MCP2518FD Time Base Counter state - the last RX frame's captured
 * timestamp (RXMSGTS, cached from the most recent FIQ capture), plus a
 * LIVE read of CiTBC (the free-running counter itself) and CiTSCON (its
 * control register, to confirm TBCEN/TBCPRE). This command does NOT
 * decide freshness/validity - it only observes state; see
 * rpi3_can_fiq.c's own comments for why a freshness policy is not yet
 * defined.
 *
 * params[0].value.a (out): last captured RX frame's timestamp (RXMSGTS),
 *   0 if no frame captured yet since boot.
 * params[0].value.b (out): live CiTBC value at the moment of this call
 *   (NOT the value at RX time - this changes on every call while the
 *   counter is running).
 * params[1] (MEMREF_OUTPUT, 4 bytes): live CiTSCON value, little-endian
 *   uint32_t - bit 16 is TBCEN, bits 9:0 are TBCPRE.
 */
#define PTA_RPI3_CAN_SPI_POC_CMD_GET_TIMESTAMP_DEBUG 16

#endif /* __PTA_RPI3_CAN_SPI_POC_H__ */
