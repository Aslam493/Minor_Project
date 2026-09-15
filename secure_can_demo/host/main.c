// SPDX-License-Identifier: BSD-2-Clause
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <tee_client_api.h>
#include <secure_can_ta.h>

static int report_teec_error(const char *operation, TEEC_Result result,
			     uint32_t origin)
{
	fprintf(stderr, "%s failed: result=0x%08" PRIx32
		" origin=0x%08" PRIx32 "\n", operation, result, origin);
	return 1;
}

int main(void)
{
	TEEC_UUID uuid = TA_SECURE_CAN_UUID;
	TEEC_Context context;
	TEEC_Session session;
	TEEC_Operation operation;
	TEEC_Result result;
	uint32_t origin = 0;
	int status = 1;

	result = TEEC_InitializeContext(NULL, &context);
	if (result != TEEC_SUCCESS)
		return report_teec_error("TEEC_InitializeContext", result, origin);

	result = TEEC_OpenSession(&context, &session, &uuid, TEEC_LOGIN_PUBLIC,
				  NULL, NULL, &origin);
	if (result != TEEC_SUCCESS) {
		report_teec_error("TEEC_OpenSession", result, origin);
		goto finalize;
	}

	memset(&operation, 0, sizeof(operation));
	operation.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_INOUT, TEEC_NONE,
						TEEC_NONE, TEEC_NONE);
	operation.params[0].value.a = SECURE_CAN_CHALLENGE;
	printf("Normal World challenge: 0x%08" PRIx32 "\n",
	       operation.params[0].value.a);

	result = TEEC_InvokeCommand(&session,
			TA_SECURE_CAN_CMD_PROCESS_CHALLENGE, &operation, &origin);
	if (result != TEEC_SUCCESS) {
		report_teec_error("TEEC_InvokeCommand", result, origin);
		goto close;
	}

	printf("Secure World response: 0x%08" PRIx32 "\n",
	       operation.params[0].value.a);
	printf("Secure World marker:   0x%08" PRIx32 " (SCAN)\n",
	       operation.params[0].value.b);

	if (operation.params[0].value.a != SECURE_CAN_RESPONSE ||
	    operation.params[0].value.b != SECURE_CAN_MARKER) {
		fprintf(stderr, "FAIL: Secure World returned unexpected data\n");
		goto close;
	}

	puts("PASS: project-specific Normal World to Secure World round trip");
	status = 0;

close:
	TEEC_CloseSession(&session);
finalize:
	TEEC_FinalizeContext(&context);
	return status;
}
