// SPDX-License-Identifier: BSD-2-Clause
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <tee_client_api.h>
#include <secure_can_ta.h>

#define SECURE_CAN_FRAME_COUNT           5U
#define SECURE_CAN_SEND_INTERVAL_SECONDS 1U

static int report_teec_error(const char *operation, TEEC_Result result,
			     uint32_t origin)
{
	fprintf(stderr, "%s failed: result=0x%08" PRIx32
		" origin=0x%08" PRIx32 "\n", operation, result, origin);
	return 1;
}

/* Stands in for a real CAN controller: builds a deterministic dummy frame
 * so the pipeline is reproducible without CAN hardware.
 */
static void fill_dummy_frame(struct secure_can_frame *frame,
			     unsigned int index)
{
	unsigned int i;

	frame->can_id = 0x100 + index;
	for (i = 0; i < SECURE_CAN_FRAME_DATA_LEN; i++)
		frame->data[i] = (uint8_t)((index * 0x11) + i);
}

static void print_frame_result(const struct secure_can_frame *frame,
			       const uint8_t *tag)
{
	unsigned int i;

	printf("Linux: CAN id=0x%03" PRIx32 " data=", frame->can_id);
	for (i = 0; i < SECURE_CAN_FRAME_DATA_LEN; i++)
		printf("%02" PRIx8 " ", frame->data[i]);
	printf("-> Secure World tag=");
	for (i = 0; i < SECURE_CAN_FRAME_TAG_LEN; i++)
		printf("%02" PRIx8, tag[i]);
	puts("  [Linux forwards the authenticated frame]");
}

static int verify_frame(TEEC_Session *session,
			const struct secure_can_frame *frame,
			const uint8_t *tag, int *accepted)
{
	TEEC_Operation operation;
	TEEC_Result result;
	uint32_t origin = 0;

	memset(&operation, 0, sizeof(operation));
	operation.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT,
						TEEC_MEMREF_TEMP_INPUT,
						TEEC_VALUE_OUTPUT, TEEC_NONE);
	operation.params[0].tmpref.buffer = (void *)frame;
	operation.params[0].tmpref.size = sizeof(*frame);
	operation.params[1].tmpref.buffer = (void *)tag;
	operation.params[1].tmpref.size = SECURE_CAN_FRAME_TAG_LEN;

	result = TEEC_InvokeCommand(session, TA_SECURE_CAN_CMD_VERIFY_FRAME,
				    &operation, &origin);
	if (result != TEEC_SUCCESS)
		return report_teec_error("VERIFY_FRAME", result, origin);

	*accepted = (int)operation.params[2].value.a;
	return 0;
}

/* Linux alone decides when each simulated frame is "received" and sent in
 * for authentication; Secure World only reacts to calls Normal World makes,
 * it never runs on its own timer or interrupt.
 */
static int run_can_frame_pipeline(TEEC_Session *session)
{
	struct secure_can_frame last_frame;
	uint8_t last_tag[SECURE_CAN_FRAME_TAG_LEN];
	unsigned int i;
	int accepted = 0;

	puts("Linux (Normal World) starting simulated CAN message pipeline");

	for (i = 0; i < SECURE_CAN_FRAME_COUNT; i++) {
		struct secure_can_frame frame;
		uint8_t tag[SECURE_CAN_FRAME_TAG_LEN];
		TEEC_Operation operation;
		TEEC_Result result;
		uint32_t origin = 0;

		fill_dummy_frame(&frame, i);

		memset(&operation, 0, sizeof(operation));
		operation.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT,
							 TEEC_MEMREF_TEMP_OUTPUT,
							 TEEC_NONE, TEEC_NONE);
		operation.params[0].tmpref.buffer = &frame;
		operation.params[0].tmpref.size = sizeof(frame);
		operation.params[1].tmpref.buffer = tag;
		operation.params[1].tmpref.size = sizeof(tag);

		result = TEEC_InvokeCommand(session,
				TA_SECURE_CAN_CMD_PROCESS_FRAME, &operation,
				&origin);
		if (result != TEEC_SUCCESS)
			return report_teec_error("PROCESS_FRAME", result,
						 origin);

		print_frame_result(&frame, tag);
		last_frame = frame;
		memcpy(last_tag, tag, sizeof(last_tag));
		sleep(SECURE_CAN_SEND_INTERVAL_SECONDS);
	}

	puts("PASS: Linux-controlled simulated CAN frames authenticated by Secure World");

	puts("Linux: simulating receipt of the last frame, asking Secure World to verify it");
	if (verify_frame(session, &last_frame, last_tag, &accepted))
		return 1;
	if (!accepted) {
		fprintf(stderr, "FAIL: Secure World rejected an authentic frame\n");
		return 1;
	}
	puts("PASS: Secure World accepted the authentic frame");

	puts("Linux: simulating a tampered frame on the bus (one data byte flipped)");
	last_frame.data[0] ^= 0xff;
	if (verify_frame(session, &last_frame, last_tag, &accepted))
		return 1;
	if (accepted) {
		fprintf(stderr, "FAIL: Secure World accepted a tampered frame\n");
		return 1;
	}
	puts("PASS: Secure World rejected the tampered frame");

	return 0;
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

	if (run_can_frame_pipeline(&session))
		goto close;

	status = 0;

close:
	TEEC_CloseSession(&session);
finalize:
	TEEC_FinalizeContext(&context);
	return status;
}
