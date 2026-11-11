// SPDX-License-Identifier: BSD-2-Clause
/*
 * Secure CAN Monitor: shared state, synchronisation and health (Component 2.2, stage 1).
 *
 * Authority: Part_E_Verification/monitor/MONITOR_STATE_DESIGN.md sections 4a to 4k.
 * Stage 1 contains: lock initialisation and init failure handling, the init_ok publication, bounded PTA lock
 * acquisition and release, and the health pair. Nothing else. In particular this file does NOT call HMAC,
 * allocate or free memory, print (apart from the one EMSG when init fails), or touch the Core 1 loop.
 *
 * Rules honoured here (design): one lock, no nesting (G9 L8); no cpu_spin_lock and no expired() in the acquire
 * loop (X8); fail closed on a frozen or backward timer (4f B3/B4); the lock is never force-released (4e I5).
 */
#include <rpi3_can_monitor.h>

struct rpi3_mon_state rpi3_mon;		/* zero-initialised in .bss; rpi3_mon_init() makes the state explicit */

bool rpi3_mon_init_ok(void)
{
	return MON_PLAT_LOAD_ACQUIRE32(&rpi3_mon.init_ok) != 0;
}

/*
 * Runs on the primary core from driver_init(), before the Normal World bootloader and therefore before Linux
 * and Core 1 exist (design 4e I1 to I3). It never fails the OS boot: if a step fails the monitor simply stays
 * unavailable (init_ok stays 0, so commands 17 to 23 return BAD_STATE) and legacy commands keep working.
 * init_ok is published LAST, with a release store.
 */
TEE_Result rpi3_mon_init(void)
{
	TEE_Result res = TEE_SUCCESS;

	memset(&rpi3_mon, 0, sizeof(rpi3_mon));
	rpi3_mon_lock = SPINLOCK_UNLOCK;		/* explicit, in addition to the .bss zero */

	rpi3_mon.profile = MON_PLAT_PROFILE_DEBUG ? RPI3_MON_PROFILE_DEBUG : RPI3_MON_PROFILE_HARDENED;
	rpi3_mon.init_ticks = MON_PLAT_TIMER();
	rpi3_mon.l.mode = RPI3_MON_MODE_F;		/* Mode F; nothing in Component 2 writes it again (S5) */

	/* Later stages add their steps here (mac contexts, boot_id), always before the publication below. */
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

void rpi3_mon_wait_init(struct rpi3_mon_wait *w)
{
	w->wait_ticks_used = 0;
	w->attempts_used = 0;
}

/*
 * Bounded lock acquisition, exactly the sequence of design section 4f B3. The wait state is shared by every
 * acquisition in one PTA command: the budget counts time spent WAITING for the lock, not time since the command
 * started.
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

void rpi3_mon_health_zero(struct rpi3_mon_health_out *h)
{
	h->p2a = 0;
	h->p2b = 0;
	h->p3a = 0;
	h->p3b = 0;
}

/*
 * The health pair: lock-free and constant data only, so it can be filled on a BUSY reply. last_poll_ticks is
 * loaded FIRST and the saturating age uses this caller's own clock read (rule O-1).
 */
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
