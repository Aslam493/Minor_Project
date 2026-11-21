// SPDX-License-Identifier: BSD-2-Clause
/* speedprobe TA: times a few loops inside the secure world. Reads nothing, writes nothing persistent, uses no secure storage. */
#include <tee_internal_api.h>
#include <tee_internal_api_extensions.h>
#include <speedprobe_ta.h>

static volatile uint64_t g_sink;
static uint64_t g_buf[32768];			/* 256 KiB */

static uint32_t ms_now(void) { TEE_Time t; TEE_GetSystemTime(&t); return t.seconds * 1000u + t.millis; }

TEE_Result TA_CreateEntryPoint(void) { return TEE_SUCCESS; }
void TA_DestroyEntryPoint(void) { }
TEE_Result TA_OpenSessionEntryPoint(uint32_t pt, TEE_Param p[4], void **s) { (void)pt; (void)p; (void)s; return TEE_SUCCESS; }
void TA_CloseSessionEntryPoint(void *s) { (void)s; }

static TEE_Result run(uint32_t pt, TEE_Param p[4])
{
	const uint32_t exp = TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_OUTPUT, TEE_PARAM_TYPE_VALUE_OUTPUT, TEE_PARAM_TYPE_VALUE_OUTPUT, TEE_PARAM_TYPE_VALUE_OUTPUT);
	uint32_t t0, t1, i, k, steps = 0, last;
	uint64_t acc = 1;
	TEE_Result res;
	TEE_OperationHandle op = TEE_HANDLE_NULL;
	TEE_ObjectHandle key = TEE_HANDLE_NULL;
	TEE_Attribute attr;
	uint8_t kb[32], msg[64], out[32];
	uint32_t outl;				/* TEE 1.1 API: the length arguments are uint32_t */

	if (pt != exp)
		return TEE_ERROR_BAD_PARAMETERS;
	for (i = 0; i < sizeof kb; i++)
		kb[i] = (uint8_t)(i * 7 + 1);
	for (i = 0; i < sizeof msg; i++)
		msg[i] = (uint8_t)(i * 3);
	for (i = 0; i < 32768; i++)
		g_buf[i] = i;

	t0 = ms_now();					/* (A) register only */
	for (i = 0; i < 2000000u; i++)
		acc += (acc >> 1) + i * 3u;
	t1 = ms_now();
	p[0].value.a = t1 - t0;

	t0 = ms_now();					/* (B) 200 passes over 16 KiB (2048 words) */
	for (k = 0; k < 200; k++)
		for (i = 0; i < 2048; i++)
			acc += g_buf[i];
	t1 = ms_now();
	p[0].value.b = t1 - t0;

	t0 = ms_now();					/* (C) 20 passes over 256 KiB */
	for (k = 0; k < 20; k++)
		for (i = 0; i < 32768; i++)
			acc += g_buf[i];
	t1 = ms_now();
	p[1].value.a = t1 - t0;

	t0 = ms_now();					/* (D) a cheap system call into the core */
	for (i = 0; i < 2000; i++) {
		TEE_Time t;

		TEE_GetSystemTime(&t);
		acc += t.millis;
	}
	t1 = ms_now();
	p[1].value.b = t1 - t0;

	p[2].value.a = 0xFFFFFFFFu;
	p[2].value.b = 0xFFFFFFFFu;
	res = TEE_AllocateOperation(&op, TEE_ALG_HMAC_SHA256, TEE_MODE_MAC, 256);
	if (res == TEE_SUCCESS)
		res = TEE_AllocateTransientObject(TEE_TYPE_HMAC_SHA256, 256, &key);
	if (res == TEE_SUCCESS) {
		TEE_InitRefAttribute(&attr, TEE_ATTR_SECRET_VALUE, kb, sizeof kb);
		res = TEE_PopulateTransientObject(key, &attr, 1);
	}
	if (res == TEE_SUCCESS)
		res = TEE_SetOperationKey(op, key);
	if (res == TEE_SUCCESS) {			/* (E) 50 HMAC-SHA256 of 12 bytes */
		t0 = ms_now();
		for (i = 0; i < 50; i++) {
			TEE_MACInit(op, NULL, 0);
			outl = sizeof out;
			res = TEE_MACComputeFinal(op, msg, 12, out, &outl);
			if (res != TEE_SUCCESS)
				break;
			acc += out[0];
		}
		t1 = ms_now();
		if (res == TEE_SUCCESS)
			p[2].value.a = t1 - t0;
	}
	if (op != TEE_HANDLE_NULL)
		TEE_FreeOperation(op);
	if (key != TEE_HANDLE_NULL)
		TEE_FreeTransientObject(key);
	op = TEE_HANDLE_NULL;
	if (TEE_AllocateOperation(&op, TEE_ALG_SHA256, TEE_MODE_DIGEST, 0) == TEE_SUCCESS) {	/* (F) 50 SHA-256 of 64 bytes */
		t0 = ms_now();
		for (i = 0; i < 50; i++) {
			outl = sizeof out;
			if (TEE_DigestDoFinal(op, msg, 64, out, &outl) != TEE_SUCCESS)
				break;
			acc += out[1];
		}
		t1 = ms_now();
		p[2].value.b = t1 - t0;
		TEE_FreeOperation(op);
	}
	last = ms_now();				/* resolution probe: distinct millisecond values in a short busy loop */
	for (i = 0; i < 20000; i++) {
		uint32_t now = ms_now();

		if (now != last) {
			steps++;
			last = now;
		}
	}
	g_sink = acc;
	p[3].value.a = (uint32_t)acc;
	p[3].value.b = steps;
	return TEE_SUCCESS;
}

TEE_Result TA_InvokeCommandEntryPoint(void *s, uint32_t cmd, uint32_t pt, TEE_Param p[4])
{
	(void)s;
	return cmd == TA_SPEEDPROBE_CMD_RUN ? run(pt, p) : TEE_ERROR_NOT_SUPPORTED;
}
