// SPDX-License-Identifier: BSD-2-Clause
/*
 * Standalone test client for the rpi3_can_spi_poc pseudo-TA.
 *
 * Default: Phase 1 (roadmap Step 14) - proves OP-TEE core can read the
 * MCP2518FD's Oscillator Configuration Register directly over real
 * SPI0 hardware, independent of Linux's own CAN drivers.
 *
 * --fiq-count: Phase 2 (roadmap Step 15) - reports how many times the
 * CAN controller's interrupt has reached Secure World directly (an FIQ
 * handled entirely in OP-TEE core, with zero Normal World involvement).
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <tee_client_api.h>
#include <pta_rpi3_can_spi_poc.h>

int main(int argc, char *argv[])
{
	bool fiq_mode = (argc > 1) && (strcmp(argv[1], "--fiq-count") == 0);
	bool fiq_control_mode = (argc > 1) &&
				(strcmp(argv[1], "--fiq-control") == 0);
	bool arm_fiq_mode = (argc > 1) && (strcmp(argv[1], "--arm-fiq") == 0);
	bool last_frame_mode = (argc > 1) && (strcmp(argv[1], "--last-frame") == 0);
	bool debug_mode = (argc > 1) && (strcmp(argv[1], "--debug") == 0);
	bool read_reg_mode = (argc > 2) && (strcmp(argv[1], "--read-reg") == 0);
	bool write_reg_mode = (argc > 3) && (strcmp(argv[1], "--write-reg") == 0);
	bool transmit_mode = (argc > 2) && (strcmp(argv[1], "--transmit") == 0);
	bool rxif_debug_mode = (argc > 1) && (strcmp(argv[1], "--rxif-debug") == 0);
	bool bdiag1_debug_mode = (argc > 1) && (strcmp(argv[1], "--bdiag1-debug") == 0);
	bool tx_fail_debug_mode = (argc > 1) && (strcmp(argv[1], "--tx-fail-debug") == 0);
	bool gpio_debug_mode = (argc > 1) && (strcmp(argv[1], "--gpio-debug") == 0);
	bool core1_heartbeat_mode = (argc > 1) &&
				     (strcmp(argv[1], "--core1-heartbeat") == 0);
	bool wake_core1_mode = (argc > 1) &&
				(strcmp(argv[1], "--wake-core1") == 0);
	/* Step 4.2: send a controlled test packet with an explicit counter +
	 * 4-byte payload, instead of --transmit's fixed {0,1,2,3,4,5,6,7}.
	 * Usage: --transmit-secure <id> <counter> <payload_hex8>
	 * e.g.:  --transmit-secure 0x123 1 AABBCCDD
	 */
	bool transmit_secure_mode = (argc > 4) &&
				     (strcmp(argv[1], "--transmit-secure") == 0);
	bool validation_debug_mode = (argc > 1) &&
				      (strcmp(argv[1], "--validation-debug") == 0);
	bool timestamp_debug_mode = (argc > 1) &&
				     (strcmp(argv[1], "--timestamp-debug") == 0);
	TEEC_UUID uuid = PTA_RPI3_CAN_SPI_POC_UUID;
	TEEC_Context context;
	TEEC_Session session;
	TEEC_Operation operation;
	TEEC_Result result;
	uint32_t origin = 0;
	uint32_t cmd;

	result = TEEC_InitializeContext(NULL, &context);
	if (result != TEEC_SUCCESS) {
		fprintf(stderr, "TEEC_InitializeContext failed: 0x%08" PRIx32 "\n",
			result);
		return 1;
	}

	result = TEEC_OpenSession(&context, &session, &uuid, TEEC_LOGIN_PUBLIC,
				  NULL, NULL, &origin);
	if (result != TEEC_SUCCESS) {
		fprintf(stderr, "TEEC_OpenSession failed: 0x%08" PRIx32
			" origin=0x%08" PRIx32 "\n", result, origin);
		TEEC_FinalizeContext(&context);
		return 1;
	}

	memset(&operation, 0, sizeof(operation));

	if (read_reg_mode) {
		unsigned long addr = strtoul(argv[2], NULL, 0);

		operation.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_INOUT,
							TEEC_NONE, TEEC_NONE,
							TEEC_NONE);
		operation.params[0].value.a = (uint32_t)addr;

		result = TEEC_InvokeCommand(&session,
				PTA_RPI3_CAN_SPI_POC_CMD_READ_REG32,
				&operation, &origin);
		TEEC_CloseSession(&session);
		TEEC_FinalizeContext(&context);
		if (result != TEEC_SUCCESS) {
			fprintf(stderr, "TEEC_InvokeCommand failed: 0x%08" PRIx32
				" origin=0x%08" PRIx32 "\n", result, origin);
			return 1;
		}
		printf("reg 0x%04" PRIx32 " = 0x%08" PRIx32 "\n",
		       operation.params[0].value.a, operation.params[0].value.b);
		return 0;
	}

	if (write_reg_mode) {
		unsigned long addr = strtoul(argv[2], NULL, 0);
		unsigned long value = strtoul(argv[3], NULL, 0);

		operation.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_INOUT,
							TEEC_NONE, TEEC_NONE,
							TEEC_NONE);
		operation.params[0].value.a = (uint32_t)addr;
		operation.params[0].value.b = (uint32_t)value;

		result = TEEC_InvokeCommand(&session,
				PTA_RPI3_CAN_SPI_POC_CMD_WRITE_REG32,
				&operation, &origin);
		TEEC_CloseSession(&session);
		TEEC_FinalizeContext(&context);
		if (result != TEEC_SUCCESS) {
			fprintf(stderr, "TEEC_InvokeCommand failed: 0x%08" PRIx32
				" origin=0x%08" PRIx32 "\n", result, origin);
			return 1;
		}
		if (!operation.params[0].value.a) {
			printf("FAIL: write to reg 0x%04lx did not succeed\n", addr);
			return 1;
		}
		printf("PASS: wrote 0x%08lx to reg 0x%04lx\n", value, addr);
		return 0;
	}

	if (transmit_mode) {
		unsigned long id = strtoul(argv[2], NULL, 0);
		uint8_t data[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };

		/* INOUT, not INPUT: params[0].a carries the CAN ID in, but
		 * params[0].b carries the PTA's success/fail result back out
		 * through that same slot - a pure INPUT parameter is not
		 * guaranteed to be copied back across the Normal/Secure World
		 * boundary, which silently discarded every previous "ok"
		 * result and made this command always report FAIL regardless
		 * of what actually happened on the chip.
		 */
		operation.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_INOUT,
							TEEC_MEMREF_TEMP_INPUT,
							TEEC_NONE, TEEC_NONE);
		operation.params[0].value.a = (uint32_t)id;
		operation.params[1].tmpref.buffer = data;
		operation.params[1].tmpref.size = sizeof(data);

		result = TEEC_InvokeCommand(&session,
				PTA_RPI3_CAN_SPI_POC_CMD_TRANSMIT_TEST_FRAME,
				&operation, &origin);
		TEEC_CloseSession(&session);
		TEEC_FinalizeContext(&context);
		if (result != TEEC_SUCCESS) {
			fprintf(stderr, "TEEC_InvokeCommand failed: 0x%08" PRIx32
				" origin=0x%08" PRIx32 "\n", result, origin);
			return 1;
		}
		if (!operation.params[0].value.b) {
			printf("FAIL: secure-world transmit did not succeed\n");
			return 1;
		}
		printf("PASS: transmitted id=0x%03lx directly from Secure World over real SPI0\n",
		       id);
		return 0;
	}

	if (last_frame_mode) {
		uint8_t data[8] = { 0 };

		operation.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_OUTPUT,
							TEEC_MEMREF_TEMP_OUTPUT,
							TEEC_NONE, TEEC_NONE);
		operation.params[1].tmpref.buffer = data;
		operation.params[1].tmpref.size = sizeof(data);

		result = TEEC_InvokeCommand(&session,
				PTA_RPI3_CAN_SPI_POC_CMD_GET_LAST_FRAME,
				&operation, &origin);
		if (result != TEEC_SUCCESS) {
			fprintf(stderr, "TEEC_InvokeCommand failed: 0x%08" PRIx32
				" origin=0x%08" PRIx32 "\n", result, origin);
			TEEC_CloseSession(&session);
			TEEC_FinalizeContext(&context);
			return 1;
		}
		TEEC_CloseSession(&session);
		TEEC_FinalizeContext(&context);

		if (!operation.params[0].value.a) {
			printf("No frame captured yet by Secure World.\n");
			return 1;
		}
		printf("Frame captured directly by Secure World: id=0x%03" PRIx32
		       " data=", operation.params[0].value.b);
		for (unsigned int i = 0; i < sizeof(data); i++)
			printf("%02x ", data[i]);
		printf("\n");
		return 0;
	}

	if (transmit_secure_mode) {
		/* Step 4.2 test packet: data[0..3] = counter (little-endian),
		 * data[4..7] = 4-byte application payload. Reuses the exact
		 * same TRANSMIT_TEST_FRAME command as --transmit above - only
		 * the client-side data construction differs, no PTA/Secure
		 * World transmit-path change needed for this. As of Step 4.4C,
		 * these same 8 bytes (id + this data) are also the canonical
		 * HMAC input on the Secure World side - the CAN-FD framing,
		 * HMAC computation, and padding are all constructed inside
		 * can0_secure_transmit() from exactly this 8-byte input, so
		 * this client-side construction did not need to change.
		 */
		unsigned long id = strtoul(argv[2], NULL, 0);
		unsigned long counter = strtoul(argv[3], NULL, 0);
		const char *payload_hex = argv[4];
		uint8_t data[8] = { 0 };
		size_t i;

		if (strlen(payload_hex) != 8) {
			fprintf(stderr, "payload must be exactly 8 hex chars "
					"(4 bytes), e.g. AABBCCDD\n");
			return 1;
		}
		for (i = 0; i < 4; i++) {
			char byte_str[3] = { payload_hex[i * 2],
					      payload_hex[i * 2 + 1], 0 };

			data[4 + i] = (uint8_t)strtoul(byte_str, NULL, 16);
		}
		data[0] = (uint8_t)counter;
		data[1] = (uint8_t)(counter >> 8);
		data[2] = (uint8_t)(counter >> 16);
		data[3] = (uint8_t)(counter >> 24);

		/* Same INOUT rationale as --transmit above. */
		operation.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_INOUT,
							TEEC_MEMREF_TEMP_INPUT,
							TEEC_NONE, TEEC_NONE);
		operation.params[0].value.a = (uint32_t)id;
		operation.params[1].tmpref.buffer = data;
		operation.params[1].tmpref.size = sizeof(data);

		result = TEEC_InvokeCommand(&session,
				PTA_RPI3_CAN_SPI_POC_CMD_TRANSMIT_TEST_FRAME,
				&operation, &origin);
		TEEC_CloseSession(&session);
		TEEC_FinalizeContext(&context);
		if (result != TEEC_SUCCESS) {
			fprintf(stderr, "TEEC_InvokeCommand failed: 0x%08" PRIx32
				" origin=0x%08" PRIx32 "\n", result, origin);
			return 1;
		}
		printf("Sent id=0x%03lx counter=%lu payload=%s data=",
		       id, counter, payload_hex);
		for (i = 0; i < sizeof(data); i++)
			printf("%02x ", data[i]);
		printf("\n");
		if (!operation.params[0].value.b) {
			printf("FAIL: secure-world transmit did not succeed\n");
			return 1;
		}
		printf("PASS: transmitted directly from Secure World over real SPI0\n");
		return 0;
	}

	if (validation_debug_mode) {
		uint8_t checked[12] = { 0 };
		uint32_t checked_id;
		uint32_t checked_counter;
		uint32_t dropped_count;
		uint32_t status;
		uint32_t last_counter;
		static const char *status_name[] = {
			"NONE (no frame validated yet since boot)",
			"ACCEPTED",
			"INVALID_ID",
			"REPLAY (counter <= secure_last_counter)",
			"STALE (timestamp age > freshness policy)",
			"HMAC_FAIL (received tag != recomputed tag)",
		};

		operation.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_OUTPUT,
							TEEC_MEMREF_TEMP_OUTPUT,
							TEEC_NONE, TEEC_NONE);
		operation.params[1].tmpref.buffer = checked;
		operation.params[1].tmpref.size = sizeof(checked);

		result = TEEC_InvokeCommand(&session,
				PTA_RPI3_CAN_SPI_POC_CMD_GET_VALIDATION_DEBUG,
				&operation, &origin);
		TEEC_CloseSession(&session);
		TEEC_FinalizeContext(&context);
		if (result != TEEC_SUCCESS) {
			fprintf(stderr, "TEEC_InvokeCommand failed: 0x%08" PRIx32
				" origin=0x%08" PRIx32 "\n", result, origin);
			return 1;
		}

		status = operation.params[0].value.a;
		last_counter = operation.params[0].value.b;
		checked_id = (uint32_t)checked[0] | ((uint32_t)checked[1] << 8) |
			     ((uint32_t)checked[2] << 16) | ((uint32_t)checked[3] << 24);
		checked_counter = (uint32_t)checked[4] | ((uint32_t)checked[5] << 8) |
				  ((uint32_t)checked[6] << 16) | ((uint32_t)checked[7] << 24);
		dropped_count = (uint32_t)checked[8] | ((uint32_t)checked[9] << 8) |
				((uint32_t)checked[10] << 16) | ((uint32_t)checked[11] << 24);

		printf("Last validation status: %s\n",
		       status <= 5 ? status_name[status] : "UNKNOWN");
		printf("secure_last_counter (highest accepted this boot): %" PRIu32 "\n",
		       last_counter);
		printf("Most recently checked: id=0x%03" PRIx32 " counter=%" PRIu32 "\n",
		       checked_id, checked_counter);
		printf("Frames dropped (secure RX buffer busy when FIQ fired): %" PRIu32 "\n",
		       dropped_count);
		return 0;
	}

	if (timestamp_debug_mode) {
		uint8_t tscon_bytes[12] = { 0 };
		uint32_t last_timestamp;
		uint32_t tbc;
		uint32_t tscon;
		uint32_t packet_epoch;
		uint32_t current_epoch;

		operation.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_OUTPUT,
							TEEC_MEMREF_TEMP_OUTPUT,
							TEEC_NONE, TEEC_NONE);
		operation.params[1].tmpref.buffer = tscon_bytes;
		operation.params[1].tmpref.size = sizeof(tscon_bytes);

		result = TEEC_InvokeCommand(&session,
				PTA_RPI3_CAN_SPI_POC_CMD_GET_TIMESTAMP_DEBUG,
				&operation, &origin);
		TEEC_CloseSession(&session);
		TEEC_FinalizeContext(&context);
		if (result != TEEC_SUCCESS) {
			fprintf(stderr, "TEEC_InvokeCommand failed: 0x%08" PRIx32
				" origin=0x%08" PRIx32 "\n", result, origin);
			return 1;
		}

		last_timestamp = operation.params[0].value.a;
		tbc = operation.params[0].value.b;
		tscon = (uint32_t)tscon_bytes[0] | ((uint32_t)tscon_bytes[1] << 8) |
			((uint32_t)tscon_bytes[2] << 16) | ((uint32_t)tscon_bytes[3] << 24);
		packet_epoch = (uint32_t)tscon_bytes[4] | ((uint32_t)tscon_bytes[5] << 8) |
			((uint32_t)tscon_bytes[6] << 16) | ((uint32_t)tscon_bytes[7] << 24);
		current_epoch = (uint32_t)tscon_bytes[8] | ((uint32_t)tscon_bytes[9] << 8) |
			((uint32_t)tscon_bytes[10] << 16) | ((uint32_t)tscon_bytes[11] << 24);

		printf("Last captured RX frame timestamp (RXMSGTS): %" PRIu32 "\n",
		       last_timestamp);
		printf("Live CiTBC (free-running counter, right now): %" PRIu32 "\n", tbc);
		printf("Live CiTSCON: 0x%08" PRIx32 " (TBCEN=%d, TBCPRE=%" PRIu32 ")\n",
		       tscon, (tscon & 0x00010000) ? 1 : 0, tscon & 0x3ff);
		printf("Last captured frame's TBC epoch: %" PRIu32 "  Current TBC epoch: %"
		       PRIu32 " (%s)\n", packet_epoch, current_epoch,
		       packet_epoch == current_epoch ? "match" : "MISMATCH - would be STALE");
		printf("Note: this is a hardware tick counter (~25ns/tick at TBCPRE=0), not "
		       "wall-clock time - do not compare against Linux time. Wraps every "
		       "~107.4s.\n");
		return 0;
	}

	operation.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_OUTPUT, TEEC_NONE,
						TEEC_NONE, TEEC_NONE);

	if (fiq_mode)
		cmd = PTA_RPI3_CAN_SPI_POC_CMD_GET_FIQ_COUNT;
	else if (fiq_control_mode)
		cmd = PTA_RPI3_CAN_SPI_POC_CMD_READ_FIQ_CONTROL;
	else if (arm_fiq_mode)
		cmd = PTA_RPI3_CAN_SPI_POC_CMD_ARM_FIQ;
	else if (debug_mode)
		cmd = PTA_RPI3_CAN_SPI_POC_CMD_GET_DEBUG;
	else if (rxif_debug_mode)
		cmd = PTA_RPI3_CAN_SPI_POC_CMD_GET_RXIF_DEBUG;
	else if (bdiag1_debug_mode)
		cmd = PTA_RPI3_CAN_SPI_POC_CMD_GET_BDIAG1_DEBUG;
	else if (tx_fail_debug_mode)
		cmd = PTA_RPI3_CAN_SPI_POC_CMD_GET_TX_FAIL_DEBUG;
	else if (gpio_debug_mode)
		cmd = PTA_RPI3_CAN_SPI_POC_CMD_GET_GPIO_DEBUG;
	else if (core1_heartbeat_mode)
		cmd = PTA_RPI3_CAN_SPI_POC_CMD_GET_CORE1_HEARTBEAT;
	else if (wake_core1_mode)
		cmd = PTA_RPI3_CAN_SPI_POC_CMD_WAKE_CORE1;
	else
		cmd = PTA_RPI3_CAN_SPI_POC_CMD_READ_OSC_REG;

	result = TEEC_InvokeCommand(&session, cmd, &operation, &origin);
	if (result != TEEC_SUCCESS) {
		fprintf(stderr, "TEEC_InvokeCommand failed: 0x%08" PRIx32
			" origin=0x%08" PRIx32 "\n", result, origin);
		TEEC_CloseSession(&session);
		TEEC_FinalizeContext(&context);
		return 1;
	}

	TEEC_CloseSession(&session);
	TEEC_FinalizeContext(&context);

	if (fiq_mode) {
		printf("FIQ count (interrupts that reached Secure World directly "
		       "from the CAN controller): %" PRIu32 "\n",
		       operation.params[0].value.a);
		if (operation.params[0].value.a == 0) {
			printf("Not yet - no CAN controller interrupt has reached "
			       "Secure World yet. Trigger one (e.g. run "
			       "secure_can_demo) then check again.\n");
			return 1;
		}
		printf("PASS: an interrupt reached Secure World directly from the "
		       "CAN controller, with no Normal World involvement\n");
		return 0;
	}

	if (fiq_control_mode) {
		printf("FIQ_CONTROL register (live value): 0x%08" PRIx32 "\n",
		       operation.params[0].value.a);
		return 0;
	}

	if (debug_mode) {
		printf("Last capture: fifo_index=%" PRIu32 " fifo_addr=0x%08" PRIx32 "\n",
		       operation.params[0].value.a, operation.params[0].value.b);
		return 0;
	}

	if (rxif_debug_mode) {
		printf("Last FIQ: RXIF mask=0x%08" PRIx32 " INT reg=0x%08" PRIx32 "\n",
		       operation.params[0].value.a, operation.params[0].value.b);
		return 0;
	}

	if (bdiag1_debug_mode) {
		printf("Last FIQ: BDIAG1=0x%08" PRIx32 "\n",
		       operation.params[0].value.a);
		return 0;
	}

	if (tx_fail_debug_mode) {
		printf("Last secure-world transmit fail point: %" PRIu32 "\n",
		       operation.params[0].value.a);
		return 0;
	}

	if (gpio_debug_mode) {
		printf("GPIO 25 (can0 /INT) live level: %s, GPEDS0=0x%08" PRIx32 "\n",
		       operation.params[0].value.a ? "HIGH (inactive)" : "LOW (asserted)",
		       operation.params[0].value.b);
		return 0;
	}

	if (core1_heartbeat_mode) {
		uint64_t heartbeat = ((uint64_t)operation.params[0].value.b << 32) |
				     operation.params[0].value.a;

		printf("Core 1 permanent Secure-World heartbeat: %" PRIu64 "\n",
		       heartbeat);
		printf("Run this command again - if the value increased, core 1 "
		       "is alive and running Secure World code forever, per "
		       "Plan A's hard core-pinning design.\n");
		return 0;
	}

	if (wake_core1_mode) {
		int32_t psci_rc = (int32_t)operation.params[0].value.a;

		printf("Wake core 1 (permanent Secure World diversion): PSCI "
		       "status %" PRId32 "\n", psci_rc);
		if (psci_rc != 0) {
			printf("FAIL: core 1 was NOT (re)woken - PSCI_E_SUCCESS "
			       "is 0; a non-zero code (e.g. -4 = "
			       "PSCI_E_ALREADY_ON if this was already run once) "
			       "means the heartbeat will not start/change.\n");
			return 1;
		}
		printf("PASS: core 1 wake requested successfully. Only call "
		       "this ONCE, after boot is fully complete - run "
		       "--core1-heartbeat now and again shortly after to "
		       "confirm it's actually incrementing.\n");
		return 0;
	}

	if (arm_fiq_mode) {
		printf("Armed. FIQ_CONTROL now: 0x%08" PRIx32 "\n",
		       operation.params[0].value.a);
		return 0;
	}

	printf("OSC register read directly by OP-TEE core over real SPI0: 0x%08"
	       PRIx32 "\n", operation.params[0].value.a);
	if (operation.params[0].value.a == 0) {
		printf("FAIL: register read as all-zero - matches the "
		       "known can1 failure signature, not can0's expected "
		       "non-zero value\n");
		return 1;
	}
	printf("PASS: Secure World talked to the MCP2518FD directly over "
	       "real SPI0 hardware, independent of Linux's CAN drivers\n");
	return 0;
}
