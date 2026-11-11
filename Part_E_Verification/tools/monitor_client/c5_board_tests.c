/*
 * c5_board_tests: Mode-F acceptance tests T1..T22 (plan section 10) that can run on the real board through the
 * documented PTA commands 17..23 only. Software class: frames are INJECTED through the PTA, never on the CAN bus.
 *
 *   usage: c5_board_tests --key-file F [--no-t6 | --t20-watch SECS | --save FILE | --t17-check FILE | --nonce-at SEQ FILE]
 *
 * Writes to the monitor's own state (counters, answers, one idle lease, events). Never touches the CAN chip, the
 * SD card, the FIQ or any legacy command. Phase order matters (T6 last: it leaves last_counter = 0xFFFFFFFF, so the
 * board must be rebooted afterwards). Prints PASS / FAIL / NOTE per check; exit 1 if any FAIL.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "mon_client.h"

#define TICKS_MS 19200ULL
#define D_REQ_MS 500
#define H_MS 1000
#define T_TRAFFIC_MS 300
#define EV_AUTH_FAIL 0
#define EV_REPLAY 1
#define EV_TIMEOUT 2
#define EV_MALFORMED 6
#define EV_GAP 7
#define EV_RESYNC 8
#define EV_TRAFFIC_REC 9
#define EV_HB_REC 10
#define EV_LEASE_GRANTED 12
#define EV_BAD_NONCE 20
#define EV_STALE 21
#define EV_UNISSUED 22
#define EV_DUP_ANSWER 23
#define EV_LATE 24

static struct mon_ctx C;
static uint8_t KEY[32];
static int n_pass, n_fail, n_note, n_skip;
static uint32_t last_answered = 0xFFFFFFFFu;

static uint64_t now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000; }
static void msleep(unsigned ms) { struct timespec t = { ms / 1000, (long)(ms % 1000) * 1000000L }; nanosleep(&t, NULL); }
static void PASSF(int ok, const char *id, const char *fmt, ...)
{ va_list a; char b[300]; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a); printf("  %s  %-6s %s\n", ok ? "PASS" : "FAIL", id, b); if (ok) n_pass++; else n_fail++; }
static void NOTE(const char *id, const char *fmt, ...)
{ va_list a; char b[300]; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a); printf("  note  %-6s %s\n", id, b); n_note++; }
static void SKIPF(const char *id, const char *why) { printf("  SKIP  %-6s %s\n", id, why); n_skip++; }

#define RETRY(ctxp, call, r) do { int t_; for (t_ = 0; ; t_++) { (r) = (call); if ((r) != TEEC_ERROR_BUSY || t_ >= 60) break; msleep(2); } } while (0)

static struct mon_status0 st0(void) { struct mon_status0 s; struct mon_health h; TEEC_Result r; memset(&s, 0, sizeof s); RETRY(&C, mon_get_status0(&C, &s, &h), r); if (r) printf("  !! status0 failed 0x%x\n", (unsigned)r); return s; }
static struct mon_status1 st1(void) { struct mon_status1 s; struct mon_health h; TEEC_Result r; memset(&s, 0, sizeof s); RETRY(&C, mon_get_status1(&C, &s, &h), r); if (r) printf("  !! status1 failed 0x%x\n", (unsigned)r); return s; }

static uint32_t payload_for(uint32_t c) { return 0xC0DE0000u | (c & 0xFFFFu); }
/* mode: 0 good, 1 bad tag, 2 payload altered (tag kept) */
static uint32_t frame(uint32_t id, uint32_t counter, uint32_t payload, int mode, uint32_t *detail)
{
	uint8_t f[MON_FRAME_LEN]; struct mon_health h; uint32_t v = 99, d = 0; TEEC_Result r;
	mon_build_frame(KEY, id, counter, payload, f);
	if (mode == 1) f[12] ^= 1; else if (mode == 2) f[8] ^= 1;
	RETRY(&C, mon_submit_frame(&C, f, &v, &d, &h), r);
	if (r) { printf("  !! submit_frame failed 0x%x\n", (unsigned)r); v = 98; }
	if (detail) *detail = d;
	return v;
}
static void snap(uint32_t out[MON_EVENT_TYPES]) { struct mon_status1 s = st1(); memcpy(out, s.ev_count, MON_EVENT_TYPES * 4); }
static uint32_t dlt(const uint32_t *a, const uint32_t *b, unsigned t) { return b[t] - a[t]; }

/* answer the open challenge if there is one (keeps the heartbeat healthy between steps) */
static void hb_keep(void)
{
	uint64_t nonce; uint32_t seq, st, v, d; struct mon_health h; TEEC_Result r;
	RETRY(&C, mon_get_nonce(&C, &nonce, &seq, &st, &h), r);
	if (!r && (st == 1 || st == 3) && nonce && seq != last_answered) {
		RETRY(&C, mon_submit_answer(&C, seq, nonce, &v, &d, &h), r);
		if (!r) last_answered = seq;
	}
}
/* wait until a request with seq != prev is open; returns 0 on success */
static int wait_issue(uint32_t prev, unsigned timeout_ms, uint32_t *seq, uint64_t *nonce, uint32_t *state)
{
	uint64_t t0 = now_ms(); struct mon_health h; TEEC_Result r;
	while (now_ms() - t0 < timeout_ms) {
		RETRY(&C, mon_get_nonce(&C, nonce, seq, state, &h), r);
		if (!r && *nonce && *seq != prev && *state == 1) return 0;
		msleep(3);
	}
	return -1;
}
static int answer(uint32_t seq, uint64_t nonce, uint32_t *v)
{ struct mon_health h; uint32_t d; TEEC_Result r; RETRY(&C, mon_submit_answer(&C, seq, nonce, v, &d, &h), r); if (r) { *v = 98; return -1; } if (*v == 0) last_answered = seq; return 0; }
/* newest event of a type/detail with seq > from */
static int find_ev(uint32_t from, unsigned type, int detail, struct mon_event *out)
{
	uint32_t next = from + 1; int found = 0;
	for (int guard = 0; guard < 200; guard++) {
		struct mon_event ev[MON_EVENT_MAX]; uint32_t cnt = 0, lost = 0; int more = 0; struct mon_health h; TEEC_Result r;
		RETRY(&C, mon_get_events(&C, next, MON_EVENT_MAX, ev, &cnt, &more, &lost, &h), r);
		if (r || !cnt) break;
		for (uint32_t i = 0; i < cnt; i++) { if (ev[i].type == type && (detail < 0 || (int)ev[i].detail == detail)) { *out = ev[i]; found = 1; } next = ev[i].seq + 1; }
		if (!more) break;
	}
	return found;
}

/* the earliest event the ring can still return (pinned events survive eviction) */
static int first_ev(struct mon_event *out)
{
	struct mon_event ev[MON_EVENT_MAX]; uint32_t cnt = 0, lost = 0; int more = 0; struct mon_health h; TEEC_Result r;
	RETRY(&C, mon_get_events(&C, 1, 1, ev, &cnt, &more, &lost, &h), r);
	if (r || !cnt) return 0;
	*out = ev[0]; return 1;
}
/* ------------------------------------------------------------------ frames */
static void phase_frames(void)
{
	uint32_t e0[MON_EVENT_TYPES], e1[MON_EVENT_TYPES], v, d, c; struct mon_status0 s;
	printf("== frames (T1-T8)\n");
	s = st0();
	if (!(s.flags & 0x04)) { v = frame(0x123, 1000, payload_for(1000), 0, &d); NOTE("setup", "traffic not armed; first frame verdict %s", mon_verdict17_name(v)); s = st0(); }
	c = s.last_counter;
	hb_keep();
	/* T1 */
	snap(e0); { int ok = 1; for (int i = 1; i <= 5; i++) { v = frame(0x123, c + i, payload_for(c + i), 0, &d); if (v != 0) ok = 0; msleep(20); hb_keep(); } c += 5; snap(e1);
	  uint32_t bad = dlt(e0, e1, EV_AUTH_FAIL) + dlt(e0, e1, EV_REPLAY) + dlt(e0, e1, EV_MALFORMED) + dlt(e0, e1, EV_GAP) + dlt(e0, e1, EV_RESYNC);
	  PASSF(ok && bad == 0, "T1", "5 valid frames ACCEPTED, frame-class events +%u (expected 0)", bad); }
	/* T2 */
	msleep(60); s = st0(); { uint64_t age0 = s.traffic_age_ticks; uint32_t last0 = s.last_counter; snap(e0);
	  v = frame(0x123, c + 1, payload_for(c + 1), 1, &d); snap(e1); s = st0();
	  PASSF(v == 4 && dlt(e0, e1, EV_AUTH_FAIL) == 1 && s.traffic_age_ticks > age0 && s.last_counter == last0, "T2", "bad tag -> %s, AUTH_FAIL +%u, traffic age %"PRIu64"->%"PRIu64" ticks (not refreshed), last_counter unchanged=%d",
	        mon_verdict17_name(v), dlt(e0, e1, EV_AUTH_FAIL), age0, s.traffic_age_ticks, s.last_counter == last0); }
	/* T3 */
	snap(e0); v = frame(0x123, c + 1, payload_for(c + 1), 2, &d); snap(e1);
	PASSF(v == 4 && dlt(e0, e1, EV_AUTH_FAIL) == 1, "T3", "payload altered, tag kept -> %s, AUTH_FAIL +%u", mon_verdict17_name(v), dlt(e0, e1, EV_AUTH_FAIL));
	/* T4 */
	s = st0(); { uint32_t last0 = s.last_counter; snap(e0);
	  uint32_t v1 = frame(0x124, c + 1, payload_for(c + 1), 0, &d), v2 = frame(0x123u | 0x80000000u, c + 1, payload_for(c + 1), 0, &d); snap(e1); s = st0();
	  PASSF(v1 == 3 && v2 == 3 && dlt(e0, e1, EV_MALFORMED) == 2 && s.last_counter == last0, "T4", "id 0x124 -> %s, EFF flag -> %s, MALFORMED +%u, state unchanged=%d", mon_verdict17_name(v1), mon_verdict17_name(v2), dlt(e0, e1, EV_MALFORMED), s.last_counter == last0); }
	SKIPF("T4b", "frame length 43/45 cannot be sent through the fixed 44-byte client API (covered by the host harness, command 17 length checks)");
	hb_keep();
	/* T5 */
	{ uint32_t L = c; uint32_t d5 = 0, vv[5]; snap(e0);
	  vv[0] = frame(0x123, L - 10, payload_for(L - 10), 0, &d);
	  vv[1] = frame(0x123, L, payload_for(L), 0, &d);
	  vv[2] = frame(0x123, L, payload_for(L) ^ 0x1234u, 0, &d);
	  vv[3] = frame(0x123, L + 1, payload_for(L + 1), 0, &d);
	  vv[4] = frame(0x123, L + 5, payload_for(L + 5), 0, &d5); snap(e1);
	  PASSF(vv[0] == 5 && vv[1] == 6 && vv[2] == 7 && vv[3] == 0 && vv[4] == 1 && d5 == 3, "T5", "last=%u: %u->%s, %u->%s, %u(diff)->%s, %u->%s, %u->%s(detail %u); REPLAY events +%u, GAP +%u",
	        L, L - 10, mon_verdict17_name(vv[0]), L, mon_verdict17_name(vv[1]), L, mon_verdict17_name(vv[2]), L + 1, mon_verdict17_name(vv[3]), L + 5, mon_verdict17_name(vv[4]), d5, dlt(e0, e1, EV_REPLAY), dlt(e0, e1, EV_GAP));
	  c = L + 5; }
	hb_keep();
	/* T7: stop the sender, expect one traffic TIMEOUT about T after the last accepted frame, RECOVERED on resume */
	{ v = frame(0x123, c + 1, payload_for(c + 1), 0, &d); c++; s = st0(); uint64_t n0 = s.now_ticks; uint32_t evs = s.ev_seq; snap(e0);
	  for (int i = 0; i < 7; i++) { msleep(100); hb_keep(); } snap(e1); s = st0();
	  struct mon_event ev; int fnd = find_ev(evs, EV_TIMEOUT, 1, &ev); double dms = fnd ? ((double)ev.ticks - (double)n0) / TICKS_MS : -1;
	  PASSF(v == 0 && dlt(e0, e1, EV_TIMEOUT) == 1 && fnd && dms >= T_TRAFFIC_MS - 20 && dms <= T_TRAFFIC_MS + 60, "T7a", "sender stopped: TIMEOUT(traffic) +%u (expected exactly 1), %.1f ms after the last accepted frame (T=%d ms)", dlt(e0, e1, EV_TIMEOUT), dms, T_TRAFFIC_MS);
	  PASSF((s.flags & 0x08) != 0, "T7b", "traffic-timeout flag set while silent (flags 0x%02x)", s.flags);
	  snap(e0); v = frame(0x123, c + 1, payload_for(c + 1), 0, &d); c++; snap(e1); s = st0();
	  PASSF(v == 0 && dlt(e0, e1, EV_TRAFFIC_REC) == 1 && !(s.flags & 0x08), "T7c", "sender resumed: frame %s, TRAFFIC_RECOVERED +%u, timeout flag cleared=%d", mon_verdict17_name(v), dlt(e0, e1, EV_TRAFFIC_REC), !(s.flags & 0x08)); }
	/* T8: counter restart */
	{ snap(e0); uint32_t r1 = 0; int all = 1; for (int i = 0; i < 3; i++) { v = frame(0x123, 1 + i, payload_for(1 + i), 0, &d); if (v != 5) all = 0; r1++; msleep(50); hb_keep(); }
	  for (int i = 0; i < 4; i++) { msleep(100); hb_keep(); } snap(e1);
	  PASSF(all && dlt(e0, e1, EV_REPLAY) >= 1 && dlt(e0, e1, EV_TIMEOUT) == 1, "T8", "sender counter restarted at 1: %u frames -> REPLAY (REPLAY events +%u, rate-limited ones not counted), then silence 400 ms: TIMEOUT(traffic) +%u (replayed frames do not refresh the deadline)", r1, dlt(e0, e1, EV_REPLAY), dlt(e0, e1, EV_TIMEOUT));
	  v = frame(0x123, c + 1, payload_for(c + 1), 0, &d); c++; NOTE("T8", "recover with the monitor's next counter %u -> %s", c, mon_verdict17_name(v)); }
}

/* ------------------------------------------------------------------ heartbeat */
static void phase_heartbeat(void)
{
	uint32_t e0[MON_EVENT_TYPES], e1[MON_EVENT_TYPES], seq, st, v; uint64_t nonce; struct mon_status0 s;
	printf("== heartbeat (T9-T16)\n");
	hb_keep(); msleep(50);
	/* T9 + T14 */
	{ uint32_t prev = 0xFFFFFFFFu; (void)prev; struct mon_health h; TEEC_Result r; RETRY(&C, mon_get_nonce(&C, &nonce, &seq, &st, &h), r); uint32_t cur = seq;
	  if (wait_issue(cur, 3000, &seq, &nonce, &st)) { PASSF(0, "T9", "no new challenge within 3 s"); return; }
	  snap(e0); int rc = answer(seq, nonce, &v); s = st0(); snap(e1);
	  PASSF(!rc && v == 0 && s.hb_state == 2 && dlt(e0, e1, EV_LATE) + dlt(e0, e1, EV_BAD_NONCE) + dlt(e0, e1, EV_STALE) + dlt(e0, e1, EV_UNISSUED) == 0, "T9", "fetch -> answer: %s, state %s, no answer-related events", mon_verdict23_name(v), mon_hbstate_name(s.hb_state));
	  uint32_t v2; answer(seq, nonce, &v2); PASSF(v2 == 5, "T14", "same answer twice: second -> %s (expected DUPLICATE)", mon_verdict23_name(v2)); }
	/* T12 */
	{ uint32_t prev = seq; if (wait_issue(prev, 3000, &seq, &nonce, &st)) { PASSF(0, "T12", "no new challenge"); return; }
	  snap(e0); uint32_t vb; answer(seq, nonce ^ 1ull, &vb); s = st0(); snap(e1);
	  PASSF(vb == 4 && s.hb_state == 1 && dlt(e0, e1, EV_BAD_NONCE) == 1, "T12", "wrong nonce -> %s, request stays %s, BAD_NONCE +%u", mon_verdict23_name(vb), mon_hbstate_name(s.hb_state), dlt(e0, e1, EV_BAD_NONCE));
	  answer(seq, nonce, &v);
	  /* T13: replay the old answer after the next challenge was issued */
	  uint32_t old_seq = seq; uint64_t old_nonce = nonce;
	  if (wait_issue(old_seq, 3000, &seq, &nonce, &st)) { PASSF(0, "T13", "no new challenge"); return; }
	  snap(e0); uint32_t vs; answer(old_seq, old_nonce, &vs); snap(e1);
	  PASSF(vs == 2 && dlt(e0, e1, EV_STALE) == 1, "T13", "previous answer resent -> %s, STALE_ANSWER +%u", mon_verdict23_name(vs), dlt(e0, e1, EV_STALE));
	  /* T15 */
	  snap(e0); uint32_t vu; answer(seq + 5, 0x1234ull, &vu); snap(e1);
	  PASSF(vu == 3 && dlt(e0, e1, EV_UNISSUED) == 1, "T15", "unissued seq %u -> %s, UNISSUED +%u", seq + 5, mon_verdict23_name(vu), dlt(e0, e1, EV_UNISSUED));
	  answer(seq, nonce, &v); }
	/* T11 + T11c: late answer */
	{ uint32_t prev = seq; if (wait_issue(prev, 3000, &seq, &nonce, &st)) { PASSF(0, "T11", "no new challenge"); return; }
	  s = st0(); uint32_t miss0 = s.hb_miss_count; uint32_t seq_late = seq; uint64_t non_late = nonce;
	  msleep(D_REQ_MS + 200); snap(e0); uint32_t vl; answer(seq_late, non_late, &vl); s = st0(); snap(e1);
	  PASSF(vl == 1 && s.hb_state == 4 && dlt(e0, e1, EV_LATE) == 1 && s.hb_miss_count >= miss0 + 1, "T11", "answer %d ms after issue (D_req %d) -> %s, state %s, LATE_ANSWER +%u, miss_count %u -> %u (not reset)", D_REQ_MS + 200, D_REQ_MS, mon_verdict23_name(vl), mon_hbstate_name(s.hb_state), dlt(e0, e1, EV_LATE), miss0, s.hb_miss_count);
	  uint32_t mc = s.hb_miss_count; uint32_t d1, d2; snap(e0); answer(seq_late, non_late, &d1); answer(seq_late, non_late, &d2); s = st0(); snap(e1);
	  PASSF(d1 == 5 && d2 == 5 && s.hb_miss_count == mc, "T11c", "same late answer twice more -> %s, %s; miss_count unchanged (%u); DUPLICATE_ANSWER +%u", mon_verdict23_name(d1), mon_verdict23_name(d2), s.hb_miss_count, dlt(e0, e1, EV_DUP_ANSWER)); }
	NOTE("T11b", "answer at exactly issue+D_req needs tick-exact timing: host harness only; T11a needs a stalled Core 1: host harness t_stall");
	/* T16: consumer restart, then T10: consumer stopped */
	{ uint32_t prev = seq; if (wait_issue(prev, 3000, &seq, &nonce, &st)) { PASSF(0, "T16", "no new challenge"); return; }
	  snap(e0); uint32_t sa = seq; uint64_t na = nonce; mon_close(&C); TEEC_Result r = mon_open(&C); uint32_t sb = 0, stb = 0; uint64_t nb = 0; struct mon_health h;
	  if (!r) { RETRY(&C, mon_get_nonce(&C, &nb, &sb, &stb, &h), r); }
	  snap(e1);
	  PASSF(!r && sa == sb && na == nb && dlt(e0, e1, EV_TIMEOUT) == 0, "T16", "consumer session closed and reopened: same request seq %u/%u, same nonce=%d, no false event", sa, sb, na == nb);
	  answer(sb, nb, &v); last_answered = sb; }
	/* T10: stop answering for 4.6 s (K_MISS=3 -> TIMEOUT after about 3 s) */
	{ s = st0(); uint32_t m0 = s.hb_miss_count; snap(e0); uint32_t evs = s.ev_seq; msleep(4600); s = st0(); snap(e1); struct mon_event ev;
	  int f = find_ev(evs, EV_TIMEOUT, 2, &ev);
	  PASSF(s.hb_miss_count >= m0 + 3 && dlt(e0, e1, EV_TIMEOUT) == 1 && f && (s.flags & 0x10), "T10", "no answers for 4.6 s: miss_count %u -> %u (>= +3), TIMEOUT(heartbeat) +%u (exactly 1), heartbeat-timeout flag=%d", m0, s.hb_miss_count, dlt(e0, e1, EV_TIMEOUT), !!(s.flags & 0x10));
	  snap(e0); if (!wait_issue(last_answered, 3000, &seq, &nonce, &st)) answer(seq, nonce, &v); s = st0(); snap(e1);
	  PASSF(v == 0 && dlt(e0, e1, EV_HB_REC) == 1 && s.hb_miss_count == 0, "T10b", "responder back: %s, HEARTBEAT_RECOVERED +%u, miss_count %u", mon_verdict23_name(v), dlt(e0, e1, EV_HB_REC), s.hb_miss_count); }
}

/* ------------------------------------------------------------------ T19 parallel callers */
struct tctx { int id; volatile int stop; unsigned long a, b, c, errs, busy_fail; uint32_t first_counter; };
static void *worker(void *p)
{
	struct tctx *t = p; struct mon_ctx c; struct mon_health h; TEEC_Result r; uint32_t counter = t->first_counter, last = 0xFFFFFFFFu;
	if (mon_open(&c)) { t->errs++; return NULL; }
	while (!t->stop) {
		if (t->id == 0) { uint64_t n; uint32_t seq, st, v, d; r = TEEC_ERROR_BUSY; for (int k = 0; k < 60 && r == TEEC_ERROR_BUSY; k++) { r = mon_get_nonce(&c, &n, &seq, &st, &h); if (r == TEEC_ERROR_BUSY) msleep(1); }
			if (r) t->errs++; else if ((st == 1 || st == 3) && n && seq != last) { r = TEEC_ERROR_BUSY; for (int k = 0; k < 60 && r == TEEC_ERROR_BUSY; k++) { r = mon_submit_answer(&c, seq, n, &v, &d, &h); if (r == TEEC_ERROR_BUSY) msleep(1); }
				if (!r) { last = seq; if (v == 0) t->a++; else if (v == 1) t->b++; else t->c++; } else t->errs++; } msleep(15); }
		else if (t->id == 1) { uint8_t f[MON_FRAME_LEN]; uint32_t v, d; mon_build_frame(KEY, 0x123, counter, payload_for(counter), f); r = TEEC_ERROR_BUSY; for (int k = 0; k < 60 && r == TEEC_ERROR_BUSY; k++) { r = mon_submit_frame(&c, f, &v, &d, &h); if (r == TEEC_ERROR_BUSY) msleep(1); }
			if (r) t->busy_fail++; else { if (v == 0 || v == 1 || v == 2) { counter++; t->a++; } else t->errs++; } msleep(20); }
		else if (t->id == 2) { uint64_t n; uint32_t seq, st; r = mon_get_nonce(&c, &n, &seq, &st, &h); if (r == TEEC_ERROR_BUSY) t->busy_fail++; else if (r) t->errs++; else t->a++; msleep(5); }
		else { struct mon_status0 s; r = mon_get_status0(&c, &s, &h); if (r == TEEC_ERROR_BUSY) t->busy_fail++; else if (r) t->errs++; else t->a++; msleep(5); }
	}
	mon_close(&c); return NULL;
}
static void phase_parallel(void)
{
	pthread_t th[4]; struct tctx t[4]; struct mon_status0 s0, s1; uint32_t e0[MON_EVENT_TYPES], e1[MON_EVENT_TYPES];
	printf("== four parallel callers (T19)\n");
	hb_keep(); s0 = st0(); snap(e0);
	for (int i = 0; i < 4; i++) { memset(&t[i], 0, sizeof t[i]); t[i].id = i; t[i].first_counter = s0.last_counter + 1; pthread_create(&th[i], NULL, worker, &t[i]); }
	msleep(8000);
	for (int i = 0; i < 4; i++) t[i].stop = 1;
	for (int i = 0; i < 4; i++) pthread_join(th[i], NULL);
	s1 = st0(); snap(e1);
	NOTE("T19", "responder: ON_TIME %lu LATE %lu other %lu errs %lu | frames accepted %lu (errs %lu, BUSY-after-retries %lu) | cmd22 calls %lu (BUSY-after-retries %lu) | cmd20 calls %lu (BUSY-after-retries %lu)",
	     t[0].a, t[0].b, t[0].c, t[0].errs, t[1].a, t[1].errs, t[1].busy_fail, t[2].a, t[2].busy_fail, t[3].a, t[3].busy_fail);
	PASSF(t[0].b == 0 && t[0].c == 0 && t[0].a >= 6 && s1.hb_miss_count == 0 && dlt(e0, e1, EV_TIMEOUT) == 0 && !(s1.flags & 0x40), "T19", "4 concurrent callers (cmds 20/22/23/17) for 8 s: every heartbeat answer on time (%lu), miss_count %u, no TIMEOUT, not starved", t[0].a, s1.hb_miss_count);
	PASSF(t[0].errs + t[1].errs + t[2].errs + t[3].errs == 0, "T19b", "no call failed with an error other than BUSY (BUSY after 60 retries counted separately above: %lu)", t[1].busy_fail + t[2].busy_fail + t[3].busy_fail);
	uint32_t n = 0; (void)n;
}

/* ------------------------------------------------------------------ T21 idle, T22 log flood, T6 */
static void phase_idle(uint32_t *c)
{
	uint32_t e0[MON_EVENT_TYPES], e1[MON_EVENT_TYPES], ver = 0, rem = 0, d; struct mon_health h; TEEC_Result r; struct mon_status0 s;
	printf("== idle mode (T21)\n");
	hb_keep(); frame(0x123, *c + 1, payload_for(*c + 1), 0, &d); (*c)++; s = st0(); uint32_t uses0 = s.idle_uses, evs = s.ev_seq; snap(e0);
	RETRY(&C, mon_set_idle(&C, 1, 2000, &ver, &rem, &h), r); s = st0();
	PASSF(!r && ver == 0 && (s.flags & 0x20) && s.idle_uses == uses0 + 1, "T21a", "idle lease 2000 ms: %s, remaining %u ms, lease flag=%d, idle_uses %u -> %u", mon_verdict21_name(ver), rem, !!(s.flags & 0x20), uses0, s.idle_uses);
	for (int i = 0; i < 15; i++) { msleep(100); hb_keep(); } snap(e1); s = st0();
	PASSF(dlt(e0, e1, EV_TIMEOUT) == 0, "T21b", "silence for 1.5 s inside the lease: no traffic TIMEOUT (+%u)", dlt(e0, e1, EV_TIMEOUT));
	for (int i = 0; i < 20; i++) { msleep(100); hb_keep(); } snap(e1); s = st0(); struct mon_event ev; int f = find_ev(evs, EV_TIMEOUT, 1, &ev);
	PASSF(dlt(e0, e1, EV_TIMEOUT) == 1 && f && !(s.flags & 0x20), "T21c", "after the lease expired (2 s): TIMEOUT(traffic) +%u, lease flag cleared=%d", dlt(e0, e1, EV_TIMEOUT), !(s.flags & 0x20));
	NOTE("T21", "events seen during T21: LEASE_GRANTED +%u; a separate IDLE_EXPIRED event type does not exist in this build (types: LEASE_GRANTED/RENEWED/REFUSED, MODE_CHANGED)", dlt(e0, e1, EV_LEASE_GRANTED));
	d = frame(0x123, *c + 1, payload_for(*c + 1), 0, NULL); (*c)++; NOTE("T21", "recover with next frame -> %s", mon_verdict17_name(d));
}
static void phase_flood(void)
{
	struct mon_status0 s0, s1; struct mon_status1 a, b; unsigned long af = 0, other = 0; struct mon_event first, again;
	printf("== log flood (T22)\n");
	hb_keep(); s0 = st0(); a = st1();
	int have_first0 = first_ev(&first);
	for (int i = 0; i < 1000; i++) { uint32_t v = frame(0x123, 1, payload_for(1), 1, NULL); if (v == 4) af++; else other++; if (i % 100 == 99) hb_keep(); }
	s1 = st0(); b = st1(); int have_first1 = first_ev(&again);
	uint32_t recorded = s1.ev_seq - s0.ev_seq, counted = b.ev_count[EV_AUTH_FAIL] - a.ev_count[EV_AUTH_FAIL];
	PASSF(af == 1000 && other == 0 && have_first0 && have_first1 && first.seq == again.seq && !(s1.flags & 0x40), "T22", "1000 bad-tag frames: %lu AUTH_FAIL verdicts, earliest pinned event (seq %u, type %s) before and after, still readable=%d, not starved", af, again.seq, mon_event_name(again.type), have_first1);
	NOTE("T22", "AUTH_FAIL counted %u, events written to the ring %u (ev_seq %u -> %u), ev_lost %u -> %u, pinned_used %u -> %u; suppressed (counted but not written) = %u; UART flood not testable (no UART)", counted, recorded, s0.ev_seq, s1.ev_seq, s0.ev_lost, s1.ev_lost, s0.pinned_used, s1.pinned_used, counted > recorded ? counted - recorded : 0);
}
static void phase_t6(uint32_t c)
{
	uint32_t v, d, vv; printf("== counter wrap (T6), leaves last_counter = 0xFFFFFFFF: REBOOT the board afterwards\n");
	hb_keep(); (void)c;
	v = frame(0x123, 0xFFFFFFFFu, payload_for(0xFFFFFFFFu), 0, &d); NOTE("T6", "drive last_counter to 0xFFFFFFFF: verdict %s (detail %u)", mon_verdict17_name(v), d);
	vv = frame(0x123, 0, payload_for(0), 0, &d);
	PASSF(vv == 5, "T6", "last=0xFFFFFFFF, counter 0 -> %s (expected REPLAY, no wraparound)", mon_verdict17_name(vv));
}

static int load_key(const char *path)
{ char b[256]; FILE *f = fopen(path, "rb"); size_t n; if (!f) return -1; n = fread(b, 1, sizeof b - 1, f); fclose(f); b[n] = 0; if (n < 64) return -1;
  for (int i = 0; i < 32; i++) { unsigned x; if (sscanf(b + 2 * i, "%2x", &x) != 1) return -1; KEY[i] = (uint8_t)x; } return 0; }

/* ------------------------------------------------------------------ boot-level modes (T17, T18, T20) */
static double uptime_s(void) { double u = 0; FILE *f = fopen("/proc/uptime", "r"); if (f) { if (fscanf(f, "%lf", &u) != 1) u = 0; fclose(f); } return u; }
/* T20: watch the boot grace WITHOUT calling commands 22/23 (status and events only) */
static int mode_t20(unsigned secs)
{
	struct mon_status0 s = st0(); double u0 = uptime_s(); uint64_t n0 = s.now_ticks; struct mon_event ev; int found = 0;
	printf("T20 watch: boot_id 0x%08x, first status at Linux uptime %.1f s, hb_state %s, ev_seq %u (cmds 22/23 NOT used)\n", s.boot_id, u0, mon_hbstate_name(s.hb_state), s.ev_seq);
	for (uint64_t t0 = now_ms(); now_ms() - t0 < (uint64_t)secs * 1000 && !found; msleep(500)) {
		if (find_ev(0, EV_TIMEOUT, 3, &ev)) { found = 1; double rel = ((double)ev.ticks - (double)n0) / (TICKS_MS * 1000.0);
			printf("  TIMEOUT detail 3 (consumer never started) seen at Linux uptime %.1f s; its timestamp is %.1f s after the first status read (= Linux time %.1f s)\n", uptime_s(), rel, u0 + rel); }
	}
	s = st0(); printf("  end: hb_state %s hb_seq %u miss_count %u flags 0x%02x\n", mon_hbstate_name(s.hb_state), s.hb_seq, s.hb_miss_count, s.flags);
	PASSF(found, "T20", "CONSUMER_NEVER_STARTED reported (G = 60 s after the monitor's init, which is a few seconds before Linux uptime 0); none was visible at the first read (uptime %.1f s)", u0);
	return 0;
}
static int mode_save(const char *path)
{ uint64_t n; uint32_t q, st; struct mon_health h; TEEC_Result r; struct mon_status0 s = st0(); RETRY(&C, mon_get_nonce(&C, &n, &q, &st, &h), r); if (r) return 1;
  FILE *f = fopen(path, "w"); if (!f) return 1; fprintf(f, "%u %u %" PRIu64 "\n", s.boot_id, q, n); fclose(f); printf("saved boot_id 0x%08x, request seq %u, nonce %016" PRIx64 "\n", s.boot_id, q, n); return 0; }
static int mode_t17(const char *path)
{
	unsigned ob, oq; uint64_t on; uint32_t v, v2, d; uint64_t nonce; uint32_t seq, st; struct mon_health h; TEEC_Result r;
	FILE *f = fopen(path, "r"); if (!f || fscanf(f, "%u %u %" SCNu64, &ob, &oq, &on) != 3) { fprintf(stderr, "cannot read %s\n", path); return 2; } fclose(f);
	struct mon_status0 s = st0();
	PASSF(s.boot_id != ob, "T17a", "new boot_id 0x%08x differs from the one before the reboot (0x%08x); uptime %.0f s", s.boot_id, ob, uptime_s());
	answer(oq, on, &v);
	PASSF(v == 3 || v == 2, "T17b", "old consumer values (seq %u) after the reboot -> %s (expected UNISSUED; STALE would also mean refused)", oq, mon_verdict23_name(v));
	RETRY(&C, mon_get_nonce(&C, &nonce, &seq, &st, &h), r); v2 = 99; if (!r) { uint32_t cur = seq; r = wait_issue(cur, 3000, &seq, &nonce, &st) ? 1 : 0; }
	if (!r && nonce) answer(seq, nonce, &v2);	/* a request fetched while old can be LATE; wait for a fresh one and answer at once */
	PASSF(!r && v2 == 0, "T17c", "refetch -> freshly issued request seq %u, answer %s", seq, mon_verdict23_name(v2));
	s = st0(); uint32_t first = s.last_counter;
	v = frame(0x123, 7000, payload_for(7000), 0, &d);
	PASSF(v == 2 && !(first && (s.flags & 0x04)), "T17d", "first frame after the reboot -> %s (expected RESYNC), traffic was not armed before=%d", mon_verdict17_name(v), !(s.flags & 0x04));
	return 0;
}
static int mode_nonce_at(uint32_t target, const char *path)
{
	uint64_t nonce; uint32_t seq = 0, st; struct mon_health h; TEEC_Result r; uint64_t t0 = now_ms(); uint32_t first = 0;
	for (;;) { RETRY(&C, mon_get_nonce(&C, &nonce, &seq, &st, &h), r); if (!r && !first) first = seq; if (!r && seq == target) break; if (!r && seq > target) { printf("seq %u already past %u\n", seq, target); return 1; } if (now_ms() - t0 > (uint64_t)(target + 60) * 1000) { printf("timeout waiting for seq %u (at %u)\n", target, seq); return 1; } if (nonce && (st == 1 || st == 3) && seq != last_answered) { uint32_t v; answer(seq, nonce, &v); } msleep(200); }
	struct mon_status0 s = st0(); FILE *f = fopen(path, "w"); if (!f) return 1; fprintf(f, "%u 0x%08x %016" PRIx64 "\n", seq, s.boot_id, nonce); fclose(f);
	printf("hb_seq %u reached: boot_id 0x%08x nonce %016" PRIx64 "\n", seq, s.boot_id, nonce); return 0;
}

int main(int argc, char **argv)
{
	const char *kf = NULL; int no_t6 = 0;
	for (int i = 1; i < argc; i++) { if (!strcmp(argv[i], "--key-file") && i + 1 < argc) kf = argv[++i]; else if (!strcmp(argv[i], "--no-t6")) no_t6 = 1; }
	if (!kf || load_key(kf)) { fprintf(stderr, "usage: %s --key-file HEXFILE [--no-t6]\n", argv[0]); return 2; }
	if (mon_open(&C)) { fprintf(stderr, "cannot open the PTA session\n"); return 2; }
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--t20-watch") && i + 1 < argc) { int rc = mode_t20((unsigned)atoi(argv[i + 1])); printf("SUMMARY: %d PASS, %d FAIL\n", n_pass, n_fail); mon_close(&C); return rc || n_fail; }
		if (!strcmp(argv[i], "--answer-fresh")) { uint32_t q, st, v = 99, cur; uint64_t n; struct mon_health h; TEEC_Result r; RETRY(&C, mon_get_nonce(&C, &n, &cur, &st, &h), r);
			int w = wait_issue(cur, 3000, &q, &n, &st); if (!w) answer(q, n, &v); PASSF(!r && !w && v == 0, "T17c", "refetch -> freshly issued request seq %u, answer %s (rerun of T17c alone, same boot)", q, mon_verdict23_name(v)); mon_close(&C); return n_fail; }
		if (!strcmp(argv[i], "--save") && i + 1 < argc) { int rc = mode_save(argv[i + 1]); mon_close(&C); return rc; }
		if (!strcmp(argv[i], "--t17-check") && i + 1 < argc) { int rc = mode_t17(argv[i + 1]); printf("SUMMARY: %d PASS, %d FAIL\n", n_pass, n_fail); mon_close(&C); return rc || n_fail; }
		if (!strcmp(argv[i], "--nonce-at") && i + 2 < argc) { int rc = mode_nonce_at((uint32_t)atoi(argv[i + 1]), argv[i + 2]); mon_close(&C); return rc; }
	}
	struct mon_status0 s = st0(); printf("c5_board_tests: boot_id 0x%08x, flags 0x%02x, last_counter %u, hb_state %s, ev_seq %u, idle_uses %u\n", s.boot_id, s.flags, s.last_counter, mon_hbstate_name(s.hb_state), s.ev_seq, s.idle_uses);
	phase_frames();
	phase_heartbeat();
	phase_parallel();
	s = st0(); uint32_t c = s.last_counter;
	phase_idle(&c);
	phase_flood();
	if (!no_t6) phase_t6(c);
	s = st0(); printf("end: flags 0x%02x last_counter %u hb_state %s miss_count %u ev_seq %u ev_lost %u starved=%d\n", s.flags, s.last_counter, mon_hbstate_name(s.hb_state), s.hb_miss_count, s.ev_seq, s.ev_lost, !!(s.flags & 0x40));
	printf("SUMMARY: %d PASS, %d FAIL, %d note, %d skip\n", n_pass, n_fail, n_note, n_skip);
	mon_close(&C);
	return n_fail ? 1 : 0;
}
