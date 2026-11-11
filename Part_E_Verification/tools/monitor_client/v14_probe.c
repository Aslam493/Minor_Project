// SPDX-License-Identifier: BSD-2-Clause
/*
 * Check V14 probe: does Linux deliver the value and size outputs of a monitor command that returns an ERROR?
 * It calls command 20 (read-only status) with a buffer that is too small. The monitor must answer SHORT_BUFFER, set the memref size to 128
 * and write the health pair (zeros for an error). The output fields are pre-filled with a marker, so "delivered" and "dropped" differ.
 * Nothing is written to the monitor or the CAN chip. Also probes BAD_PARAMETERS (page 5).
 */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <tee_client_api.h>
#include <pta_rpi3_can_spi_poc.h>

#define MARK 0xAAAAAAAAu
static int probe(TEEC_Session *s, const char *what, uint32_t page, size_t bufsz)
{
	TEEC_Operation op; uint8_t buf[256]; uint32_t origin = 0; TEEC_Result r; int delivered;

	memset(&op, 0, sizeof op); memset(buf, 0xEE, sizeof buf);
	op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_INOUT, TEEC_MEMREF_TEMP_OUTPUT, TEEC_VALUE_OUTPUT, TEEC_VALUE_OUTPUT);
	op.params[0].value.a = page; op.params[0].value.b = 0;
	op.params[1].tmpref.buffer = buf; op.params[1].tmpref.size = bufsz;
	op.params[2].value.a = MARK; op.params[2].value.b = MARK; op.params[3].value.a = MARK; op.params[3].value.b = MARK;
	r = TEEC_InvokeCommand(s, PTA_RPI3_CAN_SPI_POC_CMD_MON_GET_STATUS, &op, &origin);
	delivered = !(op.params[2].value.a == MARK && op.params[2].value.b == MARK && op.params[3].value.a == MARK && op.params[3].value.b == MARK);
	printf("%-34s result 0x%08" PRIx32 " (origin %u) | memref size now %zu (was %zu) | health pair %08x %08x %08x %08x -> %s\n", what, (uint32_t)r, origin,
	       (size_t)op.params[1].tmpref.size, bufsz, op.params[2].value.a, op.params[2].value.b, op.params[3].value.a, op.params[3].value.b,
	       delivered ? "value outputs DELIVERED on this error" : "value outputs DROPPED (marker unchanged)");
	return 0;
}

int main(void)
{
	TEEC_Context ctx; TEEC_Session s; TEEC_UUID uuid = PTA_RPI3_CAN_SPI_POC_UUID; uint32_t origin = 0;

	if (TEEC_InitializeContext(NULL, &ctx) != TEEC_SUCCESS) return 1;
	if (TEEC_OpenSession(&ctx, &s, &uuid, TEEC_LOGIN_PUBLIC, NULL, NULL, &origin) != TEEC_SUCCESS) return 1;
	probe(&s, "SHORT_BUFFER (page 0, 64-byte buffer)", 0, 64);
	probe(&s, "BAD_PARAMETERS (page 5)", 5, 128);
	probe(&s, "SUCCESS control (page 0, 128 bytes)", 0, 128);
	TEEC_CloseSession(&s); TEEC_FinalizeContext(&ctx);
	return 0;
}
