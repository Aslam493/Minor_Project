/* Host reference model of the approved Component 2.2 synchronization logic (design sections 4d-4i).
 * NOT the OP-TEE implementation. A pass here shows the logic is self-consistent, not that the real code passes. */
#include <stdlib.h>
#include "mon_model.h"

struct mock M;
uint64_t mock_timer(void)
{
	M.timer_reads++;
	if (M.tmode == 0) M.now += M.step; else if (M.tmode == 2) M.now -= M.step;
	if (M.held) { M.timer_read_in_hold = true; M.timer_read_while_held = M.now; }
	return M.now;
}
void mock_trace(char c) { if (M.ntrace < (int)sizeof(M.trace) - 1) M.trace[M.ntrace++] = c; }
bool mock_trylock(unsigned *l)
{
	if (!(M.daif & MON_DAIF_IRQ)) M.violations++;          /* cpu_spin_trylock asserts IRQ masked */
	M.trylock_calls++;
	if (M.ext_hold) {
		if (M.ext_release_after && --M.ext_release_after == 0) M.ext_hold = 0; else return false;
	}
	if (*l) return false;
	*l = 1; M.locked_count++; M.held = true; return true;
}
void mock_unlock(unsigned *l)
{
	if (!M.held) M.violations++;
	*l = 0; M.locked_count--; M.held = false; mock_trace('U');
}
uint32_t mock_mask_all(void) { uint32_t e = M.daif; M.daif = MON_DAIF_ALL; return e; }
void mock_unmask(uint32_t e)
{
	/* thread_set_exceptions asserts no spinlock is held when IRQ becomes unmasked */
	if (!(e & MON_DAIF_IRQ) && M.locked_count) M.violations++;
	M.daif = e; mock_trace('M');
}

/* Z1: every access to a lock-protected member goes through here */
struct lstate *mon_l(struct mon *m) { if (!M.held) M.violations++; return &m->l; }

/* ---- 4f bounded PTA acquisition (B1 to B3) ---- */
int mon_acquire(struct mon *m, struct mon_wait *w, uint32_t *exc)
{
	uint32_t e; uint64_t t0, t1, now;
	if (w->wait_ticks_used >= MON_BUDGET_TICKS || w->attempts_used >= MON_ATTEMPT_CAP) goto exhausted_unmasked;
	e = MON_MASK_ALL();
	t0 = MON_TIMER();
	for (;;) {
		if (MON_TRYLOCK(&m->lock)) {
			t1 = MON_TIMER();
			w->wait_ticks_used += (t1 >= t0) ? (t1 - t0) : MON_BUDGET_TICKS;  /* backward timer: consume the budget */
			*exc = e; return MON_OK;
		}
		w->attempts_used++;
		now = MON_TIMER();
		if (now < t0 || w->wait_ticks_used + (now - t0) > MON_BUDGET_TICKS || w->attempts_used >= MON_ATTEMPT_CAP) break;
	}
	MON_UNMASK(e);
exhausted_unmasked:
	MON_ATOMIC_INC32(&m->pta_lock_timeouts);
	return MON_BUSY;
}
void mon_release(struct mon *m, uint32_t exc) { MON_UNLOCK(&m->lock); MON_UNMASK(exc); }

/* ---- G8 event ring (4c) ---- */
void mon_ev_append(struct mon *m, unsigned type, uint32_t counter, uint32_t detail, uint64_t now)
{
	struct lstate *l = mon_l(m); struct ev e; uint32_t i;
	l->ev_count[type]++;                                   /* exact counter, even when suppressed */
	e.seq = 0; e.type = (uint16_t)type; e.flags = 0; e.ticks = now; e.counter = counter; e.detail = detail;
	if (type < NEV_EVID && l->pinned_n[type] < 4) {        /* first 4 per evidence class: always kept, never limited */
		e.seq = ++l->ev_seq;
		l->pinned[type * 4 + l->pinned_n[type]++] = e; l->pinned_used++; return;
	}
	/* every other event (all diagnostic classes, and evidence classes after their pinned quota): one per second PER CLASS */
	if (l->diag_have[type] && now >= l->diag_last[type] && now - l->diag_last[type] < MON_TICKS_PER_SEC) return;
	l->diag_have[type] = true; l->diag_last[type] = now;
	e.seq = ++l->ev_seq;
	i = l->ord_head % 40;
	if (l->ord_used[i]) { l->ev_lost++; e.flags |= 1; }    /* overwrite the oldest unpinned entry, count the loss */
	l->ord[i] = e; l->ord_used[i] = true; l->ord_head++;
}

/* ---- 4d S4 exactly-once evaluation ---- */
static bool expired(uint64_t now, uint64_t t0, uint64_t lim) { return now >= t0 && now - t0 > lim; }
void mon_evaluate(struct mon *m, uint64_t now)
{
	struct lstate *l = mon_l(m);
	if (l->idle_until && now > l->idle_until) {            /* lease expires by itself; window restarts */
		l->idle_until = 0; l->tr_last_valid = now; mon_ev_append(m, EV_MODE, 0, 0, now);
	}
	if (l->tr_armed && !(l->idle_until && now <= l->idle_until) && !l->tr_timeout_raised &&
	    expired(now, l->tr_last_valid, MON_T_TRAFFIC)) {
		l->tr_timeout_raised = true; mon_ev_append(m, EV_TIMEOUT, 0, 1, now);
	}
	if (l->hb_state == HB_OPEN && expired(now, l->hb_issue, MON_D_HB)) {
		l->hb_state = HB_MISSED; l->hb_miss++;
		if (l->hb_miss >= MON_K_MISS && !l->hb_timeout_raised) { l->hb_timeout_raised = true; mon_ev_append(m, EV_TIMEOUT, 0, 2, now); }
	}
}
void mon_note_valid_frame(struct mon *m, uint64_t now)
{
	struct lstate *l = mon_l(m);
	l->tr_armed = true; l->tr_last_valid = now;
	if (l->tr_timeout_raised) { l->tr_timeout_raised = false; mon_ev_append(m, EV_TRAFFIC_RECOVERED, 0, 1, now); }
}
void mon_hb_issue(struct mon *m, uint64_t now) { struct lstate *l = mon_l(m); l->hb_seq++; l->hb_issue = now; l->hb_state = HB_OPEN; l->hb_nonce_valid = false; }
void mon_hb_answer(struct mon *m, uint64_t now, bool on_time)
{
	struct lstate *l = mon_l(m);
	if (l->hb_state == HB_OPEN && on_time) { l->hb_state = HB_ANSWERED; l->hb_last_ok = now; l->hb_miss = 0; l->hb_timeout_raised = false; }
	else if (l->hb_state == HB_MISSED) l->hb_state = HB_LATE_ANSWERED;       /* a late answer repairs nothing (G6-3) */
}

/* ---- 4g Core 1 pass ---- */
void core1_pass(struct mon *m, struct core1 *c)
{
	uint32_t p = c->pass++, delta, sk; uint64_t now;
	MON_STORE64(&m->h.poll_count, MON_LOAD64(&m->h.poll_count) + 1);
	if ((p & 255u) != 0) return;
	MON_STORE64(&m->h.last_poll_ticks, MON_TIMER());
	if (!MON_TRYLOCK(&m->lock)) {                          /* one attempt, never spins; touches only G7 */
		uint32_t n = MON_LOAD32(&m->h.skip_count) + 1;
		MON_STORE32(&m->h.skip_count, n); MON_STORE32(&m->h.skip_total, MON_LOAD32(&m->h.skip_total) + 1);
		if (n >= c->K) MON_STORE32(&m->h.starved, 1);
		return;
	}
	now = MON_TIMER();
	mon_evaluate(m, now);
	delta = (uint32_t)(MON_LOAD32(&m->pta_lock_timeouts) - mon_l(m)->pta_timeouts_seen);   /* 4d S2, under the lock */
	if (delta) {
		mon_l(m)->pta_timeouts_seen += delta;
		mon_l(m)->ev_count[EV_LOCK_TIMEOUT] += delta - 1;   /* append adds the last one: exact total = delta */
		mon_ev_append(m, EV_LOCK_TIMEOUT, 0, delta, now);
	}
	sk = MON_LOAD32(&m->h.skip_count);
	if (sk >= c->K) mon_ev_append(m, EV_STARVED, 0, sk, now);   /* one event per episode that reached K */
	MON_STORE32(&m->h.skip_count, 0); MON_STORE32(&m->h.starved, 0);                       /* skip_total is never reset */
	MON_UNLOCK(&m->lock);
}

/* ---- health pair (Step 3 contract) ---- */
static void fill_health(struct mon *m, struct health_out *h)
{
	uint64_t lp = MON_LOAD64(&m->h.last_poll_ticks), now, age; uint32_t sk, st;
	now = MON_TIMER();
	mock_trace('L');
	if (lp == 0) age = 0xFFFFFFFFull;
	else { age = (now >= lp) ? (now - lp) / 19200u : 0; if (age > 0xFFFFFFFFull) age = 0xFFFFFFFFull; }
	sk = MON_LOAD32(&m->h.skip_count); st = MON_LOAD32(&m->h.starved);
	h->p2a = (uint32_t)age;
	h->p2b = (st ? HP_STARVED : 0) | (m->init_ok ? HP_INIT_OK : 0) | ((sk > 255 ? 255 : sk) << HP_SKIP_SHIFT);
	h->p3a = MON_LOAD32(&m->pta_lock_timeouts); h->p3b = MON_LOAD32(&m->h.skip_total);
}
static void zero_health(struct health_out *h) { memset(h, 0, sizeof *h); }

/* ---- cmd 20, page 0 ---- */
int mon_cmd20(struct mon *m, uint32_t page, uint32_t b, uint8_t *out, size_t outsz, size_t *outlen,
              uint32_t *oa, uint32_t *ob, struct health_out *h)
{
	struct mon_wait w = {0, 0}; uint32_t exc; struct proj loc; struct lstate *l; uint64_t now, lp; size_t i;
	zero_health(h);
	if (b != 0 || page >= 2) return MON_BAD_PARAMETERS;
	if (!m->init_ok) return MON_BAD_STATE;
	if (outsz < 128) { *outlen = 128; return MON_SHORT_BUFFER; }
	if (mon_acquire(m, &w, &exc) != MON_OK) { *oa = 0; *ob = 0; *outlen = 0; fill_health(m, h); return MON_BUSY; }
	now = MON_TIMER();
	mon_evaluate(m, now);
	l = mon_l(m);
	if (page == 1) {
		struct proj1 p1; uint32_t k;
		for (i = 0; i < sizeof p1 / 8; i++) ((uint64_t *)&p1)[i] = 0;
		p1.version = 1; p1.length = 128;
		for (k = 0; k < NEV; k++) p1.ev_count[k] = l->ev_count[k];
		p1.v_frame_total = l->vtot[0]; p1.v_hb_total = l->vtot[1]; p1.v_xcheck_total = l->vtot[2];
		mon_release(m, exc);
		for (i = 0; i < 16; i++) ((uint64_t *)out)[i] = ((uint64_t *)&p1)[i];
		*outlen = 128; *oa = 128; *ob = 1; fill_health(m, h);
		return MON_OK;
	}
	for (i = 0; i < sizeof loc / 8; i++) ((uint64_t *)&loc)[i] = 0;          /* explicit word loop, no memset */
	loc.version = 1; loc.length = 128; loc.boot_id = m->boot_id;
	loc.flags = PFLAG_INIT_OK | (l->tr_armed ? PFLAG_TR_ARMED : 0) | (l->tr_timeout_raised ? PFLAG_TR_TIMEOUT : 0) |
	            (l->hb_timeout_raised ? PFLAG_HB_TIMEOUT : 0) | ((l->idle_until && now <= l->idle_until) ? PFLAG_LEASE : 0);
	loc.now_ticks = now;
	loc.pta_timeouts_seen = l->pta_timeouts_seen; loc.last_counter = l->last_counter; loc.hb_seq = l->hb_seq;
	loc.traffic_age_ticks = (l->tr_armed && now >= l->tr_last_valid) ? now - l->tr_last_valid : ~0ull;
	loc.hb_last_ok_age_ticks = (l->hb_last_ok && now >= l->hb_last_ok) ? now - l->hb_last_ok : ~0ull;
	loc.hb_miss_count = l->hb_miss; loc.idle_uses = l->idle_uses;
	loc.lease_remaining_ticks = (l->idle_until && now <= l->idle_until) ? l->idle_until - now : 0;
	loc.ev_seq = l->ev_seq; loc.ev_ack = l->ev_ack; loc.ev_lost = l->ev_lost; loc.pinned_used = l->pinned_used;
	loc.v_frame = l->last_verdict[0]; loc.v_hb = l->last_verdict[1]; loc.v_xcheck = l->last_verdict[2];
	loc.hb_state = (uint8_t)l->hb_state; loc.mode = l->mode;
	mon_release(m, exc);
	/* lock-free fields, read only after unlocking, through the accessors */
	lp = MON_LOAD64(&m->h.last_poll_ticks);
	now = MON_TIMER();
	loc.core1_poll_age_ticks = (lp == 0) ? ~0ull : (now >= lp ? now - lp : 0);
	loc.poll_count = MON_LOAD64(&m->h.poll_count);
	loc.skip_count = MON_LOAD32(&m->h.skip_count); loc.skip_total = MON_LOAD32(&m->h.skip_total);
	loc.pta_lock_timeouts = MON_LOAD32(&m->pta_lock_timeouts);
	if (MON_LOAD32(&m->h.starved)) loc.flags |= PFLAG_STARVED;
	for (i = 0; i < 16; i++) ((uint64_t *)out)[i] = ((uint64_t *)&loc)[i];   /* caller copy happens after unlock */
	*outlen = 128; *oa = 128; *ob = 1;
	fill_health(m, h);
	return MON_OK;
}

/* ---- cmd 18: at most five records, one 64-slot scan ---- */
int mon_cmd18(struct mon *m, uint32_t from_seq, uint32_t maxn, uint8_t *out, size_t outsz, size_t *outlen,
              uint32_t *oa, uint32_t *ob, struct health_out *h)
{
	struct mon_wait w = {0, 0}; uint32_t exc, n = 0, total = 0, i, j; struct ev loc[5]; struct lstate *l; uint64_t now;
	zero_health(h);
	if (maxn < 1 || maxn > 5) return MON_BAD_PARAMETERS;
	if (!m->init_ok) return MON_BAD_STATE;
	if (outsz < 24u * maxn) { *outlen = 24u * maxn; return MON_SHORT_BUFFER; }
	if (mon_acquire(m, &w, &exc) != MON_OK) { *oa = 0; *ob = 0; *outlen = 0; fill_health(m, h); return MON_BUSY; }
	now = MON_TIMER();
	mon_evaluate(m, now);
	l = mon_l(m);
	for (i = 0; i < 64; i++) {                              /* at most 64 slots visited, keep the 5 smallest seq >= from_seq */
		const struct ev *e; bool used;
		M.scan_visits++;
		if (i < 24) { e = &l->pinned[i]; used = e->seq != 0; } else { e = &l->ord[i - 24]; used = l->ord_used[i - 24]; }
		if (!used || e->seq < from_seq) continue;
		total++;
		if (n < maxn || e->seq < loc[n - 1].seq) {
			struct ev t = *e;
			if (n < maxn) n++;
			for (j = n - 1; j > 0 && loc[j - 1].seq > t.seq; j--) loc[j] = loc[j - 1];
			loc[j] = t;
		}
	}
	*ob = l->ev_lost; *oa = n | (total > n ? 0x100u : 0);
	mon_release(m, exc);
	for (i = 0; i < n; i++) ((struct ev *)out)[i] = loc[i];  /* caller copy after unlock */
	*outlen = 24u * n;
	fill_health(m, h);
	return MON_OK;
}

int mon_cmd19(struct mon *m, uint32_t ack_upto, uint32_t b, uint32_t *oa, uint32_t *ob, struct health_out *h)
{
	struct mon_wait w = {0, 0}; uint32_t exc; struct lstate *l;
	zero_health(h);
	if (b != 0) return MON_BAD_PARAMETERS;
	if (!m->init_ok) return MON_BAD_STATE;
	if (mon_acquire(m, &w, &exc) != MON_OK) { *oa = 0; *ob = 0; fill_health(m, h); return MON_BUSY; }
	mon_evaluate(m, MON_TIMER());
	l = mon_l(m);
	if (ack_upto > l->ev_seq) ack_upto = l->ev_seq;          /* clamp */
	if (ack_upto > l->ev_ack) l->ev_ack = ack_upto;           /* monotonic bookmark: never touches ev_lost or the overwrite victim */
	*oa = 0; *ob = l->ev_ack;
	mon_release(m, exc);
	fill_health(m, h); return MON_OK;
}
int mon_cmd21(struct mon *m, uint32_t mode, uint32_t req_ms, uint32_t *oa, uint32_t *ob, struct health_out *h)
{
	struct mon_wait w = {0, 0}; uint32_t exc; struct lstate *l; uint64_t now, dur; bool active; int v;
	zero_health(h);
	if (mode > 1 || (mode == 1 && req_ms == 0) || (mode == 0 && req_ms != 0)) return MON_BAD_PARAMETERS;
	if (!m->init_ok) return MON_BAD_STATE;
	if (mon_acquire(m, &w, &exc) != MON_OK) { *oa = 0; *ob = 0; fill_health(m, h); return MON_BUSY; }
	now = MON_TIMER(); mon_evaluate(m, now); l = mon_l(m);
	active = l->idle_until && now <= l->idle_until;
	if (mode == 1) {
		if (l->idle_uses >= MON_IDLE_USES_MAX) { v = V21_REFUSED_CAP; mon_ev_append(m, EV_LEASE_REFUSED, 0, 0, now); }
		else {
			if (req_ms > MON_IDLE_MAX_MS) req_ms = MON_IDLE_MAX_MS;
			dur = (uint64_t)req_ms * 19200u;
			l->idle_until = now + dur;                        /* replaces the old expiry, never additive */
			l->idle_uses++;
			if (active) { v = V21_RENEWED; mon_ev_append(m, EV_LEASE_RENEWED, 0, req_ms, now); }
			else { v = V21_GRANTED; mon_ev_append(m, EV_LEASE_GRANTED, 0, req_ms, now); mon_ev_append(m, EV_MODE, 0, 1, now); }
		}
	} else if (active) {
		l->idle_until = 0; l->tr_last_valid = now; v = V21_RETURNED; mon_ev_append(m, EV_MODE, 0, 0, now);
	} else v = V21_NO_CHANGE;
	*oa = (uint32_t)v;
	if (l->idle_until && now <= l->idle_until) { uint64_t r = (l->idle_until - now) / 19200u; *ob = r > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)r; } else *ob = 0;
	mon_release(m, exc);
	fill_health(m, h); return MON_OK;
}
int mon_cmd23(struct mon *m, uint32_t seq, uint32_t b, uint32_t nlo, uint32_t nhi, uint32_t *oa, uint32_t *ob, struct health_out *h)
{
	struct mon_wait w = {0, 0}; uint32_t exc; struct lstate *l; uint64_t now, given = ((uint64_t)nhi << 32) | nlo; int v; uint32_t detail = 0;
	zero_health(h);
	if (b != 0) return MON_BAD_PARAMETERS;
	if (!m->init_ok) return MON_BAD_STATE;
	if (mon_acquire(m, &w, &exc) != MON_OK) { *oa = 0; *ob = 0; fill_health(m, h); return MON_BUSY; }
	now = MON_TIMER(); mon_evaluate(m, now); l = mon_l(m);
	if (l->hb_state == HB_NONE || seq > l->hb_seq) { v = V23_UNISSUED; mon_ev_append(m, EV_UNISSUED_ANSWER, seq, 0, now); }
	else if (seq < l->hb_seq) { v = V23_STALE; mon_ev_append(m, EV_STALE_ANSWER, seq, 0, now); }
	else if (l->hb_state == HB_ANSWERED || l->hb_state == HB_LATE_ANSWERED) { v = V23_DUPLICATE; mon_ev_append(m, EV_DUP_ANSWER, seq, 0, now); }
	else if (!l->hb_nonce_valid || l->hb_nonce != given) {
		v = V23_BAD_NONCE; detail = l->hb_nonce_valid ? 2 : 1; mon_ev_append(m, EV_BAD_NONCE, seq, detail, now);
	} else if (l->hb_state == HB_OPEN) {
		v = V23_ON_TIME; l->hb_state = HB_ANSWERED; l->hb_last_ok = now; l->hb_miss = 0;
		if (l->hb_timeout_raised) { l->hb_timeout_raised = false; mon_ev_append(m, EV_HB_RECOVERED, seq, 0, now); }
	} else { v = V23_LATE; l->hb_state = HB_LATE_ANSWERED; mon_ev_append(m, EV_LATE_ANSWER, seq, 0, now); }  /* miss count unchanged, nothing cleared */
	l->vtot[1]++;
	*oa = (uint32_t)v; *ob = detail;
	mon_release(m, exc);
	fill_health(m, h); return MON_OK;
}
int mon_mac_enter(struct mon *m)
{
	unsigned exp = 0;
	return __atomic_compare_exchange_n(&m->mac_pta_busy, &exp, 1u, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED) ? MON_OK : MON_BUSY;
}
void mon_mac_leave(struct mon *m) { __atomic_store_n(&m->mac_pta_busy, 0u, __ATOMIC_RELEASE); }
