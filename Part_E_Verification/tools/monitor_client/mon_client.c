// SPDX-License-Identifier: BSD-2-Clause
#include <string.h>

#include <pta_rpi3_can_spi_poc.h>
#include "mon_client.h"

#define PT TEEC_PARAM_TYPES

static void clear_op(TEEC_Operation *op) { memset(op, 0, sizeof(*op)); }

static void take_health(const TEEC_Operation *op, struct mon_health *h)
{
	if (!h)
		return;
	h->p2a = op->params[2].value.a;
	h->p2b = op->params[2].value.b;
	h->p3a = op->params[3].value.a;
	h->p3b = op->params[3].value.b;
}

static TEEC_Result invoke(struct mon_ctx *c, uint32_t cmd, TEEC_Operation *op)
{
	uint32_t origin = 0;

	return TEEC_InvokeCommand(&c->session, cmd, op, &origin);
}

TEEC_Result mon_open(struct mon_ctx *c)
{
	TEEC_UUID uuid = PTA_RPI3_CAN_SPI_POC_UUID;
	uint32_t origin = 0;
	TEEC_Result r;

	memset(c, 0, sizeof(*c));
	r = TEEC_InitializeContext(NULL, &c->context);
	if (r != TEEC_SUCCESS)
		return r;
	r = TEEC_OpenSession(&c->context, &c->session, &uuid, TEEC_LOGIN_PUBLIC, NULL, NULL, &origin);
	if (r != TEEC_SUCCESS) {
		TEEC_FinalizeContext(&c->context);
		return r;
	}
	c->open = 1;
	return TEEC_SUCCESS;
}

void mon_close(struct mon_ctx *c)
{
	if (!c->open)
		return;
	TEEC_CloseSession(&c->session);
	TEEC_FinalizeContext(&c->context);
	c->open = 0;
}

TEEC_Result mon_submit_frame(struct mon_ctx *c, const uint8_t frame[MON_FRAME_LEN], uint32_t *verdict, uint32_t *detail, struct mon_health *h)
{
	TEEC_Operation op;
	uint8_t buf[MON_FRAME_LEN];
	TEEC_Result r;

	memcpy(buf, frame, sizeof(buf));
	clear_op(&op);
	op.paramTypes = PT(TEEC_VALUE_OUTPUT, TEEC_MEMREF_TEMP_INPUT, TEEC_VALUE_OUTPUT, TEEC_VALUE_OUTPUT);
	op.params[1].tmpref.buffer = buf;
	op.params[1].tmpref.size = sizeof(buf);
	r = invoke(c, PTA_RPI3_CAN_SPI_POC_CMD_MON_SUBMIT_FRAME, &op);
	if (r == TEEC_SUCCESS) {
		if (verdict) *verdict = op.params[0].value.a;
		if (detail) *detail = op.params[0].value.b;
	}
	take_health(&op, h);
	return r;
}

TEEC_Result mon_get_events(struct mon_ctx *c, uint32_t first_seq, uint32_t max, struct mon_event ev[MON_EVENT_MAX], uint32_t *count, int *more, uint32_t *lost, struct mon_health *h)
{
	TEEC_Operation op;
	struct mon_event buf[MON_EVENT_MAX];
	TEEC_Result r;
	uint32_t n;

	if (max < 1 || max > MON_EVENT_MAX)
		return TEEC_ERROR_BAD_PARAMETERS;
	clear_op(&op);
	memset(buf, 0, sizeof(buf));
	op.paramTypes = PT(TEEC_VALUE_INOUT, TEEC_MEMREF_TEMP_OUTPUT, TEEC_VALUE_OUTPUT, TEEC_VALUE_OUTPUT);
	op.params[0].value.a = first_seq;
	op.params[0].value.b = max;
	op.params[1].tmpref.buffer = buf;
	op.params[1].tmpref.size = sizeof(struct mon_event) * max;
	r = invoke(c, PTA_RPI3_CAN_SPI_POC_CMD_MON_GET_EVENTS, &op);
	if (r == TEEC_SUCCESS) {
		n = op.params[0].value.a & 0xFFU;
		if (n > max || n * sizeof(struct mon_event) > op.params[1].tmpref.size)
			return TEEC_ERROR_GENERIC;		/* the monitor broke its own contract: do not trust the buffer */
		memcpy(ev, buf, n * sizeof(struct mon_event));
		if (count) *count = n;
		if (more) *more = (op.params[0].value.a >> 8) & 1U;
		if (lost) *lost = op.params[0].value.b;
	}
	take_health(&op, h);
	return r;
}

TEEC_Result mon_ack_events(struct mon_ctx *c, uint32_t upto_seq, uint32_t *effective, struct mon_health *h)
{
	TEEC_Operation op;
	TEEC_Result r;

	clear_op(&op);
	op.paramTypes = PT(TEEC_VALUE_INOUT, TEEC_NONE, TEEC_VALUE_OUTPUT, TEEC_VALUE_OUTPUT);
	op.params[0].value.a = upto_seq;
	op.params[0].value.b = 0;
	r = invoke(c, PTA_RPI3_CAN_SPI_POC_CMD_MON_ACK_EVENTS, &op);
	if (r == TEEC_SUCCESS && effective)
		*effective = op.params[0].value.b;
	take_health(&op, h);
	return r;
}

static TEEC_Result get_status(struct mon_ctx *c, uint32_t page, void *out, struct mon_health *h)
{
	TEEC_Operation op;
	uint8_t buf[128];
	TEEC_Result r;

	clear_op(&op);
	memset(buf, 0, sizeof(buf));
	op.paramTypes = PT(TEEC_VALUE_INOUT, TEEC_MEMREF_TEMP_OUTPUT, TEEC_VALUE_OUTPUT, TEEC_VALUE_OUTPUT);
	op.params[0].value.a = page;
	op.params[0].value.b = 0;
	op.params[1].tmpref.buffer = buf;
	op.params[1].tmpref.size = sizeof(buf);
	r = invoke(c, PTA_RPI3_CAN_SPI_POC_CMD_MON_GET_STATUS, &op);
	if (r == TEEC_SUCCESS) {
		if (op.params[1].tmpref.size != sizeof(buf) || op.params[0].value.a != sizeof(buf))
			return TEEC_ERROR_GENERIC;
		memcpy(out, buf, sizeof(buf));
	}
	take_health(&op, h);
	return r;
}

TEEC_Result mon_get_status0(struct mon_ctx *c, struct mon_status0 *out, struct mon_health *h) { return get_status(c, 0, out, h); }
TEEC_Result mon_get_status1(struct mon_ctx *c, struct mon_status1 *out, struct mon_health *h) { return get_status(c, 1, out, h); }

TEEC_Result mon_set_idle(struct mon_ctx *c, uint32_t mode, uint32_t ms, uint32_t *verdict, uint32_t *remaining_ms, struct mon_health *h)
{
	TEEC_Operation op;
	TEEC_Result r;

	clear_op(&op);
	op.paramTypes = PT(TEEC_VALUE_INOUT, TEEC_NONE, TEEC_VALUE_OUTPUT, TEEC_VALUE_OUTPUT);
	op.params[0].value.a = mode;
	op.params[0].value.b = ms;
	r = invoke(c, PTA_RPI3_CAN_SPI_POC_CMD_MON_SET_IDLE, &op);
	if (r == TEEC_SUCCESS) {
		if (verdict) *verdict = op.params[0].value.a;
		if (remaining_ms) *remaining_ms = op.params[0].value.b;
	}
	take_health(&op, h);
	return r;
}

TEEC_Result mon_get_nonce(struct mon_ctx *c, uint64_t *nonce, uint32_t *seq, uint32_t *state, struct mon_health *h)
{
	TEEC_Operation op;
	TEEC_Result r;

	clear_op(&op);
	op.paramTypes = PT(TEEC_VALUE_OUTPUT, TEEC_VALUE_OUTPUT, TEEC_VALUE_OUTPUT, TEEC_VALUE_OUTPUT);
	r = invoke(c, PTA_RPI3_CAN_SPI_POC_CMD_MON_GET_NONCE, &op);
	if (r == TEEC_SUCCESS) {
		if (nonce) *nonce = (uint64_t)op.params[0].value.a | ((uint64_t)op.params[0].value.b << 32);
		if (seq) *seq = op.params[1].value.a;
		if (state) *state = op.params[1].value.b;
	}
	take_health(&op, h);
	return r;
}

TEEC_Result mon_submit_answer(struct mon_ctx *c, uint32_t seq, uint64_t nonce, uint32_t *verdict, uint32_t *detail, struct mon_health *h)
{
	TEEC_Operation op;
	TEEC_Result r;

	clear_op(&op);
	op.paramTypes = PT(TEEC_VALUE_INOUT, TEEC_VALUE_INPUT, TEEC_VALUE_OUTPUT, TEEC_VALUE_OUTPUT);
	op.params[0].value.a = seq;
	op.params[0].value.b = 0;
	op.params[1].value.a = (uint32_t)nonce;
	op.params[1].value.b = (uint32_t)(nonce >> 32);
	r = invoke(c, PTA_RPI3_CAN_SPI_POC_CMD_MON_SUBMIT_HB_ANSWER, &op);
	if (r == TEEC_SUCCESS) {
		if (verdict) *verdict = op.params[0].value.a;
		if (detail) *detail = op.params[0].value.b;
	}
	take_health(&op, h);
	return r;
}

/* ---------------------------------------------------------------- SHA-256 / HMAC (standalone, for frame tags) */
static const uint32_t K256[64] = {
	0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
	0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
	0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
	0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};
struct sha256 { uint32_t h[8]; uint8_t buf[64]; size_t n; uint64_t total; };
#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha_block(struct sha256 *s, const uint8_t *p)
{
	uint32_t w[64], a, b, c, d, e, f, g, h, t1, t2;
	int i;

	for (i = 0; i < 16; i++)
		w[i] = ((uint32_t)p[4*i] << 24) | ((uint32_t)p[4*i+1] << 16) | ((uint32_t)p[4*i+2] << 8) | p[4*i+3];
	for (i = 16; i < 64; i++) {
		uint32_t s0 = ROR(w[i-15], 7) ^ ROR(w[i-15], 18) ^ (w[i-15] >> 3);
		uint32_t s1 = ROR(w[i-2], 17) ^ ROR(w[i-2], 19) ^ (w[i-2] >> 10);
		w[i] = w[i-16] + s0 + w[i-7] + s1;
	}
	a = s->h[0]; b = s->h[1]; c = s->h[2]; d = s->h[3]; e = s->h[4]; f = s->h[5]; g = s->h[6]; h = s->h[7];
	for (i = 0; i < 64; i++) {
		t1 = h + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K256[i] + w[i];
		t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
		h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
	}
	s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += h;
}

static void sha_init(struct sha256 *s)
{
	static const uint32_t iv[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
	memcpy(s->h, iv, sizeof(iv)); s->n = 0; s->total = 0;
}

static void sha_update(struct sha256 *s, const uint8_t *p, size_t len)
{
	s->total += len;
	while (len) {
		size_t k = 64 - s->n;
		if (k > len) k = len;
		memcpy(s->buf + s->n, p, k); s->n += k; p += k; len -= k;
		if (s->n == 64) { sha_block(s, s->buf); s->n = 0; }
	}
}

static void sha_final(struct sha256 *s, uint8_t out[32])
{
	uint64_t bits = s->total * 8;
	uint8_t pad = 0x80, z = 0, l[8];
	int i;

	sha_update(s, &pad, 1);
	while (s->n != 56) sha_update(s, &z, 1);
	for (i = 0; i < 8; i++) l[i] = (uint8_t)(bits >> (56 - 8 * i));
	sha_update(s, l, 8);
	for (i = 0; i < 8; i++) { out[4*i] = (uint8_t)(s->h[i] >> 24); out[4*i+1] = (uint8_t)(s->h[i] >> 16); out[4*i+2] = (uint8_t)(s->h[i] >> 8); out[4*i+3] = (uint8_t)s->h[i]; }
}

void mon_hmac_sha256(const uint8_t *key, size_t keylen, const uint8_t *msg, size_t len, uint8_t out[32])
{
	uint8_t k[64] = {0}, ipad[64], opad[64], inner[32];
	struct sha256 s;
	int i;

	if (keylen > 64) { sha_init(&s); sha_update(&s, key, keylen); sha_final(&s, k); }
	else memcpy(k, key, keylen);
	for (i = 0; i < 64; i++) { ipad[i] = k[i] ^ 0x36; opad[i] = k[i] ^ 0x5c; }
	sha_init(&s); sha_update(&s, ipad, 64); sha_update(&s, msg, len); sha_final(&s, inner);
	sha_init(&s); sha_update(&s, opad, 64); sha_update(&s, inner, 32); sha_final(&s, out);
}

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

void mon_build_frame(const uint8_t key[32], uint32_t can_id, uint32_t counter, uint32_t payload, uint8_t out[MON_FRAME_LEN])
{
	put32(out, can_id); put32(out + 4, counter); put32(out + 8, payload);
	mon_hmac_sha256(key, 32, out, 12, out + 12);
}

/* ---------------------------------------------------------------- names */
static const char *const EVN[MON_EVENT_TYPES] = {
	"AUTH_FAIL", "REPLAY", "TIMEOUT", "ALTERED", "UNACCOUNTED", "CHIPB_FAULT", "MALFORMED", "COUNTER_GAP", "COUNTER_RESYNC",
	"TRAFFIC_RECOVERED", "HEARTBEAT_RECOVERED", "MODE_CHANGED", "LEASE_GRANTED", "LEASE_RENEWED", "LEASE_REFUSED", "LOCK_TIMEOUT",
	"MON_STARVED", "HB_SEQ_EXHAUSTED", "LEGACY_CMD_REFUSED", "LEGACY_CMD_USED", "BAD_NONCE", "STALE_ANSWER", "UNISSUED_ANSWER",
	"DUPLICATE_ANSWER", "LATE_ANSWER"
};
const char *mon_event_name(unsigned t) { return t < MON_EVENT_TYPES ? EVN[t] : "?"; }
const char *mon_verdict17_name(unsigned v) { static const char *const n[] = {"ACCEPTED","GAP","RESYNC","MALFORMED","AUTH_FAIL","REPLAY","DUPLICATE","CONFLICT"}; return v < 8 ? n[v] : "?"; }
const char *mon_verdict21_name(unsigned v) { static const char *const n[] = {"GRANTED","RENEWED","RETURNED","REFUSED_CAP","NO_CHANGE"}; return v < 5 ? n[v] : "?"; }
const char *mon_verdict23_name(unsigned v) { static const char *const n[] = {"ON_TIME","LATE","STALE","UNISSUED","BAD_NONCE","DUPLICATE"}; return v < 6 ? n[v] : "?"; }
const char *mon_hbstate_name(unsigned s) { static const char *const n[] = {"NONE","OPEN","ANSWERED","MISSED","LATE_ANSWERED"}; return s < 5 ? n[s] : "?"; }
