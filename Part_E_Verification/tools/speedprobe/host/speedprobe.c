// SPDX-License-Identifier: BSD-2-Clause
/* speedprobe client: opens the speedprobe TA once, runs the timing loops, prints the result next to what a Cortex-A53 with caches should deliver. Changes nothing. */
#include <stdio.h>
#include <string.h>
#include <tee_client_api.h>
#include <speedprobe_ta.h>

int main(void)
{
	TEEC_Context ctx;
	TEEC_Session sess;
	TEEC_Operation op;
	TEEC_UUID uuid = TA_SPEEDPROBE_UUID;
	uint32_t origin;
	TEEC_Result r;

	r = TEEC_InitializeContext(NULL, &ctx);
	if (r) { fprintf(stderr, "InitializeContext 0x%x\n", r); return 1; }
	r = TEEC_OpenSession(&ctx, &sess, &uuid, TEEC_LOGIN_PUBLIC, NULL, NULL, &origin);
	if (r) {
		fprintf(stderr, "OpenSession failed: 0x%x (origin %u): the TA was not found or its signature was refused\n", r, origin);
		TEEC_FinalizeContext(&ctx);
		return 1;
	}
	memset(&op, 0, sizeof op);
	op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_OUTPUT, TEEC_VALUE_OUTPUT, TEEC_VALUE_OUTPUT, TEEC_VALUE_OUTPUT);
	r = TEEC_InvokeCommand(&sess, TA_SPEEDPROBE_CMD_RUN, &op, &origin);
	if (r) { fprintf(stderr, "Invoke failed: 0x%x\n", r); TEEC_CloseSession(&sess); TEEC_FinalizeContext(&ctx); return 1; }
	printf("speedprobe (loops run inside the secure world, S-EL0), milliseconds:\n");
	printf("  A register-only loop, 2,000,000 iterations      : %6u ms   (Cortex-A53 with caches at 1.2 GHz: about 8-15)\n", op.params[0].value.a);
	printf("  B 200 passes over 16 KiB (L1-resident)          : %6u ms   (about 1-3)\n", op.params[0].value.b);
	printf("  C 20 passes over 256 KiB (L2-resident)          : %6u ms   (about 3-8)\n", op.params[1].value.a);
	printf("  D 2000 cheap system calls (TEE_GetSystemTime)   : %6u ms   (about 2-6)\n", op.params[1].value.b);
	if (op.params[2].value.a == 0xFFFFFFFFu)
		printf("  E 50 HMAC-SHA256 of 12 bytes (TEE API)          : failed\n");
	else
		printf("  E 50 HMAC-SHA256 of 12 bytes (TEE API)          : %6u ms   (about 1-3)\n", op.params[2].value.a);
	printf("  F 50 SHA-256 of 64 bytes (TEE API)              : %6u ms   (about 1-3)\n", op.params[2].value.b);
	printf("  (checksum %u, distinct millisecond steps seen in a short loop: %u)\n", op.params[3].value.a, op.params[3].value.b);
	TEEC_CloseSession(&sess);
	TEEC_FinalizeContext(&ctx);
	return 0;
}
