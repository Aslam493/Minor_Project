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
#include <string.h>

#include <tee_client_api.h>
#include <pta_rpi3_can_spi_poc.h>

int main(int argc, char *argv[])
{
	bool fiq_mode = (argc > 1) && (strcmp(argv[1], "--fiq-count") == 0);
	bool fiq_control_mode = (argc > 1) &&
				(strcmp(argv[1], "--fiq-control") == 0);
	bool arm_fiq_mode = (argc > 1) && (strcmp(argv[1], "--arm-fiq") == 0);
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
	operation.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_OUTPUT, TEEC_NONE,
						TEEC_NONE, TEEC_NONE);

	if (fiq_mode)
		cmd = PTA_RPI3_CAN_SPI_POC_CMD_GET_FIQ_COUNT;
	else if (fiq_control_mode)
		cmd = PTA_RPI3_CAN_SPI_POC_CMD_READ_FIQ_CONTROL;
	else if (arm_fiq_mode)
		cmd = PTA_RPI3_CAN_SPI_POC_CMD_ARM_FIQ;
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
