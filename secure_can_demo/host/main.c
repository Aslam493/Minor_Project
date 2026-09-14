// SPDX-License-Identifier: BSD-2-Clause
#define _GNU_SOURCE
#include <inttypes.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <tee_client_api.h>
#include <secure_can_ta.h>

static int report_teec_error(const char *operation, TEEC_Result result,
			     uint32_t origin)
{
	fprintf(stderr, "%s failed: result=0x%08" PRIx32
		" origin=0x%08" PRIx32 "\n", operation, result, origin);
	return 1;
}
static int pin_to_linux_cpu(unsigned int cpu)
{
	cpu_set_t set;

	CPU_ZERO(&set);
	CPU_SET(cpu, &set);
	if (sched_setaffinity(0, sizeof(set), &set)) {
		perror("sched_setaffinity");
		return 1;
	}

	printf("Linux verifier pinned to CPU %u\n", cpu);
	return 0;
}

static int get_phase_a_status(TEEC_Session *session, uint32_t *counter,
			      uint32_t *handler_core, uint32_t *target_core,
			      uint32_t *initialized)
{
	TEEC_Operation operation;
	TEEC_Result result;
	uint32_t origin = 0;

	memset(&operation, 0, sizeof(operation));
	operation.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_OUTPUT,
					       TEEC_VALUE_OUTPUT,
					       TEEC_NONE, TEEC_NONE);
	result = TEEC_InvokeCommand(session, PTA_SECURE_CAN_PHASE_A_GET_STATUS,
				    &operation, &origin);
	if (result != TEEC_SUCCESS)
		return report_teec_error("Phase A GET_STATUS", result, origin);

	*counter = operation.params[0].value.a;
	*handler_core = operation.params[0].value.b;
	*target_core = operation.params[1].value.a;
	*initialized = operation.params[1].value.b;
	return 0;
}

static int arm_phase_a_timer(TEEC_Session *session)
{
	TEEC_Operation operation;
	TEEC_Result result;
	uint32_t origin = 0;

	memset(&operation, 0, sizeof(operation));
	operation.paramTypes = TEEC_PARAM_TYPES(TEEC_NONE, TEEC_NONE,
					       TEEC_NONE, TEEC_NONE);
	result = TEEC_InvokeCommand(session, PTA_SECURE_CAN_PHASE_A_ARM_TIMER,
				    &operation, &origin);
	if (result != TEEC_SUCCESS)
		return report_teec_error("Phase A ARM_TIMER", result, origin);

	puts("Secure timer activated from Linux CPU 1");
	return 0;
}

static int verify_phase_a_timer(TEEC_Context *context)
{
	TEEC_UUID uuid = PTA_SECURE_CAN_PHASE_A_UUID;
	TEEC_Session session;
	TEEC_Result result;
	uint32_t origin = 0;
	uint32_t first = 0;
	uint32_t second = 0;
	uint32_t handler_core = 0;
	uint32_t target_core = 0;
	uint32_t initialized = 0;
	int status = 1;


	result = TEEC_OpenSession(context, &session, &uuid, TEEC_LOGIN_PUBLIC,
				  NULL, NULL, &origin);
	if (result != TEEC_SUCCESS)
		return report_teec_error("Phase A TEEC_OpenSession", result,
					 origin);

	if (pin_to_linux_cpu(1) || arm_phase_a_timer(&session) ||
	    pin_to_linux_cpu(0))
		goto out;

	if (get_phase_a_status(&session, &first, &handler_core, &target_core,
			       &initialized))
		goto out;

	sleep(1);

	if (get_phase_a_status(&session, &second, &handler_core, &target_core,
			       &initialized))
		goto out;

	printf("Secure timer counter:  %" PRIu32 " -> %" PRIu32 "\n",
	       first, second);
	printf("Secure handler core:   %" PRIu32 " (target %" PRIu32 ")\n",
	       handler_core, target_core);

	if (!initialized || second <= first || handler_core != target_core) {
		fprintf(stderr, "FAIL: independent secure timer evidence invalid\n");
		goto out;
	}

	puts("PASS: Secure World counter advanced without a client invocation");
	status = 0;
out:
	TEEC_CloseSession(&session);
	return status;
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

	if (pin_to_linux_cpu(0))
		return 1;

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
	if (verify_phase_a_timer(&context))
		goto close;
	status = 0;

close:
	TEEC_CloseSession(&session);
finalize:
	TEEC_FinalizeContext(&context);
	return status;
}
