#include <stdio.h>
#include <stdlib.h>
#include "mon_model.h"

static int nchk, nfail, tchk, tfail;
#define CHECK(c) do { nchk++; tchk++; if (!(c)) { nfail++; tfail++; printf("  FAIL line %d: %s\n", __LINE__, #c); } } while (0)
static const char *cur;
static void begin(const char *name) { cur = name; tchk = tfail = 0; }
static void end(void) { printf("%-4s %-52s %3d checks, %s\n", tfail ? "FAIL" : "ok", cur, tchk, tfail ? "FAILED" : "all passed"); }

static void setup(struct mon *m)
{
	memset(m, 0, sizeof *m); memset(&M, 0, sizeof M);
	M.now = 1000000000ull; M.step = 60; M.daif = 0;       /* PTA context: nothing masked */
	m->init_ok = true; m->boot_id = 0xB007;
}
static uint32_t grab(struct mon *m, uint64_t extra_now)  /* take the lock the normal way, for building state */
{
	struct mon_wait w = {0, 0}; uint32_t e; (void)extra_now;
	if (mon_acquire(m, &w, &e) != MON_OK) abort();
	return e;
}

static void t_acquire_free(void)       /* X1, V7 */
{
	struct mon m; struct mon_wait w = {0, 0}; uint32_t e;
	begin("X1/V7 free lock: first attempt, no increment"); setup(&m);
	CHECK(mon_acquire(&m, &w, &e) == MON_OK); CHECK(M.trylock_calls == 1); CHECK(m.pta_lock_timeouts == 0);
	CHECK(M.held && M.daif == MON_DAIF_ALL);              /* exceptions stay masked while held */
	mon_release(&m, e); CHECK(!M.held && M.daif == 0 && M.violations == 0); end();
}
static void t_acquire_held(void)       /* X2, X6, V6 */
{
	struct mon m; struct mon_wait w = {0, 0}; uint32_t e; uint64_t t0;
	begin("X2/X6/V6 held lock: BUSY within budget, state restored"); setup(&m);
	M.ext_hold = 1; t0 = M.now;
	CHECK(mon_acquire(&m, &w, &e) == MON_BUSY);
	CHECK(M.now - t0 <= MON_BUDGET_TICKS + 3 * M.step);   /* budget plus about one iteration */
	CHECK(w.attempts_used > 100 && w.attempts_used < MON_ATTEMPT_CAP);
	CHECK(m.pta_lock_timeouts == 1);                       /* exactly one increment per failed acquisition (V6) */
	CHECK(M.daif == 0 && M.locked_count == 0 && !M.held && M.violations == 0); end();
}
static void t_frozen(void)             /* X3 */
{
	struct mon m; struct mon_wait w = {0, 0}; uint32_t e;
	begin("X3 frozen timer: exits through the attempt cap"); setup(&m); M.tmode = 1; M.ext_hold = 1;
	CHECK(mon_acquire(&m, &w, &e) == MON_BUSY); CHECK(w.attempts_used == MON_ATTEMPT_CAP); CHECK(m.pta_lock_timeouts == 1);
	CHECK(M.daif == 0 && M.violations == 0); end();
}
static void t_backward(void)           /* X4 */
{
	struct mon m; struct mon_wait w = {0, 0}; uint32_t e;
	begin("X4 backward timer: exits at the first failed attempt"); setup(&m); M.tmode = 2; M.ext_hold = 1;
	CHECK(mon_acquire(&m, &w, &e) == MON_BUSY); CHECK(w.attempts_used == 1); CHECK(M.daif == 0 && M.violations == 0); end();
}
static void t_shared_budget(void)      /* X5 */
{
	struct mon m; struct mon_wait w = {0, 0}; uint32_t e; uint32_t a1, full;
	begin("X5 two acquisitions share the budget; HMAC time not counted"); setup(&m);
	M.ext_hold = 1; M.ext_release_after = 100;
	CHECK(mon_acquire(&m, &w, &e) == MON_OK); a1 = w.attempts_used; mon_release(&m, e);
	CHECK(w.wait_ticks_used > 0 && w.wait_ticks_used < MON_BUDGET_TICKS);
	M.now += 50000000ull;                                  /* simulated HMAC between the holds: far longer than the budget */
	M.ext_hold = 1; M.ext_release_after = 0;
	CHECK(mon_acquire(&m, &w, &e) == MON_BUSY);
	full = MON_BUDGET_TICKS / (uint32_t)M.step;            /* attempts a fresh budget would have allowed */
	CHECK(w.attempts_used - a1 < full - 50);               /* the second wait got only the remainder */
	CHECK(w.wait_ticks_used <= MON_BUDGET_TICKS + 3 * M.step);
	CHECK(m.pta_lock_timeouts == 1); end();
}
static void t_exception_order(void)    /* X6 negative control: the mock does detect a wrong order */
{
	struct mon m; struct mon_wait w = {0, 0}; uint32_t e;
	begin("X6 control: unmask-before-unlock is detected"); setup(&m);
	CHECK(mon_acquire(&m, &w, &e) == MON_OK); M.violations = 0;
	MON_UNMASK(e);                                          /* wrong order on purpose */
	CHECK(M.violations == 1); MON_UNLOCK(&m.lock); end();
}
static void t_busy_untouched(void)     /* X7, Z4, HP3 */
{
	struct mon m; uint8_t out[128]; size_t len = 99; uint32_t oa = 7, ob = 7; struct health_out h; struct lstate before;
	begin("X7/Z4/HP3 BUSY path: no protected access, no copy"); setup(&m);
	m.l.ev_seq = 0x5A5A; m.l.hb_seq = 0x3C3C; memset(out, 0xEE, sizeof out); before = m.l;
	M.ext_hold = 1;
	CHECK(mon_cmd20(&m, 0, 0, out, 128, &len, &oa, &ob, &h) == MON_BUSY);
	CHECK(M.violations == 0); CHECK(memcmp(&before, &m.l, sizeof before) == 0);
	CHECK(len == 0 && oa == 0 && ob == 0); CHECK(out[0] == 0xEE && out[127] == 0xEE);
	CHECK(h.p3a == 1);                                      /* the health pair is filled on BUSY */
	CHECK(M.trace[M.ntrace - 1] == 'L'); end();
}
static void t_wrap(void)               /* V9 */
{
	struct mon m; int i;
	begin("V9 counter wraps modulo 2^32, delta stays correct"); setup(&m);
	m.pta_lock_timeouts = 0xFFFFFFF0u; m.l.pta_timeouts_seen = 0xFFFFFFF0u;
	for (i = 0; i < 32; i++) MON_ATOMIC_INC32(&m.pta_lock_timeouts);
	CHECK(m.pta_lock_timeouts == 0x10u); CHECK((uint32_t)(m.pta_lock_timeouts - m.l.pta_timeouts_seen) == 0x20u); end();
}
static void t_observe(void)            /* V10, Y2 */
{
	struct mon m; struct core1 c = {0, 3}; struct lstate before; int i;
	begin("V10/Y2 Core 1 observation: nothing consumed on a failed attempt"); setup(&m); M.daif = MON_DAIF_ALL;
	m.pta_lock_timeouts = 3; M.ext_hold = 1; before = m.l;
	core1_pass(&m, &c);                                     /* pass 0: attempt fails */
	CHECK(memcmp(&before, &m.l, sizeof before) == 0 && M.violations == 0);
	CHECK(m.h.skip_count == 1 && m.h.skip_total == 1);
	M.ext_hold = 0;
	for (i = 1; i < 256; i++) core1_pass(&m, &c);           /* no attempt until pass 256 */
	CHECK(m.l.pta_timeouts_seen == 0);
	core1_pass(&m, &c);                                     /* pass 256: attempt succeeds */
	CHECK(m.l.pta_timeouts_seen == 3 && m.l.ev_count[EV_LOCK_TIMEOUT] == 3 && m.l.ev_seq == 1);
	CHECK(m.l.ord[0].detail == 3 && m.l.ord[0].type == EV_LOCK_TIMEOUT);
	CHECK(m.h.skip_count == 0 && m.h.starved == 0 && m.h.skip_total == 1 && M.violations == 0); end();
}
static void t_cadence(void)            /* Y1, Y9 */
{
	struct mon m; struct core1 c = {0, 3}; int i;
	begin("Y1/Y9 one non-spinning attempt per 256 passes"); setup(&m); M.daif = MON_DAIF_ALL;
	for (i = 0; i < 1000; i++) core1_pass(&m, &c);
	CHECK(M.trylock_calls == 4 && m.h.poll_count == 1000);
	setup(&m); M.daif = MON_DAIF_ALL; M.ext_hold = 1; memset(&c, 0, sizeof c); c.K = 3;
	for (i = 0; i < 1000; i++) core1_pass(&m, &c);
	CHECK(M.trylock_calls == 4);                            /* a held lock does not make Core 1 retry or spin */
	c.pass = 0xFFFFFF00u; for (i = 0; i < 600; i++) core1_pass(&m, &c);   /* the local counter wraps harmlessly */
	CHECK(M.violations == 0); end();
}
static void t_starve(void)             /* Y3, Y4, Y5 */
{
	struct mon m; struct core1 c = {0, 3}; int i;
	begin("Y3/Y4/Y5 starvation episodes"); setup(&m); M.daif = MON_DAIF_ALL; M.ext_hold = 1;
	for (i = 0; i < 2 * 256 + 1; i++) core1_pass(&m, &c);   /* three failed attempts: K reached on the third */
	CHECK(m.h.skip_count == 3 && m.h.starved == 1);
	{ int k; for (k = 0; k < 256; k++) core1_pass(&m, &c); }
	CHECK(m.h.skip_count == 4 && m.l.ev_seq == 0);          /* Y5: held, starved stays 1, no event can be appended */
	M.ext_hold = 0; { int k; for (k = 0; k < 256; k++) core1_pass(&m, &c); }   /* next attempt succeeds */
	CHECK(m.h.skip_count == 0 && m.h.starved == 0 && m.h.skip_total == 4);     /* skip_total is cumulative */
	CHECK(m.l.ev_count[EV_STARVED] == 1 && m.l.ord[0].type == EV_STARVED && m.l.ord[0].detail == 4);   /* Y4 */
	M.ext_hold = 1; { int k; for (k = 0; k < 2 * 256; k++) core1_pass(&m, &c); }   /* shorter episode: 2 failures */
	M.ext_hold = 0; { int k; for (k = 0; k < 256; k++) core1_pass(&m, &c); }
	CHECK(m.h.skip_count == 0 && m.l.ev_count[EV_STARVED] == 1);              /* no event below K */
	CHECK(M.violations == 0); end();
}
static void t_exactly_once(void)       /* V12, Z5 */
{
	struct mon m; uint32_t e; uint64_t t; int i;
	begin("V12/Z5 exactly-once timeout and heartbeat transitions"); setup(&m); M.daif = 0;
	e = grab(&m, 0); t = M.now; mon_note_valid_frame(&m, t); mon_release(&m, e);
	for (i = 0; i < 4; i++) {                               /* alternate the two contexts, repeatedly past the deadline */
		M.daif = (i & 1) ? MON_DAIF_ALL : 0; e = grab(&m, 0); mon_evaluate(&m, t + MON_T_TRAFFIC + 100 + i); mon_release(&m, e);
	}
	CHECK(m.l.tr_timeout_raised && m.l.ev_count[EV_TIMEOUT] == 1);
	M.daif = 0; e = grab(&m, 0); mon_note_valid_frame(&m, t + MON_T_TRAFFIC + 500);
	mon_note_valid_frame(&m, t + MON_T_TRAFFIC + 501); mon_release(&m, e);
	CHECK(!m.l.tr_timeout_raised && m.l.ev_count[EV_TRAFFIC_RECOVERED] == 1);                  /* recovered exactly once */
	e = grab(&m, 0); m.l.tr_armed = false;                  /* isolate the heartbeat: no further traffic timeouts */
	mon_hb_issue(&m, t); mon_evaluate(&m, t + MON_D_HB + 5); mon_evaluate(&m, t + MON_D_HB + 6);
	CHECK(m.l.hb_state == HB_MISSED && m.l.hb_miss == 1 && !m.l.hb_timeout_raised);   /* one miss, counted once */
	mon_hb_answer(&m, t + MON_D_HB + 7, false);
	CHECK(m.l.hb_state == HB_LATE_ANSWERED && m.l.hb_miss == 1);                       /* a late answer repairs nothing */
	mon_hb_issue(&m, t + 2 * MON_D_HB); mon_evaluate(&m, t + 3 * MON_D_HB + 5);                  /* second miss */
	CHECK(m.l.hb_miss == 2 && !m.l.hb_timeout_raised);
	mon_hb_issue(&m, t + 4 * MON_D_HB); for (i = 0; i < 3; i++) mon_evaluate(&m, t + 5 * MON_D_HB + 5 + i);
	CHECK(m.l.hb_miss == 3 && m.l.hb_timeout_raised && m.l.ev_count[EV_TIMEOUT] == 2); /* raised once at k = 2 */
	mon_hb_issue(&m, t + 6 * MON_D_HB); mon_hb_answer(&m, t + 6 * MON_D_HB + 1, true);
	CHECK(m.l.hb_miss == 0 && !m.l.hb_timeout_raised && m.l.hb_state == HB_ANSWERED);  /* only an on-time answer clears */
	mon_release(&m, e); CHECK(M.violations == 0); end();
}
static void t_lease(void)              /* G5-3/4/6 inside evaluate */
{
	struct mon m; uint32_t e; uint64_t t;
	begin("G5 lease expiry restarts the window; no stale timeout"); setup(&m);
	e = grab(&m, 0); t = M.now; mon_note_valid_frame(&m, t); m.l.idle_until = t + 5 * MON_T_TRAFFIC;
	mon_evaluate(&m, t + 2 * MON_T_TRAFFIC); CHECK(!m.l.tr_timeout_raised);                 /* leased: no timeout */
	mon_evaluate(&m, t + 5 * MON_T_TRAFFIC + 10);
	CHECK(m.l.idle_until == 0 && !m.l.tr_timeout_raised && m.l.tr_last_valid == t + 5 * MON_T_TRAFFIC + 10);
	mon_release(&m, e); end();
}
static uint8_t g_out[256];
static void t_cmd20(void)              /* N1 N2 N3 N6 N7 N8 HP2 HP4 HP5 */
{
	struct mon m; struct proj *p = (struct proj *)g_out; size_t len; uint32_t oa, ob; struct health_out h; int r, i;
	begin("N1-N8/HP2/HP4/HP5 command 20 contract"); setup(&m);
	m.h.last_poll_ticks = M.now - 5 * 19200; m.h.poll_count = 77; m.h.skip_count = 2; m.h.skip_total = 9; m.pta_lock_timeouts = 4;
	m.l.last_counter = 41; m.l.hb_seq = 6; m.l.hb_state = HB_ANSWERED;
	memset(g_out, 0xEE, sizeof g_out);
	r = mon_cmd20(&m, 0, 0, g_out, 128, &len, &oa, &ob, &h);
	CHECK(r == MON_OK && len == 128 && oa == 128 && ob == 1);                               /* N1 */
	CHECK(p->version == 1 && p->length == 128 && p->boot_id == 0xB007 && p->rsv[0] == 0 && p->rsv[1] == 0 &&
	      p->rsv[2] == 0 && p->rsv2 == 0 && g_out[128] == 0xEE);                             /* reserved bytes zero, no overrun */
	CHECK(p->poll_count == 77 && p->skip_total == 9 && p->pta_lock_timeouts == 4 && p->last_counter == 41 && p->hb_seq == 6);
	CHECK(p->core1_poll_age_ticks >= 5 * 19200 && p->core1_poll_age_ticks < 6 * 19200);
	CHECK(p->traffic_age_ticks == ~0ull && p->hb_last_ok_age_ticks == ~0ull && p->lease_remaining_ticks == 0);  /* N6 */
	CHECK(M.timer_read_in_hold);                                                             /* N7: clock read inside the hold */
	CHECK(h.p3a == 4 && h.p3b == 9 && ((h.p2b >> 8) & 255) == 2 && (h.p2b & HP_INIT_OK) && h.p2a >= 5 && h.p2a <= 6);
	{ int last_u = -1, first_l = -1; for (i = 0; i < M.ntrace; i++) { if (M.trace[i] == 'U') last_u = i; if (M.trace[i] == 'L' && first_l < 0) first_l = i; }
	  CHECK(last_u >= 0 && first_l > last_u); }                                                /* HP5: lock-free reads after unlock */
	memset(g_out, 0xEE, 128); h.p3a = 1;
	CHECK(mon_cmd20(&m, 1, 0, g_out, 128, &len, &oa, &ob, &h) == MON_OK && ((struct proj1 *)g_out)->version == 1 && len == 128);   /* N2: page 1 is supported (DEC-IF-10) */
	CHECK(mon_cmd20(&m, 2, 0, g_out, 128, &len, &oa, &ob, &h) == MON_BAD_PARAMETERS && h.p3a == 0);
	CHECK(mon_cmd20(&m, 0, 1, g_out, 128, &len, &oa, &ob, &h) == MON_BAD_PARAMETERS);        /* reserved field non-zero */
	memset(g_out, 0xEE, 128);
	len = 0; CHECK(mon_cmd20(&m, 0, 0, g_out, 127, &len, &oa, &ob, &h) == MON_SHORT_BUFFER && len == 128 && g_out[0] == 0xEE && h.p3a == 0);   /* N3 */
	m.init_ok = false;                                                                        /* N8 precedence */
	CHECK(mon_cmd20(&m, 1, 0, g_out, 128, &len, &oa, &ob, &h) == MON_BAD_STATE);
	CHECK(mon_cmd20(&m, 2, 0, g_out, 128, &len, &oa, &ob, &h) == MON_BAD_PARAMETERS);
	CHECK(mon_cmd20(&m, 0, 0, g_out, 10, &len, &oa, &ob, &h) == MON_BAD_STATE && h.p2a == 0 && h.p2b == 0);   /* W3, HP2 */
	m.init_ok = true;
	CHECK(mon_cmd20(&m, 1, 0, g_out, 10, &len, &oa, &ob, &h) == MON_SHORT_BUFFER);          /* page 1 also checks the buffer */
	M.ext_hold = 1; { unsigned before_calls = M.trylock_calls;
	CHECK(mon_cmd20(&m, 0, 0, g_out, 10, &len, &oa, &ob, &h) == MON_SHORT_BUFFER);           /* buffer size before BUSY */
	CHECK(M.trylock_calls == before_calls && M.violations == 0); }   /* the lock was never even tried */
	m.h.last_poll_ticks = 0;                                                                  /* HP4 sentinels */
	M.ext_hold = 0; CHECK(mon_cmd20(&m, 0, 0, g_out, 128, &len, &oa, &ob, &h) == MON_OK && h.p2a == 0xFFFFFFFFu && p->core1_poll_age_ticks == ~0ull);
	m.h.last_poll_ticks = 1; M.now = 19200ull * 5000000000ull; m.h.skip_count = 300;
	CHECK(mon_cmd20(&m, 0, 0, g_out, 128, &len, &oa, &ob, &h) == MON_OK && h.p2a == 0xFFFFFFFFu && ((h.p2b >> 8) & 255) == 255);
	end();
}
static void t_lease_projection(void)   /* N6 lease and flags */
{
	struct mon m; size_t len; uint32_t oa, ob; struct health_out h; struct proj *p = (struct proj *)g_out;
	begin("N6 lease_remaining and flags"); setup(&m); m.l.tr_armed = true; m.l.tr_last_valid = M.now; m.l.idle_until = M.now + 1000000;
	CHECK(mon_cmd20(&m, 0, 0, g_out, 128, &len, &oa, &ob, &h) == MON_OK);
	CHECK((p->flags & PFLAG_LEASE) && (p->flags & PFLAG_TR_ARMED) && p->lease_remaining_ticks > 900000 && p->lease_remaining_ticks <= 1000000);
	m.h.starved = 1; CHECK(mon_cmd20(&m, 0, 0, g_out, 128, &len, &oa, &ob, &h) == MON_OK && (p->flags & PFLAG_STARVED) && (h.p2b & HP_STARVED)); end();
}
static void t_cmd18(void)              /* five-record limit, ordering, scan, ring invariants */
{
	struct mon m; uint8_t out[160]; size_t len; uint32_t oa, ob, next = 1, got = 0, prev = 0, c; struct health_out h; struct ev *e = (struct ev *)out;
	int i, r, rounds = 0, pins = 0;
	begin("cmd 18 five records, order, bounded scan, ring invariants"); setup(&m);
	for (i = 0; i < 3; i++) { M.daif = 0; { uint32_t x = grab(&m, 0); M.now += MON_TICKS_PER_SEC; mon_ev_append(&m, EV_AUTH, i, 0, M.now); mon_release(&m, x); } }
	for (i = 0; i < 6; i++) { uint32_t x = grab(&m, 0); M.now += MON_TICKS_PER_SEC; mon_ev_append(&m, EV_REPLAY, i, 0, M.now); mon_release(&m, x); }
	CHECK(mon_cmd18(&m, 1, 0, out, 160, &len, &oa, &ob, &h) == MON_BAD_PARAMETERS);        /* outside 1 to 5 */
	CHECK(mon_cmd18(&m, 1, 6, out, 160, &len, &oa, &ob, &h) == MON_BAD_PARAMETERS);
	CHECK(mon_cmd18(&m, 1, 5, out, 119, &len, &oa, &ob, &h) == MON_SHORT_BUFFER && len == 120);
	M.scan_visits = 0;
	r = mon_cmd18(&m, 1, 5, out, 160, &len, &oa, &ob, &h);
	CHECK(r == MON_OK && len == 120 && (oa & 255) == 5 && (oa & 0x100) && M.scan_visits <= 64);
	do {                                                                                   /* drain: next call starts at last seq + 1 */
		r = mon_cmd18(&m, next, 5, out, 160, &len, &oa, &ob, &h); rounds++;
		for (c = 0; c < (oa & 255); c++) { CHECK(e[c].seq > prev); prev = e[c].seq; got++; }
		if (oa & 255) next = prev + 1;
	} while (r == MON_OK && (oa & 0x100) && rounds < 10);
	CHECK(got == 9 && rounds == 2);                                                        /* 9 events, two calls of 5 + 4 */
	for (i = 0; i < 100; i++) { uint32_t x = grab(&m, 0); M.now += MON_TICKS_PER_SEC; mon_ev_append(&m, EV_ALTERED, i, 0, M.now); mon_release(&m, x); }
	for (i = 0; i < 6; i++) pins += m.l.pinned_n[i];
	CHECK(m.l.pinned_used == (uint32_t)pins && m.l.pinned_used <= 24 && m.l.pinned_n[EV_ALTERED] == 4);   /* 4 per class, total <= 24 */
	CHECK(m.l.pinned[EV_AUTH * 4 + 0].seq == 1 && m.l.pinned[EV_REPLAY * 4 + 0].seq == 4);  /* flood never overwrites pinned evidence */
	CHECK(m.l.ev_lost == 58);              /* 2 + 96 ordinary events into 40 slots: 58 overwrites, every one counted */
	r = mon_cmd18(&m, 1, 5, out, 160, &len, &oa, &ob, &h); CHECK(r == MON_OK && ob == m.l.ev_lost && M.scan_visits <= 64 * 20);
	CHECK(M.violations == 0); end();
}
static void t_ratelimit(void)          /* DEC-RATE-1, Q5 */
{
	struct mon m; uint32_t e; uint64_t t; int i;
	begin("Q5 per-class diagnostic rate limit (1 event per second per class)"); setup(&m);
	e = grab(&m, 0); t = M.now;
	mon_ev_append(&m, EV_LOCK_TIMEOUT, 0, 1, t);
	mon_ev_append(&m, EV_STARVED, 0, 2, t + 10);              /* different class, same second: must NOT be suppressed */
	CHECK(m.l.ev_seq == 2 && m.l.ev_count[EV_LOCK_TIMEOUT] == 1 && m.l.ev_count[EV_STARVED] == 1);
	mon_ev_append(&m, EV_LOCK_TIMEOUT, 0, 3, t + 100);        /* same class within the second: suppressed, still counted */
	mon_ev_append(&m, EV_STARVED, 0, 4, t + 200);
	CHECK(m.l.ev_seq == 2 && m.l.ev_count[EV_LOCK_TIMEOUT] == 2 && m.l.ev_count[EV_STARVED] == 2);
	mon_ev_append(&m, EV_LOCK_TIMEOUT, 0, 5, t + MON_TICKS_PER_SEC - 1);   /* one tick short of a second: still suppressed */
	CHECK(m.l.ev_seq == 2);
	mon_ev_append(&m, EV_LOCK_TIMEOUT, 0, 6, t + MON_TICKS_PER_SEC);       /* exactly one second later: allowed */
	CHECK(m.l.ev_seq == 3 && m.l.ev_count[EV_LOCK_TIMEOUT] == 4);
	mon_ev_append(&m, EV_STARVED, 0, 7, t + MON_TICKS_PER_SEC + 50);       /* STARVED has its own clock from t+10 */
	CHECK(m.l.ev_seq == 4);
	for (i = 0; i < 20; i++) mon_ev_append(&m, EV_AUTH, i, 0, t + 300);    /* evidence: first 4 always kept, then limited per class */
	CHECK(m.l.pinned_n[EV_AUTH] == 4 && m.l.ev_count[EV_AUTH] == 20 && m.l.ev_seq == 9);   /* 4 pinned + 1 ordinary, 15 suppressed, all 20 counted */
	{ int pre = (int)m.l.ev_seq; mon_ev_append(&m, EV_MODE, 0, 0, t); mon_ev_append(&m, EV_TRAFFIC_RECOVERED, 0, 0, t);
	  CHECK((int)m.l.ev_seq == pre + 2); }                       /* a third and fourth class are independent too */
	mon_release(&m, e); CHECK(M.violations == 0); end();
}
static void evadd(struct mon *m, unsigned type, uint32_t c) { uint32_t x = grab(m, 0); M.now += MON_TICKS_PER_SEC; mon_ev_append(m, type, c, 0, M.now); mon_release(m, x); }
static void t_evidence_flood(void)     /* DEC-IF-10 option Y */
{
	struct mon m; int i, ordinary_used = 0; uint32_t e;
	begin("EV3 evidence flood cannot evict other unread evidence"); setup(&m);
	for (i = 0; i < 5; i++) evadd(&m, EV_TIMEOUT, i);                    /* 4 pinned + 1 ordinary TIMEOUT event */
	CHECK(m.l.pinned_n[EV_TIMEOUT] == 4 && m.l.ord_used[0] && m.l.ord[0].type == EV_TIMEOUT);
	e = grab(&m, 0); for (i = 0; i < 1000; i++) mon_ev_append(&m, EV_AUTH, i, 0, M.now + 10); mon_release(&m, e);   /* a one-second flood of AUTH_FAIL */
	for (i = 0; i < 40; i++) ordinary_used += m.l.ord_used[i];
	CHECK(m.l.ev_count[EV_AUTH] == 1000 && m.l.pinned_n[EV_AUTH] == 4);                      /* exact counter, pinned quota kept */
	CHECK(ordinary_used == 2 && m.l.ev_lost == 0 && m.l.ord[0].type == EV_TIMEOUT);          /* the TIMEOUT entry survives, nothing lost */
	e = grab(&m, 0); for (i = 0; i < 1000; i++) mon_ev_append(&m, EV_REPLAY, i, 1, M.now + 20); mon_release(&m, e);
	CHECK(m.l.ev_count[EV_REPLAY] == 1000 && m.l.ev_lost == 0 && m.l.ord[0].type == EV_TIMEOUT);
	CHECK(M.violations == 0); end();
}
static void t_cmd19(void)              /* DEC-IF-4 ACK1-ACK4 */
{
	struct mon m; uint32_t oa = 9, ob = 9, lost; struct health_out h; struct lstate before; int i;
	begin("ACK1-4 command 19 is a bookmark only"); setup(&m);
	for (i = 0; i < 60; i++) evadd(&m, EV_ALTERED, i);                                       /* ring wraps: ev_lost > 0 */
	lost = m.l.ev_lost; CHECK(lost > 0 && m.l.ev_seq == 60);
	CHECK(mon_cmd19(&m, 1, 7, &oa, &ob, &h) == MON_BAD_PARAMETERS && h.p3a == 0);            /* ACK4 */
	before = m.l;
	CHECK(mon_cmd19(&m, 1000000, 0, &oa, &ob, &h) == MON_OK && oa == 0 && ob == 60);        /* ACK2 clamp, effective value */
	CHECK(m.l.ev_ack == 60 && m.l.ev_lost == lost);                                          /* ACK1 */
	CHECK(memcmp(before.ord, m.l.ord, sizeof before.ord) == 0 && memcmp(before.pinned, m.l.pinned, sizeof before.pinned) == 0);
	CHECK(mon_cmd19(&m, 3, 0, &oa, &ob, &h) == MON_OK && ob == 60 && m.l.ev_ack == 60);     /* ACK3 monotonic, no-op */
	evadd(&m, EV_ALTERED, 99); evadd(&m, EV_ALTERED, 100);                                   /* acknowledged entries are still overwritten AND counted */
	CHECK(m.l.ev_lost == lost + 2);
	m.init_ok = false; CHECK(mon_cmd19(&m, 1, 0, &oa, &ob, &h) == MON_BAD_STATE && h.p2a == 0);
	m.init_ok = true; M.ext_hold = 1; CHECK(mon_cmd19(&m, 1, 0, &oa, &ob, &h) == MON_BUSY && h.p3a == 1);   /* ACK4 health on BUSY */
	CHECK(M.violations == 0); end();
}
static void t_cmd21(void)              /* DEC-IF-5 L21-1..L21-9 */
{
	struct mon m; uint32_t oa, ob; struct health_out h; uint64_t until; int i;
	begin("L21-1..9 command 21 idle lease"); setup(&m);
	CHECK(mon_cmd21(&m, 2, 5, &oa, &ob, &h) == MON_BAD_PARAMETERS);                          /* L21-1 */
	CHECK(mon_cmd21(&m, 1, 0, &oa, &ob, &h) == MON_BAD_PARAMETERS);
	CHECK(mon_cmd21(&m, 0, 5, &oa, &ob, &h) == MON_BAD_PARAMETERS);
	CHECK(mon_cmd21(&m, 0, 0, &oa, &ob, &h) == MON_OK && oa == V21_NO_CHANGE && m.l.ev_seq == 0 && m.l.idle_uses == 0);   /* no change, no event, no use */
	CHECK(mon_cmd21(&m, 1, 1000, &oa, &ob, &h) == MON_OK && oa == V21_GRANTED && ob >= 999 && ob <= 1000);
	CHECK(m.l.idle_uses == 1 && m.l.ev_count[EV_LEASE_GRANTED] == 1 && m.l.ev_count[EV_MODE] == 1);   /* L21-3, L21-9 */
	until = m.l.idle_until;
	CHECK(mon_cmd21(&m, 1, 500, &oa, &ob, &h) == MON_OK && oa == V21_RENEWED && ob >= 499 && ob <= 500);   /* shortening is allowed */
	CHECK(m.l.idle_until < until && m.l.idle_uses == 2);                                     /* L21-5 replaces, not additive */
	CHECK(mon_cmd21(&m, 1, 120000, &oa, &ob, &h) == MON_OK && oa == V21_RENEWED && ob >= 59999 && ob <= 60000);   /* L21-2 clamp */
	m.l.tr_armed = true; m.l.tr_timeout_raised = true; m.l.tr_last_valid = 1;
	CHECK(mon_cmd21(&m, 1, 1000, &oa, &ob, &h) == MON_OK && m.l.tr_timeout_raised);         /* L21-6 a grant never clears it */
	m.l.tr_timeout_raised = false; m.l.tr_last_valid = 1;
	CHECK(mon_cmd21(&m, 0, 0, &oa, &ob, &h) == MON_OK && oa == V21_RETURNED && ob == 0 && m.l.idle_until == 0 && m.l.tr_last_valid > 1);   /* window restarts */
	CHECK(m.l.idle_uses == 4);                                                               /* returning is free */
	m.l.idle_until = M.now - 5;                                                              /* L21-8 expired lease: evaluation returns to EXPECTED first */
	CHECK(mon_cmd21(&m, 1, 1000, &oa, &ob, &h) == MON_OK && oa == V21_GRANTED);
	m.l.hb_state = HB_OPEN; m.l.hb_issue = M.now - 2 * MON_D_HB;                             /* L21-7 heartbeat still evaluated during IDLE */
	CHECK(mon_cmd21(&m, 1, 1000, &oa, &ob, &h) == MON_OK && m.l.hb_state == HB_MISSED && m.l.hb_miss == 1);
	for (i = 0; i < 20; i++) mon_cmd21(&m, 1, 1000, &oa, &ob, &h);                            /* L21-4 the cap */
	{ uint32_t uses = m.l.idle_uses; uint64_t u2 = m.l.idle_until;
	  CHECK(uses == MON_IDLE_USES_MAX && mon_cmd21(&m, 1, 1000, &oa, &ob, &h) == MON_OK && oa == V21_REFUSED_CAP);
	  CHECK(m.l.idle_uses == uses && m.l.idle_until == u2 && m.l.ev_count[EV_LEASE_REFUSED] >= 1); }
	CHECK(mon_cmd21(&m, 0, 0, &oa, &ob, &h) == MON_OK && oa == V21_RETURNED);                 /* returning still works at the cap */
	CHECK(M.violations == 0); end();
}
static void t_cmd23(void)              /* DEC-IF-7 N23-1..N23-10 */
{
	struct mon m; uint32_t oa, ob; struct health_out h; uint32_t e; uint64_t N = 0x1122334455667788ull;
	begin("N23 command 23 heartbeat answer (stored nonce, no HMAC)"); setup(&m);
	CHECK(mon_cmd23(&m, 1, 3, 0, 0, &oa, &ob, &h) == MON_BAD_PARAMETERS);
	CHECK(mon_cmd23(&m, 1, 0, 0, 0, &oa, &ob, &h) == MON_OK && oa == V23_UNISSUED);          /* state NONE */
	e = grab(&m, 0); mon_hb_issue(&m, M.now); m.l.hb_nonce = N; m.l.hb_nonce_valid = true; mon_release(&m, e);   /* seq 1 OPEN, nonce fetched */
	CHECK(mon_cmd23(&m, 2, 0, 0, 0, &oa, &ob, &h) == MON_OK && oa == V23_UNISSUED);          /* seq beyond hb_seq */
	CHECK(mon_cmd23(&m, 0, 0, 0, 0, &oa, &ob, &h) == MON_OK && oa == V23_STALE);
	CHECK(mon_cmd23(&m, 1, 0, 1, 2, &oa, &ob, &h) == MON_OK && oa == V23_BAD_NONCE && ob == 2 && m.l.hb_state == HB_OPEN && m.l.hb_miss == 0);   /* no state change */
	m.l.hb_nonce_valid = false;
	CHECK(mon_cmd23(&m, 1, 0, (uint32_t)N, (uint32_t)(N >> 32), &oa, &ob, &h) == MON_OK && oa == V23_BAD_NONCE && ob == 1);   /* never fetched */
	m.l.hb_nonce_valid = true; m.l.hb_timeout_raised = true; m.l.hb_miss = 2;
	CHECK(mon_cmd23(&m, 1, 0, (uint32_t)N, (uint32_t)(N >> 32), &oa, &ob, &h) == MON_OK && oa == V23_ON_TIME);
	CHECK(m.l.hb_state == HB_ANSWERED && m.l.hb_miss == 0 && !m.l.hb_timeout_raised && m.l.ev_count[EV_HB_RECOVERED] == 1 && m.l.hb_last_ok != 0);
	CHECK(mon_cmd23(&m, 1, 0, (uint32_t)N, (uint32_t)(N >> 32), &oa, &ob, &h) == MON_OK && oa == V23_DUPLICATE);
	e = grab(&m, 0); mon_hb_issue(&m, M.now); m.l.hb_nonce = N; m.l.hb_nonce_valid = true; mon_release(&m, e);   /* seq 2 */
	M.now += MON_D_HB + 1000;                                                                 /* deadline passes: evaluation marks MISSED */
	CHECK(mon_cmd23(&m, 2, 0, (uint32_t)N, (uint32_t)(N >> 32), &oa, &ob, &h) == MON_OK && oa == V23_LATE);
	CHECK(m.l.hb_state == HB_LATE_ANSWERED && m.l.hb_miss == 1 && m.l.ev_count[EV_LATE_ANSWER] == 1);   /* miss count not reset */
	CHECK(mon_cmd23(&m, 2, 0, (uint32_t)N, (uint32_t)(N >> 32), &oa, &ob, &h) == MON_OK && oa == V23_DUPLICATE);
	e = grab(&m, 0); mon_hb_issue(&m, M.now); mon_release(&m, e);                                 /* seq 3: new challenge, nonce NOT stored yet */
	CHECK(m.l.hb_nonce_valid == false);                                                       /* G6-4: the old nonce is invalid */
	CHECK(mon_cmd23(&m, 3, 0, (uint32_t)N, (uint32_t)(N >> 32), &oa, &ob, &h) == MON_OK && oa == V23_BAD_NONCE && ob == 1);   /* the old nonce does not answer the new challenge */
	CHECK(m.l.vtot[1] >= 8 && h.p3a == 0);
	m.init_ok = false; CHECK(mon_cmd23(&m, 2, 0, 0, 0, &oa, &ob, &h) == MON_BAD_STATE && h.p2a == 0);
	m.init_ok = true; M.ext_hold = 1; CHECK(mon_cmd23(&m, 2, 0, 0, 0, &oa, &ob, &h) == MON_BUSY && oa == 0 && h.p3a == 1);
	CHECK(M.violations == 0); end();
}
static void t_page1(void)              /* EV5 */
{
	struct mon m; size_t len; uint32_t oa, ob; struct health_out h; struct proj1 *p = (struct proj1 *)g_out; int i;
	begin("EV5 command 20 page 1 layout and content"); setup(&m);
	for (i = 0; i < 6; i++) evadd(&m, EV_AUTH, i);
	for (i = 0; i < 3; i++) evadd(&m, EV_MALFORMED, i);
	m.l.vtot[0] = 11; m.l.vtot[1] = 22;
	memset(g_out, 0xEE, sizeof g_out);
	CHECK(mon_cmd20(&m, 1, 0, g_out, 128, &len, &oa, &ob, &h) == MON_OK && len == 128 && oa == 128 && ob == 1);
	CHECK(p->version == 1 && p->length == 128 && p->rsv == 0 && g_out[128] == 0xEE);
	CHECK(p->ev_count[EV_AUTH] == 6 && p->ev_count[EV_MALFORMED] == 3 && p->v_frame_total == 11 && p->v_hb_total == 22 && p->v_xcheck_total == 0);   /* exact counters count every occurrence */
	CHECK(mon_cmd20(&m, 2, 0, g_out, 128, &len, &oa, &ob, &h) == MON_BAD_PARAMETERS);
	CHECK(M.violations == 0); end();
}
static void t_gates(void)              /* W3, Z1 negative control, V11 */
{
	struct mon m; uint8_t out[128]; size_t len; uint32_t oa, ob; struct health_out h;
	begin("W3/Z1/V11 init gate, lock-held control, mac guard"); setup(&m); m.init_ok = false;
	CHECK(mon_cmd18(&m, 1, 1, out, 128, &len, &oa, &ob, &h) == MON_BAD_STATE && h.p3a == 0 && h.p2a == 0);
	setup(&m); (void)mon_l(&m); CHECK(M.violations == 1);                                    /* Z1: access without the lock is flagged */
	setup(&m); CHECK(mon_mac_enter(&m) == MON_OK); CHECK(mon_mac_enter(&m) == MON_BUSY);
	mon_mac_leave(&m); CHECK(mon_mac_enter(&m) == MON_OK); end();
}
int main(void)
{
	printf("Component 2.2 host reference model: logic tests (NOT the OP-TEE code)\n");
	t_acquire_free(); t_acquire_held(); t_frozen(); t_backward(); t_shared_budget(); t_exception_order(); t_busy_untouched();
	t_wrap(); t_observe(); t_cadence(); t_starve(); t_exactly_once(); t_lease(); t_cmd20(); t_lease_projection(); t_cmd18(); t_ratelimit(); t_evidence_flood(); t_cmd19(); t_cmd21(); t_cmd23(); t_page1(); t_gates();
	printf("total: %d checks, %d failed\n", nchk, nfail);
	return nfail != 0;
}
