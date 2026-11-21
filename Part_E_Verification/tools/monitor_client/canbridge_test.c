// SPDX-License-Identifier: BSD-2-Clause
/* Host test of canbridge (pack/unpack) with independent known-answer tags computed by Python's hmac module.
 * Build: make canbridge_test   (links the real mon_client.c for mon_build_frame; TEEC symbols are never called here) */
#include <stdio.h>
#include <string.h>
#include "canbridge.h"

static int n_pass, n_fail;
#define CHECK(c, name) do { if (c) { n_pass++; printf("  ok   %s\n", name); } else { n_fail++; printf("  FAIL %s\n", name); } } while (0)

static void hex(const uint8_t *p, size_t n, char *o) { static const char *d = "0123456789abcdef"; for (size_t i = 0; i < n; i++) { o[2 * i] = d[p[i] >> 4]; o[2 * i + 1] = d[p[i] & 15]; } o[2 * n] = 0; }

int main(void)
{
	uint8_t key[32], msg[MON_FRAME_LEN], ref[MON_FRAME_LEN]; struct canfd_frame f; char h[100]; unsigned i;
	static const struct { uint32_t id, ctr, pl; const char *tag; } kat[] = {
		{ 0x123, 5, 0xC0DE0005, "0293467bec3088eb285b4c7aed6a508e13df9faca2cfc0771597e6db513fab88" },
		{ 0x123, 1000, 0xC0DE03E8, "6420560134140b2fe3bc1566308d6d03a9634b53d0a2eb3340827069f9fd015d" },
		{ 0x80000123u, 7, 1, "00673e0606dd6d43140b2f0645170daeec39af2fc526981cf33ae8a4b83a09a8" },
	};
	for (i = 0; i < 32; i++) key[i] = (uint8_t)i;
	printf("canbridge host test\n");
	for (i = 0; i < 3; i++) {
		cb_pack(key, kat[i].id, kat[i].ctr, kat[i].pl, 0, &f);
		CHECK(f.len == 48 && f.can_id == kat[i].id, "frame length 48, identifier kept");
		hex(f.data + 8, 32, h); CHECK(!strcmp(h, kat[i].tag), "tag equals the independent HMAC-SHA256 known answer");
		CHECK(cb_unpack(&f, msg) == CB_OK, "unpack accepts the packed frame");
		mon_build_frame(key, kat[i].id, kat[i].ctr, kat[i].pl, ref);
		CHECK(!memcmp(msg, ref, MON_FRAME_LEN), "unpacked 44 bytes are exactly the monitor message");
	}
	cb_pack(key, 0x123, 5, 7, 0, &f);
	CHECK(!memcmp(f.data + 40, "\0\0\0\0\0\0\0\0", 8), "padding is zero");
	f.data[44] = 0xAA; CHECK(cb_unpack(&f, msg) == CB_OK, "padding changes are not noticed (padding is unauthenticated, as stated)");
	cb_pack(key, 0x123, 5, 7, 1, &f); cb_unpack(&f, msg); mon_build_frame(key, 0x123, 5, 7, ref);
	CHECK(msg[12] == (ref[12] ^ 1) && !memcmp(msg, ref, 12) && !memcmp(msg + 13, ref + 13, 31), "bad_tag flips exactly one bit of the tag");
	cb_pack(key, 0x123, 5, 7, 0, &f); f.len = 8;   CHECK(cb_unpack(&f, msg) == CB_BAD_LEN, "classic length 8 rejected");
	f.len = 47; CHECK(cb_unpack(&f, msg) == CB_BAD_LEN, "length 47 rejected");
	f.len = 64; CHECK(cb_unpack(&f, msg) == CB_BAD_LEN, "length 64 rejected");
	cb_pack(key, 0x123 | CAN_ERR_FLAG, 5, 7, 0, &f); CHECK(cb_unpack(&f, msg) == CB_ERR_FRAME, "error frame rejected");
	cb_pack(key, 0x123 | CAN_RTR_FLAG, 5, 7, 0, &f); CHECK(cb_unpack(&f, msg) == CB_RTR, "remote frame rejected");
	cb_pack(key, 0x123 | CAN_EFF_FLAG, 5, 7, 0, &f); cb_unpack(&f, msg);
	CHECK(msg[3] == 0x80 && msg[0] == 0x23 && msg[1] == 0x01, "extended-id flag reaches the message as bit 31 (the monitor will call it MALFORMED)");
	{ /* packet-file parser */
		char ln[256]; uint32_t dl = 0; struct canfd_frame g; int k;
		cb_pack(key, 0x123, 5, 0xC0DE0005, 0, &f);
		strcpy(ln, "250 123 "); for (k = 0; k < 48; k++) sprintf(ln + 8 + 2 * k, "%02x", f.data[k]);
		CHECK(cb_parse_line(ln, &dl, &g) == 0 && dl == 250 && g.can_id == 0x123 && g.len == 48 && !memcmp(g.data, f.data, 48), "packet-file line is parsed back to the same frame");
		strcat(ln, "  # comment"); CHECK(cb_parse_line(ln, &dl, &g) == 0, "trailing comment allowed");
		CHECK(cb_parse_line("# only a comment", &dl, &g) == 1 && cb_parse_line("   ", &dl, &g) == 1 && cb_parse_line("", &dl, &g) == 1, "comment and blank lines are skipped");
		strcpy(ln, "0 80000123 "); for (k = 0; k < 48; k++) sprintf(ln + 11 + 2 * k, "%02X", f.data[k]);
		CHECK(cb_parse_line(ln, &dl, &g) == 0 && g.can_id == 0x80000123u, "extended-id flag and upper-case hex are kept");
		strcpy(ln, "10 123 00"); CHECK(cb_parse_line(ln, &dl, &g) < 0, "too-short data is refused");
		strcpy(ln, "10 123 "); for (k = 0; k < 48; k++) sprintf(ln + 7 + 2 * k, "%02x", f.data[k]); ln[10] = 'z'; CHECK(cb_parse_line(ln, &dl, &g) < 0, "non-hex data is refused");
		strcpy(ln, "10 123 "); for (k = 0; k < 48; k++) sprintf(ln + 7 + 2 * k, "%02x", f.data[k]); strcat(ln, "00"); CHECK(cb_parse_line(ln, &dl, &g) < 0, "data longer than 48 bytes is refused");
		CHECK(cb_parse_line("abc 123 00", &dl, &g) < 0 && cb_parse_line("99999999 123 00", &dl, &g) < 0, "bad or huge delay is refused");
	}
	{ /* forwarder queue */
		static struct cb_queue q; uint8_t a[MON_FRAME_LEN], out[40 * MON_FRAME_LEN]; unsigned k, j; int ok = 1;
		cb_q_init(&q);
		CHECK(q.n == 0 && cb_q_peek(&q, 5, out) == 0, "a new queue is empty");
		for (k = 0; k < 10; k++) { memset(a, (int)k, sizeof a); cb_q_push(&q, a); }
		CHECK(q.n == 10 && cb_q_peek(&q, 4, out) == 4 && out[0] == 0 && out[44] == 1 && out[3 * 44] == 3 && q.n == 10, "peek returns the oldest first and removes nothing");
		CHECK(cb_q_peek(&q, 32, out) == 10, "peek never returns more than is queued");
		cb_q_drop(&q, 3); CHECK(q.n == 7 && cb_q_peek(&q, 1, out) == 1 && out[0] == 3, "drop removes the oldest");
		cb_q_drop(&q, 100); CHECK(q.n == 0 && q.dropped == 0, "dropping more than queued empties the queue and is not an overflow");
		cb_q_init(&q);
		for (k = 0; k < CB_QCAP; k++) { memset(a, (int)(k & 255), sizeof a); a[0] = (uint8_t)(k & 255); a[1] = (uint8_t)(k >> 8); cb_q_push(&q, a); }
		CHECK(q.n == CB_QCAP && q.dropped == 0, "the queue holds CB_QCAP messages without loss");
		memset(a, 0x77, sizeof a); a[0] = 0xFE; a[1] = 0xFE; j = (unsigned)cb_q_push(&q, a);
		CHECK(j == 1 && q.n == CB_QCAP && q.dropped == 1, "when full the OLDEST is dropped and counted");
		CHECK(cb_q_peek(&q, 1, out) == 1 && out[0] == 1 && out[1] == 0, "after one overflow the oldest left is message 1");
		for (k = 0; k < 2 * CB_QCAP + 88; k++) { memset(a, 0, sizeof a); a[0] = (uint8_t)k; a[1] = (uint8_t)(k >> 8); cb_q_push(&q, a); }
		{ uint8_t big[CB_QCAP * MON_FRAME_LEN]; static uint8_t chk[CB_QCAP * MON_FRAME_LEN]; unsigned n2 = cb_q_peek(&q, CB_QCAP, big); (void)chk;
		  for (k = 0; k < n2; k++) { unsigned want = 2 * CB_QCAP + 88 - CB_QCAP + k; if (big[k * 44] != (uint8_t)want || big[k * 44 + 1] != (uint8_t)(want >> 8)) ok = 0; }
		  CHECK(n2 == CB_QCAP && ok && q.dropped == 1 + 2 * CB_QCAP + 88, "after wrapping many times the queue holds exactly the newest CB_QCAP messages in order"); }
	}
	printf("canbridge_test: %d checks, %d failed\n", n_pass + n_fail, n_fail);
	return n_fail != 0;
}
