// SPDX-License-Identifier: BSD-2-Clause
/*
 * Secure CAN Monitor: shared state, synchronisation, events and the logic of PTA commands 17 to 23.
 *
 * Authority: Part_E_Verification/monitor/MONITOR_STATE_DESIGN.md sections 4a to 4k.
 *
 * Rules honoured here (design):
 *  - one lock, no nesting (G9 L8); PTA callers acquire with the bounded, fail-closed sequence of 4f; Core 1 makes one
 *    non-spinning trylock per 256 passes (4g); the lock is never force-released (4e I5);
 *  - nothing runs under the lock except small bounded work (CS1 to CS5): no HMAC, no heap, no printing, no caller
 *    copy, no memcpy/memcmp/memset. Every helper that runs inside a hold is __nostackcheck (no compiler hook);
 *  - caller buffers are copied once before the lock and out after it (R-A8);
 *  - the HMAC runs outside the lock with the guarded mac_pta context (4d S3); Core 1 never calls HMAC or the heap (F27);
 *  - G7 words and pta_lock_timeouts are touched only through the lock-free accessors / the atomic increment.
 */
#include <rpi3_can_monitor.h>
#include <rpi3_can_monitor_keys.h>	/* the prototype keys are visible to this file only */

struct rpi3_mon_state rpi3_mon;		/* zero-initialised in .bss; rpi3_mon_init() makes the state explicit */

#define RPI3_MON_CRIT	static __nostackcheck	/* runs inside a lock hold: no compiler hook, no forbidden call */

static const uint8_t rpi3_mon_key_frame[RPI3_MON_KEY_LEN] = RPI3_MON_KEY_FRAME_INIT;
static const uint8_t rpi3_mon_key_hb[RPI3_MON_KEY_LEN] = RPI3_MON_KEY_HB_INIT;

/* ---------------------------------------------------------------- small helpers */

RPI3_MON_CRIT bool rpi3_mon_expired(uint64_t now, uint64_t t0, uint64_t limit)
{
	return now >= t0 && now - t0 > limit;		/* strict >, never true for a backward clock (R-A4, deadlines only) */
}

static uint32_t rpi3_mon_le32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void rpi3_mon_put_le32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static void rpi3_mon_put_le64(uint8_t *p, uint64_t v)
{
	rpi3_mon_put_le32(p, (uint32_t)v);
	rpi3_mon_put_le32(p + 4, (uint32_t)(v >> 32));
}

static bool rpi3_mon_ct_equal(const uint8_t *a, const uint8_t *b, size_t n)
{
	uint8_t diff = 0;
	size_t i;

	for (i = 0; i < n; i++)
		diff |= (uint8_t)(a[i] ^ b[i]);
	return diff == 0;
}

/* ---------------------------------------------------------------- HMAC and the mac_pta guard (outside the lock only) */

bool rpi3_mon_mac_enter(void)
{
	uint32_t expected = 0;

	return __atomic_compare_exchange_n(&rpi3_mon.mac_pta_busy, &expected, 1U, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}

void rpi3_mon_mac_leave(void)
{
	__atomic_store_n(&rpi3_mon.mac_pta_busy, 0U, __ATOMIC_RELEASE);
}

/* HMAC-SHA256 of msg with key, 32 bytes out. The caller holds the mac_pta guard (or is the single-threaded init). */
static TEE_Result rpi3_mon_hmac(const uint8_t *key, const uint8_t *msg, size_t len, uint8_t *out32)
{
	TEE_Result res = MON_PLAT_MAC_INIT(rpi3_mon.mac_pta, key, RPI3_MON_KEY_LEN);

	if (res == TEE_SUCCESS)
		res = MON_PLAT_MAC_UPDATE(rpi3_mon.mac_pta, msg, len);
	if (res == TEE_SUCCESS)
		res = MON_PLAT_MAC_FINAL(rpi3_mon.mac_pta, out32, 32);
	return res;
}

/* ---------------------------------------------------------------- init (design 4e) */

bool rpi3_mon_init_ok(void)
{
	return MON_PLAT_LOAD_ACQUIRE32(&rpi3_mon.init_ok) != 0;
}

/* The steps that need the crypto layer: the mac_pta context and boot_id (DEC-IF-15). Primary core, single-threaded. */
static TEE_Result rpi3_mon_init_crypto(void)
{
	uint8_t in[10];
	uint8_t dig[32];
	uint8_t prng[2];
	TEE_Result res = MON_PLAT_MAC_ALLOC(&rpi3_mon.mac_pta);

	if (res != TEE_SUCCESS)
		return res;
	res = MON_PLAT_RNG(prng, sizeof(prng));
	if (res != TEE_SUCCESS)
		return res;
	rpi3_mon_put_le64(in, rpi3_mon.init_ticks);
	in[8] = prng[0];
	in[9] = prng[1];
	res = rpi3_mon_hmac(rpi3_mon_key_hb, in, sizeof(in), dig);
	if (res != TEE_SUCCESS)
		return res;
	rpi3_mon.boot_id = rpi3_mon_le32(dig);		/* the first four bytes of the HMAC, little endian */
	return TEE_SUCCESS;
}

/*
 * Runs on the primary core from driver_init(), before the Normal World bootloader and therefore before Linux and
 * Core 1 exist. It never fails the OS boot: if a step fails the monitor stays unavailable (init_ok stays 0, so
 * commands 17 to 23 return BAD_STATE) and legacy commands keep working. init_ok is published LAST.
 */
TEE_Result rpi3_mon_init(void)
{
	TEE_Result res = TEE_SUCCESS;

	memset(&rpi3_mon, 0, sizeof(rpi3_mon));
	rpi3_mon_lock = SPINLOCK_UNLOCK;		/* explicit, in addition to the .bss zero */

	rpi3_mon.profile = MON_PLAT_PROFILE_DEBUG ? RPI3_MON_PROFILE_DEBUG : RPI3_MON_PROFILE_HARDENED;
	rpi3_mon.init_ticks = MON_PLAT_TIMER();
	rpi3_mon.l.mode = RPI3_MON_MODE_F;		/* Mode F; nothing in Component 2 writes it again (S5) */

	res = rpi3_mon_init_crypto();
	if (res == TEE_SUCCESS)
		res = MON_PLAT_INIT_EXTRA();
	if (res != TEE_SUCCESS) {
		MON_PLAT_EMSG("rpi3_mon: init step failed (0x%x): the monitor stays unavailable for this boot",
			      (unsigned int)res);
		return TEE_SUCCESS;
	}

	MON_PLAT_STORE_RELEASE32(&rpi3_mon.init_ok, 1);
	return TEE_SUCCESS;
}

MON_PLAT_REGISTER_INIT(rpi3_mon_init);

/* ---------------------------------------------------------------- bounded PTA lock acquisition (design 4f) */

void rpi3_mon_wait_init(struct rpi3_mon_wait *w)
{
	w->wait_ticks_used = 0;
	w->attempts_used = 0;
}

/*
 * Exactly the sequence of design section 4f B3. The wait state is shared by every acquisition in one PTA command:
 * the budget counts time spent WAITING for the lock, not time since the command started.
 */
enum rpi3_mon_rc rpi3_mon_acquire(struct rpi3_mon_wait *w, uint32_t *exc)
{
	uint32_t e = 0;
	uint64_t t0 = 0;
	uint64_t t1 = 0;
	uint64_t now = 0;

	/* 1. pre-check, before masking anything */
	if (w->wait_ticks_used >= RPI3_MON_BUDGET_TICKS || w->attempts_used >= RPI3_MON_ATTEMPT_CAP)
		goto exhausted;

	/* 2. mask every exception, then read the timer */
	e = MON_PLAT_MASK_ALL();
	t0 = MON_PLAT_TIMER();

	for (;;) {
		/* 3. one attempt */
		if (MON_PLAT_TRYLOCK(&rpi3_mon_lock)) {
			/* 5. success: charge the wait, keep exceptions masked until rpi3_mon_release() */
			t1 = MON_PLAT_TIMER();
			w->wait_ticks_used += (t1 >= t0) ? (t1 - t0) : RPI3_MON_BUDGET_TICKS;
			*exc = e;
			return RPI3_MON_OK;
		}

		/* 4. failure: count the attempt and test the three independent bounds, fail closed */
		w->attempts_used++;
		now = MON_PLAT_TIMER();
		if (now < t0 ||					/* frozen or backward timer: exhausted */
		    w->wait_ticks_used + (now - t0) > RPI3_MON_BUDGET_TICKS ||
		    w->attempts_used >= RPI3_MON_ATTEMPT_CAP)	/* timer-independent backstop */
			break;
	}

	/* 6. exhausted after masking: the lock was never taken, so restore exceptions first */
	MON_PLAT_UNMASK(e);

exhausted:
	/* 6/7. one increment per exhausted command, lock-free */
	MON_PLAT_INC32(&rpi3_mon_pta_lock_timeouts);
	return RPI3_MON_BUSY;
}

/* Unlock first, then restore exceptions: OP-TEE asserts that IRQ is never unmasked while a spinlock is held. */
void rpi3_mon_release(uint32_t exc)
{
	MON_PLAT_UNLOCK_RESTORE(&rpi3_mon_lock, exc);
}

/* ---------------------------------------------------------------- health pair (design 4k K1) */

void rpi3_mon_health_zero(struct rpi3_mon_health_out *h)
{
	h->p2a = 0;
	h->p2b = 0;
	h->p3a = 0;
	h->p3b = 0;
}

/* Lock-free and constant data only, so it can be filled on a BUSY reply. last_poll_ticks is loaded FIRST and the
 * saturating age uses this caller's own clock read (rule O-1). */
void rpi3_mon_health_fill(struct rpi3_mon_health_out *h)
{
	uint64_t lp = rpi3_mon_load_u64(&rpi3_mon.health.last_poll_ticks);
	uint64_t now = MON_PLAT_TIMER();
	uint32_t sk = rpi3_mon_load_u32(&rpi3_mon.health.skip_count);
	uint32_t st = rpi3_mon_load_u32(&rpi3_mon.health.starved);
	uint64_t age = 0;
	uint32_t b = 0;

	if (lp == 0) {
		age = RPI3_MON_HP_AGE_NEVER;
	} else if (now >= lp) {
		age = (now - lp) / RPI3_MON_TICKS_PER_MS;
		if (age > RPI3_MON_HP_AGE_NEVER)
			age = RPI3_MON_HP_AGE_NEVER;
	}

	if (st)
		b |= RPI3_MON_HP_STARVED;
	if (rpi3_mon_init_ok())
		b |= RPI3_MON_HP_INIT_OK;
	b |= (sk > 255U ? 255U : sk) << RPI3_MON_HP_SKIP_SHIFT;

	h->p2a = (uint32_t)age;
	h->p2b = b;
	h->p3a = rpi3_mon_load_u32(&rpi3_mon_pta_lock_timeouts);
	h->p3b = rpi3_mon_load_u32(&rpi3_mon.health.skip_total);
}

/* ---------------------------------------------------------------- critical-section helpers (lock held, no hook, no call) */

/* G8 and G8-6: first four events of an evidence class are pinned and never limited; every other event is limited
 * to one per second PER CLASS; the exact counter is always incremented; an overwrite of an unread entry is counted. */
RPI3_MON_CRIT void rpi3_mon_ev_append(struct rpi3_mon_lstate *l, unsigned int type, uint32_t counter, uint32_t detail,
				      uint64_t now)
{
	struct rpi3_mon_event e;
	uint32_t i;

	l->ev_count[type]++;
	if (l->ev_seq == 0xFFFFFFFFU)			/* seq space exhausted: counters keep counting, no more records */
		return;
	e.seq = 0;
	e.type = (uint16_t)type;
	e.flags = 0;
	e.ticks = now;
	e.counter = counter;
	e.detail = detail;

	if (type < RPI3_MON_EV_N_EVIDENCE && l->pinned_n[type] < RPI3_MON_PINNED_PER_CLASS) {
		e.seq = ++l->ev_seq;
		l->pinned[type * RPI3_MON_PINNED_PER_CLASS + l->pinned_n[type]] = e;
		l->pinned_n[type]++;
		l->pinned_used++;
		return;
	}

	if (l->diag_have[type] && now >= l->diag_last_ticks[type] &&
	    now - l->diag_last_ticks[type] < RPI3_MON_TICKS_PER_SEC)
		return;					/* suppressed: counted, not recorded */
	l->diag_have[type] = 1;
	l->diag_last_ticks[type] = now;

	e.seq = ++l->ev_seq;
	i = l->ordinary_head % RPI3_MON_ORDINARY_SLOTS;
	if (l->ordinary_used[i]) {
		l->ev_lost++;				/* an unread entry is overwritten: always counted */
		e.flags |= 1;
	}
	l->ordinary[i] = e;
	l->ordinary_used[i] = 1;
	l->ordinary_head++;
}

/* Traffic window refreshed by an accepted, gap or resync frame; clears a raised timeout exactly once. */
RPI3_MON_CRIT void rpi3_mon_traffic_refresh(struct rpi3_mon_lstate *l, uint64_t now)
{
	l->tr_armed = true;
	l->tr_last_valid_ticks = now;
	if (l->tr_timeout_raised) {
		l->tr_timeout_raised = false;
		rpi3_mon_ev_append(l, RPI3_MON_EV_TRAFFIC_RECOVERED, 0, 1, now);
	}
}

/* Exactly-once evaluation of the lease, traffic deadline and heartbeat (design 4d S4, 4c G5 and G6). */
RPI3_MON_CRIT void rpi3_mon_evaluate(struct rpi3_mon_lstate *l, uint64_t now)
{
	bool lease_active;

	if (l->idle_until_ticks && now > l->idle_until_ticks) {	/* the lease expires by itself and the window restarts */
		l->idle_until_ticks = 0;
		l->tr_last_valid_ticks = now;
		rpi3_mon_ev_append(l, RPI3_MON_EV_MODE_CHANGED, 0, 0, now);
	}
	lease_active = l->idle_until_ticks && now <= l->idle_until_ticks;

	if (l->tr_armed && !lease_active && !l->tr_timeout_raised &&
	    rpi3_mon_expired(now, l->tr_last_valid_ticks, RPI3_MON_T_TRAFFIC_TICKS)) {
		l->tr_timeout_raised = true;
		rpi3_mon_ev_append(l, RPI3_MON_EV_TIMEOUT, 0, 1, now);
	}

	if (l->hb_state == RPI3_MON_HB_OPEN && rpi3_mon_expired(now, l->hb_issue_ticks, RPI3_MON_D_REQ_TICKS)) {
		l->hb_state = RPI3_MON_HB_MISSED;		/* one miss, counted once */
		l->hb_miss_count++;
	}
	/* k misses, or never answered at all, after the boot grace G: raised once, cleared by an on-time answer */
	if (!l->hb_timeout_raised && now >= rpi3_mon.init_ticks && now - rpi3_mon.init_ticks > RPI3_MON_G_TICKS &&
	    (l->hb_miss_count >= RPI3_MON_K_MISS || l->hb_last_ok_ticks == 0)) {
		l->hb_timeout_raised = true;
		rpi3_mon_ev_append(l, RPI3_MON_EV_TIMEOUT, 0, l->hb_last_ok_ticks == 0 ? 3 : 2, now);
	}
}

/* Core 1 only: issue a challenge when one is due. Never a catch-up burst: the next one is H after NOW. */
RPI3_MON_CRIT void rpi3_mon_hb_issue_if_due(struct rpi3_mon_lstate *l, uint64_t now)
{
	if (now < l->hb_next_issue)
		return;
	if (l->hb_seq == 0xFFFFFFFFU) {			/* never wraps (G6-5) */
		if (l->ev_count[RPI3_MON_EV_HB_SEQ_EXHAUSTED] == 0)
			rpi3_mon_ev_append(l, RPI3_MON_EV_HB_SEQ_EXHAUSTED, 0, 0, now);
		return;
	}
	l->hb_seq++;
	l->hb_issue_ticks = now;
	l->hb_state = RPI3_MON_HB_OPEN;
	l->hb_nonce_valid = false;			/* the previous nonce is invalid (G6-4) */
	l->hb_next_issue = now + RPI3_MON_H_TICKS;
}

RPI3_MON_CRIT bool rpi3_mon_frame_equal(const uint8_t *a, const uint8_t *b)
{
	uint32_t i;

	for (i = 0; i < RPI3_MON_FRAME_LEN; i++)
		if (a[i] != b[i])
			return false;
	return true;
}

RPI3_MON_CRIT void rpi3_mon_frame_store(uint8_t *dst, const uint8_t *src)
{
	uint32_t i;

	volatile uint8_t *vd = dst;	/* volatile: GCC may not turn this loop into a memcpy call inside the hold (design CS3, check H1) */

	for (i = 0; i < RPI3_MON_FRAME_LEN; i++)
		vd[i] = src[i];
}

/* Counter rules of command 17, in the approved order, direct unsigned comparisons, no delta variable. */
RPI3_MON_CRIT void rpi3_mon_frame_apply(struct rpi3_mon_lstate *l, const uint8_t *buf, uint32_t counter, uint64_t now,
					uint32_t *vd, uint32_t *det)
{
	struct rpi3_mon_ctr *c = &l->ctr_f;
	bool accept = false;

	*det = 0;
	if (!c->have_last) {
		*vd = RPI3_MON_V17_RESYNC;
		accept = true;
	} else if (counter < c->last_counter) {
		*vd = RPI3_MON_V17_REPLAY;
	} else if (counter == c->last_counter) {
		*vd = rpi3_mon_frame_equal(buf, c->last_frame44) ? RPI3_MON_V17_DUPLICATE : RPI3_MON_V17_CONFLICT;
	} else if (c->last_counter != 0xFFFFFFFFU && counter == c->last_counter + 1U) {
		*vd = RPI3_MON_V17_ACCEPTED;
		accept = true;
	} else {
		*vd = RPI3_MON_V17_GAP;			/* counter > last + 1 */
		*det = counter - c->last_counter - 1U;
		accept = true;
	}
	if (accept) {
		c->have_last = true;
		c->last_counter = counter;
		rpi3_mon_frame_store(c->last_frame44, buf);
		c->last_accept_ticks = now;
		rpi3_mon_traffic_refresh(l, now);
	}
}

/* Verdict to event (design K10) and the group verdict record. */
RPI3_MON_CRIT void rpi3_mon_frame_record(struct rpi3_mon_lstate *l, uint32_t vd, uint32_t det, uint32_t counter,
					 uint64_t now)
{
	l->verdict_frame.last_type = (uint8_t)vd;
	l->verdict_frame.last_ticks = now;
	l->verdict_frame.total++;
	switch (vd) {
	case RPI3_MON_V17_GAP:
		rpi3_mon_ev_append(l, RPI3_MON_EV_COUNTER_GAP, counter, det, now);
		break;
	case RPI3_MON_V17_RESYNC:
		rpi3_mon_ev_append(l, RPI3_MON_EV_COUNTER_RESYNC, counter, 0, now);
		break;
	case RPI3_MON_V17_MALFORMED:
		rpi3_mon_ev_append(l, RPI3_MON_EV_MALFORMED, counter, det, now);
		break;
	case RPI3_MON_V17_AUTH_FAIL:
		rpi3_mon_ev_append(l, RPI3_MON_EV_AUTH_FAIL, counter, 0, now);
		break;
	case RPI3_MON_V17_REPLAY:
		rpi3_mon_ev_append(l, RPI3_MON_EV_REPLAY, counter, 1, now);
		break;
	case RPI3_MON_V17_DUPLICATE:
		rpi3_mon_ev_append(l, RPI3_MON_EV_REPLAY, counter, 2, now);
		break;
	case RPI3_MON_V17_CONFLICT:
		rpi3_mon_ev_append(l, RPI3_MON_EV_REPLAY, counter, 3, now);
		break;
	default:
		break;
	}
}

/* Command 20 page 0, locked part, written with explicit stores (no memset, no memcpy, no 128-byte struct copy). The
 * words go out through a volatile pointer so the compiler cannot turn the loops into library calls. */
/* word-wise view of a local projection struct; may_alias so the copy loop is legal C */
typedef uint64_t __attribute__((__may_alias__)) mon_u64_alias;

RPI3_MON_CRIT void rpi3_mon_build_proj0(volatile uint64_t *w, const struct rpi3_mon_lstate *l, uint64_t now,
					uint32_t boot_id, uint8_t profile)
{
	struct rpi3_mon_proj0 p;
	mon_u64_alias *q = (mon_u64_alias *)(void *)&p;
	uint32_t i;
	bool lease = l->idle_until_ticks && now <= l->idle_until_ticks;

	for (i = 0; i < sizeof(p) / 8; i++)
		w[i] = 0;
	p.version = 1;
	p.length = 128;
	p.boot_id = boot_id;
	p.flags = RPI3_MON_PFLAG_INIT_OK | (profile == RPI3_MON_PROFILE_HARDENED ? RPI3_MON_PFLAG_HARDENED : 0) |
		  (l->tr_armed ? RPI3_MON_PFLAG_TR_ARMED : 0) | (l->tr_timeout_raised ? RPI3_MON_PFLAG_TR_TIMEOUT : 0) |
		  (l->hb_timeout_raised ? RPI3_MON_PFLAG_HB_TIMEOUT : 0) | (lease ? RPI3_MON_PFLAG_LEASE : 0);
	p.now_ticks = now;
	p.core1_poll_age_ticks = 0;
	p.poll_count = 0;
	p.skip_count = 0;
	p.skip_total = 0;
	p.pta_lock_timeouts = 0;
	p.pta_timeouts_seen = l->pta_timeouts_seen;
	p.last_counter = l->ctr_f.last_counter;
	p.hb_seq = l->hb_seq;
	p.traffic_age_ticks = (l->tr_armed && now >= l->tr_last_valid_ticks) ? now - l->tr_last_valid_ticks : ~0ULL;
	p.hb_last_ok_age_ticks = (l->hb_last_ok_ticks && now >= l->hb_last_ok_ticks) ? now - l->hb_last_ok_ticks : ~0ULL;
	p.hb_miss_count = l->hb_miss_count;
	p.idle_uses = l->idle_uses;
	p.lease_remaining_ticks = lease ? l->idle_until_ticks - now : 0;
	p.ev_seq = l->ev_seq;
	p.ev_ack = l->ev_ack;
	p.ev_lost = l->ev_lost;
	p.pinned_used = l->pinned_used;
	p.v_frame = l->verdict_frame.last_type;
	p.v_hb = l->verdict_hb.last_type;
	p.v_xcheck = l->verdict_xcheck.last_type;
	p.hb_state = l->hb_state;
	p.mode = l->mode;
	p.rsv[0] = 0;
	p.rsv[1] = 0;
	p.rsv[2] = 0;
	p.rsv2 = 0;
	for (i = 0; i < sizeof(p) / 8; i++)
		w[i] = q[i];
}

RPI3_MON_CRIT void rpi3_mon_build_proj1(volatile uint64_t *w, const struct rpi3_mon_lstate *l)
{
	struct rpi3_mon_proj1 p;
	mon_u64_alias *q = (mon_u64_alias *)(void *)&p;
	uint32_t i;

	p.version = 1;
	p.length = 128;
	for (i = 0; i < RPI3_MON_EV_COUNT; i++)
		p.ev_count[i] = l->ev_count[i];
	p.v_frame_total = l->verdict_frame.total;
	p.v_hb_total = l->verdict_hb.total;
	p.v_xcheck_total = l->verdict_xcheck.total;
	p.rsv = 0;
	for (i = 0; i < sizeof(p) / 8; i++)
		w[i] = q[i];
}

/* ---------------------------------------------------------------- common prologue for the PTA command logic */

/* Take the lock for a command: fresh wait state; on exhaustion the caller returns BUSY. */
static enum rpi3_mon_rc rpi3_mon_hold(struct rpi3_mon_wait *w, uint32_t *exc)
{
	rpi3_mon_wait_init(w);
	return rpi3_mon_acquire(w, exc);
}

/* ---------------------------------------------------------------- command 17: submit frame (design K2) */

enum rpi3_mon_res rpi3_mon_cmd17(const uint8_t *msg, size_t len, uint32_t *verdict, uint32_t *detail,
				 struct rpi3_mon_health_out *h)
{
	uint8_t buf[RPI3_MON_FRAME_LEN];
	uint8_t tag[32];
	uint32_t vd = RPI3_MON_V17_ACCEPTED;
	uint32_t det = 0;
	uint32_t counter = 0;
	uint32_t can_id = 0;
	bool authentic = false;
	struct rpi3_mon_wait w;
	struct rpi3_mon_lstate *l;
	uint32_t exc = 0;
	uint64_t now;
	size_t i;

	rpi3_mon_health_zero(h);
	*verdict = 0;
	*detail = 0;
	if (msg == NULL && len > 0)
		return RPI3_MON_R_BAD_PARAMETERS;
	if (!rpi3_mon_init_ok())
		return RPI3_MON_R_BAD_STATE;

	if (len != RPI3_MON_FRAME_LEN) {
		vd = RPI3_MON_V17_MALFORMED;
		det = RPI3_MON_MAL_LENGTH;			/* the buffer is not read */
	} else {
		for (i = 0; i < RPI3_MON_FRAME_LEN; i++)	/* one copy, byte reads, before any lock (R-A8) */
			buf[i] = msg[i];
		can_id = rpi3_mon_le32(buf);
		counter = rpi3_mon_le32(buf + 4);
		if (can_id > RPI3_MON_CAN_ID_MAX) {
			vd = RPI3_MON_V17_MALFORMED;
			det = RPI3_MON_MAL_CAN_ID_RANGE;
		} else if (can_id != RPI3_MON_STREAM_CAN_ID) {
			vd = RPI3_MON_V17_MALFORMED;
			det = RPI3_MON_MAL_UNLISTED;
		} else {
			TEE_Result r;

			if (!rpi3_mon_mac_enter()) {		/* mac_pta in use: nothing has changed, resubmit */
				rpi3_mon_health_fill(h);
				return RPI3_MON_R_BUSY;
			}
			r = rpi3_mon_hmac(rpi3_mon_key_frame, buf, 12, tag);	/* outside the lock */
			rpi3_mon_mac_leave();
			if (r != TEE_SUCCESS)
				return RPI3_MON_R_BAD_STATE;	/* cannot authenticate: fail closed, change nothing */
			if (rpi3_mon_ct_equal(tag, buf + 12, 32))
				authentic = true;
			else
				vd = RPI3_MON_V17_AUTH_FAIL;
		}
	}

	if (rpi3_mon_hold(&w, &exc) != RPI3_MON_OK) {		/* BUSY comes before any state change */
		rpi3_mon_health_fill(h);
		return RPI3_MON_R_BUSY;
	}
	now = MON_PLAT_TIMER();
	l = rpi3_mon_l();
	rpi3_mon_evaluate(l, now);
	if (authentic)
		rpi3_mon_frame_apply(l, buf, counter, now, &vd, &det);
	rpi3_mon_frame_record(l, vd, det, counter, now);
	rpi3_mon_release(exc);

	*verdict = vd;
	*detail = det;
	rpi3_mon_health_fill(h);
	return RPI3_MON_R_OK;
}

/* ---------------------------------------------------------------- command 18: read events (K3) */

enum rpi3_mon_res rpi3_mon_cmd18(uint32_t from_seq, uint32_t max_entries, uint8_t *out, size_t *len, uint32_t *oa,
				 uint32_t *ob, struct rpi3_mon_health_out *h)
{
	struct rpi3_mon_event loc[5];
	struct rpi3_mon_wait w;
	struct rpi3_mon_lstate *l;
	uint32_t exc = 0;
	uint32_t n = 0;
	uint32_t total = 0;
	uint32_t i;
	uint32_t j;
	uint32_t lost;
	uint64_t now;

	rpi3_mon_health_zero(h);
	if (max_entries < 1 || max_entries > 5)
		return RPI3_MON_R_BAD_PARAMETERS;
	if (out == NULL && *len > 0)
		return RPI3_MON_R_BAD_PARAMETERS;
	if (!rpi3_mon_init_ok())
		return RPI3_MON_R_BAD_STATE;
	if (*len < sizeof(struct rpi3_mon_event) * max_entries) {
		*len = sizeof(struct rpi3_mon_event) * max_entries;
		return RPI3_MON_R_SHORT_BUFFER;
	}
	if (rpi3_mon_hold(&w, &exc) != RPI3_MON_OK) {
		*oa = 0;
		*ob = 0;
		*len = 0;
		rpi3_mon_health_fill(h);
		return RPI3_MON_R_BUSY;
	}
	now = MON_PLAT_TIMER();
	l = rpi3_mon_l();
	rpi3_mon_evaluate(l, now);
	/* one scan of at most 64 slots: keep the five smallest seq at or above from_seq, in order */
	for (i = 0; i < RPI3_MON_PINNED_SLOTS + RPI3_MON_ORDINARY_SLOTS; i++) {
		const struct rpi3_mon_event *e;
		struct rpi3_mon_event t;

		if (i < RPI3_MON_PINNED_SLOTS) {
			e = &l->pinned[i];
			if (e->seq == 0)
				continue;
		} else {
			e = &l->ordinary[i - RPI3_MON_PINNED_SLOTS];
			if (!l->ordinary_used[i - RPI3_MON_PINNED_SLOTS])
				continue;
		}
		if (e->seq < from_seq)
			continue;
		total++;
		if (n < max_entries || e->seq < loc[n - 1].seq) {
			t = *e;				/* 24-byte copy, inline */
			if (n < max_entries)
				n++;
			for (j = n - 1; j > 0 && loc[j - 1].seq > t.seq; j--)
				loc[j] = loc[j - 1];
			loc[j] = t;
		}
	}
	lost = l->ev_lost;
	rpi3_mon_release(exc);

	if (n)
		memcpy(out, loc, sizeof(struct rpi3_mon_event) * n);	/* to the caller, after the unlock */
	*len = sizeof(struct rpi3_mon_event) * n;
	*oa = n | (total > n ? 0x100U : 0U);
	*ob = lost;
	rpi3_mon_health_fill(h);
	return RPI3_MON_R_OK;
}

/* ---------------------------------------------------------------- command 19: acknowledge, a bookmark only (K4) */

enum rpi3_mon_res rpi3_mon_cmd19(uint32_t ack_upto, uint32_t b, uint32_t *oa, uint32_t *ob, struct rpi3_mon_health_out *h)
{
	struct rpi3_mon_wait w;
	struct rpi3_mon_lstate *l;
	uint32_t exc = 0;
	uint32_t eff;

	rpi3_mon_health_zero(h);
	if (b != 0)
		return RPI3_MON_R_BAD_PARAMETERS;
	if (!rpi3_mon_init_ok())
		return RPI3_MON_R_BAD_STATE;
	if (rpi3_mon_hold(&w, &exc) != RPI3_MON_OK) {
		*oa = 0;
		*ob = 0;
		rpi3_mon_health_fill(h);
		return RPI3_MON_R_BUSY;
	}
	l = rpi3_mon_l();
	rpi3_mon_evaluate(l, MON_PLAT_TIMER());
	if (ack_upto > l->ev_seq)			/* clamp */
		ack_upto = l->ev_seq;
	if (ack_upto > l->ev_ack)			/* monotonic; never touches ev_lost or the overwrite victim */
		l->ev_ack = ack_upto;
	eff = l->ev_ack;
	rpi3_mon_release(exc);

	*oa = 0;
	*ob = eff;
	rpi3_mon_health_fill(h);
	return RPI3_MON_R_OK;
}

/* ---------------------------------------------------------------- command 20: status (K5) */

enum rpi3_mon_res rpi3_mon_cmd20(uint32_t page, uint32_t b, uint8_t *out, size_t *len, uint32_t *oa, uint32_t *ob,
				 struct rpi3_mon_health_out *h)
{
	union { struct rpi3_mon_proj0 p0; struct rpi3_mon_proj1 p1; uint64_t w[16]; } loc;
	struct rpi3_mon_wait w;
	struct rpi3_mon_lstate *l;
	uint32_t exc = 0;
	uint64_t now;
	uint64_t lp;

	rpi3_mon_health_zero(h);
	if (b != 0 || page >= 2)
		return RPI3_MON_R_BAD_PARAMETERS;
	if (out == NULL && *len > 0)
		return RPI3_MON_R_BAD_PARAMETERS;
	if (!rpi3_mon_init_ok())
		return RPI3_MON_R_BAD_STATE;
	if (*len < 128) {
		*len = 128;
		return RPI3_MON_R_SHORT_BUFFER;
	}
	if (rpi3_mon_hold(&w, &exc) != RPI3_MON_OK) {
		*oa = 0;
		*ob = 0;
		*len = 0;
		rpi3_mon_health_fill(h);
		return RPI3_MON_R_BUSY;
	}
	now = MON_PLAT_TIMER();
	l = rpi3_mon_l();
	rpi3_mon_evaluate(l, now);
	if (page == 0)
		rpi3_mon_build_proj0((volatile uint64_t *)loc.w, l, now, rpi3_mon.boot_id, rpi3_mon.profile);
	else
		rpi3_mon_build_proj1((volatile uint64_t *)loc.w, l);
	rpi3_mon_release(exc);

	if (page == 0) {				/* the six lock-free fields, read only after the unlock */
		lp = rpi3_mon_load_u64(&rpi3_mon.health.last_poll_ticks);
		now = MON_PLAT_TIMER();
		loc.p0.core1_poll_age_ticks = (lp == 0) ? ~0ULL : (now >= lp ? now - lp : 0);
		loc.p0.poll_count = rpi3_mon_load_u64(&rpi3_mon.health.poll_count);
		loc.p0.skip_count = rpi3_mon_load_u32(&rpi3_mon.health.skip_count);
		loc.p0.skip_total = rpi3_mon_load_u32(&rpi3_mon.health.skip_total);
		loc.p0.pta_lock_timeouts = rpi3_mon_load_u32(&rpi3_mon_pta_lock_timeouts);
		if (rpi3_mon_load_u32(&rpi3_mon.health.starved))
			loc.p0.flags |= RPI3_MON_PFLAG_STARVED;
	}
	memcpy(out, loc.w, 128);
	*len = 128;
	*oa = 128;
	*ob = 1;
	rpi3_mon_health_fill(h);
	return RPI3_MON_R_OK;
}

/* ---------------------------------------------------------------- command 21: idle lease (K6) */

enum rpi3_mon_res rpi3_mon_cmd21(uint32_t mode, uint32_t req_ms, uint32_t *oa, uint32_t *ob, struct rpi3_mon_health_out *h)
{
	struct rpi3_mon_wait w;
	struct rpi3_mon_lstate *l;
	uint32_t exc = 0;
	uint64_t now;
	uint64_t rem;
	bool active;
	uint32_t v;

	rpi3_mon_health_zero(h);
	if (mode > 1 || (mode == 1 && req_ms == 0) || (mode == 0 && req_ms != 0))
		return RPI3_MON_R_BAD_PARAMETERS;
	if (!rpi3_mon_init_ok())
		return RPI3_MON_R_BAD_STATE;
	if (rpi3_mon_hold(&w, &exc) != RPI3_MON_OK) {
		*oa = 0;
		*ob = 0;
		rpi3_mon_health_fill(h);
		return RPI3_MON_R_BUSY;
	}
	now = MON_PLAT_TIMER();
	l = rpi3_mon_l();
	rpi3_mon_evaluate(l, now);			/* a just-expired lease is already EXPECTED */
	active = l->idle_until_ticks && now <= l->idle_until_ticks;
	if (mode == 1) {
		if (l->idle_uses >= RPI3_MON_IDLE_USES_MAX) {
			v = RPI3_MON_V21_REFUSED_CAP;
			rpi3_mon_ev_append(l, RPI3_MON_EV_LEASE_REFUSED, 0, 0, now);
		} else {
			if (req_ms > RPI3_MON_IDLE_MAX_MS)
				req_ms = RPI3_MON_IDLE_MAX_MS;
			l->idle_until_ticks = now + (uint64_t)req_ms * RPI3_MON_TICKS_PER_MS;	/* replaces, never additive */
			l->idle_uses++;
			if (active) {
				v = RPI3_MON_V21_RENEWED;
				rpi3_mon_ev_append(l, RPI3_MON_EV_LEASE_RENEWED, 0, req_ms, now);
			} else {
				v = RPI3_MON_V21_GRANTED;
				rpi3_mon_ev_append(l, RPI3_MON_EV_LEASE_GRANTED, 0, req_ms, now);
				rpi3_mon_ev_append(l, RPI3_MON_EV_MODE_CHANGED, 0, 1, now);
			}
		}
	} else if (active) {
		l->idle_until_ticks = 0;
		l->tr_last_valid_ticks = now;		/* the traffic window restarts (G5-4) */
		v = RPI3_MON_V21_RETURNED;
		rpi3_mon_ev_append(l, RPI3_MON_EV_MODE_CHANGED, 0, 0, now);
	} else {
		v = RPI3_MON_V21_NO_CHANGE;
	}
	rem = (l->idle_until_ticks && now <= l->idle_until_ticks) ? (l->idle_until_ticks - now) / RPI3_MON_TICKS_PER_MS : 0;
	rpi3_mon_release(exc);

	*oa = v;
	*ob = rem > 0xFFFFFFFFULL ? 0xFFFFFFFFU : (uint32_t)rem;
	rpi3_mon_health_fill(h);
	return RPI3_MON_R_OK;
}

/* ---------------------------------------------------------------- command 22: get nonce (K7) */

enum rpi3_mon_res rpi3_mon_cmd22(uint32_t *nonce_lo, uint32_t *nonce_hi, uint32_t *hb_seq, uint32_t *hb_state,
				 struct rpi3_mon_health_out *h)
{
	struct rpi3_mon_wait w;
	struct rpi3_mon_lstate *l;
	uint32_t exc = 0;
	uint8_t in[16];
	uint8_t dig[32];
	uint64_t issue;
	uint64_t nonce = 0;
	uint32_t seq;
	uint32_t st;
	bool valid;
	bool store_ok;
	TEE_Result r;

	rpi3_mon_health_zero(h);
	*nonce_lo = 0;
	*nonce_hi = 0;
	*hb_seq = 0;
	*hb_state = 0;
	if (!rpi3_mon_init_ok())
		return RPI3_MON_R_BAD_STATE;

	/* hold 1: evaluate and read the request */
	if (rpi3_mon_hold(&w, &exc) != RPI3_MON_OK) {
		rpi3_mon_health_fill(h);
		return RPI3_MON_R_BUSY;
	}
	l = rpi3_mon_l();
	rpi3_mon_evaluate(l, MON_PLAT_TIMER());
	seq = l->hb_seq;
	st = l->hb_state;
	issue = l->hb_issue_ticks;
	valid = l->hb_nonce_valid;
	nonce = l->hb_nonce;
	rpi3_mon_release(exc);

	if (st != RPI3_MON_HB_OPEN && st != RPI3_MON_HB_MISSED) {	/* not answerable: state only, zero nonce */
		nonce = 0;
		goto out;
	}
	if (!valid) {
		if (!rpi3_mon_mac_enter()) {		/* mac_pta in use: nothing changed, retry */
			rpi3_mon_health_fill(h);
			return RPI3_MON_R_BUSY;
		}
		rpi3_mon_put_le32(in, rpi3_mon.boot_id);
		rpi3_mon_put_le32(in + 4, seq);
		rpi3_mon_put_le64(in + 8, issue);
		r = rpi3_mon_hmac(rpi3_mon_key_hb, in, sizeof(in), dig);	/* outside the lock */
		rpi3_mon_mac_leave();
		if (r != TEE_SUCCESS)
			return RPI3_MON_R_BAD_STATE;
		nonce = (uint64_t)rpi3_mon_le32(dig) | ((uint64_t)rpi3_mon_le32(dig + 4) << 32);

		/* hold 2, with the remaining budget of the same command */
		if (rpi3_mon_acquire(&w, &exc) != RPI3_MON_OK) {
			rpi3_mon_health_fill(h);
			return RPI3_MON_R_BUSY;
		}
		l = rpi3_mon_l();
		store_ok = l->hb_seq == seq && (l->hb_state == RPI3_MON_HB_OPEN || l->hb_state == RPI3_MON_HB_MISSED);
		if (store_ok) {
			l->hb_nonce = nonce;
			l->hb_nonce_valid = true;
		}
		seq = l->hb_seq;			/* report the CURRENT request */
		st = l->hb_state;
		rpi3_mon_release(exc);
		if (!store_ok)				/* a new challenge arrived meanwhile: zero nonce, the caller retries */
			nonce = 0;
	}
out:
	*nonce_lo = (uint32_t)nonce;
	*nonce_hi = (uint32_t)(nonce >> 32);
	*hb_seq = seq;
	*hb_state = st;
	rpi3_mon_health_fill(h);
	return RPI3_MON_R_OK;
}

/* ---------------------------------------------------------------- command 23: heartbeat answer (K8) */

enum rpi3_mon_res rpi3_mon_cmd23(uint32_t seq, uint32_t b, uint32_t nonce_lo, uint32_t nonce_hi, uint32_t *oa,
				 uint32_t *ob, struct rpi3_mon_health_out *h)
{
	struct rpi3_mon_wait w;
	struct rpi3_mon_lstate *l;
	uint32_t exc = 0;
	uint64_t now;
	uint64_t given = ((uint64_t)nonce_hi << 32) | nonce_lo;
	uint32_t v;
	uint32_t detail = 0;

	rpi3_mon_health_zero(h);
	if (b != 0)
		return RPI3_MON_R_BAD_PARAMETERS;
	if (!rpi3_mon_init_ok())
		return RPI3_MON_R_BAD_STATE;
	if (rpi3_mon_hold(&w, &exc) != RPI3_MON_OK) {
		*oa = 0;
		*ob = 0;
		rpi3_mon_health_fill(h);
		return RPI3_MON_R_BUSY;
	}
	now = MON_PLAT_TIMER();
	l = rpi3_mon_l();
	rpi3_mon_evaluate(l, now);			/* before the comparison: the deadline boundary uses this clock */
	if (l->hb_state == RPI3_MON_HB_NONE || seq > l->hb_seq) {
		v = RPI3_MON_V23_UNISSUED;
		rpi3_mon_ev_append(l, RPI3_MON_EV_UNISSUED_ANSWER, seq, 0, now);
	} else if (seq < l->hb_seq) {
		v = RPI3_MON_V23_STALE;
		rpi3_mon_ev_append(l, RPI3_MON_EV_STALE_ANSWER, seq, 0, now);
	} else if (l->hb_state == RPI3_MON_HB_ANSWERED || l->hb_state == RPI3_MON_HB_LATE_ANSWERED) {
		v = RPI3_MON_V23_DUPLICATE;
		rpi3_mon_ev_append(l, RPI3_MON_EV_DUP_ANSWER, seq, 0, now);
	} else if (!l->hb_nonce_valid || l->hb_nonce != given) {
		v = RPI3_MON_V23_BAD_NONCE;
		detail = l->hb_nonce_valid ? 2U : 1U;		/* 1 never fetched, 2 different */
		rpi3_mon_ev_append(l, RPI3_MON_EV_BAD_NONCE, seq, detail, now);
	} else if (l->hb_state == RPI3_MON_HB_OPEN) {
		v = RPI3_MON_V23_ON_TIME;
		l->hb_state = RPI3_MON_HB_ANSWERED;
		l->hb_last_ok_ticks = now;
		l->hb_miss_count = 0;
		if (l->hb_timeout_raised) {
			l->hb_timeout_raised = false;
			rpi3_mon_ev_append(l, RPI3_MON_EV_HB_RECOVERED, seq, 0, now);
		}
	} else {
		v = RPI3_MON_V23_LATE;				/* MISSED: recorded, repairs nothing (G6-3) */
		l->hb_state = RPI3_MON_HB_LATE_ANSWERED;
		rpi3_mon_ev_append(l, RPI3_MON_EV_LATE_ANSWER, seq, 0, now);
	}
	l->verdict_hb.last_type = (uint8_t)v;
	l->verdict_hb.last_ticks = now;
	l->verdict_hb.total++;
	rpi3_mon_release(exc);

	*oa = v;
	*ob = detail;
	rpi3_mon_health_fill(h);
	return RPI3_MON_R_OK;
}

/* ---------------------------------------------------------------- legacy-command event (K9): best effort, never waits */

void rpi3_mon_note_legacy(uint32_t cmd_id, bool refused)
{
	uint32_t e;
	struct rpi3_mon_lstate *l;

	if (!rpi3_mon_init_ok())
		return;
	e = MON_PLAT_MASK_ALL();
	if (MON_PLAT_TRYLOCK(&rpi3_mon_lock)) {			/* ONE attempt; if the lock is busy the event is dropped */
		l = rpi3_mon_l();
		rpi3_mon_ev_append(l, refused ? RPI3_MON_EV_LEGACY_REFUSED : RPI3_MON_EV_LEGACY_USED, 0, cmd_id,
				   MON_PLAT_TIMER());
		MON_PLAT_UNLOCK_RESTORE(&rpi3_mon_lock, e);
	} else {
		MON_PLAT_UNMASK(e);
	}
}

/* ---------------------------------------------------------------- Core 1 (design 4g, D1 = Option B) */

void rpi3_mon_core1_entry_check(void)
{
	if (!MON_PLAT_EXC_FIQ_IRQ_MASKED())
		MON_PLAT_PANIC("rpi3_mon: the Core 1 monitor loop was entered with FIQ or IRQ unmasked");
}

/*
 * One lock attempt per 256 passes. Never spins, never waits. On failure only G7 changes (the skip counters and the
 * starvation flag). On success: evaluate, issue a due challenge, observe pta_lock_timeouts, close a starvation
 * episode, unlock. Core 1 never calls HMAC and never touches the heap here (F27).
 */
void rpi3_mon_core1_attempt(void)
{
	struct rpi3_mon_lstate *l;
	uint64_t now;
	uint32_t count;
	uint32_t delta;
	uint32_t sk;

	if (!MON_PLAT_TRYLOCK(&rpi3_mon_lock)) {
		sk = rpi3_mon_load_u32(&rpi3_mon.health.skip_count) + 1U;
		rpi3_mon_store_u32(&rpi3_mon.health.skip_count, sk);
		rpi3_mon_store_u32(&rpi3_mon.health.skip_total, rpi3_mon_load_u32(&rpi3_mon.health.skip_total) + 1U);
		if (sk >= RPI3_MON_K_STARVE)
			rpi3_mon_store_u32(&rpi3_mon.health.starved, 1U);
		return;
	}
	now = MON_PLAT_TIMER();
	l = rpi3_mon_l();
	rpi3_mon_evaluate(l, now);
	rpi3_mon_hb_issue_if_due(l, now);

	count = rpi3_mon_load_u32(&rpi3_mon_pta_lock_timeouts);		/* S2: read under the lock, then compare */
	delta = count - l->pta_timeouts_seen;
	if (delta) {
		l->pta_timeouts_seen = count;
		l->ev_count[RPI3_MON_EV_LOCK_TIMEOUT] += delta - 1U;	/* the append below adds one: the exact total is delta */
		rpi3_mon_ev_append(l, RPI3_MON_EV_LOCK_TIMEOUT, 0, delta, now);
	}
	sk = rpi3_mon_load_u32(&rpi3_mon.health.skip_count);
	if (sk >= RPI3_MON_K_STARVE)					/* one MON_STARVED event per episode that reached K */
		rpi3_mon_ev_append(l, RPI3_MON_EV_MON_STARVED, 0, sk, now);
	rpi3_mon_store_u32(&rpi3_mon.health.skip_count, 0U);		/* skip_total is cumulative and never reset */
	rpi3_mon_store_u32(&rpi3_mon.health.starved, 0U);
	l->last_eval_ticks = now;
	MON_PLAT_UNLOCK(&rpi3_mon_lock);
}
