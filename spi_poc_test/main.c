// SPDX-License-Identifier: BSD-2-Clause
/*
 * Standalone test client for the rpi3_can_spi_poc pseudo-TA (Phase 1 of
 * Secure Peripheral Ownership, KNOWLEDGE.md / roadmap Step 14). Proves
 * OP-TEE core can read the MCP2518FD's Oscillator Configuration
 * Register directly over real SPI0 hardware, independent of Linux's own
 * CAN drivers.
 */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <tee_client_api.h>
#include <pta_rpi3_can_spi_poc.h>

int main(void)
{
	TEEC_UUID uuid = PTA_RPI3_CAN_SPI_POC_UUID;
	TEEC_Context context;
	TEEC_Session session;
	TEEC_Operation operation;
	TEEC_Result result;
	uint32_t origin = 0;

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

	result = TEEC_InvokeCommand(&session,
			PTA_RPI3_CAN_SPI_POC_CMD_READ_OSC_REG,
			&operation, &origin);
	if (result != TEEC_SUCCESS) {
		fprintf(stderr, "TEEC_InvokeCommand failed: 0x%08" PRIx32
			" origin=0x%08" PRIx32 "\n", result, origin);
		TEEC_CloseSession(&session);
		TEEC_FinalizeContext(&context);
		return 1;
	}

	printf("OSC register read directly by OP-TEE core over real SPI0: 0x%08"
	       PRIx32 "\n", operation.params[0].value.a);
	if (operation.params[0].value.a == 0) {
		printf("FAIL: register read as all-zero - matches the "
		       "known can1 failure signature, not can0's expected "
		       "non-zero value\n");
		TEEC_CloseSession(&session);
		TEEC_FinalizeContext(&context);
		return 1;
	}
	printf("PASS: Secure World talked to the MCP2518FD directly over "
	       "real SPI0 hardware, independent of Linux's CAN drivers\n");

	TEEC_CloseSession(&session);
	TEEC_FinalizeContext(&context);
	return 0;
}
