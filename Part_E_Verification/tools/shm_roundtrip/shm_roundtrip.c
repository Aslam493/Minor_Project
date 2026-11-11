// SPDX-License-Identifier: BSD-2-Clause
/*
 * shm_roundtrip - shared-memory behaviour of the Linux <-> Secure World
 * channel (Component 1, subcomponent 1.3, Task 5).
 *
 * Touches NO CAN hardware, writes no device state. TA commands used are pure
 * computation (classify / HMAC tag / verify); the only PTA command used is
 * cmd 15 (returns stored values). Buffers are guarded with a 0xAA canary so
 * the test can show exactly which bytes Secure World wrote.
 *
 *   A  classify with a TEMP input buffer (boundary of the SECURE id range)
 *   B  tag + verify round trip, tamper cases, determinism
 *   C  buffer-size edges (too small, too large, exact) on the TA
 *   D  buffer-size edges and canary on PTA cmd 15 (memory output)
 *   E  an allocated shared-memory block with offsets: Secure World must write
 *      only inside the window it was given
 *   F  which shared-memory mode is in use (capability flags + shadow-buffer
 *      check; the return code of RegisterSharedMemory alone is not evidence)
 */
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <tee_client_api.h>

#include <pta_rpi3_can_spi_poc.h>
#include <secure_can_ta.h>

#define CANARY 0xAA

static int failures;

static void check(const char *what, int ok)
{
	printf("  %-4s %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok)
		failures++;
}

static const char *rname(TEEC_Result r)
{
	switch (r) {
	case TEEC_SUCCESS:                return "SUCCESS";
	case TEEC_ERROR_BAD_PARAMETERS:   return "BAD_PARAMETERS";
	case TEEC_ERROR_SHORT_BUFFER:     return "SHORT_BUFFER";
	case TEEC_ERROR_NOT_SUPPORTED:    return "NOT_SUPPORTED";
	case TEEC_ERROR_OUT_OF_MEMORY:    return "OUT_OF_MEMORY";
	case TEEC_ERROR_GENERIC:          return "GENERIC";
	case TEEC_ERROR_ITEM_NOT_FOUND:   return "ITEM_NOT_FOUND";
	default:                          return "other";
	}
}

static int all_canary(const uint8_t *p, size_t n)
{
	for (size_t i = 0; i < n; i++)
		if (p[i] != CANARY)
			return 0;
	return 1;
}

static struct secure_can_frame mkframe(uint32_t id)
{
	struct secure_can_frame f;

	memset(&f, 0, sizeof(f));
	f.can_id = id;
	for (unsigned int i = 0; i < SECURE_CAN_FRAME_DATA_LEN; i++)
		f.data[i] = (uint8_t)(i + 1);
	return f;
}

/* classify: MEMREF_TEMP_INPUT (frame), VALUE_OUTPUT */
static TEEC_Result do_classify(TEEC_Session *s, const struct secure_can_frame *f,
			       size_t in_size, uint32_t *cls)
{
	TEEC_Operation op;
	uint32_t origin;
	TEEC_Result r;

	memset(&op, 0, sizeof(op));
	op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT,
					 TEEC_VALUE_OUTPUT, TEEC_NONE, TEEC_NONE);
	op.params[0].tmpref.buffer = (void *)f;
	op.params[0].tmpref.size = in_size;
	r = TEEC_InvokeCommand(s, TA_SECURE_CAN_CMD_CLASSIFY_FRAME, &op, &origin);
	*cls = op.params[1].value.a;
	return r;
}

/* process_frame: MEMREF_TEMP_INPUT (frame), MEMREF_TEMP_OUTPUT (tag) */
static TEEC_Result do_tag(TEEC_Session *s, const void *in, size_t in_size,
			  void *out, size_t out_size, size_t *out_ret)
{
	TEEC_Operation op;
	uint32_t origin;
	TEEC_Result r;

	memset(&op, 0, sizeof(op));
	op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT,
					 TEEC_MEMREF_TEMP_OUTPUT, TEEC_NONE,
					 TEEC_NONE);
	op.params[0].tmpref.buffer = (void *)in;
	op.params[0].tmpref.size = in_size;
	op.params[1].tmpref.buffer = out;
	op.params[1].tmpref.size = out_size;
	r = TEEC_InvokeCommand(s, TA_SECURE_CAN_CMD_PROCESS_FRAME, &op, &origin);
	*out_ret = op.params[1].tmpref.size;
	return r;
}

/* verify_frame: MEMREF_TEMP_INPUT (frame), MEMREF_TEMP_INPUT (tag), VALUE_OUTPUT */
static TEEC_Result do_verify(TEEC_Session *s, const struct secure_can_frame *f,
			     const uint8_t *tag, size_t tag_size, uint32_t *ok)
{
	TEEC_Operation op;
	uint32_t origin;
	TEEC_Result r;

	memset(&op, 0, sizeof(op));
	op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT,
					 TEEC_MEMREF_TEMP_INPUT,
					 TEEC_VALUE_OUTPUT, TEEC_NONE);
	op.params[0].tmpref.buffer = (void *)f;
	op.params[0].tmpref.size = sizeof(*f);
	op.params[1].tmpref.buffer = (void *)tag;
	op.params[1].tmpref.size = tag_size;
	r = TEEC_InvokeCommand(s, TA_SECURE_CAN_CMD_VERIFY_FRAME, &op, &origin);
	*ok = op.params[2].value.a;
	return r;
}

static void part_a(TEEC_Session *s)
{
	static const struct { uint32_t id; uint32_t want; } t[] = {
		{ 0x000, SECURE_CAN_CLASS_SECURE },
		{ 0x100, SECURE_CAN_CLASS_SECURE },
		{ 0x102, SECURE_CAN_CLASS_SECURE },  /* boundary: last SECURE id */
		{ 0x103, SECURE_CAN_CLASS_NORMAL },  /* boundary: first NORMAL id */
		{ 0x7ff, SECURE_CAN_CLASS_NORMAL },
	};
	char msg[96];

	printf("A. classify through a temporary shared buffer\n");
	for (unsigned int i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
		struct secure_can_frame f = mkframe(t[i].id);
		uint32_t cls = 99;
		TEEC_Result r = do_classify(s, &f, sizeof(f), &cls);

		snprintf(msg, sizeof(msg), "id 0x%03x -> class %u (expect %u)",
			 t[i].id, cls, t[i].want);
		check(msg, r == TEEC_SUCCESS && cls == t[i].want);
	}
}

static void part_b(TEEC_Session *s)
{
	struct secure_can_frame f = mkframe(0x100);
	uint8_t tag1[8], tag2[8], bad[8];
	size_t n1 = 0, n2 = 0;
	uint32_t ok = 99;
	TEEC_Result r;

	printf("B. tag and verify round trip through shared buffers\n");
	memset(tag1, CANARY, sizeof(tag1));
	memset(tag2, CANARY, sizeof(tag2));
	r = do_tag(s, &f, sizeof(f), tag1, sizeof(tag1), &n1);
	check("tag computed, 8 bytes returned", r == TEEC_SUCCESS && n1 == 8);
	printf("       tag = %02x%02x%02x%02x%02x%02x%02x%02x\n", tag1[0],
	       tag1[1], tag1[2], tag1[3], tag1[4], tag1[5], tag1[6], tag1[7]);
	r = do_tag(s, &f, sizeof(f), tag2, sizeof(tag2), &n2);
	check("same frame gives the same tag (deterministic)",
	      r == TEEC_SUCCESS && memcmp(tag1, tag2, 8) == 0);

	r = do_verify(s, &f, tag1, 8, &ok);
	check("authentic frame accepted", r == TEEC_SUCCESS && ok == 1);

	memcpy(bad, tag1, 8);
	bad[3] ^= 0x01;
	r = do_verify(s, &f, bad, 8, &ok);
	check("one flipped tag bit rejected", r == TEEC_SUCCESS && ok == 0);

	f.data[5] ^= 0x01;
	r = do_verify(s, &f, tag1, 8, &ok);
	check("one flipped data bit rejected", r == TEEC_SUCCESS && ok == 0);
	f.data[5] ^= 0x01;

	f.can_id = 0x101;
	r = do_verify(s, &f, tag1, 8, &ok);
	check("changed CAN id rejected", r == TEEC_SUCCESS && ok == 0);
}

static void part_c(TEEC_Session *s)
{
	struct secure_can_frame f = mkframe(0x100);
	uint8_t in13[13], in11[11], out[64], tag[8];
	size_t n = 0;
	uint32_t ok = 0;
	TEEC_Result r;
	char msg[110];

	printf("C. buffer-size edges on the TA\n");
	memset(in13, 0, sizeof(in13));
	memcpy(in13, &f, sizeof(f));
	memcpy(in11, &f, sizeof(in11));

	r = do_tag(s, in11, sizeof(in11), out, 8, &n);
	snprintf(msg, sizeof(msg), "input 11 bytes (frame is 12) -> %s", rname(r));
	check(msg, r == TEEC_ERROR_BAD_PARAMETERS);
	r = do_tag(s, in13, sizeof(in13), out, 8, &n);
	snprintf(msg, sizeof(msg), "input 13 bytes (frame is 12) -> %s", rname(r));
	check(msg, r == TEEC_ERROR_BAD_PARAMETERS);

	memset(out, CANARY, sizeof(out));
	r = do_tag(s, &f, sizeof(f), out, 7, &n);
	snprintf(msg, sizeof(msg), "output buffer 7 bytes (need 8) -> %s", rname(r));
	check(msg, r == TEEC_ERROR_SHORT_BUFFER);
	check("short output buffer left untouched", all_canary(out, 7));

	memset(out, CANARY, sizeof(out));
	r = do_tag(s, &f, sizeof(f), out, 8, &n);
	check("output buffer exactly 8 bytes -> success",
	      r == TEEC_SUCCESS && n == 8);

	memset(out, CANARY, sizeof(out));
	r = do_tag(s, &f, sizeof(f), out, sizeof(out), &n);
	snprintf(msg, sizeof(msg), "output buffer 64 bytes -> size reported back as %zu", n);
	check(msg, r == TEEC_SUCCESS && n == 8);
	check("bytes 8..63 of the 64-byte buffer untouched (canary intact)",
	      all_canary(out + 8, sizeof(out) - 8));

	memset(tag, 0, sizeof(tag));
	r = do_verify(s, &f, tag, 7, &ok);
	snprintf(msg, sizeof(msg), "verify with 7-byte tag -> %s", rname(r));
	check(msg, r == TEEC_ERROR_BAD_PARAMETERS);
	{
		uint8_t tag9[9] = { 0 };

		r = do_verify(s, &f, tag9, 9, &ok);
		snprintf(msg, sizeof(msg), "verify with 9-byte tag -> %s", rname(r));
		check(msg, r == TEEC_ERROR_BAD_PARAMETERS);
	}
}

static void part_d(TEEC_Session *pta)
{
	uint8_t buf[64];
	TEEC_Operation op;
	uint32_t origin;
	TEEC_Result r;
	char msg[110];
	static const size_t sizes[] = { 11, 12, 64 };

	printf("D. buffer-size edges on the PTA (cmd 15 returns 12 bytes)\n");
	for (unsigned int i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
		memset(buf, CANARY, sizeof(buf));
		memset(&op, 0, sizeof(op));
		op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_OUTPUT,
						 TEEC_MEMREF_TEMP_OUTPUT,
						 TEEC_NONE, TEEC_NONE);
		op.params[1].tmpref.buffer = buf;
		op.params[1].tmpref.size = sizes[i];
		r = TEEC_InvokeCommand(pta,
				PTA_RPI3_CAN_SPI_POC_CMD_GET_VALIDATION_DEBUG,
				&op, &origin);
		snprintf(msg, sizeof(msg), "buffer %zu bytes -> %s", sizes[i],
			 rname(r));
		if (sizes[i] < 12) {
			check(msg, r == TEEC_ERROR_SHORT_BUFFER);
			check("short buffer left untouched",
			      all_canary(buf, sizes[i]));
		} else {
			check(msg, r == TEEC_SUCCESS);
			check("bytes beyond the 12 returned are untouched",
			      all_canary(buf + 12, sizes[i] - 12));
		}
	}
}

static void part_e(TEEC_Context *ctx, TEEC_Session *s)
{
	enum { SHM = 4096, IN_OFF = 100, OUT_OFF = 300, OUT_LEN = 32 };
	TEEC_SharedMemory shm;
	struct secure_can_frame f = mkframe(0x100);
	uint8_t ref[8], *p, saved_in[sizeof(f)];
	size_t n;
	TEEC_Operation op;
	uint32_t origin;
	TEEC_Result r;
	int clean = 1;

	printf("E. allocated shared memory, partial references with offsets\n");
	memset(&shm, 0, sizeof(shm));
	shm.size = SHM;
	shm.flags = TEEC_MEM_INPUT | TEEC_MEM_OUTPUT;
	r = TEEC_AllocateSharedMemory(ctx, &shm);
	check("4096-byte shared block allocated", r == TEEC_SUCCESS);
	if (r != TEEC_SUCCESS)
		return;
	p = shm.buffer;
	memset(p, CANARY, SHM);
	memcpy(p + IN_OFF, &f, sizeof(f));
	memcpy(saved_in, p + IN_OFF, sizeof(f));

	memset(&op, 0, sizeof(op));
	op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_PARTIAL_INPUT,
					 TEEC_MEMREF_PARTIAL_OUTPUT, TEEC_NONE,
					 TEEC_NONE);
	op.params[0].memref.parent = &shm;
	op.params[0].memref.offset = IN_OFF;
	op.params[0].memref.size = sizeof(f);
	op.params[1].memref.parent = &shm;
	op.params[1].memref.offset = OUT_OFF;
	op.params[1].memref.size = OUT_LEN;
	r = TEEC_InvokeCommand(s, TA_SECURE_CAN_CMD_PROCESS_FRAME, &op, &origin);
	check("tag requested through offset windows (in@100, out@300 len 32)",
	      r == TEEC_SUCCESS);
	n = op.params[1].memref.size;
	check("size written back is 8", n == 8);

	if (do_tag(s, &f, sizeof(f), ref, sizeof(ref), &n) == TEEC_SUCCESS)
		check("tag at offset 300 equals the tag from the temporary-buffer path",
		      memcmp(p + OUT_OFF, ref, 8) == 0);

	for (size_t i = 0; i < SHM; i++) {
		if (i >= OUT_OFF && i < OUT_OFF + 8)
			continue;
		if (i >= IN_OFF && i < IN_OFF + sizeof(f))
			continue;
		if (p[i] != CANARY)
			clean = 0;
	}
	check("every byte outside the 8-byte output window is unchanged", clean);
	check("the input window was not modified",
	      memcmp(p + IN_OFF, saved_in, sizeof(f)) == 0);
	check("bytes 308..331 of the 32-byte output window untouched",
	      all_canary(p + OUT_OFF + 8, OUT_LEN - 8));
	TEEC_ReleaseSharedMemory(&shm);
}

static void part_f(TEEC_Context *ctx)
{
	static uint8_t user_buf[4096] __attribute__((aligned(4096)));
	TEEC_SharedMemory shm;
	TEEC_Result r;

	/*
	 * Success of TEEC_RegisterSharedMemory() alone proves nothing: when the
	 * kernel does not offer registration, libteec silently falls back to an
	 * allocated block plus copying. The decisive facts are the context's
	 * reg_mem flag (kernel capability) and whether a shadow buffer was used.
	 */
	printf("F. which shared-memory mode is in use? (static pool or dynamic)\n");
	printf("       kernel offers registration of user memory (ctx.reg_mem): %s\n",
	       ctx->imp.reg_mem ? "YES" : "NO");
	printf("       kernel accepts NULL memory references (ctx.memref_null): %s\n",
	       ctx->imp.memref_null ? "YES" : "NO");
	memset(&shm, 0, sizeof(shm));
	shm.buffer = user_buf;
	shm.size = sizeof(user_buf);
	shm.flags = TEEC_MEM_INPUT | TEEC_MEM_OUTPUT;
	r = TEEC_RegisterSharedMemory(ctx, &shm);
	printf("       TEEC_RegisterSharedMemory -> 0x%08x (%s)\n", (unsigned)r,
	       rname(r));
	if (r == TEEC_SUCCESS) {
		printf("       shadow buffer used by libteec: %s\n",
		       shm.imp.shadow_buffer ? "YES (data is copied)" : "NO");
		TEEC_ReleaseSharedMemory(&shm);
	}
	if (!ctx->imp.reg_mem)
		printf("       OBSERVATION: no dynamic shared memory. Every shared buffer "
		       "comes from the kernel's static pool (the reserved region), "
		       "and 'registration' is libteec allocating a block and copying "
		       "into it.\n");
	else
		printf("       OBSERVATION: user memory can be registered directly => "
		       "dynamic shared memory is active\n");
}

int main(void)
{
	TEEC_Context ctx;
	TEEC_Session ta, pta;
	TEEC_UUID ta_uuid = TA_SECURE_CAN_UUID;
	TEEC_UUID pta_uuid = PTA_RPI3_CAN_SPI_POC_UUID;
	uint32_t origin;
	TEEC_Result r = TEEC_InitializeContext(NULL, &ctx);

	if (r != TEEC_SUCCESS) {
		fprintf(stderr, "init context failed 0x%08x\n", (unsigned)r);
		return 1;
	}
	r = TEEC_OpenSession(&ctx, &ta, &ta_uuid, TEEC_LOGIN_PUBLIC, NULL, NULL,
			     &origin);
	if (r != TEEC_SUCCESS) {
		fprintf(stderr, "open TA failed 0x%08x\n", (unsigned)r);
		return 1;
	}
	r = TEEC_OpenSession(&ctx, &pta, &pta_uuid, TEEC_LOGIN_PUBLIC, NULL,
			     NULL, &origin);
	if (r != TEEC_SUCCESS) {
		fprintf(stderr, "open PTA failed 0x%08x\n", (unsigned)r);
		return 1;
	}

	part_a(&ta);
	part_b(&ta);
	part_c(&ta);
	part_d(&pta);
	part_e(&ctx, &ta);
	part_f(&ctx);

	TEEC_CloseSession(&pta);
	TEEC_CloseSession(&ta);
	TEEC_FinalizeContext(&ctx);
	printf("\nSUMMARY: %s (%d failure%s)\n", failures ? "FAIL" : "ALL PASS",
	       failures, failures == 1 ? "" : "s");
	return failures ? 1 : 0;
}
