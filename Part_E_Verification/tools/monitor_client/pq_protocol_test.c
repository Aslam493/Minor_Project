// SPDX-License-Identifier: BSD-2-Clause
/*
 * pq_protocol_test - board test of the Mode P request/reply PROTOCOL (commands 30 and 31), with deliberate mistakes that the real forwarder never makes.
 *
 *   usage: pq_protocol_test          (Mode P image only; stop can_forwarder first; no key needed)
 *
 * What it checks, each with the exact expected verdict and the exact change in Secure World counters (events and status page 2):
 *   P1  a new request appears about every 50 ms, numbers strictly increase by 1 (observed over 40 requests; Linux-side timing is coarse)
 *   P2  an answer for request+1 (not issued yet) -> UNISSUED, a packet inside it is NOT judged, the open request stays open
 *   P3  an answer for request-1 -> STALE, a packet inside it is NOT judged, the open request stays open
 *   P4  the right answer with one deliberately wrong packet -> ON_TIME, the packet IS judged (AUTH_FAIL), request answered
 *   P5  the same answer again -> DUPLICATE, a packet inside it is NOT judged
 *   P6  an answer after the 40 ms deadline -> LATE (the packet IS still judged), LATE_ANSWER event, status page 2 n_late +1
 *   P7  a request that is never answered is seen as MISSED by command 30
 *   P8  parameter errors: 33 packets, wrong parameter types -> BAD_PARAMETERS, nothing changes
 *   P9  status page 2: bucket sum equals ON_TIME + LATE, last answer time is plausible
 * The "wrong packet" is a 44-byte message with can_id 0x123, a counter, a payload and an INVALID tag: Secure World must judge it AUTH_FAIL and
 * must not move its counter. Writes nothing persistent. Exit 1 if any check fails.
 */
#define _GNU_SOURCE
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pta_rpi3_can_spi_poc.h>
#include "mon_client.h"

#define BAD_PARAMETERS 0xFFFF0006u
static struct mon_ctx C;
static int n_pass, n_fail;
static void CK(int ok, const char *id, const char *fmt, ...)
{ va_list a; char b[300]; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a); printf("  %s  %-4s %s\n", ok ? "PASS" : "FAIL", id, b); if (ok) n_pass++; else n_fail++; }
static uint64_t now_us(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000u + (uint64_t)t.tv_nsec / 1000u; }
static void msleep(unsigned ms) { struct timespec t = { ms / 1000, (long)(ms % 1000) * 1000000L }; nanosleep(&t, NULL); }

static int ev_idx(const char *name) { unsigned i; for (i = 0; i < MON_EVENT_TYPES; i++) if (!strcmp(mon_event_name(i), name)) return (int)i; return -1; }
static struct mon_status1 st1(void) { struct mon_status1 s; struct mon_health h; TEEC_Result r; int t; memset(&s, 0, sizeof s); for (t = 0; t < 50; t++) { r = mon_get_status1(&C, &s, &h); if (r != TEEC_ERROR_BUSY) break; msleep(2); } return s; }
static struct mon_status2 st2(void) { struct mon_status2 s; struct mon_health h; TEEC_Result r; int t; memset(&s, 0, sizeof s); for (t = 0; t < 50; t++) { r = mon_get_status2(&C, &s, &h); if (r != TEEC_ERROR_BUSY) break; msleep(2); } return s; }
static uint32_t evc(const struct mon_status1 *s, const char *n) { int i = ev_idx(n); return i < 0 ? 0 : s->ev_count[i]; }

static TEEC_Result req(uint32_t *seq, uint32_t *state, uint32_t *mb, uint32_t *due)
{ struct mon_health h; TEEC_Result r; int t; for (t = 0; t < 50; t++) { r = mon_get_packet_request(&C, seq, state, mb, due, &h); if (r != TEEC_ERROR_BUSY) break; msleep(2); } return r; }
static TEEC_Result ans(uint32_t seq, uint32_t n, const uint8_t *pk, uint32_t *v, uint32_t *proc)
{ struct mon_health h; TEEC_Result r; int t; for (t = 0; t < 50; t++) { r = mon_submit_packets(&C, seq, n, pk, v, proc, &h); if (r != TEEC_ERROR_BUSY) break; msleep(2); } return r; }
/* wait until a request newer than 'after' is OPEN; returns its number, 0 on timeout */
static uint32_t wait_new_open(uint32_t after, unsigned timeout_ms, uint64_t *t_seen_us)
{ uint64_t end = now_us() + (uint64_t)timeout_ms * 1000u; uint32_t s, st, mb, d; while (now_us() < end) { if (!req(&s, &st, &mb, &d) && st == 1 && s > after) { if (t_seen_us) *t_seen_us = now_us(); return s; } } return 0; }

static void wrong_packet(uint8_t m[44], uint32_t counter)
{ memset(m, 0, 44); m[0] = 0x23; m[1] = 0x01; m[4] = (uint8_t)counter; m[5] = (uint8_t)(counter >> 8); m[8] = 0x11; memset(m + 12, 0xA5, 32); }	/* id 0x123, bad tag */

int main(void)
{
	TEEC_Result r; uint32_t S, S2, v, proc, seq0; uint8_t pk[MON_PQ_MAX_BATCH * MON_FRAME_LEN]; struct mon_status1 a, b; struct mon_status2 p, q; struct mon_status0 z; struct mon_health h;
	uint64_t t0, t1, first = 0, prev = 0, mn = ~0ull, mx = 0, sum = 0; unsigned i, nint = 0; uint32_t last;

	r = mon_open(&C); if (r) { fprintf(stderr, "cannot open the monitor (0x%08x)\n", (unsigned)r); return 1; }
	memset(&z, 0, sizeof z); (void)mon_get_status0(&C, &z, &h);
	if (z.mode != 2) { printf("NOT the Mode P image (mode %u): stopping\n", z.mode); mon_close(&C); return 2; }
	printf("pq_protocol_test: Mode P image, last_counter %u\n", z.last_counter);

	printf("== P1 request numbers and spacing (40 requests)\n");
	S = wait_new_open(0, 500, &first); CK(S != 0, "P1a", "a request is open (number %u)", S);
	if (S) { last = S; prev = first; for (i = 0; i < 40; i++) { uint64_t t; uint32_t n2 = wait_new_open(last, 300, &t); if (!n2) break; if (n2 != last + 1) { nint += 1000; } sum += t - prev; if (t - prev < mn) mn = t - prev; if (t - prev > mx) mx = t - prev; prev = t; last = n2; nint++; } }
	CK(nint == 40, "P1b", "40 consecutive requests seen, each number exactly previous + 1 (seen %u; a value of 1000 or more means a number was skipped)", nint);
	CK(nint == 40 && sum / 40 > 40000 && sum / 40 < 60000, "P1c", "average spacing %" PRIu64 " us (expected about 50000; Linux-side observation, min %" PRIu64 " us, max %" PRIu64 " us)", nint ? sum / nint : 0, mn == ~0ull ? 0 : mn, mx);

	printf("== P2 UNISSUED: an answer for the NEXT request number\n");
	S = wait_new_open(0, 300, NULL); a = st1(); p = st2(); wrong_packet(pk, 5000);
	r = ans(S + 1, 1, pk, &v, &proc); b = st1();
	CK(!r, "P2a", "the call succeeded (0x%08x)", (unsigned)r);
	CK(!r && !strcmp(mon_verdict31_name(v), "UNISSUED") && proc == 0, "P2b", "verdict %s (expected UNISSUED), packets judged %u (expected 0)", mon_verdict31_name(v), proc);
	CK(evc(&b, "UNISSUED_ANSWER") == evc(&a, "UNISSUED_ANSWER") + 1 && b.v_frame_total == a.v_frame_total && evc(&b, "AUTH_FAIL") == evc(&a, "AUTH_FAIL"), "P2c", "UNISSUED_ANSWER +1, the packet inside was NOT judged (frame total and AUTH_FAIL unchanged)");
	{ uint32_t s2, st2_, mb2, d2; int ok = !req(&s2, &st2_, &mb2, &d2); CK(ok && (s2 > S || (s2 == S && st2_ == 1)), "P2d", "the open request is still open or a newer one was issued (seq %u state %u)", s2, st2_); }

	printf("== P3 STALE: an answer for an old request number\n");
	S = wait_new_open(S, 300, NULL); a = st1(); wrong_packet(pk, 5001);
	r = ans(S - 1, 1, pk, &v, &proc); b = st1();
	CK(!r && !strcmp(mon_verdict31_name(v), "STALE") && proc == 0, "P3a", "verdict %s (expected STALE), packets judged %u (expected 0)", mon_verdict31_name(v), proc);
	CK(evc(&b, "STALE_ANSWER") == evc(&a, "STALE_ANSWER") + 1 && b.v_frame_total == a.v_frame_total, "P3b", "STALE_ANSWER +1, the packet inside was NOT judged");

	printf("== P4 ON_TIME with one deliberately wrong packet\n");
	S = wait_new_open(S, 300, &t0); a = st1(); p = st2(); wrong_packet(pk, 5002);
	r = ans(S, 1, pk, &v, &proc); t1 = now_us(); b = st1(); q = st2();
	CK(!r && !strcmp(mon_verdict31_name(v), "ON_TIME") && proc == 1, "P4a", "verdict %s, packets judged %u (expected ON_TIME, 1); Linux-side time from first sight to reply %" PRIu64 " us", mon_verdict31_name(v), proc, t1 - t0);
	CK(evc(&b, "AUTH_FAIL") == evc(&a, "AUTH_FAIL") + 1 && b.v_frame_total == a.v_frame_total + 1, "P4b", "the wrong packet was judged: AUTH_FAIL +1, frame total +1");
	{ struct mon_status0 z2; memset(&z2, 0, sizeof z2); (void)mon_get_status0(&C, &z2, &h); CK(z2.last_counter == z.last_counter, "P4c", "AUTH_FAIL did not move last_counter (%u -> %u)", z.last_counter, z2.last_counter); }
	CK(q.n_ontime == p.n_ontime + 1 && q.n_late == p.n_late, "P4d", "status page 2: ON_TIME +1 (%u -> %u), LATE unchanged", p.n_ontime, q.n_ontime);
	CK(q.last_ticks > 0 && q.last_ticks < 40u * 19200u, "P4e", "Secure World measured %.3f ms from request issue to this answer (must be under 40 ms)", q.last_ticks / 19200.0);

	printf("== P5 DUPLICATE: the same answer again\n");
	a = st1(); wrong_packet(pk, 5003);
	r = ans(S, 1, pk, &v, &proc); b = st1();
	CK(!r && !strcmp(mon_verdict31_name(v), "DUPLICATE") && proc == 0, "P5a", "verdict %s (expected DUPLICATE), packets judged %u (expected 0)", mon_verdict31_name(v), proc);
	CK(evc(&b, "DUPLICATE_ANSWER") == evc(&a, "DUPLICATE_ANSWER") + 1 && b.v_frame_total == a.v_frame_total, "P5b", "DUP_ANSWER +1, the packet inside was NOT judged");

	printf("== P6 LATE: answer after the 40 ms deadline\n");
	S2 = wait_new_open(S, 300, &t0); p = st2(); a = st1();
	if (S2) { msleep(43); wrong_packet(pk, 5004); r = ans(S2, 1, pk, &v, &proc); b = st1(); q = st2(); seq0 = S2; }
	else { r = 1; seq0 = 0; }
	CK(S2 && !r && !strcmp(mon_verdict31_name(v), "LATE") && proc == 1, "P6a", "verdict %s, packets judged %u (expected LATE, 1: a late reply is still judged)", S2 && !r ? mon_verdict31_name(v) : "-", proc);
	CK(S2 && evc(&b, "LATE_ANSWER") == evc(&a, "LATE_ANSWER") + 1 && q.n_late == p.n_late + 1, "P6b", "LATE_ANSWER +1 and status page 2 n_late %u -> %u", p.n_late, q.n_late);
	CK(S2 && q.last_ticks >= 40u * 19200u, "P6c", "Secure World measured %.3f ms for this answer (must be over 40 ms)", q.last_ticks / 19200.0);
	(void)seq0;

	printf("== P7 a request nobody answers is seen as MISSED\n");
	{ uint32_t S3 = wait_new_open(S2, 300, NULL), s, stt, m, d; int seen = 0; uint64_t end = now_us() + 49000; while (S3 && now_us() < end) { if (!req(&s, &stt, &m, &d) && s == S3 && stt == 3) { seen = 1; break; } }
	  CK(S3 && seen, "P7", "request %u went from OPEN to MISSED when left unanswered for more than 40 ms (state 3)", S3); }

	printf("== P8 parameter errors change nothing\n");
	a = st1(); p = st2();
	{ TEEC_Operation op; uint32_t org = 0; static uint8_t big[33 * MON_FRAME_LEN]; memset(&op, 0, sizeof op);
	  op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_INOUT, TEEC_MEMREF_TEMP_INPUT, TEEC_VALUE_OUTPUT, TEEC_VALUE_OUTPUT);
	  op.params[0].value.a = 1; op.params[0].value.b = 33; op.params[1].tmpref.buffer = big; op.params[1].tmpref.size = sizeof big;
	  r = TEEC_InvokeCommand(&C.session, PTA_RPI3_CAN_SPI_POC_CMD_MON_SUBMIT_PACKETS, &op, &org); CK(r == BAD_PARAMETERS, "P8a", "33 packets -> BAD_PARAMETERS (0x%08x)", (unsigned)r);
	  memset(&op, 0, sizeof op); op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_INOUT, TEEC_MEMREF_TEMP_INPUT, TEEC_VALUE_OUTPUT, TEEC_VALUE_OUTPUT);
	  op.params[0].value.a = 1; op.params[0].value.b = 2; op.params[1].tmpref.buffer = big; op.params[1].tmpref.size = MON_FRAME_LEN;
	  r = TEEC_InvokeCommand(&C.session, PTA_RPI3_CAN_SPI_POC_CMD_MON_SUBMIT_PACKETS, &op, &org); CK(r == BAD_PARAMETERS, "P8b", "2 packets announced but only 1 packet of bytes -> BAD_PARAMETERS (0x%08x)", (unsigned)r);
	  memset(&op, 0, sizeof op); op.paramTypes = TEEC_PARAM_TYPES(TEEC_NONE, TEEC_NONE, TEEC_NONE, TEEC_NONE);
	  r = TEEC_InvokeCommand(&C.session, PTA_RPI3_CAN_SPI_POC_CMD_MON_SUBMIT_PACKETS, &op, &org); CK(r == BAD_PARAMETERS, "P8c", "command 31 with the wrong parameter types -> BAD_PARAMETERS (0x%08x)", (unsigned)r);
	  r = TEEC_InvokeCommand(&C.session, PTA_RPI3_CAN_SPI_POC_CMD_MON_GET_PACKET_REQUEST, &op, &org); CK(r == BAD_PARAMETERS, "P8d", "command 30 with the wrong parameter types -> BAD_PARAMETERS (0x%08x)", (unsigned)r); }
	b = st1(); q = st2();
	CK(b.v_frame_total == a.v_frame_total && evc(&b, "AUTH_FAIL") == evc(&a, "AUTH_FAIL") && q.n_ontime == p.n_ontime && q.n_late == p.n_late, "P8e", "no packet judged and no request answered by the bad calls");

	printf("== P9 status page 2 is consistent\n");
	q = st2(); { uint64_t sumb = 0; unsigned k; for (k = 0; k < MON_PQ_BUCKETS; k++) sumb += q.bucket[k];
	  CK(sumb == (uint64_t)q.n_ontime + q.n_late, "P9a", "sum of the 24 buckets (%" PRIu64 ") = ON_TIME %u + LATE %u", sumb, q.n_ontime, q.n_late);
	  CK(q.version == 1 && q.length == 128 && q.rsv == 0 && q.max_ticks >= q.last_ticks, "P9b", "version 1, length 128, reserved 0, max %.3f ms >= last %.3f ms", (double)q.max_ticks / 19200.0, q.last_ticks / 19200.0); }

	printf("SUMMARY %d PASS, %d FAIL\n", n_pass, n_fail);
	mon_close(&C);
	return n_fail ? 1 : 0;
}
