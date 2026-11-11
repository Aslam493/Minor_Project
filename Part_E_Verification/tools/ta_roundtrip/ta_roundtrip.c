// SPDX-License-Identifier: BSD-2-Clause
/*
 * ta_roundtrip - Linux -> Secure World request/response check
 * (Component 1, subcomponent 1.3, Task 4).
 *
 * Touches NO CAN hardware, writes nothing, changes no state:
 *   Part A  project TA   : challenge/response (cmd 0), plus two error probes
 *   Part B  project PTA  : Core 1 heartbeat (cmd 12) twice, plus two probes
 *
 * Error probes (they are expected to FAIL, that is the test):
 *   - wrong parameter types   -> must return TEEC_ERROR_BAD_PARAMETERS
 *   - unknown command id      -> must return TEEC_ERROR_NOT_SUPPORTED
 * They prove that the Secure World side validates input and that the error
 * code and its origin travel back to Linux intact.
 *
 * Expected values come from the project headers, not from this file.
 */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <tee_client_api.h>

#include <pta_rpi3_can_spi_poc.h>
#include <secure_can_ta.h>

static int failures;

static const char *origin_name(uint32_t o)
{
	switch (o) {
	case TEEC_ORIGIN_API:         return "API";
	case TEEC_ORIGIN_COMMS:       return "COMMS";
	case TEEC_ORIGIN_TEE:         return "TEE";
	case TEEC_ORIGIN_TRUSTED_APP: return "TRUSTED_APP";
	default:                      return "?";
	}
}

static void check(const char *what, int ok)
{
	printf("  %-4s %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok)
		failures++;
}

/* Run one command with a given parameter-type word, return the TEEC result. */
static TEEC_Result invoke(TEEC_Session *s, uint32_t cmd, uint32_t types,
			  uint32_t a_in, uint32_t b_in, uint32_t *a_out,
			  uint32_t *b_out, uint32_t *origin)
{
	TEEC_Operation op;
	TEEC_Result r;

	memset(&op, 0, sizeof(op));
	op.paramTypes = types;
	op.params[0].value.a = a_in;
	op.params[0].value.b = b_in;
	*origin = 0;
	r = TEEC_InvokeCommand(s, cmd, &op, origin);
	if (a_out)
		*a_out = op.params[0].value.a;
	if (b_out)
		*b_out = op.params[0].value.b;
	return r;
}

static int part_a_ta(TEEC_Context *ctx)
{
	TEEC_UUID uuid = TA_SECURE_CAN_UUID;
	TEEC_Session s;
	TEEC_Result r;
	uint32_t origin = 0, a = 0, b = 0;
	const uint32_t t_inout = TEEC_PARAM_TYPES(TEEC_VALUE_INOUT, TEEC_NONE,
						  TEEC_NONE, TEEC_NONE);
	const uint32_t t_in = TEEC_PARAM_TYPES(TEEC_VALUE_INPUT, TEEC_NONE,
					       TEEC_NONE, TEEC_NONE);

	printf("Part A: project TA abecf637-ca20-467e-8e6c-0b8779ba6b2f\n");
	r = TEEC_OpenSession(ctx, &s, &uuid, TEEC_LOGIN_PUBLIC, NULL, NULL,
			     &origin);
	printf("  open session: result 0x%08x origin %s\n", (unsigned)r,
	       origin_name(origin));
	check("session opened", r == TEEC_SUCCESS);
	if (r != TEEC_SUCCESS)
		return -1;

	/* 1. the project's own challenge */
	r = invoke(&s, TA_SECURE_CAN_CMD_PROCESS_CHALLENGE, t_inout,
		   SECURE_CAN_CHALLENGE, 0, &a, &b, &origin);
	printf("  challenge 0x%08x -> response 0x%08x marker 0x%08x "
	       "(result 0x%08x)\n", SECURE_CAN_CHALLENGE, a, b, (unsigned)r);
	printf("  expected  response 0x%08x marker 0x%08x (from header)\n",
	       SECURE_CAN_RESPONSE, SECURE_CAN_MARKER);
	check("challenge answered with the expected response",
	      r == TEEC_SUCCESS && a == SECURE_CAN_RESPONSE);
	check("marker 'SCAN' returned", b == SECURE_CAN_MARKER);

	/* 2. a different value, so the answer is not a constant */
	r = invoke(&s, TA_SECURE_CAN_CMD_PROCESS_CHALLENGE, t_inout,
		   0x00000000U, 0, &a, &b, &origin);
	printf("  challenge 0x00000000 -> response 0x%08x\n", a);
	check("second value transformed too (0 -> mask)",
	      r == TEEC_SUCCESS && a == SECURE_CAN_MASK);

	/* 3. error probes */
	r = invoke(&s, TA_SECURE_CAN_CMD_PROCESS_CHALLENGE, t_in,
		   SECURE_CAN_CHALLENGE, 0, NULL, NULL, &origin);
	printf("  wrong param types -> result 0x%08x origin %s\n", (unsigned)r,
	       origin_name(origin));
	check("wrong parameter types rejected (BAD_PARAMETERS)",
	      r == TEEC_ERROR_BAD_PARAMETERS);
	check("error origin is the Trusted Application",
	      origin == TEEC_ORIGIN_TRUSTED_APP);

	r = invoke(&s, 99, t_inout, 0, 0, NULL, NULL, &origin);
	printf("  unknown command 99 -> result 0x%08x origin %s\n",
	       (unsigned)r, origin_name(origin));
	check("unknown command rejected (NOT_SUPPORTED)",
	      r == TEEC_ERROR_NOT_SUPPORTED);

	TEEC_CloseSession(&s);
	return 0;
}

static int part_b_pta(TEEC_Context *ctx)
{
	TEEC_UUID uuid = PTA_RPI3_CAN_SPI_POC_UUID;
	TEEC_Session s;
	TEEC_Result r;
	uint32_t origin = 0, a = 0, b = 0;
	uint64_t h1, h2;
	const uint32_t t_out = TEEC_PARAM_TYPES(TEEC_VALUE_OUTPUT, TEEC_NONE,
						TEEC_NONE, TEEC_NONE);
	const uint32_t t_in = TEEC_PARAM_TYPES(TEEC_VALUE_INPUT, TEEC_NONE,
					       TEEC_NONE, TEEC_NONE);

	printf("Part B: project PTA 9a986af1-048a-44a8-b984-1e902290d63c\n");
	r = TEEC_OpenSession(ctx, &s, &uuid, TEEC_LOGIN_PUBLIC, NULL, NULL,
			     &origin);
	printf("  open session: result 0x%08x origin %s\n", (unsigned)r,
	       origin_name(origin));
	check("session opened", r == TEEC_SUCCESS);
	if (r != TEEC_SUCCESS)
		return -1;

	r = invoke(&s, PTA_RPI3_CAN_SPI_POC_CMD_GET_CORE1_HEARTBEAT, t_out, 0,
		   0, &a, &b, &origin);
	h1 = ((uint64_t)b << 32) | a;
	check("heartbeat read #1", r == TEEC_SUCCESS);
	usleep(500000);
	r = invoke(&s, PTA_RPI3_CAN_SPI_POC_CMD_GET_CORE1_HEARTBEAT, t_out, 0,
		   0, &a, &b, &origin);
	h2 = ((uint64_t)b << 32) | a;
	check("heartbeat read #2", r == TEEC_SUCCESS);
	printf("  heartbeat #1 = %" PRIu64 "\n  heartbeat #2 = %" PRIu64
	       "  (delta %" PRIu64 " in ~0.5 s)\n", h1, h2, h2 - h1);
	check("Core 1 counter increased between the two reads", h2 > h1);

	r = invoke(&s, PTA_RPI3_CAN_SPI_POC_CMD_GET_CORE1_HEARTBEAT, t_in, 0, 0,
		   NULL, NULL, &origin);
	printf("  heartbeat with wrong param types -> result 0x%08x origin %s\n",
	       (unsigned)r, origin_name(origin));
	check("wrong parameter types rejected (BAD_PARAMETERS)",
	      r == TEEC_ERROR_BAD_PARAMETERS);

	r = invoke(&s, 17, t_out, 0, 0, NULL, NULL, &origin);
	printf("  command 17 -> result 0x%08x origin %s\n", (unsigned)r,
	       origin_name(origin));
	check("command 17 does not exist yet (NOT_SUPPORTED): monitor "
	      "commands start from here", r == TEEC_ERROR_NOT_SUPPORTED);

	TEEC_CloseSession(&s);
	return 0;
}

int main(void)
{
	TEEC_Context ctx;
	TEEC_Result r = TEEC_InitializeContext(NULL, &ctx);

	if (r != TEEC_SUCCESS) {
		fprintf(stderr, "TEEC_InitializeContext failed 0x%08x\n",
			(unsigned)r);
		return 1;
	}
	part_a_ta(&ctx);
	part_b_pta(&ctx);
	TEEC_FinalizeContext(&ctx);

	printf("\nSUMMARY: %s (%d failure%s)\n", failures ? "FAIL" : "ALL PASS",
	       failures, failures == 1 ? "" : "s");
	return failures ? 1 : 0;
}
