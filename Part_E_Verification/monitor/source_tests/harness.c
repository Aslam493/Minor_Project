/* Host harness: compiles the REAL rpi3_can_monitor.c (included below, so the tests can reach its internal critical-section
 * helpers) and links the REAL PTA glue core/pta/rpi3_can_monitor_pta.c, with a mock platform. NOT a board test. */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <time.h>
#include <rpi3_can_monitor.c>
#include "vectors.h"

static uint32_t hook_init_ok_seen = 99; static unsigned hook_lock_seen = 99, hook_profile_seen = 99, hook_calls;
TEE_Result mock_init_extra(void)
{
	hook_calls++; hook_init_ok_seen = rpi3_mon.init_ok; hook_lock_seen = rpi3_mon_lock; hook_profile_seen = rpi3_mon.profile;
	return mock_init_extra_result;
}

static int nchk, nfail, tchk, tfail; static const char *cur;
#define CHECK(c) do { nchk++; tchk++; if (!(c)) { nfail++; tfail++; printf("  FAIL line %d: %s\n", __LINE__, #c); } } while (0)
static void begin(const char *n) { cur = n; tchk = tfail = 0; }
static void end(void) { printf("%-4s %-70s %3d checks\n", tfail ? "FAIL" : "ok", cur, tchk); }

static const uint8_t KF[32] = RPI3_MON_KEY_FRAME_INIT, KH[32] = RPI3_MON_KEY_HB_INIT;
static void setup(void)
{
	memset(&M, 0, sizeof M); M.now = 1000000000ull; M.step = 60;
	mock_init_extra_result = TEE_SUCCESS; mock_profile_debug = 0; mock_emsg_count = 0; hook_calls = 0;
	mock_fail_mac_alloc = mock_fail_mac_init = mock_fail_mac_final = mock_fail_rng = 0; mock_hmac_calls = 0; mock_mac_final_hook = NULL;
	memset(&rpi3_mon, 0, sizeof rpi3_mon); (void)rpi3_mon_init();
	M.timer_reads = 0; M.mask_calls = M.unmask_calls = 0; M.violations = 0; M.ntrace = 0; mock_hmac_calls = 0;
}
static void frozen(uint64_t t) { M.tmode = 1; M.now = t; rpi3_mon.init_ticks = t; }	/* the boot grace period has not passed unless a test says so */
static uint32_t take(void) { struct rpi3_mon_wait w; uint32_t e = 0; rpi3_mon_wait_init(&w); if (rpi3_mon_acquire(&w, &e) != RPI3_MON_OK) abort(); return e; }
static void put(uint32_t e) { rpi3_mon_release(e); }
static void make_frame(uint8_t f[44], uint32_t id, uint32_t counter, uint32_t payload, bool good)
{
	rpi3_mon_put_le32(f, id); rpi3_mon_put_le32(f + 4, counter); rpi3_mon_put_le32(f + 8, payload);
	host_hmac_sha256(KF, 32, f, 12, f + 12); if (!good) f[12] ^= 1;
}
static enum rpi3_mon_res c17(const uint8_t *f, size_t len, uint32_t *v, uint32_t *d) { struct rpi3_mon_health_out h; return rpi3_mon_cmd17(f, len, v, d, &h); }

/* ------------------------------------------------------------------ crypto vectors (independent of the code under test) */
static void t_vectors(void)
{
	uint8_t o[32], in[16]; uint8_t k[20]; uint8_t head[12]; uint64_t n; int i;
	begin("VEC mock HMAC matches RFC 4231 and the independent Python vectors");
	host_hmac_sha256(RFC4231_2_KEY, 4, RFC4231_2_DATA, sizeof RFC4231_2_DATA, o); CHECK(memcmp(o, RFC4231_2_MAC, 32) == 0);
	memset(k, 0x0b, 20); host_hmac_sha256(k, 20, (const uint8_t *)"Hi There", 8, o);
	CHECK(o[0] == 0xb0 && o[1] == 0x34 && o[2] == 0x4c && o[31] == 0xf7);					/* RFC 4231 test case 1 */
	rpi3_mon_put_le32(head, 0x123); rpi3_mon_put_le32(head + 4, 100); rpi3_mon_put_le32(head + 8, 0xDDCCBBAA);
	CHECK(memcmp(head, VEC_FRAME_HEAD12, 12) == 0); host_hmac_sha256(KF, 32, head, 12, o); CHECK(memcmp(o, VEC_FRAME_TAG, 32) == 0);
	rpi3_mon_put_le32(in, 0x11223344); rpi3_mon_put_le32(in + 4, 7); rpi3_mon_put_le64(in + 8, 0x0000000123456789ull);
	CHECK(memcmp(in, VEC_NONCE_INPUT, 16) == 0); host_hmac_sha256(KH, 32, in, 16, o); CHECK(memcmp(o, VEC_NONCE, 8) == 0);
	n = (uint64_t)rpi3_mon_le32(o) | ((uint64_t)rpi3_mon_le32(o + 4) << 32);
	CHECK(((uint8_t)n) == VEC_NONCE[0] && ((uint8_t)(n >> 56)) == VEC_NONCE[7]);				/* the little-endian nonce word */
	for (i = 0; i < 1; i++) CHECK(KF[0] != KH[0] || KF[1] != KH[1]);						/* distinct keys */
	end();
}

/* ------------------------------------------------------------------ init (design 4e) */
static void t_init(void)
{
	uint8_t in[10]; uint8_t d[32]; struct rpi3_mon_health_out h;
	begin("INIT-1 init succeeds: explicit state, mac_pta allocated, boot_id derived, init_ok published");
	memset(&M, 0, sizeof M); M.now = 1000000000ull; M.step = 60; mock_init_extra_result = TEE_SUCCESS; mock_emsg_count = 0;
	mock_fail_mac_alloc = mock_fail_mac_init = mock_fail_mac_final = mock_fail_rng = 0;
	memset(&rpi3_mon, 0xA5, sizeof rpi3_mon);								/* garbage in .bss must not matter */
	CHECK(rpi3_mon_init() == TEE_SUCCESS && rpi3_mon_init_ok() && rpi3_mon.init_ok == 1);
	CHECK(rpi3_mon_lock == SPINLOCK_UNLOCK && rpi3_mon.profile == RPI3_MON_PROFILE_HARDENED && rpi3_mon.l.mode == RPI3_MON_MODE_F);
	CHECK(rpi3_mon.mac_pta != NULL && rpi3_mon.mac_core1 == NULL && rpi3_mon.mac_pta_busy == 0);
	CHECK(rpi3_mon.boot_id == VEC_BOOTID);									/* DEC-IF-15, against the independent vector */
	rpi3_mon_put_le64(in, 1000000060ull); in[8] = 0x5A; in[9] = 0xA5; host_hmac_sha256(KH, 32, in, 10, d);
	CHECK(rpi3_mon.boot_id == rpi3_mon_le32(d) && rpi3_mon.init_ticks == 1000000060ull);
	CHECK(mock_emsg_count == 0 && rpi3_mon.l.ev_seq == 0 && rpi3_mon.health.poll_count == 0 && rpi3_mon_pta_lock_timeouts == 0);
	end();
	begin("INIT-2 debug profile is a build-time choice"); setup(); mock_profile_debug = 1; (void)rpi3_mon_init();
	CHECK(rpi3_mon.profile == RPI3_MON_PROFILE_DEBUG); mock_profile_debug = 0; (void)rpi3_mon_init(); CHECK(rpi3_mon.profile == RPI3_MON_PROFILE_HARDENED); end();
	begin("INIT-3 every init failure keeps the boot going and the monitor unavailable (fail closed)");
	{ int i; for (i = 0; i < 5; i++) { setup(); if (i == 0) mock_init_extra_result = TEE_ERROR_GENERIC; if (i == 1) mock_fail_mac_alloc = 1;
		if (i == 2) mock_fail_rng = 1; if (i == 3) mock_fail_mac_init = 1; if (i == 4) mock_fail_mac_final = 1;
		CHECK(rpi3_mon_init() == TEE_SUCCESS && !rpi3_mon_init_ok() && rpi3_mon.init_ok == 0 && mock_emsg_count == 1); } }
	rpi3_mon_health_fill(&h); CHECK((h.p2b & RPI3_MON_HP_INIT_OK) == 0); end();
	begin("INIT-4 init_ok is published LAST, after every other step");
	setup(); hook_init_ok_seen = hook_lock_seen = hook_profile_seen = 99; hook_calls = 0; rpi3_mon_lock = 7; rpi3_mon.init_ok = 1; (void)rpi3_mon_init();
	CHECK(hook_calls == 1 && hook_init_ok_seen == 0 && hook_lock_seen == 0 && hook_profile_seen == RPI3_MON_PROFILE_HARDENED && rpi3_mon.boot_id != 0 && rpi3_mon.init_ok == 1);
	end();
	begin("INIT-5 a left-locked state is reset, and init can succeed after a failure");
	setup(); rpi3_mon_lock = 1; mock_init_extra_result = TEE_ERROR_GENERIC; (void)rpi3_mon_init(); CHECK(rpi3_mon_lock == 0 && !rpi3_mon_init_ok());
	mock_init_extra_result = TEE_SUCCESS; (void)rpi3_mon_init(); CHECK(rpi3_mon_init_ok()); end();
}
static void t_layout(void)
{
	begin("V1 runtime layout: lock, G7 and timeout counter on separate 64-byte lines");
	CHECK(((uintptr_t)&rpi3_mon_lock % 64) == 0 && ((uintptr_t)&rpi3_mon.health % 64) == 0 && ((uintptr_t)&rpi3_mon_pta_lock_timeouts % 64) == 0);
	CHECK((uintptr_t)&rpi3_mon.health / 64 != (uintptr_t)&rpi3_mon_pta_lock_timeouts / 64 && (uintptr_t)&rpi3_mon.l / 64 != (uintptr_t)&rpi3_mon_lock / 64);
	CHECK((uintptr_t)&rpi3_mon.health.poll_count % 8 == 0 && (uintptr_t)&rpi3_mon.health.last_poll_ticks % 8 == 0);
	end();
}

/* ------------------------------------------------------------------ bounded acquisition (design 4f) */
static void t_locks(void)
{
	struct rpi3_mon_wait w; uint32_t e, a1, full; uint64_t t0; struct rpi3_mon_lstate before; struct rpi3_mon_health hb; int i;
	begin("X1/V7 free lock: acquired on the first attempt, no timeout increment"); setup(); rpi3_mon_wait_init(&w);
	CHECK(rpi3_mon_acquire(&w, &e) == RPI3_MON_OK && M.trylock_calls == 1 && rpi3_mon_pta_lock_timeouts == 0 && M.held && M.daif == MON_DAIF_ALL && rpi3_mon_lock == 1);
	rpi3_mon_release(e); CHECK(!M.held && M.daif == 0 && rpi3_mon_lock == 0 && M.violations == 0 && M.ntrace == 2 && M.trace[0] == 'U' && M.trace[1] == 'M'); end();
	begin("X2/X6/V6 held lock: BUSY within the budget, state restored, one increment"); setup(); rpi3_mon_wait_init(&w); M.ext_hold = 1; t0 = M.now;
	CHECK(rpi3_mon_acquire(&w, &e) == RPI3_MON_BUSY && M.now - t0 <= RPI3_MON_BUDGET_TICKS + 3 * M.step && w.attempts_used > 100 && w.attempts_used < RPI3_MON_ATTEMPT_CAP);
	CHECK(rpi3_mon_pta_lock_timeouts == 1 && M.daif == 0 && M.locked_count == 0 && !M.held && M.violations == 0 && M.mask_calls == 1 && M.unmask_calls == 1); end();
	begin("X3/X4 frozen timer exits by the cap; backward timer exits at once"); setup(); rpi3_mon_wait_init(&w); M.tmode = 1; M.ext_hold = 1;
	CHECK(rpi3_mon_acquire(&w, &e) == RPI3_MON_BUSY && w.attempts_used == RPI3_MON_ATTEMPT_CAP && rpi3_mon_pta_lock_timeouts == 1);
	setup(); rpi3_mon_wait_init(&w); M.tmode = 2; M.ext_hold = 1; CHECK(rpi3_mon_acquire(&w, &e) == RPI3_MON_BUSY && w.attempts_used == 1 && M.daif == 0 && M.violations == 0); end();
	begin("X5 two acquisitions share one budget; time between them is not counted"); setup(); rpi3_mon_wait_init(&w); M.ext_hold = 1; M.ext_release_after = 100;
	CHECK(rpi3_mon_acquire(&w, &e) == RPI3_MON_OK); a1 = w.attempts_used; rpi3_mon_release(e); CHECK(w.wait_ticks_used > 0 && w.wait_ticks_used < RPI3_MON_BUDGET_TICKS);
	M.now += 50000000ull; M.ext_hold = 1; M.ext_release_after = 0; CHECK(rpi3_mon_acquire(&w, &e) == RPI3_MON_BUSY); full = (uint32_t)(RPI3_MON_BUDGET_TICKS / M.step);
	CHECK(w.attempts_used - a1 < full - 50 && w.wait_ticks_used <= RPI3_MON_BUDGET_TICKS + 3 * M.step && rpi3_mon_pta_lock_timeouts == 1);
	setup(); rpi3_mon_wait_init(&w); w.wait_ticks_used = RPI3_MON_BUDGET_TICKS; CHECK(rpi3_mon_acquire(&w, &e) == RPI3_MON_BUSY && M.mask_calls == 0 && M.trylock_calls == 0 && rpi3_mon_pta_lock_timeouts == 1);
	rpi3_mon_wait_init(&w); w.attempts_used = RPI3_MON_ATTEMPT_CAP; CHECK(rpi3_mon_acquire(&w, &e) == RPI3_MON_BUSY && M.mask_calls == 0 && rpi3_mon_pta_lock_timeouts == 2); end();
	begin("X6/X4b order control and backward timer after success"); setup(); rpi3_mon_wait_init(&w);
	CHECK(rpi3_mon_acquire(&w, &e) == RPI3_MON_OK); M.violations = 0; mock_unmask(e); CHECK(M.violations == 1); mock_unlock_restore(&rpi3_mon_lock, e);
	setup(); rpi3_mon_wait_init(&w); M.tmode = 2; CHECK(rpi3_mon_acquire(&w, &e) == RPI3_MON_OK && w.wait_ticks_used == RPI3_MON_BUDGET_TICKS); rpi3_mon_release(e); M.tmode = 0;
	{ unsigned masks = M.mask_calls, tries = M.trylock_calls; CHECK(rpi3_mon_acquire(&w, &e) == RPI3_MON_BUSY && M.mask_calls == masks && M.trylock_calls == tries); } end();
	begin("X7 BUSY path touches no lock-protected or G7 state"); setup(); rpi3_mon_wait_init(&w); rpi3_mon.l.ev_seq = 0x5A5A; before = rpi3_mon.l; hb = rpi3_mon.health; M.ext_hold = 1;
	CHECK(rpi3_mon_acquire(&w, &e) == RPI3_MON_BUSY && M.violations == 0 && memcmp(&before, &rpi3_mon.l, sizeof before) == 0 && memcmp(&hb, &rpi3_mon.health, sizeof hb) == 0); end();
	begin("V9 pta_lock_timeouts wraps modulo 2^32 and the unsigned delta stays correct"); setup(); rpi3_mon_pta_lock_timeouts = 0xFFFFFFF0u; rpi3_mon.l.pta_timeouts_seen = 0xFFFFFFF0u;
	for (i = 0; i < 32; i++) MON_PLAT_INC32(&rpi3_mon_pta_lock_timeouts); CHECK(rpi3_mon_pta_lock_timeouts == 0x10u && (uint32_t)(rpi3_mon_pta_lock_timeouts - rpi3_mon.l.pta_timeouts_seen) == 0x20u); end();
	begin("Z1 lock-protected state is flagged when touched without the lock"); setup(); (void)rpi3_mon_l(); CHECK(M.violations == 1); M.violations = 0;
	rpi3_mon_wait_init(&w); CHECK(rpi3_mon_acquire(&w, &e) == RPI3_MON_OK); (void)rpi3_mon_l(); CHECK(M.violations == 0); rpi3_mon_release(e); end();
	begin("V11 mac_pta guard: second entry is refused until the first leaves"); setup(); CHECK(rpi3_mon_mac_enter() && !rpi3_mon_mac_enter()); rpi3_mon_mac_leave(); CHECK(rpi3_mon_mac_enter()); rpi3_mon_mac_leave(); end();
}
static void t_health(void)
{
	struct rpi3_mon_health_out h; uint64_t v = 0x1122334455667788ull;
	begin("HP4 health pair: values, sentinels, saturation, accessors"); setup();
	rpi3_mon_store_u64(&rpi3_mon.health.poll_count, v); CHECK(rpi3_mon_load_u64(&rpi3_mon.health.poll_count) == v);
	rpi3_mon_health_zero(&h); CHECK(h.p2a == 0 && h.p2b == 0 && h.p3a == 0 && h.p3b == 0);
	rpi3_mon.health.last_poll_ticks = 0; rpi3_mon_health_fill(&h); CHECK(h.p2a == RPI3_MON_HP_AGE_NEVER && (h.p2b & RPI3_MON_HP_INIT_OK) && !(h.p2b & RPI3_MON_HP_STARVED));
	rpi3_mon.health.last_poll_ticks = M.now - 5 * RPI3_MON_TICKS_PER_MS; rpi3_mon.health.skip_count = 2; rpi3_mon.health.skip_total = 9; rpi3_mon_pta_lock_timeouts = 4; rpi3_mon.health.starved = 1; rpi3_mon_health_fill(&h);
	CHECK(h.p2a >= 5 && h.p2a <= 6 && ((h.p2b >> 8) & 255) == 2 && (h.p2b & RPI3_MON_HP_STARVED) && h.p3a == 4 && h.p3b == 9);
	rpi3_mon.health.last_poll_ticks = 1; M.now = RPI3_MON_TICKS_PER_MS * 5000000000ull; rpi3_mon.health.skip_count = 300; rpi3_mon_health_fill(&h); CHECK(h.p2a == RPI3_MON_HP_AGE_NEVER && ((h.p2b >> 8) & 255) == 255);
	rpi3_mon.health.last_poll_ticks = M.now + 1000; rpi3_mon_health_fill(&h); CHECK(h.p2a == 0 && M.violations == 0); end();
}

/* ------------------------------------------------------------------ event ring, per-class limits, exactly-once evaluation */
static void evadd(unsigned type, uint32_t counter, uint32_t detail)	/* appends one second after the previous call */
{
	uint32_t e = take(); M.now += RPI3_MON_TICKS_PER_SEC; rpi3_mon_ev_append(rpi3_mon_l(), type, counter, detail, M.now); put(e);
}
static void t_events(void)
{
	uint32_t e; int i, ord, sum;
	begin("EV2/Q3 the first four events of each evidence class are always kept; invariants hold"); setup(); frozen(5000000000ull);
	e = take(); for (i = 0; i < 6; i++) { int k; for (k = 0; k < 4; k++) rpi3_mon_ev_append(rpi3_mon_l(), (unsigned)i, (uint32_t)k, 0, M.now); } put(e);
	for (i = 0, sum = 0; i < 6; i++) { CHECK(rpi3_mon.l.pinned_n[i] == 4); sum += rpi3_mon.l.pinned_n[i]; }
	CHECK(rpi3_mon.l.pinned_used == 24 && sum == 24 && rpi3_mon.l.ev_seq == 24 && rpi3_mon.l.ev_lost == 0);
	for (i = 0; i < 24; i++) CHECK(rpi3_mon.l.pinned[i].seq == (uint32_t)i + 1 && rpi3_mon.l.pinned[i].type == (uint16_t)(i / 4));
	end();
	begin("EV3 evidence flood: after the pinned quota AUTH_FAIL and REPLAY are limited per class, nothing else is evicted"); setup(); frozen(5000000000ull);
	for (i = 0; i < 5; i++) evadd(RPI3_MON_EV_TIMEOUT, (uint32_t)i, 0);					/* 4 pinned + 1 ordinary TIMEOUT */
	e = take(); for (i = 0; i < 1000; i++) rpi3_mon_ev_append(rpi3_mon_l(), RPI3_MON_EV_AUTH_FAIL, (uint32_t)i, 0, M.now + 10);
	for (i = 0; i < 1000; i++) rpi3_mon_ev_append(rpi3_mon_l(), RPI3_MON_EV_REPLAY, (uint32_t)i, 1, M.now + 20); put(e);
	for (i = 0, ord = 0; i < 40; i++) ord += rpi3_mon.l.ordinary_used[i];
	CHECK(rpi3_mon.l.ev_count[RPI3_MON_EV_AUTH_FAIL] == 1000 && rpi3_mon.l.ev_count[RPI3_MON_EV_REPLAY] == 1000 && rpi3_mon.l.pinned_n[RPI3_MON_EV_AUTH_FAIL] == 4);
	CHECK(ord == 3 && rpi3_mon.l.ev_lost == 0 && rpi3_mon.l.ordinary[0].type == RPI3_MON_EV_TIMEOUT); end();
	begin("Q5 per-class diagnostic limit: one per second per class; classes never suppress each other"); setup(); frozen(5000000000ull);
	e = take(); { uint64_t t = M.now; struct rpi3_mon_lstate *l = rpi3_mon_l();
	rpi3_mon_ev_append(l, RPI3_MON_EV_LOCK_TIMEOUT, 0, 1, t); rpi3_mon_ev_append(l, RPI3_MON_EV_MON_STARVED, 0, 2, t + 10);
	CHECK(l->ev_seq == 2 && l->ev_count[RPI3_MON_EV_LOCK_TIMEOUT] == 1 && l->ev_count[RPI3_MON_EV_MON_STARVED] == 1);
	rpi3_mon_ev_append(l, RPI3_MON_EV_LOCK_TIMEOUT, 0, 3, t + 100); rpi3_mon_ev_append(l, RPI3_MON_EV_MON_STARVED, 0, 4, t + 200);
	CHECK(l->ev_seq == 2 && l->ev_count[RPI3_MON_EV_LOCK_TIMEOUT] == 2 && l->ev_count[RPI3_MON_EV_MON_STARVED] == 2);
	rpi3_mon_ev_append(l, RPI3_MON_EV_LOCK_TIMEOUT, 0, 5, t + RPI3_MON_TICKS_PER_SEC - 1); CHECK(l->ev_seq == 2);			/* one tick short */
	rpi3_mon_ev_append(l, RPI3_MON_EV_LOCK_TIMEOUT, 0, 6, t + RPI3_MON_TICKS_PER_SEC); CHECK(l->ev_seq == 3 && l->ev_count[RPI3_MON_EV_LOCK_TIMEOUT] == 4);	/* exactly one second */
	rpi3_mon_ev_append(l, RPI3_MON_EV_MODE_CHANGED, 0, 0, t); rpi3_mon_ev_append(l, RPI3_MON_EV_TRAFFIC_RECOVERED, 0, 0, t); CHECK(l->ev_seq == 5); }	/* third and fourth class */
	put(e); end();
	begin("G8 ring overflow: unread entries overwritten are counted, pinned entries never overwritten, seq exhaustion"); setup(); frozen(5000000000ull);
	for (i = 0; i < 3; i++) evadd(RPI3_MON_EV_AUTH_FAIL, (uint32_t)i, 0);
	for (i = 0; i < 6; i++) evadd(RPI3_MON_EV_REPLAY, (uint32_t)i, 0);
	for (i = 0; i < 100; i++) evadd(RPI3_MON_EV_ALTERED, (uint32_t)i, 0);
	CHECK(rpi3_mon.l.ev_lost == 58 && rpi3_mon.l.pinned_used == 11 && rpi3_mon.l.pinned[0].seq == 1 && rpi3_mon.l.pinned[4].seq == 4);
	rpi3_mon.l.ev_seq = 0xFFFFFFFFu; { uint32_t before = rpi3_mon.l.ev_count[RPI3_MON_EV_ALTERED]; evadd(RPI3_MON_EV_ALTERED, 1, 0);
	CHECK(rpi3_mon.l.ev_count[RPI3_MON_EV_ALTERED] == before + 1 && rpi3_mon.l.ev_seq == 0xFFFFFFFFu); } end();
}
static void t_eval(void)
{
	uint32_t e; uint64_t t; int i; struct rpi3_mon_lstate *l;
	begin("V12/Z5 exactly-once traffic timeout and recovery, from both contexts"); setup(); frozen(5000000000ull); t = M.now;
	e = take(); rpi3_mon_traffic_refresh(rpi3_mon_l(), t); put(e);
	for (i = 0; i < 4; i++) { M.daif = (i & 1) ? MON_DAIF_ALL : 0; e = take(); rpi3_mon_evaluate(rpi3_mon_l(), t + RPI3_MON_T_TRAFFIC_TICKS + 100 + (uint64_t)i); put(e); }
	CHECK(rpi3_mon.l.tr_timeout_raised && rpi3_mon.l.ev_count[RPI3_MON_EV_TIMEOUT] == 1 && rpi3_mon.l.pinned[RPI3_MON_EV_TIMEOUT * 4].detail == 1);
	M.daif = 0; e = take(); rpi3_mon_traffic_refresh(rpi3_mon_l(), t + RPI3_MON_T_TRAFFIC_TICKS + 500); rpi3_mon_traffic_refresh(rpi3_mon_l(), t + RPI3_MON_T_TRAFFIC_TICKS + 501); put(e);
	CHECK(!rpi3_mon.l.tr_timeout_raised && rpi3_mon.l.ev_count[RPI3_MON_EV_TRAFFIC_RECOVERED] == 1); end();
	begin("G5 lease expiry returns to EXPECTED, restarts the window, no stale timeout"); setup(); frozen(5000000000ull); t = M.now;
	e = take(); l = rpi3_mon_l(); rpi3_mon_traffic_refresh(l, t); l->idle_until_ticks = t + 5 * RPI3_MON_T_TRAFFIC_TICKS;
	rpi3_mon_evaluate(l, t + 2 * RPI3_MON_T_TRAFFIC_TICKS); CHECK(!l->tr_timeout_raised);
	rpi3_mon_evaluate(l, t + 5 * RPI3_MON_T_TRAFFIC_TICKS + 10); CHECK(l->idle_until_ticks == 0 && !l->tr_timeout_raised && l->tr_last_valid_ticks == t + 5 * RPI3_MON_T_TRAFFIC_TICKS + 10 && l->ev_count[RPI3_MON_EV_MODE_CHANGED] == 1);
	/* C6: the expiry is distinguishable from an explicit return: MODE_CHANGED detail 2 = lease expired */
	{ const struct rpi3_mon_event *x = &l->ordinary[(l->ordinary_head + RPI3_MON_ORDINARY_SLOTS - 1) % RPI3_MON_ORDINARY_SLOTS]; CHECK(x->type == RPI3_MON_EV_MODE_CHANGED && x->detail == 2); } put(e); end();
	begin("EV6/V12 heartbeat: k misses after the grace period, never-started, no catch-up burst, no wrap"); setup(); frozen(9000000000000ull); t = M.now;
	rpi3_mon.init_ticks = t - RPI3_MON_G_TICKS - RPI3_MON_TICKS_PER_SEC;						/* the boot grace has passed */
	e = take(); l = rpi3_mon_l(); l->hb_last_ok_ticks = 1;								/* it has been answered at least once */
	for (i = 1; i <= 3; i++) { uint64_t ti = t + (uint64_t)i * 4 * RPI3_MON_H_TICKS; rpi3_mon_hb_issue_if_due(l, ti); CHECK(l->hb_seq == (uint32_t)i && l->hb_state == RPI3_MON_HB_OPEN);
		rpi3_mon_hb_issue_if_due(l, ti); CHECK(l->hb_seq == (uint32_t)i);						/* the same instant never issues twice */
		rpi3_mon_evaluate(l, ti + RPI3_MON_D_REQ_TICKS + 5); rpi3_mon_evaluate(l, ti + RPI3_MON_D_REQ_TICKS + 6);
		CHECK(l->hb_miss_count == (uint32_t)i && l->hb_state == RPI3_MON_HB_MISSED); CHECK(l->hb_timeout_raised == (i >= 3)); }
	CHECK(RPI3_MON_K_MISS == 3 && RPI3_MON_K_STARVE == 8);						/* the header values are pinned by literal, so a changed constant cannot hide itself */
	CHECK(l->hb_timeout_raised && l->ev_count[RPI3_MON_EV_TIMEOUT] == 1 && l->pinned[RPI3_MON_EV_TIMEOUT * 4].detail == 2 && l->hb_next_issue == t + 12 * RPI3_MON_H_TICKS + RPI3_MON_H_TICKS);
	put(e); end();
	begin("EV6 never-started: no answer ever and the grace period is over raises TIMEOUT detail 3 once; before the grace nothing"); setup(); frozen(9000000000000ull); t = M.now;
	rpi3_mon.init_ticks = t - RPI3_MON_G_TICKS + RPI3_MON_TICKS_PER_SEC; e = take(); rpi3_mon_evaluate(rpi3_mon_l(), t); CHECK(!rpi3_mon.l.hb_timeout_raised); put(e);
	e = take(); rpi3_mon_evaluate(rpi3_mon_l(), t + 2 * RPI3_MON_TICKS_PER_SEC); rpi3_mon_evaluate(rpi3_mon_l(), t + 3 * RPI3_MON_TICKS_PER_SEC); put(e);
	CHECK(rpi3_mon.l.hb_timeout_raised && rpi3_mon.l.ev_count[RPI3_MON_EV_TIMEOUT] == 1 && rpi3_mon.l.pinned[RPI3_MON_EV_TIMEOUT * 4].detail == 3); end();
	begin("G6-5 the challenge sequence never wraps"); setup(); frozen(9000000000000ull); e = take(); l = rpi3_mon_l(); l->hb_seq = 0xFFFFFFFFu;
	rpi3_mon_hb_issue_if_due(l, M.now); rpi3_mon_hb_issue_if_due(l, M.now + 99 * RPI3_MON_H_TICKS); CHECK(l->hb_seq == 0xFFFFFFFFu && l->ev_count[RPI3_MON_EV_HB_SEQ_EXHAUSTED] == 1); put(e); end();
}

/* ------------------------------------------------------------------ command 17 */
static uint8_t *unreadable_page(void) { void *p = mmap(NULL, 4096, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0); return p == MAP_FAILED ? NULL : p; }
static uint8_t g_f44[44];
static void corrupt_caller_buffer(void *arg) { (void)arg; memset(g_f44, 0xEE, sizeof g_f44); }
static void t_c17(void)
{
	uint8_t f[44], g[44]; uint32_t v, d; struct rpi3_mon_health_out h; struct rpi3_mon_lstate before; uint8_t *bad = unreadable_page();
	begin("C17-1 size not 44 is MALFORMED detail 1 and the buffer is never read (no size cap)"); setup(); frozen(5000000000ull);
	CHECK(bad != NULL); CHECK(c17(bad, 43, &v, &d) == RPI3_MON_R_OK && v == RPI3_MON_V17_MALFORMED && d == 1); CHECK(c17(bad, 45, &v, &d) == RPI3_MON_R_OK && v == RPI3_MON_V17_MALFORMED && d == 1);
	CHECK(c17(bad, 100000, &v, &d) == RPI3_MON_R_OK && v == RPI3_MON_V17_MALFORMED && d == 1); CHECK(c17(NULL, 0, &v, &d) == RPI3_MON_R_OK && v == RPI3_MON_V17_MALFORMED && d == 1);
	CHECK(rpi3_mon.l.ev_count[RPI3_MON_EV_MALFORMED] == 4 && mock_hmac_calls == 0 && !rpi3_mon.l.ctr_f.have_last && !rpi3_mon.l.tr_armed);
	CHECK(c17(NULL, 44, &v, &d) == RPI3_MON_R_BAD_PARAMETERS && v == 0);							/* C17-9 */
	end();
	begin("C17-2 can_id range and the stream list {0x123}"); setup(); frozen(5000000000ull); make_frame(f, 0x20000000u, 1, 0, true);
	CHECK(c17(f, 44, &v, &d) == RPI3_MON_R_OK && v == RPI3_MON_V17_MALFORMED && d == 2); make_frame(f, 0x124, 1, 0, true);
	CHECK(c17(f, 44, &v, &d) == RPI3_MON_R_OK && v == RPI3_MON_V17_MALFORMED && d == 3 && mock_hmac_calls == 0 && !rpi3_mon.l.ctr_f.have_last); end();
	begin("C17-3 AUTH_FAIL changes no counter or deadline state"); setup(); frozen(5000000000ull); make_frame(f, 0x123, 100, 0xDDCCBBAA, false);
	CHECK(c17(f, 44, &v, &d) == RPI3_MON_R_OK && v == RPI3_MON_V17_AUTH_FAIL && !rpi3_mon.l.ctr_f.have_last && !rpi3_mon.l.tr_armed && rpi3_mon.l.ev_count[RPI3_MON_EV_AUTH_FAIL] == 1);
	CHECK(rpi3_mon.l.verdict_frame.total == 1 && rpi3_mon.l.verdict_frame.last_type == RPI3_MON_V17_AUTH_FAIL && mock_hmac_calls == 1); end();
	begin("C17-3b every one of the 32 tag bytes is compared"); setup(); frozen(5000000000ull); make_frame(f, 0x123, 100, 1, true); f[43] ^= 0x80;
	CHECK(c17(f, 44, &v, &d) == RPI3_MON_R_OK && v == RPI3_MON_V17_AUTH_FAIL); make_frame(f, 0x123, 100, 1, true); f[12] ^= 0x01; CHECK(c17(f, 44, &v, &d) == RPI3_MON_R_OK && v == RPI3_MON_V17_AUTH_FAIL); end();
	begin("C17-4/5 the counter vector and the deadline rule"); setup(); frozen(5000000000ull);
	make_frame(f, 0x123, 100, 0xDDCCBBAA, true); CHECK(memcmp(f + 12, VEC_FRAME_TAG, 32) == 0);			/* the frame carries the independent tag */
	CHECK(c17(f, 44, &v, &d) == RPI3_MON_R_OK && v == RPI3_MON_V17_RESYNC && rpi3_mon.l.ctr_f.last_counter == 100 && rpi3_mon.l.tr_armed);
	{ uint64_t t1 = rpi3_mon.l.tr_last_valid_ticks; M.now += 1000;
	make_frame(f, 0x123, 90, 1, true); CHECK(c17(f, 44, &v, &d) == RPI3_MON_R_OK && v == RPI3_MON_V17_REPLAY);
	make_frame(f, 0x123, 100, 0xDDCCBBAA, true); CHECK(c17(f, 44, &v, &d) == RPI3_MON_R_OK && v == RPI3_MON_V17_DUPLICATE);
	make_frame(f, 0x123, 100, 0x01020304, true); CHECK(c17(f, 44, &v, &d) == RPI3_MON_R_OK && v == RPI3_MON_V17_CONFLICT);
	make_frame(f, 0x123, 101, 5, false); CHECK(c17(f, 44, &v, &d) == RPI3_MON_R_OK && v == RPI3_MON_V17_AUTH_FAIL);
	CHECK(rpi3_mon.l.tr_last_valid_ticks == t1 && rpi3_mon.l.ctr_f.last_counter == 100);				/* C17-5: nothing but ACCEPTED/GAP/RESYNC refreshes */
	M.now += 1000; make_frame(f, 0x123, 101, 5, true); CHECK(c17(f, 44, &v, &d) == RPI3_MON_R_OK && v == RPI3_MON_V17_ACCEPTED && rpi3_mon.l.tr_last_valid_ticks > t1); }
	M.now += 1000; make_frame(f, 0x123, 105, 5, true); CHECK(c17(f, 44, &v, &d) == RPI3_MON_R_OK && v == RPI3_MON_V17_GAP && d == 3);
	CHECK(rpi3_mon.l.ev_count[RPI3_MON_EV_COUNTER_GAP] == 1 && rpi3_mon.l.ev_count[RPI3_MON_EV_COUNTER_RESYNC] == 1 && rpi3_mon.l.ev_count[RPI3_MON_EV_REPLAY] == 3);
	CHECK(rpi3_mon.l.pinned[RPI3_MON_EV_REPLAY * 4 + 0].detail == 1 && rpi3_mon.l.pinned[RPI3_MON_EV_REPLAY * 4 + 1].detail == 2 && rpi3_mon.l.pinned[RPI3_MON_EV_REPLAY * 4 + 2].detail == 3);	/* REPLAY / DUPLICATE / CONFLICT */
	setup(); frozen(5000000000ull); make_frame(f, 0x123, 0, 1, true); CHECK(c17(f, 44, &v, &d) == RPI3_MON_R_OK && v == RPI3_MON_V17_RESYNC);	/* first frame with counter 0 */
	make_frame(f, 0x123, 0xFFFFFFFFu, 1, true); CHECK(c17(f, 44, &v, &d) == RPI3_MON_R_OK && v == RPI3_MON_V17_GAP);
	make_frame(f, 0x123, 0, 1, true); CHECK(c17(f, 44, &v, &d) == RPI3_MON_R_OK && v == RPI3_MON_V17_REPLAY);			/* no wrap: counter 0 after 0xFFFFFFFF */
	make_frame(g, 0x123, 0xFFFFFFFFu, 1, true); CHECK(c17(g, 44, &v, &d) == RPI3_MON_R_OK && v == RPI3_MON_V17_DUPLICATE);
	make_frame(f, 0x123, 0x80000000u, 1, true); CHECK(c17(f, 44, &v, &d) == RPI3_MON_R_OK && v == RPI3_MON_V17_REPLAY); end();
	begin("C17-6 BUSY precedes any state change, so a resubmission is a fresh frame"); setup(); frozen(5000000000ull); make_frame(f, 0x123, 7, 1, true);
	M.ext_hold = 1; before = rpi3_mon.l; CHECK(c17(f, 44, &v, &d) == RPI3_MON_R_BUSY && v == 0 && memcmp(&before, &rpi3_mon.l, sizeof before) == 0 && rpi3_mon_pta_lock_timeouts == 1);
	M.ext_hold = 0; CHECK(c17(f, 44, &v, &d) == RPI3_MON_R_OK && v == RPI3_MON_V17_RESYNC);					/* not DUPLICATE */
	setup(); frozen(5000000000ull); make_frame(f, 0x123, 7, 1, true); rpi3_mon.mac_pta_busy = 1; before = rpi3_mon.l; mock_hmac_calls = 0;
	CHECK(c17(f, 44, &v, &d) == RPI3_MON_R_BUSY && mock_hmac_calls == 0 && memcmp(&before, &rpi3_mon.l, sizeof before) == 0 && rpi3_mon_pta_lock_timeouts == 0); rpi3_mon.mac_pta_busy = 0; end();
	begin("C17-7 the caller buffer is copied before the lock and the HMAC runs with the lock released"); setup(); frozen(5000000000ull); make_frame(g_f44, 0x123, 9, 0x55, true); memcpy(f, g_f44, 44);
	M.hook_after_lock = corrupt_caller_buffer;									/* the caller rewrites its buffer the instant the lock is taken */
	CHECK(c17(g_f44, 44, &v, &d) == RPI3_MON_R_OK && v == RPI3_MON_V17_RESYNC && memcmp(rpi3_mon.l.ctr_f.last_frame44, f, 44) == 0); M.hook_after_lock = NULL;
	CHECK(M.hmac_in_hold == 0 && M.violations == 0 && mock_hmac_calls == 1); end();
	begin("C17 fail closed: not initialised, HMAC failure, and the health pair"); setup(); frozen(5000000000ull); make_frame(f, 0x123, 3, 1, true);
	rpi3_mon.init_ok = 0; CHECK(rpi3_mon_cmd17(f, 44, &v, &d, &h) == RPI3_MON_R_BAD_STATE && h.p2a == 0 && h.p2b == 0 && h.p3a == 0 && h.p3b == 0); rpi3_mon.init_ok = 1;
	mock_fail_mac_final = 1; before = rpi3_mon.l; CHECK(c17(f, 44, &v, &d) == RPI3_MON_R_BAD_STATE && memcmp(&before, &rpi3_mon.l, sizeof before) == 0); mock_fail_mac_final = 0;
	CHECK(rpi3_mon_cmd17(f, 44, &v, &d, &h) == RPI3_MON_R_OK && (h.p2b & RPI3_MON_HP_INIT_OK) && h.p2a == RPI3_MON_HP_AGE_NEVER); end();
}

/* ------------------------------------------------------------------ commands 18 to 23 */
static uint8_t *g_outbuf;
static unsigned g_out_written_before_unlock;
static void check_out_untouched(void *arg) { (void)arg; if (g_outbuf[0] != 0xEE) g_out_written_before_unlock++; }
static void t_c18(void)
{
	uint8_t out[160]; size_t len; uint32_t oa, ob, next = 1, got = 0, prev = 0, c, i; struct rpi3_mon_health_out h; struct rpi3_mon_event *ev = (struct rpi3_mon_event *)(void *)out; int r, rounds = 0;
	begin("Q1/Q4 command 18: five records, 120 bytes, strict order, bounded scan, copy-out after the unlock"); setup(); frozen(5000000000ull);
	_Static_assert(RPI3_MON_PINNED_SLOTS + RPI3_MON_ORDINARY_SLOTS == 64, "Q4: the scan visits at most 64 slots");
	for (i = 0; i < 3; i++) evadd(RPI3_MON_EV_AUTH_FAIL, i, 0); for (i = 0; i < 6; i++) evadd(RPI3_MON_EV_REPLAY, i, 1);
	len = 160; CHECK(rpi3_mon_cmd18(1, 0, out, &len, &oa, &ob, &h) == RPI3_MON_R_BAD_PARAMETERS); CHECK(rpi3_mon_cmd18(1, 6, out, &len, &oa, &ob, &h) == RPI3_MON_R_BAD_PARAMETERS);
	len = 5; CHECK(rpi3_mon_cmd18(1, 1, NULL, &len, &oa, &ob, &h) == RPI3_MON_R_BAD_PARAMETERS);
	len = 119; CHECK(rpi3_mon_cmd18(1, 5, out, &len, &oa, &ob, &h) == RPI3_MON_R_SHORT_BUFFER && len == 120);
	memset(out, 0xEE, sizeof out); g_outbuf = out; g_out_written_before_unlock = 0; M.hook_before_unlock = check_out_untouched;
	len = 160; CHECK(rpi3_mon_cmd18(1, 5, out, &len, &oa, &ob, &h) == RPI3_MON_R_OK && len == 120 && (oa & 255) == 5 && (oa & 0x100) && out[120] == 0xEE);
	M.hook_before_unlock = NULL; CHECK(g_out_written_before_unlock == 0);								/* Z3: nothing reaches the caller before the unlock */
	do { len = 160; r = rpi3_mon_cmd18(next, 5, out, &len, &oa, &ob, &h); rounds++;
		for (c = 0; c < (oa & 255); c++) { CHECK(ev[c].seq > prev); prev = ev[c].seq; got++; } if (oa & 255) next = prev + 1; } while (r == RPI3_MON_R_OK && (oa & 0x100) && rounds < 10);
	CHECK(got == 9 && rounds == 2);
	for (i = 0; i < 100; i++) evadd(RPI3_MON_EV_ALTERED, i, 0);
	len = 160; CHECK(rpi3_mon_cmd18(1, 5, out, &len, &oa, &ob, &h) == RPI3_MON_R_OK && ob == rpi3_mon.l.ev_lost && rpi3_mon.l.ev_lost == 58 && M.violations == 0);
	M.ext_hold = 1; len = 160; memset(out, 0xEE, sizeof out); CHECK(rpi3_mon_cmd18(1, 5, out, &len, &oa, &ob, &h) == RPI3_MON_R_BUSY && len == 0 && oa == 0 && out[0] == 0xEE && h.p3a == 1); end();
}
static void t_c19(void)
{
	uint32_t oa = 9, ob = 9, lost; struct rpi3_mon_health_out h; struct rpi3_mon_lstate before; int i;
	begin("ACK1-4 command 19 is a bookmark only: never touches ev_lost or the overwrite victim"); setup(); frozen(5000000000ull);
	for (i = 0; i < 60; i++) evadd(RPI3_MON_EV_ALTERED, (uint32_t)i, 0); lost = rpi3_mon.l.ev_lost; CHECK(lost > 0 && rpi3_mon.l.ev_seq == 60);
	CHECK(rpi3_mon_cmd19(1, 7, &oa, &ob, &h) == RPI3_MON_R_BAD_PARAMETERS && h.p3a == 0); before = rpi3_mon.l;
	CHECK(rpi3_mon_cmd19(1000000, 0, &oa, &ob, &h) == RPI3_MON_R_OK && oa == 0 && ob == 60 && rpi3_mon.l.ev_ack == 60 && rpi3_mon.l.ev_lost == lost);	/* ACK2 */
	CHECK(memcmp(before.ordinary, rpi3_mon.l.ordinary, sizeof before.ordinary) == 0 && memcmp(before.pinned, rpi3_mon.l.pinned, sizeof before.pinned) == 0);	/* ACK1 */
	CHECK(rpi3_mon_cmd19(3, 0, &oa, &ob, &h) == RPI3_MON_R_OK && ob == 60 && rpi3_mon.l.ev_ack == 60);						/* ACK3 */
	evadd(RPI3_MON_EV_ALTERED, 99, 0); evadd(RPI3_MON_EV_ALTERED, 100, 0); CHECK(rpi3_mon.l.ev_lost == lost + 2);				/* acknowledged entries are still counted */
	rpi3_mon.init_ok = 0; CHECK(rpi3_mon_cmd19(1, 0, &oa, &ob, &h) == RPI3_MON_R_BAD_STATE && h.p2a == 0); rpi3_mon.init_ok = 1;
	M.ext_hold = 1; CHECK(rpi3_mon_cmd19(1, 0, &oa, &ob, &h) == RPI3_MON_R_BUSY && h.p3a == 1 && (h.p2b & RPI3_MON_HP_INIT_OK)); end();
}
static void lock_free_move(void *arg) { (void)arg; rpi3_mon.health.last_poll_ticks = 777; rpi3_mon.health.poll_count = 4242; }
static void t_c20(void)
{
	uint8_t out[200]; size_t len; uint32_t oa, ob; struct rpi3_mon_health_out h; struct rpi3_mon_proj0 *p = (struct rpi3_mon_proj0 *)(void *)out; struct rpi3_mon_proj1 *q = (struct rpi3_mon_proj1 *)(void *)out; int i;
	begin("N1/N5/N7/HP5 command 20 page 0: exact layout, clock inside the hold, lock-free fields read after the unlock"); setup();
	rpi3_mon.health.last_poll_ticks = M.now - 5 * RPI3_MON_TICKS_PER_MS; rpi3_mon.health.poll_count = 77; rpi3_mon.health.skip_count = 2; rpi3_mon.health.skip_total = 9; rpi3_mon_pta_lock_timeouts = 4;
	rpi3_mon.l.ctr_f.last_counter = 41; rpi3_mon.l.hb_seq = 6; rpi3_mon.l.hb_state = RPI3_MON_HB_ANSWERED; rpi3_mon.l.pta_timeouts_seen = 3;
	memset(out, 0xEE, sizeof out); len = 160; M.hook_before_unlock = lock_free_move;
	CHECK(rpi3_mon_cmd20(0, 0, out, &len, &oa, &ob, &h) == RPI3_MON_R_OK && len == 128 && oa == 128 && ob == 1 && out[128] == 0xEE);
	M.hook_before_unlock = NULL;
	CHECK(p->version == 1 && p->length == 128 && p->boot_id == VEC_BOOTID && p->rsv[0] == 0 && p->rsv[1] == 0 && p->rsv[2] == 0 && p->rsv2 == 0);
	CHECK(p->now_ticks == M.timer_in_hold || p->now_ticks <= M.timer_in_hold);									/* N7: read inside the hold */
	CHECK(p->poll_count == 4242 && p->core1_poll_age_ticks > 900000000ull && p->skip_total == 9 && p->pta_lock_timeouts == 4 && p->pta_timeouts_seen == 3 && p->last_counter == 41 && p->hb_seq == 6);
	CHECK(p->hb_state == RPI3_MON_HB_ANSWERED && p->mode == RPI3_MON_MODE_F && (p->flags & RPI3_MON_PFLAG_INIT_OK) && (p->flags & RPI3_MON_PFLAG_HARDENED) && !(p->flags & RPI3_MON_PFLAG_STARVED));
	CHECK(p->traffic_age_ticks == ~0ULL && p->hb_last_ok_age_ticks == ~0ULL && p->lease_remaining_ticks == 0);					/* N6 sentinels */
	CHECK(h.p3a == 4 && h.p3b == 9 && ((h.p2b >> 8) & 255) == 2 && (h.p2b & RPI3_MON_HP_INIT_OK)); end();
	begin("N6 lease, armed traffic, starved flag, never-answered sentinel"); setup(); frozen(5000000000ull);
	rpi3_mon.l.tr_armed = true; rpi3_mon.l.tr_last_valid_ticks = M.now - 1000; rpi3_mon.l.idle_until_ticks = M.now + 1000000; rpi3_mon.health.starved = 1; len = 160;
	CHECK(rpi3_mon_cmd20(0, 0, out, &len, &oa, &ob, &h) == RPI3_MON_R_OK && (p->flags & RPI3_MON_PFLAG_LEASE) && (p->flags & RPI3_MON_PFLAG_TR_ARMED) && (p->flags & RPI3_MON_PFLAG_STARVED));
	CHECK(p->lease_remaining_ticks == 1000000 && p->traffic_age_ticks == 1000 && (h.p2b & RPI3_MON_HP_STARVED));
	evadd(RPI3_MON_EV_AUTH_FAIL, 1, 0); evadd(RPI3_MON_EV_AUTH_FAIL, 2, 0); evadd(RPI3_MON_EV_REPLAY, 3, 1); len = 160;
	CHECK(rpi3_mon_cmd20(0, 0, out, &len, &oa, &ob, &h) == RPI3_MON_R_OK && p->pinned_used >= 3 && p->pinned_used == rpi3_mon.l.pinned_used && p->ev_seq == rpi3_mon.l.ev_seq);	/* the lease also expires in this call, so more than three */ end();
	begin("EV5/N2 command 20 page 1: exact counters, totals, reserved zero; page 2 is BAD_PARAMETERS"); setup(); frozen(5000000000ull);
	for (i = 0; i < 6; i++) evadd(RPI3_MON_EV_AUTH_FAIL, (uint32_t)i, 0); for (i = 0; i < 3; i++) evadd(RPI3_MON_EV_MALFORMED, (uint32_t)i, 1); rpi3_mon.l.verdict_frame.total = 11; rpi3_mon.l.verdict_hb.total = 22;
	memset(out, 0xEE, sizeof out); len = 160; CHECK(rpi3_mon_cmd20(1, 0, out, &len, &oa, &ob, &h) == RPI3_MON_R_OK && len == 128 && oa == 128 && ob == 1 && out[128] == 0xEE);
	CHECK(q->version == 1 && q->length == 128 && q->rsv == 0 && q->ev_count[RPI3_MON_EV_AUTH_FAIL] == 6 && q->ev_count[RPI3_MON_EV_MALFORMED] == 3 && q->v_frame_total == 11 && q->v_hb_total == 22 && q->v_xcheck_total == 0);
	len = 160; CHECK(rpi3_mon_cmd20(2, 0, out, &len, &oa, &ob, &h) == RPI3_MON_R_BAD_PARAMETERS && h.p3a == 0 && h.p2b == 0); CHECK(rpi3_mon_cmd20(0, 1, out, &len, &oa, &ob, &h) == RPI3_MON_R_BAD_PARAMETERS); end();
	begin("N3/N4/N8/HP2 command 20: short buffer, BUSY, precedence, health fill rule"); setup(); frozen(5000000000ull); memset(out, 0xEE, sizeof out);
	len = 127; CHECK(rpi3_mon_cmd20(0, 0, out, &len, &oa, &ob, &h) == RPI3_MON_R_SHORT_BUFFER && len == 128 && out[0] == 0xEE && h.p3a == 0 && h.p2b == 0);
	len = 160; M.ext_hold = 1; CHECK(rpi3_mon_cmd20(0, 0, out, &len, &oa, &ob, &h) == RPI3_MON_R_BUSY && len == 0 && oa == 0 && ob == 0 && out[0] == 0xEE && h.p3a == 1 && M.violations == 0);
	len = 10; CHECK(rpi3_mon_cmd20(0, 0, out, &len, &oa, &ob, &h) == RPI3_MON_R_SHORT_BUFFER); M.ext_hold = 0;					/* buffer size before BUSY, the lock is never tried */
	rpi3_mon.init_ok = 0; len = 160; CHECK(rpi3_mon_cmd20(1, 0, out, &len, &oa, &ob, &h) == RPI3_MON_R_BAD_STATE && h.p2a == 0); CHECK(rpi3_mon_cmd20(2, 0, out, &len, &oa, &ob, &h) == RPI3_MON_R_BAD_PARAMETERS);
	len = 10; CHECK(rpi3_mon_cmd20(0, 0, out, &len, &oa, &ob, &h) == RPI3_MON_R_BAD_STATE); rpi3_mon.init_ok = 1; len = 5; CHECK(rpi3_mon_cmd20(0, 0, NULL, &len, &oa, &ob, &h) == RPI3_MON_R_BAD_PARAMETERS); end();
}
static void t_c21(void)
{
	uint32_t oa, ob; struct rpi3_mon_health_out h; uint64_t until; int i;
	begin("L21-1..9 command 21: idle lease"); setup(); frozen(5000000000ull);
	CHECK(rpi3_mon_cmd21(2, 5, &oa, &ob, &h) == RPI3_MON_R_BAD_PARAMETERS); CHECK(rpi3_mon_cmd21(1, 0, &oa, &ob, &h) == RPI3_MON_R_BAD_PARAMETERS); CHECK(rpi3_mon_cmd21(0, 5, &oa, &ob, &h) == RPI3_MON_R_BAD_PARAMETERS);
	CHECK(rpi3_mon_cmd21(0, 0, &oa, &ob, &h) == RPI3_MON_R_OK && oa == RPI3_MON_V21_NO_CHANGE && rpi3_mon.l.ev_seq == 0 && rpi3_mon.l.idle_uses == 0);
	CHECK(rpi3_mon_cmd21(1, 1000, &oa, &ob, &h) == RPI3_MON_R_OK && oa == RPI3_MON_V21_GRANTED && ob == 1000 && rpi3_mon.l.idle_uses == 1 && rpi3_mon.l.ev_count[RPI3_MON_EV_LEASE_GRANTED] == 1 && rpi3_mon.l.ev_count[RPI3_MON_EV_MODE_CHANGED] == 1);
	until = rpi3_mon.l.idle_until_ticks; CHECK(rpi3_mon_cmd21(1, 500, &oa, &ob, &h) == RPI3_MON_R_OK && oa == RPI3_MON_V21_RENEWED && ob == 500 && rpi3_mon.l.idle_until_ticks < until && rpi3_mon.l.idle_uses == 2);
	CHECK(rpi3_mon_cmd21(1, 120000, &oa, &ob, &h) == RPI3_MON_R_OK && oa == RPI3_MON_V21_RENEWED && ob == RPI3_MON_IDLE_MAX_MS);					/* clamped */
	rpi3_mon.l.tr_armed = true; rpi3_mon.l.tr_timeout_raised = true; rpi3_mon.l.tr_last_valid_ticks = 1; CHECK(rpi3_mon_cmd21(1, 1000, &oa, &ob, &h) == RPI3_MON_R_OK && rpi3_mon.l.tr_timeout_raised);
	rpi3_mon.l.diag_have[RPI3_MON_EV_MODE_CHANGED] = 0;	/* time is frozen: clear the 1-per-second class limiter so the return event is recorded */
	rpi3_mon.l.tr_timeout_raised = false; rpi3_mon.l.tr_last_valid_ticks = 1; CHECK(rpi3_mon_cmd21(0, 0, &oa, &ob, &h) == RPI3_MON_R_OK && oa == RPI3_MON_V21_RETURNED && ob == 0 && rpi3_mon.l.idle_until_ticks == 0 && rpi3_mon.l.tr_last_valid_ticks == M.now && rpi3_mon.l.idle_uses == 4);
	/* C6: an explicit return stays detail 0 (distinct from the expiry detail 2) */
	{ const struct rpi3_mon_event *x = &rpi3_mon.l.ordinary[(rpi3_mon.l.ordinary_head + RPI3_MON_ORDINARY_SLOTS - 1) % RPI3_MON_ORDINARY_SLOTS]; CHECK(x->type == RPI3_MON_EV_MODE_CHANGED && x->detail == 0); }
	rpi3_mon.l.idle_until_ticks = M.now - 5; CHECK(rpi3_mon_cmd21(1, 1000, &oa, &ob, &h) == RPI3_MON_R_OK && oa == RPI3_MON_V21_GRANTED);					/* an expired lease is a grant, not a renewal */
	rpi3_mon.l.hb_state = RPI3_MON_HB_OPEN; rpi3_mon.l.hb_issue_ticks = M.now - 2 * RPI3_MON_D_REQ_TICKS; CHECK(rpi3_mon_cmd21(1, 1000, &oa, &ob, &h) == RPI3_MON_R_OK && rpi3_mon.l.hb_state == RPI3_MON_HB_MISSED);	/* heartbeat still evaluated */
	for (i = 0; i < 30; i++) (void)rpi3_mon_cmd21(1, 1000, &oa, &ob, &h);
	{ uint32_t uses = rpi3_mon.l.idle_uses; uint64_t u2 = rpi3_mon.l.idle_until_ticks; CHECK(uses == RPI3_MON_IDLE_USES_MAX && rpi3_mon_cmd21(1, 1000, &oa, &ob, &h) == RPI3_MON_R_OK && oa == RPI3_MON_V21_REFUSED_CAP && rpi3_mon.l.idle_uses == uses && rpi3_mon.l.idle_until_ticks == u2); }
	CHECK(rpi3_mon_cmd21(0, 0, &oa, &ob, &h) == RPI3_MON_R_OK && oa == RPI3_MON_V21_RETURNED && M.violations == 0); end();
}
static void bump_challenge(void) { rpi3_mon.l.hb_seq++; rpi3_mon.l.hb_state = RPI3_MON_HB_OPEN; rpi3_mon.l.hb_nonce_valid = false; }
static void block_lock(void) { M.ext_hold = 1; }
static void t_c22(void)
{
	uint32_t lo, hi, seq, st; struct rpi3_mon_health_out h; uint32_t e; unsigned calls;
	begin("N22 command 22: nonce against the independent vector, lazy store, races, budget, guard"); setup(); frozen(0x0000000123456789ull); rpi3_mon.boot_id = 0x11223344;
	CHECK(rpi3_mon_cmd22(&lo, &hi, &seq, &st, &h) == RPI3_MON_R_OK && lo == 0 && hi == 0 && seq == 0 && st == RPI3_MON_HB_NONE && h.p3a == 0);			/* N22-2 NONE */
	e = take(); rpi3_mon.l.hb_seq = 6; rpi3_mon_hb_issue_if_due(rpi3_mon_l(), M.now); put(e); CHECK(rpi3_mon.l.hb_seq == 7 && rpi3_mon.l.hb_issue_ticks == 0x0000000123456789ull);
	calls = mock_hmac_calls; CHECK(rpi3_mon_cmd22(&lo, &hi, &seq, &st, &h) == RPI3_MON_R_OK && seq == 7 && st == RPI3_MON_HB_OPEN);
	CHECK(lo == rpi3_mon_le32(VEC_NONCE) && hi == rpi3_mon_le32(VEC_NONCE + 4) && mock_hmac_calls == calls + 1 && rpi3_mon.l.hb_nonce_valid);					/* N22-3, N22-9 */
	CHECK(rpi3_mon_cmd22(&lo, &hi, &seq, &st, &h) == RPI3_MON_R_OK && lo == rpi3_mon_le32(VEC_NONCE) && mock_hmac_calls == calls + 1);						/* N22-4 */
	CHECK(M.hmac_in_hold == 0 && M.violations == 0 && (h.p2b & RPI3_MON_HP_INIT_OK)); end();					/* N22-8, N22-10 */
	begin("N22-3 MISSED still returns the nonce; ANSWERED and LATE_ANSWERED return state only"); setup(); frozen(0x0000000123456789ull); rpi3_mon.boot_id = 0x11223344;
	e = take(); rpi3_mon.l.hb_seq = 6; rpi3_mon_hb_issue_if_due(rpi3_mon_l(), M.now); put(e); M.now += RPI3_MON_D_REQ_TICKS + 100;
	CHECK(rpi3_mon_cmd22(&lo, &hi, &seq, &st, &h) == RPI3_MON_R_OK && st == RPI3_MON_HB_MISSED && lo == rpi3_mon_le32(VEC_NONCE) && rpi3_mon.l.hb_miss_count == 1);
	rpi3_mon.l.hb_state = RPI3_MON_HB_ANSWERED; CHECK(rpi3_mon_cmd22(&lo, &hi, &seq, &st, &h) == RPI3_MON_R_OK && lo == 0 && hi == 0 && st == RPI3_MON_HB_ANSWERED && seq == 7);
	rpi3_mon.l.hb_state = RPI3_MON_HB_LATE_ANSWERED; CHECK(rpi3_mon_cmd22(&lo, &hi, &seq, &st, &h) == RPI3_MON_R_OK && lo == 0 && hi == 0 && st == RPI3_MON_HB_LATE_ANSWERED); end();
	begin("N22-5 a new challenge between the two holds: nothing stored, new state, zero nonce, no internal retry"); setup(); frozen(0x0000000123456789ull);
	e = take(); rpi3_mon.l.hb_seq = 6; rpi3_mon_hb_issue_if_due(rpi3_mon_l(), M.now); put(e); mock_mac_final_hook = bump_challenge; calls = mock_hmac_calls;
	CHECK(rpi3_mon_cmd22(&lo, &hi, &seq, &st, &h) == RPI3_MON_R_OK && lo == 0 && hi == 0 && seq == 8 && st == RPI3_MON_HB_OPEN && !rpi3_mon.l.hb_nonce_valid && mock_hmac_calls == calls + 1); mock_mac_final_hook = NULL; end();
	begin("N22-6/7 the second hold uses the remaining budget (BUSY); a busy mac guard gives BUSY before the HMAC"); setup(); frozen(0x0000000123456789ull);
	e = take(); rpi3_mon.l.hb_seq = 6; rpi3_mon_hb_issue_if_due(rpi3_mon_l(), M.now); put(e); mock_mac_final_hook = block_lock;
	CHECK(rpi3_mon_cmd22(&lo, &hi, &seq, &st, &h) == RPI3_MON_R_BUSY && !rpi3_mon.l.hb_nonce_valid && rpi3_mon_pta_lock_timeouts == 1 && h.p3a == 1); mock_mac_final_hook = NULL; M.ext_hold = 0;
	rpi3_mon.mac_pta_busy = 1; calls = mock_hmac_calls; CHECK(rpi3_mon_cmd22(&lo, &hi, &seq, &st, &h) == RPI3_MON_R_BUSY && mock_hmac_calls == calls && !rpi3_mon.l.hb_nonce_valid); rpi3_mon.mac_pta_busy = 0;
	mock_fail_mac_final = 1; CHECK(rpi3_mon_cmd22(&lo, &hi, &seq, &st, &h) == RPI3_MON_R_BAD_STATE && !rpi3_mon.l.hb_nonce_valid); mock_fail_mac_final = 0;
	rpi3_mon.init_ok = 0; CHECK(rpi3_mon_cmd22(&lo, &hi, &seq, &st, &h) == RPI3_MON_R_BAD_STATE && h.p2a == 0); end();
}
static void t_c23(void)
{
	uint32_t oa, ob; struct rpi3_mon_health_out h; uint32_t e; uint64_t N = 0x1122334455667788ull; uint32_t lo = (uint32_t)N, hi = (uint32_t)(N >> 32);
	begin("N23 command 23: stored-nonce compare, no HMAC, precedence and transitions"); setup(); frozen(5000000000ull); mock_hmac_calls = 0;
	CHECK(rpi3_mon_cmd23(1, 3, 0, 0, &oa, &ob, &h) == RPI3_MON_R_BAD_PARAMETERS);
	CHECK(rpi3_mon_cmd23(1, 0, 0, 0, &oa, &ob, &h) == RPI3_MON_R_OK && oa == RPI3_MON_V23_UNISSUED);
	e = take(); rpi3_mon_hb_issue_if_due(rpi3_mon_l(), M.now); rpi3_mon.l.hb_nonce = N; rpi3_mon.l.hb_nonce_valid = true; put(e);
	CHECK(rpi3_mon_cmd23(2, 0, 0, 0, &oa, &ob, &h) == RPI3_MON_R_OK && oa == RPI3_MON_V23_UNISSUED); CHECK(rpi3_mon_cmd23(0, 0, 0, 0, &oa, &ob, &h) == RPI3_MON_R_OK && oa == RPI3_MON_V23_STALE);
	CHECK(rpi3_mon_cmd23(1, 0, 1, 2, &oa, &ob, &h) == RPI3_MON_R_OK && oa == RPI3_MON_V23_BAD_NONCE && ob == 2 && rpi3_mon.l.hb_state == RPI3_MON_HB_OPEN && rpi3_mon.l.hb_miss_count == 0);
	rpi3_mon.l.hb_nonce_valid = false; CHECK(rpi3_mon_cmd23(1, 0, lo, hi, &oa, &ob, &h) == RPI3_MON_R_OK && oa == RPI3_MON_V23_BAD_NONCE && ob == 1);						/* never fetched */
	rpi3_mon.l.hb_nonce_valid = true; rpi3_mon.l.hb_timeout_raised = true; rpi3_mon.l.hb_miss_count = 2;
	CHECK(rpi3_mon_cmd23(1, 0, lo, hi, &oa, &ob, &h) == RPI3_MON_R_OK && oa == RPI3_MON_V23_ON_TIME && rpi3_mon.l.hb_state == RPI3_MON_HB_ANSWERED && rpi3_mon.l.hb_miss_count == 0 && !rpi3_mon.l.hb_timeout_raised);
	CHECK(rpi3_mon.l.ev_count[RPI3_MON_EV_HB_RECOVERED] == 1 && rpi3_mon.l.hb_last_ok_ticks == M.now);
	CHECK(rpi3_mon_cmd23(1, 0, lo, hi, &oa, &ob, &h) == RPI3_MON_R_OK && oa == RPI3_MON_V23_DUPLICATE);
	e = take(); M.now += 1000; rpi3_mon_hb_issue_if_due(rpi3_mon_l(), M.now + RPI3_MON_H_TICKS); CHECK(!rpi3_mon.l.hb_nonce_valid); rpi3_mon.l.hb_nonce = N; rpi3_mon.l.hb_nonce_valid = true; put(e);
	M.now += RPI3_MON_H_TICKS + RPI3_MON_D_REQ_TICKS + 100;										/* the deadline of challenge 2 passes: MISSED */
	CHECK(rpi3_mon_cmd23(2, 0, lo, hi, &oa, &ob, &h) == RPI3_MON_R_OK && oa == RPI3_MON_V23_LATE && rpi3_mon.l.hb_state == RPI3_MON_HB_LATE_ANSWERED && rpi3_mon.l.hb_miss_count == 1 && rpi3_mon.l.ev_count[RPI3_MON_EV_LATE_ANSWER] == 1);
	CHECK(rpi3_mon_cmd23(2, 0, lo, hi, &oa, &ob, &h) == RPI3_MON_R_OK && oa == RPI3_MON_V23_DUPLICATE);
	M.now += 3 * RPI3_MON_H_TICKS; e = take(); rpi3_mon_hb_issue_if_due(rpi3_mon_l(), M.now); put(e);					/* challenge 3, nonce NOT stored */
	CHECK(!rpi3_mon.l.hb_nonce_valid); CHECK(rpi3_mon_cmd23(3, 0, lo, hi, &oa, &ob, &h) == RPI3_MON_R_OK && oa == RPI3_MON_V23_BAD_NONCE && ob == 1);			/* the old nonce does not answer a new challenge */
	/* N23-2/3/10: exact event counters per class, from the call sequence above (counters are exact even when the ring suppresses repeats) */
	CHECK(rpi3_mon.l.ev_count[RPI3_MON_EV_UNISSUED_ANSWER] == 2 && rpi3_mon.l.ev_count[RPI3_MON_EV_STALE_ANSWER] == 1 && rpi3_mon.l.ev_count[RPI3_MON_EV_BAD_NONCE] == 3);
	CHECK(rpi3_mon.l.ev_count[RPI3_MON_EV_DUP_ANSWER] == 2 && rpi3_mon.l.ev_count[RPI3_MON_EV_HB_RECOVERED] == 1 && rpi3_mon.l.ev_count[RPI3_MON_EV_LATE_ANSWER] == 1);
	CHECK(rpi3_mon.l.verdict_hb.total == 10);
	CHECK(rpi3_mon.l.verdict_hb.total >= 8 && mock_hmac_calls == 0 && M.violations == 0);						/* no HMAC in command 23 */
	rpi3_mon.init_ok = 0; CHECK(rpi3_mon_cmd23(2, 0, 0, 0, &oa, &ob, &h) == RPI3_MON_R_BAD_STATE && h.p2a == 0); rpi3_mon.init_ok = 1;
	M.ext_hold = 1; CHECK(rpi3_mon_cmd23(2, 0, 0, 0, &oa, &ob, &h) == RPI3_MON_R_BUSY && oa == 0 && h.p3a == 1); end();
}

/* ------------------------------------------------------------------ PTA glue: dispatch, legacy policy, parameter mapping */
#define PTT(a, b, c, d)	TEE_PARAM_TYPES(TEE_PARAM_TYPE_##a, TEE_PARAM_TYPE_##b, TEE_PARAM_TYPE_##c, TEE_PARAM_TYPE_##d)
static void clearp(TEE_Param p[4]) { memset(p, 0xAB, 4 * sizeof(TEE_Param)); }
static void t_glue(void)
{
	TEE_Param p[4]; bool handled; TEE_Result r; static const uint32_t refused[] = {0, 3, 6, 7, 13, 14}, readers[] = {4, 5, 8, 9, 10, 11, 15, 16}, kept[] = {1, 2, 12}; size_t i; int prof;
	uint32_t anytypes[] = {PTT(NONE, NONE, NONE, NONE), PTT(VALUE_OUTPUT, NONE, NONE, NONE), PTT(MEMREF_INOUT, VALUE_INPUT, VALUE_INOUT, MEMREF_OUTPUT)};
	begin("DS1 an unknown id is NOT_SUPPORTED for any parameter types"); setup(); frozen(5000000000ull);
	{ static const uint32_t unk[] = {
#ifndef CFG_RPI3_CAN_MONITOR_TEST_STATS
		24, 25, 26,	/* in the test-statistics build these three are the test commands (24 statistics, 25 control, 26 W1) */
#endif
		27, 99, 1000, 0xFFFFFFFFu}; size_t k; for (i = 0; i < sizeof unk / sizeof unk[0]; i++) for (k = 0; k < 3; k++) { clearp(p); handled = false;
		r = rpi3_mon_pta_invoke(unk[i], anytypes[k], p, &handled); CHECK(handled && r == TEE_ERROR_NOT_SUPPORTED); } } end();
	begin("LG1/LG4 refused ids 0,3,6,7,13,14: NOT_SUPPORTED before any parameter check, in both profiles; refusal never waits"); setup(); frozen(5000000000ull);
	for (prof = 0; prof < 2; prof++) { mock_profile_debug = prof; for (i = 0; i < sizeof refused / sizeof refused[0]; i++) { size_t k; for (k = 0; k < 3; k++) { clearp(p); handled = false;
		r = rpi3_mon_pta_invoke(refused[i], anytypes[k], p, &handled); CHECK(handled && r == TEE_ERROR_NOT_SUPPORTED); } } }
	CHECK(rpi3_mon.l.ev_count[RPI3_MON_EV_LEGACY_REFUSED] == 2 * 6 * 3 && rpi3_mon.l.ordinary[0].detail == refused[0] && rpi3_mon.l.ev_count[RPI3_MON_EV_LEGACY_USED] == 0);
	M.ext_hold = 1; { unsigned tries = M.trylock_calls; clearp(p); r = rpi3_mon_pta_invoke(14, anytypes[0], p, &handled); CHECK(r == TEE_ERROR_NOT_SUPPORTED && M.trylock_calls == tries + 1 && rpi3_mon_pta_lock_timeouts == 0 && M.daif == 0); } end();
	begin("LG2 chip A debug readers: refused when hardened; allowed with an event in the debug profile"); setup(); frozen(5000000000ull);
	for (i = 0; i < sizeof readers / sizeof readers[0]; i++) { mock_profile_debug = 0; clearp(p); handled = true; r = rpi3_mon_pta_invoke(readers[i], anytypes[1], p, &handled); CHECK(handled && r == TEE_ERROR_NOT_SUPPORTED);
		mock_profile_debug = 1; handled = true; r = rpi3_mon_pta_invoke(readers[i], anytypes[1], p, &handled); CHECK(!handled && r == TEE_SUCCESS); }
	CHECK(rpi3_mon.l.ev_count[RPI3_MON_EV_LEGACY_USED] == 8 && rpi3_mon.l.ev_count[RPI3_MON_EV_LEGACY_REFUSED] == 8); end();
	begin("LG3 commands 1, 2, 12 stay on the unchanged legacy path in every profile, with no event"); setup(); frozen(5000000000ull);
	for (prof = 0; prof < 2; prof++) { mock_profile_debug = prof; for (i = 0; i < sizeof kept / sizeof kept[0]; i++) { handled = true; r = rpi3_mon_pta_invoke(kept[i], anytypes[1], p, &handled); CHECK(!handled && r == TEE_SUCCESS); } }
	CHECK(rpi3_mon.l.ev_seq == 0); end();
	begin("DS4 monitor init failure: 17 to 23 return BAD_STATE, legacy 1, 2, 12 still work, refusals still apply"); setup(); frozen(5000000000ull); rpi3_mon.init_ok = 0;
	{ struct { uint32_t id, pt; } c[] = { {17, PTT(VALUE_OUTPUT, MEMREF_INPUT, VALUE_OUTPUT, VALUE_OUTPUT)}, {18, PTT(VALUE_INOUT, MEMREF_OUTPUT, VALUE_OUTPUT, VALUE_OUTPUT)}, {19, PTT(VALUE_INOUT, NONE, VALUE_OUTPUT, VALUE_OUTPUT)},
	  {20, PTT(VALUE_INOUT, MEMREF_OUTPUT, VALUE_OUTPUT, VALUE_OUTPUT)}, {21, PTT(VALUE_INOUT, NONE, VALUE_OUTPUT, VALUE_OUTPUT)}, {22, PTT(VALUE_OUTPUT, VALUE_OUTPUT, VALUE_OUTPUT, VALUE_OUTPUT)}, {23, PTT(VALUE_INOUT, VALUE_INPUT, VALUE_OUTPUT, VALUE_OUTPUT)} };
	  static uint8_t buf[200]; for (i = 0; i < 7; i++) { clearp(p); p[0].value.a = 0; p[0].value.b = (c[i].id == 18) ? 5 : 0; p[1].memref.buffer = buf; p[1].memref.size = sizeof buf; handled = false; r = rpi3_mon_pta_invoke(c[i].id, c[i].pt, p, &handled);
		CHECK(handled && r == TEE_ERROR_BAD_STATE && p[2].value.a == 0 && p[2].value.b == 0 && p[3].value.a == 0 && p[3].value.b == 0); } }
	handled = true; CHECK(rpi3_mon_pta_invoke(12, anytypes[1], p, &handled) == TEE_SUCCESS && !handled); CHECK(rpi3_mon_pta_invoke(3, anytypes[1], p, &handled) == TEE_ERROR_NOT_SUPPORTED && handled); end();
	begin("HP1 exact param_types: any other combination is BAD_PARAMETERS and writes nothing"); setup(); frozen(5000000000ull);
	for (i = 17; i <= 23; i++) { size_t k; for (k = 0; k < 3; k++) { clearp(p); handled = false; r = rpi3_mon_pta_invoke((uint32_t)i, anytypes[k], p, &handled);
		CHECK(handled && r == TEE_ERROR_BAD_PARAMETERS && p[0].value.a == 0xABABABABu && p[2].value.a == 0xABABABABu && p[3].value.b == 0xABABABABu); } }
	clearp(p); CHECK(rpi3_mon_pta_invoke(18, PTT(VALUE_INOUT, MEMREF_OUTPUT, VALUE_OUTPUT, NONE), p, &handled) == TEE_ERROR_BAD_PARAMETERS); end();
}
static void t_glue_cmds(void)
{
	TEE_Param p[4]; bool handled; TEE_Result r; static uint8_t buf[200], f[44]; uint32_t i; struct rpi3_mon_proj0 *pj = (struct rpi3_mon_proj0 *)(void *)buf;
	begin("GLUE parameter mapping: outputs, sizes and the health pair for commands 17 to 23"); setup(); frozen(5000000000ull);
	make_frame(f, 0x123, 100, 0xDDCCBBAA, true); clearp(p); p[1].memref.buffer = f; p[1].memref.size = 44; handled = false;
	r = rpi3_mon_pta_invoke(17, PTT(VALUE_OUTPUT, MEMREF_INPUT, VALUE_OUTPUT, VALUE_OUTPUT), p, &handled);
	CHECK(handled && r == TEE_SUCCESS && p[0].value.a == RPI3_MON_V17_RESYNC && p[0].value.b == 0 && (p[2].value.b & RPI3_MON_HP_INIT_OK) && p[3].value.a == 0);
	for (i = 0; i < 6; i++) evadd(RPI3_MON_EV_AUTH_FAIL, i, 0);
	memset(buf, 0xEE, sizeof buf); clearp(p); p[0].value.a = 1; p[0].value.b = 5; p[1].memref.buffer = buf; p[1].memref.size = 119;
	r = rpi3_mon_pta_invoke(18, PTT(VALUE_INOUT, MEMREF_OUTPUT, VALUE_OUTPUT, VALUE_OUTPUT), p, &handled); CHECK(r == TEE_ERROR_SHORT_BUFFER && p[1].memref.size == 120 && p[0].value.a == 1 && p[2].value.a == 0 && buf[0] == 0xEE);
	p[0].value.a = 1; p[0].value.b = 5; p[1].memref.size = 160; r = rpi3_mon_pta_invoke(18, PTT(VALUE_INOUT, MEMREF_OUTPUT, VALUE_OUTPUT, VALUE_OUTPUT), p, &handled);
	CHECK(r == TEE_SUCCESS && p[1].memref.size == 120 && (p[0].value.a & 255) == 5 && (p[0].value.a & 0x100));
	clearp(p); p[0].value.a = 0; p[0].value.b = 0; p[1].memref.buffer = buf; p[1].memref.size = 128; memset(buf, 0xEE, sizeof buf);
	r = rpi3_mon_pta_invoke(20, PTT(VALUE_INOUT, MEMREF_OUTPUT, VALUE_OUTPUT, VALUE_OUTPUT), p, &handled); CHECK(r == TEE_SUCCESS && p[0].value.a == 128 && p[0].value.b == 1 && p[1].memref.size == 128 && pj->version == 1 && pj->boot_id == VEC_BOOTID);
	clearp(p); p[0].value.a = 1; p[0].value.b = 1000; r = rpi3_mon_pta_invoke(21, PTT(VALUE_INOUT, NONE, VALUE_OUTPUT, VALUE_OUTPUT), p, &handled); CHECK(r == TEE_SUCCESS && p[0].value.a == RPI3_MON_V21_GRANTED && p[0].value.b == 1000);
	clearp(p); p[0].value.a = 99; p[0].value.b = 0; r = rpi3_mon_pta_invoke(19, PTT(VALUE_INOUT, NONE, VALUE_OUTPUT, VALUE_OUTPUT), p, &handled); CHECK(r == TEE_SUCCESS && p[0].value.a == 0 && p[0].value.b == rpi3_mon.l.ev_ack);
	clearp(p); r = rpi3_mon_pta_invoke(22, PTT(VALUE_OUTPUT, VALUE_OUTPUT, VALUE_OUTPUT, VALUE_OUTPUT), p, &handled); CHECK(r == TEE_SUCCESS && p[0].value.a == 0 && p[1].value.b == RPI3_MON_HB_NONE);
	clearp(p); p[0].value.a = 1; p[0].value.b = 0; p[1].value.a = 0; p[1].value.b = 0; r = rpi3_mon_pta_invoke(23, PTT(VALUE_INOUT, VALUE_INPUT, VALUE_OUTPUT, VALUE_OUTPUT), p, &handled); CHECK(r == TEE_SUCCESS && p[0].value.a == RPI3_MON_V23_UNISSUED);
	M.ext_hold = 1; clearp(p); p[0].value.a = 1; p[0].value.b = 0; r = rpi3_mon_pta_invoke(23, PTT(VALUE_INOUT, VALUE_INPUT, VALUE_OUTPUT, VALUE_OUTPUT), p, &handled);
	CHECK(r == TEE_ERROR_BUSY && p[0].value.a == 0 && p[0].value.b == 0 && p[3].value.a == 1);							/* BUSY: zero values and a filled health pair */
	clearp(p); p[1].memref.buffer = buf; p[1].memref.size = 128; p[0].value.a = 0; p[0].value.b = 0; r = rpi3_mon_pta_invoke(20, PTT(VALUE_INOUT, MEMREF_OUTPUT, VALUE_OUTPUT, VALUE_OUTPUT), p, &handled);
	CHECK(r == TEE_ERROR_BUSY && p[1].memref.size == 0 && p[3].value.a >= 2); end();
}

/* ------------------------------------------------------------------ Core 1 (design 4g, D1 = Option B) */
static void t_core1(void)
{
	uint32_t pass, i, p; struct rpi3_mon_lstate before; unsigned t0;
	begin("Y1/Y9 one non-spinning attempt per 16384 passes; no timer read on the other passes; the counter wraps harmlessly"); setup(); M.daif = MON_DAIF_ALL; frozen(5000000000ull);
	for (pass = 0; pass < 1000; pass++) rpi3_mon_core1_pass(pass); CHECK(M.trylock_calls == 1 && rpi3_mon.health.poll_count == 1000 && mock_hmac_calls == 0);
	t0 = M.timer_reads; for (pass = 1; pass < 16384; pass++) rpi3_mon_core1_pass(pass); CHECK(M.timer_reads == t0 + 0 && rpi3_mon.health.poll_count == 17383);			/* passes 1..16383 read no clock */
	setup(); M.daif = MON_DAIF_ALL; M.ext_hold = 1; frozen(5000000000ull); for (pass = 0; pass < 1000; pass++) rpi3_mon_core1_pass(pass); CHECK(M.trylock_calls == 1);			/* a held lock is never retried or spun on */
	setup(); M.daif = MON_DAIF_ALL; frozen(5000000000ull); for (i = 0, p = 0xFFFFC000u; i < 20000; i++) rpi3_mon_core1_pass(p++); CHECK(M.trylock_calls == 2 && M.violations == 0); end();
	begin("Y2 a failed attempt changes only G7"); setup(); M.daif = MON_DAIF_ALL; frozen(5000000000ull); rpi3_mon_pta_lock_timeouts = 3; M.ext_hold = 1; before = rpi3_mon.l; rpi3_mon_core1_attempt();
	CHECK(memcmp(&before, &rpi3_mon.l, sizeof before) == 0 && rpi3_mon.health.skip_count == 1 && rpi3_mon.health.skip_total == 1 && M.violations == 0 && rpi3_mon_pta_lock_timeouts == 3); end();
	begin("Y3/Y4/Y5 starvation: K consecutive failures, one MON_STARVED per episode, skip_total cumulative, a stuck lock gives no event"); setup(); M.daif = MON_DAIF_ALL; frozen(5000000000ull); M.ext_hold = 1;
	for (i = 1; i <= RPI3_MON_K_STARVE + 3; i++) { rpi3_mon_core1_attempt(); CHECK(rpi3_mon.health.starved == (i >= RPI3_MON_K_STARVE ? 1u : 0u)); }
	CHECK(rpi3_mon.health.skip_count == RPI3_MON_K_STARVE + 3 && rpi3_mon.l.ev_seq == 0);											/* Y5: stuck, starved, no event */
	M.ext_hold = 0; rpi3_mon_core1_attempt(); CHECK(rpi3_mon.health.skip_count == 0 && rpi3_mon.health.starved == 0 && rpi3_mon.health.skip_total == RPI3_MON_K_STARVE + 3);
	CHECK(rpi3_mon.l.ev_count[RPI3_MON_EV_MON_STARVED] == 1 && rpi3_mon.l.ordinary[0].type == RPI3_MON_EV_MON_STARVED && rpi3_mon.l.ordinary[0].detail == RPI3_MON_K_STARVE + 3);	/* Y4 */
	M.ext_hold = 1; for (i = 0; i < RPI3_MON_K_STARVE - 1; i++) rpi3_mon_core1_attempt(); M.ext_hold = 0; rpi3_mon_core1_attempt(); CHECK(rpi3_mon.l.ev_count[RPI3_MON_EV_MON_STARVED] == 1);	/* a shorter episode logs nothing */
	CHECK(M.violations == 0 && mock_hmac_calls == 0); end();
	begin("V10/S2 timeouts are observed only after a successful lock, never consumed on a failure, with the exact total and wrap"); setup(); M.daif = MON_DAIF_ALL; frozen(5000000000ull);
	rpi3_mon_pta_lock_timeouts = 3; M.ext_hold = 1; rpi3_mon_core1_attempt(); CHECK(rpi3_mon.l.pta_timeouts_seen == 0 && rpi3_mon.l.ev_seq == 0);
	M.ext_hold = 0; rpi3_mon_core1_attempt(); CHECK(rpi3_mon.l.pta_timeouts_seen == 3 && rpi3_mon.l.ev_count[RPI3_MON_EV_LOCK_TIMEOUT] == 3 && rpi3_mon.l.ordinary[0].detail == 3);
	rpi3_mon_core1_attempt(); CHECK(rpi3_mon.l.ev_count[RPI3_MON_EV_LOCK_TIMEOUT] == 3);										/* nothing new: nothing consumed twice */
	rpi3_mon_pta_lock_timeouts = 0xFFFFFFF0u; rpi3_mon.l.pta_timeouts_seen = 0xFFFFFFF0u; for (i = 0; i < 32; i++) MON_PLAT_INC32(&rpi3_mon_pta_lock_timeouts);
	M.now += 5 * RPI3_MON_TICKS_PER_SEC; rpi3_mon_core1_attempt(); CHECK(rpi3_mon.l.pta_timeouts_seen == 0x10u && rpi3_mon.l.ev_count[RPI3_MON_EV_LOCK_TIMEOUT] == 3 + 32 && rpi3_mon.l.ordinary[rpi3_mon.l.ordinary_head - 1].detail == 32); end();
	begin("Core 1 issues one challenge when due, never a burst, and records last_eval_ticks"); setup(); M.daif = MON_DAIF_ALL; frozen(5000000000ull);
	rpi3_mon_core1_attempt(); CHECK(rpi3_mon.l.hb_seq == 1 && rpi3_mon.l.hb_state == RPI3_MON_HB_OPEN && rpi3_mon.l.hb_next_issue == M.now + RPI3_MON_H_TICKS && rpi3_mon.l.last_eval_ticks == M.now && !rpi3_mon.l.hb_nonce_valid);
	rpi3_mon_core1_attempt(); CHECK(rpi3_mon.l.hb_seq == 1); M.now += 3 * RPI3_MON_H_TICKS; rpi3_mon_core1_attempt(); CHECK(rpi3_mon.l.hb_seq == 2);			/* a long gap issues ONE challenge */
	CHECK(mock_hmac_calls == 0); end();
	begin("Y6/D1 the entry check panics unless FIQ and IRQ are both masked; the loop never changes the mask"); setup(); M.daif = MON_DAIF_ALL; rpi3_mon_core1_entry_check(); CHECK(M.panics == 0);
	M.daif = 0; rpi3_mon_core1_entry_check(); CHECK(M.panics == 1); M.daif = MON_DAIF_IRQ; rpi3_mon_core1_entry_check(); CHECK(M.panics == 2); M.daif = MON_DAIF_FIQ; rpi3_mon_core1_entry_check(); CHECK(M.panics == 3);
	M.daif = MON_DAIF_ALL; M.panics = 0; frozen(5000000000ull); for (pass = 0; pass < 3000; pass++) rpi3_mon_core1_pass(pass); CHECK(M.daif == MON_DAIF_ALL && M.mask_calls == 0 && M.unmask_calls == 0 && M.panics == 0); end();
}

/* ------------------------------------------------------------------ concurrency: Core 1 against several PTA contexts on the real lock word */
#define NPTA 3
#define PITER 6000
static volatile int stop_core1, broken; static unsigned long ok_n[NPTA], busy_n[NPTA], c1_passes;
static void *core1_thr(void *a)
{
	uint32_t pass = 0; (void)a; memset(&M, 0, sizeof M); M.tmode = 3; M.daif = MON_DAIF_ALL;
	while (!stop_core1) rpi3_mon_core1_pass(pass++); c1_passes = pass; if (M.violations) broken = 1; return NULL;
}
static void *pta_thr(void *a)
{
	long id = (long)a; int i; uint8_t out[200]; size_t len; uint32_t oa, ob, nlo = 0, nhi = 0, sq, st; struct rpi3_mon_health_out h; enum rpi3_mon_res r;
	memset(&M, 0, sizeof M); M.tmode = 3;
	for (i = 0; i < PITER; i++) {
		len = sizeof out; r = RPI3_MON_R_OK;
		switch ((i + (int)id) % 6) {
		case 0: r = rpi3_mon_cmd18(1, 5, out, &len, &oa, &ob, &h); if (r == RPI3_MON_R_OK) { struct rpi3_mon_event *ev = (struct rpi3_mon_event *)(void *)out; uint32_t c; for (c = 1; c < (oa & 255); c++) if (ev[c].seq <= ev[c - 1].seq) broken = 1; } break;
		case 1: r = rpi3_mon_cmd19((uint32_t)i * 3, 0, &oa, &ob, &h); break;
		case 2: r = rpi3_mon_cmd20(0, 0, out, &len, &oa, &ob, &h); if (r == RPI3_MON_R_OK) { struct rpi3_mon_proj0 *p = (struct rpi3_mon_proj0 *)(void *)out; if (p->ev_ack > p->ev_seq || p->pinned_used > 24 || p->version != 1) broken = 1; } break;
		case 3: r = rpi3_mon_cmd21((uint32_t)(i & 1), (i & 1) ? 100u : 0u, &oa, &ob, &h); break;
		case 4: r = rpi3_mon_cmd23(1, 0, (uint32_t)i, 0, &oa, &ob, &h); break;
		case 5: r = rpi3_mon_cmd20(1, 0, out, &len, &oa, &ob, &h); if (r == RPI3_MON_R_OK) { struct rpi3_mon_proj1 *q = (struct rpi3_mon_proj1 *)(void *)out; if (q->version != 1 || q->rsv != 0) broken = 1; } break;
		}
		if (r == RPI3_MON_R_OK) ok_n[id]++; else if (r == RPI3_MON_R_BUSY) busy_n[id]++; else broken = 1;
	}
	(void)nlo; (void)nhi; (void)sq; (void)st; if (M.violations) broken = 1; return NULL;
}
static void t_stress(void)
{
	pthread_t c1, t[NPTA]; long i; unsigned long ok = 0, busy = 0; int sum, k;
	begin("STRESS Core 1 + 3 PTA contexts (commands 18..23 minus 17/22) on the real lock: invariants and exact counters"); setup();
	stop_core1 = 0; broken = 0; memset(ok_n, 0, sizeof ok_n); memset(busy_n, 0, sizeof busy_n); c1_passes = 0;
	pthread_create(&c1, NULL, core1_thr, NULL); for (i = 0; i < NPTA; i++) pthread_create(&t[i], NULL, pta_thr, (void *)i);
	for (i = 0; i < NPTA; i++) pthread_join(t[i], NULL); stop_core1 = 1; pthread_join(c1, NULL);
	for (i = 0; i < NPTA; i++) { ok += ok_n[i]; busy += busy_n[i]; }
	CHECK(!broken); CHECK(ok + busy == (unsigned long)NPTA * PITER && rpi3_mon_lock == 0);
	CHECK(rpi3_mon_pta_lock_timeouts == busy);										/* exactly one increment per exhausted command */
	CHECK(rpi3_mon.health.poll_count == c1_passes && c1_passes > 0);								/* G7: one writer, no lost update */
	memset(&M, 0, sizeof M); M.tmode = 3; M.daif = MON_DAIF_ALL; rpi3_mon_core1_attempt();
	CHECK(rpi3_mon.l.pta_timeouts_seen == rpi3_mon_pta_lock_timeouts && rpi3_mon.l.ev_count[RPI3_MON_EV_LOCK_TIMEOUT] == rpi3_mon_pta_lock_timeouts);	/* Core 1 consumed every timeout exactly once */
	for (k = 0, sum = 0; k < 6; k++) sum += rpi3_mon.l.pinned_n[k]; CHECK((uint32_t)sum == rpi3_mon.l.pinned_used && rpi3_mon.l.pinned_used <= 24 && rpi3_mon.l.ev_ack <= rpi3_mon.l.ev_seq && rpi3_mon.l.idle_uses <= RPI3_MON_IDLE_USES_MAX);
	printf("     (PTA ok %lu, BUSY %lu, Core 1 passes %lu, skip_total %u, events %u)\n", ok, busy, c1_passes, rpi3_mon.health.skip_total, rpi3_mon.l.ev_seq); end();
}
#define GITER 1500
static unsigned long g17_ok[NPTA]; static volatile int g_broken;
static void *guard_thr(void *a)
{
	long id = (long)a; int i; uint8_t f[44]; uint32_t v, d, lo, hi, sq, st; struct rpi3_mon_health_out h; enum rpi3_mon_res r; memset(&M, 0, sizeof M); M.tmode = 3;
	for (i = 0; i < GITER; i++) {
		if ((i + id) & 1) { make_frame(f, 0x123, (uint32_t)(i * 7 + (int)id), 5, (i % 3) != 0); r = rpi3_mon_cmd17(f, 44, &v, &d, &h); if (r == RPI3_MON_R_OK) g17_ok[id]++; else if (r != RPI3_MON_R_BUSY) g_broken = 1; }
		else { r = rpi3_mon_cmd22(&lo, &hi, &sq, &st, &h); if (r != RPI3_MON_R_OK && r != RPI3_MON_R_BUSY) g_broken = 1; }
	}
	if (M.violations || M.hmac_in_hold) g_broken = 1; return NULL;
}
static void t_stress_guard(void)
{
	pthread_t c1, t[NPTA]; long i; unsigned long ok = 0;
	begin("STRESS HMAC commands 17 and 22 from 3 contexts: the mac_pta guard serialises the shared context, Core 1 issues challenges"); setup();
	stop_core1 = 0; g_broken = 0; broken = 0; memset(g17_ok, 0, sizeof g17_ok); mock_hmac_calls = 0;
	pthread_create(&c1, NULL, core1_thr, NULL); for (i = 0; i < NPTA; i++) pthread_create(&t[i], NULL, guard_thr, (void *)i);
	for (i = 0; i < NPTA; i++) pthread_join(t[i], NULL); stop_core1 = 1; pthread_join(c1, NULL); for (i = 0; i < NPTA; i++) ok += g17_ok[i];
	CHECK(!g_broken && !broken && rpi3_mon_lock == 0 && rpi3_mon.mac_pta_busy == 0);
	CHECK(rpi3_mon.l.verdict_frame.total == ok);										/* every OK command 17 was recorded exactly once, none twice */
	printf("     (command 17 processed %lu, HMAC calls %u, challenges %u)\n", ok, mock_hmac_calls, rpi3_mon.l.hb_seq); end();
}
#ifdef CFG_RPI3_CAN_MONITOR_TEST_STATS
/* TEST-ONLY build: the hold-time stamps, attempt-interval statistics, Core 1 hold request and the two test commands (24, 25). */
static void t_tstats(void)
{
	struct rpi3_mon_tstats *ts = &rpi3_mon_tstats; uint32_t e, done = 0; TEE_Param p[4]; bool handled; TEE_Result r; static uint8_t buf[512]; struct rpi3_mon_wait w;
	begin("TS1 PTA hold stamp: one hold of one timer step is counted, bucketed (floor log2) and attributed to the PTA"); setup(); M.daif = MON_DAIF_ALL; M.tmode = 0; M.step = 60;
	memset(ts, 0, sizeof *ts); e = take(); put(e);
	CHECK(ts->hold_count == 1 && ts->hold_sum == 60 && ts->hold_max == 60 && ts->hold_hist[5] == 1 && ts->hold_max_ctx[RPI3_MON_TS_CTX_PTA] == 60 && ts->hold_max_ctx[RPI3_MON_TS_CTX_CORE1] == 0); end();
	begin("TS2 Core 1 attempt: hold stamp, interval statistics (first attempt has no interval), counts only successful holds"); setup(); M.daif = MON_DAIF_ALL; M.tmode = 0; M.step = 60; memset(ts, 0, sizeof *ts);
	rpi3_mon_core1_attempt(); CHECK(ts->hold_count == 1 && ts->hold_max_ctx[RPI3_MON_TS_CTX_CORE1] == 60 && ts->att_count == 0 && ts->att_prev != 0);
	rpi3_mon_core1_attempt(); CHECK(ts->hold_count == 2 && ts->att_count == 1 && ts->att_min == ts->att_max && ts->att_sum == ts->att_min && ts->att_min >= 180);
	M.ext_hold = 1; rpi3_mon_core1_attempt(); CHECK(ts->hold_count == 2 && ts->att_count == 2); M.ext_hold = 0; end();		/* a failed attempt takes no hold but is an interval */
	begin("TS3 an exhausted PTA wait records the ticks waited and the attempts, bounded by the budget and the cap"); setup(); M.daif = MON_DAIF_ALL; M.tmode = 0; M.step = 60; memset(ts, 0, sizeof *ts);
	M.ext_hold = 1; rpi3_mon_wait_init(&w); CHECK(rpi3_mon_acquire(&w, &e) == RPI3_MON_BUSY);
	CHECK(ts->wait_exhausted == 1 && ts->wait_last_ticks > RPI3_MON_BUDGET_TICKS && ts->wait_last_ticks < 2 * RPI3_MON_BUDGET_TICKS && ts->wait_last_attempts > 0 && ts->wait_last_attempts <= RPI3_MON_ATTEMPT_CAP && ts->hold_count == 0); M.ext_hold = 0; end();
	begin("TS4 Core 1 hold request: the next successful attempt holds at least the requested ticks, once; the request is clamped to 10 ms"); setup(); M.daif = MON_DAIF_ALL; M.tmode = 0; M.step = 60; memset(ts, 0, sizeof *ts);
	CHECK(rpi3_mon_ts_ctrl(1, 6000, &done) == 0 && ts->hold_req_ticks == 6000 && done == 0); rpi3_mon_core1_attempt();
	CHECK(ts->hold_req_ticks == 0 && ts->hold_req_done == 1 && ts->hold_max_ctx[RPI3_MON_TS_CTX_CORE1] >= 6000 && ts->hold_max_ctx[RPI3_MON_TS_CTX_CORE1] < 6000 + 1000);
	rpi3_mon_core1_attempt(); CHECK(ts->hold_req_done == 1);												/* once */
	CHECK(rpi3_mon_ts_ctrl(1, 99999999ull, &done) == 0 && ts->hold_req_ticks == RPI3_MON_TS_HOLD_MAX_TICKS);
	CHECK(rpi3_mon_ts_ctrl(2, 0, &done) == 0 && rpi3_mon_ts_ctrl(7, 0, &done) == 1);									/* read-only op; unknown op */
	CHECK(rpi3_mon_ts_ctrl(0, 0, &done) == 0 && ts->hold_count == 0 && ts->hold_req_ticks == 0); end();
	begin("TS6 run-time cadence: the test command changes passes per lock attempt; invalid values are refused; 0 restores the default"); setup(); M.daif = MON_DAIF_ALL; frozen(5000000000ull); memset(ts, 0, sizeof *ts); rpi3_mon_ts_cadence_mask = 0; { uint32_t pass;
	  for (pass = 0; pass < 65536; pass++) rpi3_mon_core1_pass(pass); CHECK(M.trylock_calls == 4);							/* default 16384: 65536 / 16384 */
	  M.trylock_calls = 0; CHECK(rpi3_mon_ts_ctrl(3, 1024, &done) == 0 && rpi3_mon_ts_cadence_mask == 1023); for (pass = 0; pass < 4096; pass++) rpi3_mon_core1_pass(pass); CHECK(M.trylock_calls == 4);
	  M.trylock_calls = 0; CHECK(rpi3_mon_ts_ctrl(3, 16, &done) == 0); for (pass = 0; pass < 4096; pass++) rpi3_mon_core1_pass(pass); CHECK(M.trylock_calls == 256);
	  CHECK(rpi3_mon_ts_ctrl(3, 100, &done) == 1 && rpi3_mon_ts_ctrl(3, 8, &done) == 1 && rpi3_mon_ts_ctrl(3, 131072, &done) == 1 && rpi3_mon_ts_cadence_mask == 15);	/* refused, value unchanged */
	  CHECK(rpi3_mon_ts_ctrl(3, 0, &done) == 0 && rpi3_mon_ts_cadence_mask == 0); M.trylock_calls = 0; for (pass = 0; pass < 65536; pass++) rpi3_mon_core1_pass(pass); CHECK(M.trylock_calls == 4);
	  rpi3_mon_ts_cadence_mask = 1023; memset(p, 0, sizeof p); p[0].memref.buffer = buf; p[0].memref.size = sizeof buf; handled = false; r = rpi3_mon_pta_invoke(24, PTT(MEMREF_OUTPUT, NONE, NONE, NONE), p, &handled);
	  CHECK(r == TEE_SUCCESS && ((struct rpi3_mon_tstats *)(void *)buf)->cadence_mask == 1023); rpi3_mon_ts_cadence_mask = 0; } end();
	begin("TS5 test commands 24 and 25: exact param types, short buffer, copy of the statistics, and 27 stays unknown"); setup(); frozen(5000000000ull); memset(ts, 0, sizeof *ts); ts->hold_count = 7; ts->hold_max = 99;
	memset(p, 0, sizeof p); handled = false; p[0].memref.buffer = buf; p[0].memref.size = sizeof buf;
	r = rpi3_mon_pta_invoke(24, PTT(MEMREF_OUTPUT, NONE, NONE, NONE), p, &handled); CHECK(handled && r == TEE_SUCCESS && p[0].memref.size == sizeof *ts && ((struct rpi3_mon_tstats *)(void *)buf)->hold_count == 7 && ((struct rpi3_mon_tstats *)(void *)buf)->hold_max == 99);
	memset(p, 0, sizeof p); p[0].memref.buffer = buf; p[0].memref.size = 64; r = rpi3_mon_pta_invoke(24, PTT(MEMREF_OUTPUT, NONE, NONE, NONE), p, &handled); CHECK(r == TEE_ERROR_SHORT_BUFFER && p[0].memref.size == sizeof *ts);
	r = rpi3_mon_pta_invoke(24, PTT(VALUE_OUTPUT, NONE, NONE, NONE), p, &handled); CHECK(r == TEE_ERROR_BAD_PARAMETERS);
	memset(p, 0, sizeof p); p[0].value.a = 1; p[0].value.b = 1234; p[1].value.a = 0; r = rpi3_mon_pta_invoke(25, PTT(VALUE_INOUT, VALUE_INPUT, NONE, NONE), p, &handled); CHECK(r == TEE_SUCCESS && p[0].value.a == 0 && ts->hold_req_ticks == 1234);
	memset(p, 0, sizeof p); p[0].value.a = 9; r = rpi3_mon_pta_invoke(25, PTT(VALUE_INOUT, VALUE_INPUT, NONE, NONE), p, &handled); CHECK(r == TEE_ERROR_BAD_PARAMETERS);
	r = rpi3_mon_pta_invoke(25, PTT(VALUE_INOUT, NONE, NONE, NONE), p, &handled); CHECK(r == TEE_ERROR_BAD_PARAMETERS);
	r = rpi3_mon_pta_invoke(27, PTT(NONE, NONE, NONE, NONE), p, &handled); CHECK(handled && r == TEE_ERROR_NOT_SUPPORTED); end();
	begin("TS7 W1 command 26: lock memory attributes from the PTA core and from Core 1; exact param types, short buffer, Core 1 absent = TIMEOUT with the PTA half valid, request withdrawn"); setup();
	{ uint8_t wb[64]; struct rpi3_mon_w1 *w = (struct rpi3_mon_w1 *)(void *)wb; pthread_t c1; const uint64_t PAR = 0xFF00000012345000ULL | (3ULL << 7);
	  memset(p, 0, sizeof p); p[0].memref.buffer = wb; p[0].memref.size = 32; r = rpi3_mon_pta_invoke(26, PTT(MEMREF_OUTPUT, NONE, NONE, NONE), p, &handled); CHECK(handled && r == TEE_ERROR_SHORT_BUFFER && p[0].memref.size == 64);
	  r = rpi3_mon_pta_invoke(26, PTT(VALUE_OUTPUT, NONE, NONE, NONE), p, &handled); CHECK(r == TEE_ERROR_BAD_PARAMETERS);
	  memset(wb, 0xAA, sizeof wb); memset(p, 0, sizeof p); p[0].memref.buffer = wb; p[0].memref.size = 64; r = rpi3_mon_pta_invoke(26, PTT(MEMREF_OUTPUT, NONE, NONE, NONE), p, &handled);
	  CHECK(r == TEE_ERROR_TIMEOUT && w->va == (uint64_t)(uintptr_t)&rpi3_mon_lock && w->pta_par == PAR && w->pta_sctlr == 0x30D0199DULL && w->pta_mair == (0xFFULL | (0x04ULL << 8)) && w->c1_done == 0 && w->c1_par == 0 && rpi3_mon_ts_w1_req == 0);
	  stop_core1 = 0; pthread_create(&c1, NULL, core1_thr, NULL); memset(wb, 0xAA, sizeof wb); memset(p, 0, sizeof p); p[0].memref.buffer = wb; p[0].memref.size = 64;
	  r = rpi3_mon_pta_invoke(26, PTT(MEMREF_OUTPUT, NONE, NONE, NONE), p, &handled); stop_core1 = 1; pthread_join(c1, NULL);
	  CHECK(r == TEE_SUCCESS && w->c1_done == 1 && w->c1_par == PAR && w->c1_sctlr == 0x30D0199DULL && w->c1_mair == (0xFFULL | (0x04ULL << 8)) && w->pta_par == PAR && rpi3_mon_ts_w1_req == 0 && !broken); }
	end();
	begin("TS8 W7 forced Core 1 panic: refused without the exact magic; armed once; Core 1 panics at its next attempt before taking the lock; one panic only; unarmed Core 1 never panics"); setup(); M.daif = MON_DAIF_ALL; frozen(5000000000ull);
	{ uint32_t done = 0, pass; unsigned tl;
	  for (pass = 0; pass < 65536; pass++) rpi3_mon_core1_pass(pass); CHECK(M.panics == 0 && rpi3_mon_ts_w7_req == 0);			/* never armed: no panic */
	  CHECK(rpi3_mon_ts_ctrl(4, 0, &done) == 1 && rpi3_mon_ts_ctrl(4, 1, &done) == 1 && rpi3_mon_ts_ctrl(4, RPI3_MON_TS_W7_MAGIC + 1U, &done) == 1 && rpi3_mon_ts_w7_req == 0);	/* wrong magic refused */
	  CHECK(rpi3_mon_ts_ctrl(4, RPI3_MON_TS_W7_MAGIC, &done) == 0 && rpi3_mon_ts_w7_req == RPI3_MON_TS_W7_MAGIC && M.panics == 0);	/* armed, nothing yet */
	  tl = M.trylock_calls; rpi3_mon_core1_attempt(); CHECK(M.panics == 1 && rpi3_mon_ts_w7_req == 0 && M.trylock_calls == tl + 1U && M.violations == 0);	/* panic requested before the lock; request consumed (the mock panic returns) */
	  for (pass = 0; pass < 65536; pass++) rpi3_mon_core1_pass(pass); CHECK(M.panics == 1); }									/* exactly one */
	end();
	begin("TS9 stack watermark: not ready reads 0; the fill covers exactly [lo, hi) and nothing above; unchanged stack reports the fill depth; touching a lower word moves the figure; a second fill does not erase it; ops 5/6/8 only"); setup();
	{ uint32_t done = 0, i; uint32_t *w = (uint32_t *)(void *)mock_tmp_stack; memset(mock_tmp_stack, 0x11, sizeof mock_tmp_stack); rpi3_mon_ts_stk_ready = 0;
	  CHECK(rpi3_mon_ts_ctrl(5, 0, &done) == 0 && done == 0 && rpi3_mon_ts_ctrl(6, 0, &done) == 0 && done == 0 && rpi3_mon_ts_ctrl(8, 0, &done) == 0 && done == 0);	/* not filled yet */
	  rpi3_mon_ts_stack_fill_core1(); CHECK(rpi3_mon_ts_stk_ready == 1 && rpi3_mon_ts_stk_top == (uint64_t)(uintptr_t)(mock_tmp_stack + 4096) && rpi3_mon_ts_stk_lo == (uint64_t)(uintptr_t)mock_tmp_stack && rpi3_mon_ts_stk_hi == (uint64_t)(uintptr_t)(mock_tmp_stack + 3072));
	  for (i = 0; i < 768; i++) CHECK(w[i] == RPI3_MON_TS_STK_PATTERN);						/* [0, 3072) filled */
	  for (i = 768; i < 1024; i++) CHECK(w[i] == 0x11111111u);							/* [3072, 4096) untouched */
	  CHECK(rpi3_mon_ts_ctrl(5, 0, &done) == 0 && done == 1024 && rpi3_mon_ts_ctrl(6, 0, &done) == 0 && done == 4096 && rpi3_mon_ts_ctrl(8, 0, &done) == 0 && done == 1024);	/* nothing used below the fill point */
	  w[500] = 0xDEADBEEFu; CHECK(rpi3_mon_ts_ctrl(5, 0, &done) == 0 && done == 4096 - 500 * 4);			/* the word at byte 2000 was touched: 2096 bytes used */
	  w[300] = 0x12345678u; CHECK(rpi3_mon_ts_ctrl(5, 0, &done) == 0 && done == 4096 - 300 * 4);						/* lower word: deeper */
	  RPI3_MON_TS_STACK_CORE1(); CHECK(w[300] == 0x12345678u);								/* the hook fills only while not ready: no second fill */
	  CHECK(rpi3_mon_ts_ctrl(7, 0, &done) == 1 && rpi3_mon_ts_ctrl(9, 0, &done) == 1); }					/* ops 7 and 9 stay unknown */
	end();
}
#endif

/* Plan C5 acceptance: a Core 1 stall of 3H gives exactly one MISS, one new challenge (no catch-up burst) and no MON_STARVED. */
static void t_stall(void)
{
	uint32_t e; uint64_t t; int i;
	begin("C5 stall of 3H: one MISS, one new issue, no burst, no MON_STARVED"); setup(); M.daif = MON_DAIF_ALL; frozen(9000000000000ull); t = M.now;
	rpi3_mon.init_ticks = t - RPI3_MON_G_TICKS - RPI3_MON_TICKS_PER_SEC;				/* the boot grace has passed */
	e = take(); rpi3_mon_l()->hb_last_ok_ticks = 1; rpi3_mon_hb_issue_if_due(rpi3_mon_l(), M.now); put(e);
	CHECK(rpi3_mon.l.hb_seq == 1 && rpi3_mon.l.hb_state == RPI3_MON_HB_OPEN && rpi3_mon.l.hb_miss_count == 0);
	M.now += 3 * RPI3_MON_H_TICKS;								/* Core 1 did not run for three challenge periods */
	rpi3_mon_core1_attempt();
	CHECK(rpi3_mon.l.hb_miss_count == 1 && rpi3_mon.l.hb_seq == 2 && rpi3_mon.l.hb_state == RPI3_MON_HB_OPEN);
	CHECK(rpi3_mon.l.ev_count[RPI3_MON_EV_MON_STARVED] == 0 && rpi3_mon.health.skip_count == 0 && !rpi3_mon.health.starved);
	CHECK(rpi3_mon.l.hb_next_issue == M.now + RPI3_MON_H_TICKS);					/* next issue is H after NOW, not three catch-up issues */
	for (i = 0; i < 3; i++) rpi3_mon_core1_attempt();							/* the same instant: nothing more is issued or counted */
	CHECK(rpi3_mon.l.hb_seq == 2 && rpi3_mon.l.hb_miss_count == 1 && rpi3_mon.l.ev_count[RPI3_MON_EV_TIMEOUT] == 0);	/* one miss is below k = 3 */
	end();
}


#ifdef CFG_RPI3_CAN_MONITOR_UART
/* ---- C6: UART ALIVE line. The harness drives the real Core 1 attempt (which takes the snapshot inside its lock) and the real service. */
static void uart_reset(int mode) { memset(&MU, 0, sizeof MU); MU.map_result = 0x3f215040u; MU.mode = mode; }
static void uart_drain_all(void) { for (int i = 0; i < 100; i++) rpi3_mon_uart_service(); }
static const char *uart_text(void) { static char b[4097]; unsigned n = MU.puts < 4096 ? MU.puts : 4096; memcpy(b, MU.out, n); b[n] = 0; return b; }
static void t_uart(void)
{
	uint64_t t; uint32_t e; struct rpi3_mon_lstate *l;

	begin("U1 ALIVE line text (exact fields), written only through the mock UART"); uart_reset(0); setup(); M.daif = MON_DAIF_ALL; frozen(5000000000ull); t = M.now;
	rpi3_mon_store_u64(&rpi3_mon.health.last_poll_ticks, t - 19200);	/* 1000 us ago */
	rpi3_mon_core1_attempt(); rpi3_mon_uart_service(); uart_drain_all();
	CHECK(strncmp(uart_text(), "ALIVE n=1 poll_age_us=1000 ev=0 lost=0 hb=OPEN miss=0 tr=--- drop=0\r\n", 70) == 0);
	if (strncmp(uart_text(), "ALIVE n=1 poll_age_us=1000 ev=0 lost=0 hb=OPEN miss=0 tr=--- drop=0\r\n", 70)) printf("     got: %s", uart_text());
	CHECK(MU.puts == strlen("ALIVE n=1 poll_age_us=1000 ev=0 lost=0 hb=OPEN miss=0 tr=--- drop=0\r\n")); end();

	begin("U2 never blocks: transmitter never ready -> no byte written, 1,000,000 service calls return, text stays queued"); uart_reset(1); setup(); M.daif = MON_DAIF_ALL; frozen(5000000000ull);
	rpi3_mon_core1_attempt(); for (int i = 0; i < 1000000; i++) rpi3_mon_uart_service();
	CHECK(MU.puts == 0 && MU.lsr_reads >= 1 && MU.lsr_reads <= 1000000); end();

	begin("U3 bounded burst: at most 8 bytes per service call, whole line comes out intact"); uart_reset(0); setup(); M.daif = MON_DAIF_ALL; frozen(5000000000ull);
	rpi3_mon_core1_attempt(); rpi3_mon_uart_service(); CHECK(MU.puts == 8); rpi3_mon_uart_service(); CHECK(MU.puts == 16);
	uart_drain_all(); CHECK(MU.puts > 16 && strstr(uart_text(), "ALIVE n=1 ") == uart_text() && strstr(uart_text(), "\r\n") != NULL); end();

	begin("U4 room limits the burst: mini-UART style FIFO (room 3) -> exactly 3 bytes, no spin"); uart_reset(2); MU.room = 3; setup(); M.daif = MON_DAIF_ALL; frozen(5000000000ull);
	rpi3_mon_core1_attempt(); rpi3_mon_uart_service(); CHECK(MU.puts == 3 && MU.room == 0); rpi3_mon_uart_service(); CHECK(MU.puts == 3); end();

	begin("U5 once per second: no second snapshot inside the second; second line n=2 after it"); uart_reset(0); setup(); M.daif = MON_DAIF_ALL; frozen(5000000000ull); t = M.now;
	rpi3_mon_core1_attempt(); uart_drain_all(); unsigned n1 = MU.puts;
	M.now = t + RPI3_MON_TICKS_PER_SEC - 1; rpi3_mon_core1_attempt(); uart_drain_all(); CHECK(MU.puts == n1);
	M.now = t + RPI3_MON_TICKS_PER_SEC; rpi3_mon_core1_attempt(); uart_drain_all(); CHECK(MU.puts > n1 && strstr(uart_text() + n1, "ALIVE n=2 ") == uart_text() + n1); end();

	begin("U6 a line not yet drained: the next second is skipped and counted (drop=1 in the following line)"); uart_reset(1); setup(); M.daif = MON_DAIF_ALL; frozen(5000000000ull); t = M.now;
	rpi3_mon_core1_attempt(); rpi3_mon_uart_service();			/* line formatted, transmitter never ready: stays queued */
	M.now = t + RPI3_MON_TICKS_PER_SEC; rpi3_mon_core1_attempt();	/* dropped */
	MU.mode = 0; uart_drain_all(); M.now = t + 2 * RPI3_MON_TICKS_PER_SEC; rpi3_mon_core1_attempt(); uart_drain_all();
	CHECK(strstr(uart_text(), "ALIVE n=2 ") != NULL && strstr(uart_text(), "drop=1") != NULL); end();

	begin("U7 events: at most 3 per line, oldest first, none repeated, then the rest the next second"); uart_reset(0); setup(); M.daif = MON_DAIF_ALL; frozen(5000000000ull); t = M.now;
	e = take(); l = rpi3_mon_l(); rpi3_mon_ev_append(l, RPI3_MON_EV_AUTH_FAIL, 1, 0, t); rpi3_mon_ev_append(l, RPI3_MON_EV_REPLAY, 2, 0, t); rpi3_mon_ev_append(l, RPI3_MON_EV_TIMEOUT, 0, 1, t);
	rpi3_mon_ev_append(l, RPI3_MON_EV_ALTERED, 0, 7, t); rpi3_mon_ev_append(l, RPI3_MON_EV_UNACCOUNTED, 0, 9, t); put(e);
	rpi3_mon_core1_attempt(); uart_drain_all();
	CHECK(strstr(uart_text(), "EV seq=1 AUTH_FAIL d=0\r\n") && strstr(uart_text(), "EV seq=2 REPLAY d=0\r\n") && strstr(uart_text(), "EV seq=3 TIMEOUT d=1\r\n") && !strstr(uart_text(), "EV seq=4"));
	unsigned n2 = MU.puts; M.now = t + RPI3_MON_TICKS_PER_SEC; rpi3_mon_core1_attempt(); uart_drain_all();
	CHECK(strstr(uart_text() + n2, "EV seq=4 ALTERED d=7\r\n") && strstr(uart_text() + n2, "EV seq=5 UNACCOUNTED d=9\r\n") && !strstr(uart_text() + n2, "EV seq=3") && strstr(uart_text() + n2, " ev=5 ")); end();

	begin("U8 no mapping: the feature stays off, no byte, monitor state untouched"); uart_reset(0); MU.map_result = 0; setup(); M.daif = MON_DAIF_ALL; frozen(5000000000ull);
	rpi3_mon_core1_attempt(); for (int i = 0; i < 100; i++) rpi3_mon_uart_service();
	e = take(); l = rpi3_mon_l(); CHECK(MU.puts == 0 && MU.lsr_reads == 0 && rpi3_mon_init_ok() && l->hb_state == RPI3_MON_HB_OPEN); put(e); end();

	begin("U10 decimal output edge values: 0, 10, 100, 1000000000, 4294967295 (no array, most significant digit first)");
	{ const uint32_t vals[5] = { 0u, 10u, 100u, 1000000000u, 4294967295u }; int allok = 1;
	  for (int i = 0; i < 5; i++) { char want[64], want2[64]; uart_reset(0); setup(); M.daif = MON_DAIF_ALL; frozen(5000000000ull);
	    e = take(); l = rpi3_mon_l(); l->ev_lost = vals[i]; l->hb_miss_count = vals[4 - i]; put(e);
	    rpi3_mon_core1_attempt(); uart_drain_all();
	    snprintf(want, sizeof want, " lost=%u ", vals[i]); snprintf(want2, sizeof want2, " miss=%u ", vals[4 - i]);
	    if (!strstr(uart_text(), want) || !strstr(uart_text(), want2)) { allok = 0; printf("     value %u: got %s", vals[i], uart_text()); } }
	  CHECK(allok); } end();

	begin("U9 the snapshot is taken inside the lock hold and changes no monitor state"); uart_reset(0); setup(); M.daif = MON_DAIF_ALL; frozen(5000000000ull); t = M.now;
	rpi3_mon_core1_attempt();
	e = take(); l = rpi3_mon_l(); CHECK(l->ev_seq == 0 && l->hb_miss_count == 0 && l->hb_seq == 1); put(e); CHECK(M.violations == 0); end();
}
#endif

/* ---- U1 (2026-11-12): C5 acceptance gaps found in the audit. Test-only: no source change.
 * T11b exact deadline boundary (strict >, R17/O-11); T11a answer handler does the OPEN->MISSED step itself and Core 1 adds no second
 * miss afterwards; T10b (plan meaning, audit O-10): misses already above k when the grace period ends still raise TIMEOUT once. */
static void t_c5_gaps(void)
{
	uint32_t oa, ob; struct rpi3_mon_health_out h; uint32_t e; uint64_t t, N = 0x1122334455667788ull; uint32_t lo = (uint32_t)N, hi = (uint32_t)(N >> 32);
	struct rpi3_mon_lstate *l;

	begin("T11b answer exactly at issue+D_req is on time; one tick later is late (Core 1 and the answer handler use the same strict boundary)");
	setup(); M.daif = MON_DAIF_ALL; frozen(5000000000ull); t = M.now;
	e = take(); l = rpi3_mon_l(); rpi3_mon_hb_issue_if_due(l, t); l->hb_nonce = N; l->hb_nonce_valid = true; CHECK(l->hb_issue_ticks == t && l->hb_state == RPI3_MON_HB_OPEN);
	rpi3_mon_evaluate(l, t + RPI3_MON_D_REQ_TICKS); CHECK(l->hb_state == RPI3_MON_HB_OPEN && l->hb_miss_count == 0);			/* Core 1 at exactly D_req: still open */
	rpi3_mon_evaluate(l, t + RPI3_MON_D_REQ_TICKS + 1); CHECK(l->hb_state == RPI3_MON_HB_MISSED && l->hb_miss_count == 1); put(e);	/* one tick later: the miss */
	setup(); M.daif = MON_DAIF_ALL; frozen(5000000000ull); t = M.now;
	e = take(); l = rpi3_mon_l(); rpi3_mon_hb_issue_if_due(l, t); l->hb_nonce = N; l->hb_nonce_valid = true; put(e);
	M.now = t + RPI3_MON_D_REQ_TICKS;
	CHECK(rpi3_mon_cmd23(1, 0, lo, hi, &oa, &ob, &h) == RPI3_MON_R_OK && oa == RPI3_MON_V23_ON_TIME && rpi3_mon.l.hb_state == RPI3_MON_HB_ANSWERED && rpi3_mon.l.hb_miss_count == 0 && rpi3_mon.l.ev_count[RPI3_MON_EV_LATE_ANSWER] == 0);
	setup(); M.daif = MON_DAIF_ALL; frozen(5000000000ull); t = M.now;
	e = take(); l = rpi3_mon_l(); rpi3_mon_hb_issue_if_due(l, t); l->hb_nonce = N; l->hb_nonce_valid = true; put(e);
	M.now = t + RPI3_MON_D_REQ_TICKS + 1;
	CHECK(rpi3_mon_cmd23(1, 0, lo, hi, &oa, &ob, &h) == RPI3_MON_R_OK && oa == RPI3_MON_V23_LATE && rpi3_mon.l.hb_state == RPI3_MON_HB_LATE_ANSWERED && rpi3_mon.l.hb_miss_count == 1 && rpi3_mon.l.ev_count[RPI3_MON_EV_LATE_ANSWER] == 1);
	end();

	begin("T11a deadline race: Core 1 has not looked yet; the answer handler records the one miss, then LATE; Core 1 later adds no second miss");
	setup(); M.daif = MON_DAIF_ALL; frozen(5000000000ull); t = M.now;
	e = take(); l = rpi3_mon_l(); rpi3_mon_hb_issue_if_due(l, t); l->hb_nonce = N; l->hb_nonce_valid = true; CHECK(l->hb_state == RPI3_MON_HB_OPEN && l->hb_miss_count == 0); put(e);
	M.now = t + RPI3_MON_D_REQ_TICKS + 1;										/* deadline passed, no Core 1 attempt has run */
	CHECK(rpi3_mon_cmd23(1, 0, lo, hi, &oa, &ob, &h) == RPI3_MON_R_OK && oa == RPI3_MON_V23_LATE && rpi3_mon.l.hb_miss_count == 1 && rpi3_mon.l.ev_count[RPI3_MON_EV_LATE_ANSWER] == 1);
	M.now = t + RPI3_MON_D_REQ_TICKS + 1000; rpi3_mon_core1_attempt(); M.now += 1000; rpi3_mon_core1_attempt();	/* Core 1 now evaluates, twice */
	e = take(); l = rpi3_mon_l();
	CHECK(l->hb_miss_count == 1 && l->hb_state == RPI3_MON_HB_LATE_ANSWERED && !l->hb_timeout_raised && l->ev_count[RPI3_MON_EV_TIMEOUT] == 0 && l->ev_count[RPI3_MON_EV_LATE_ANSWER] == 1); put(e);
	CHECK(M.violations == 0); end();

	begin("T10b (audit O-10): miss_count already above k when the grace period ends still raises TIMEOUT detail 2, exactly once");
	setup(); M.daif = MON_DAIF_ALL; frozen(9000000000000ull); t = M.now;
	rpi3_mon.init_ticks = t - RPI3_MON_G_TICKS + 10 * RPI3_MON_TICKS_PER_SEC;						/* the grace period ends 10 s from now */
	e = take(); l = rpi3_mon_l(); l->hb_last_ok_ticks = 1; l->hb_miss_count = 55;						/* answered once, then 55 misses: far above k = 3 */
	rpi3_mon_evaluate(l, t); CHECK(!l->hb_timeout_raised && l->ev_count[RPI3_MON_EV_TIMEOUT] == 0);			/* inside the grace period: nothing */
	rpi3_mon_evaluate(l, t + 11 * RPI3_MON_TICKS_PER_SEC); CHECK(l->hb_timeout_raised && l->ev_count[RPI3_MON_EV_TIMEOUT] == 1 && l->pinned[RPI3_MON_EV_TIMEOUT * 4].detail == 2);
	rpi3_mon_evaluate(l, t + 12 * RPI3_MON_TICKS_PER_SEC); rpi3_mon_evaluate(l, t + 40 * RPI3_MON_TICKS_PER_SEC); CHECK(l->ev_count[RPI3_MON_EV_TIMEOUT] == 1);	/* never repeated */
	put(e); end();
}

int main(void)
{
	printf("Real-source host harness: rpi3_can_monitor.c + the PTA glue with a mock platform (NOT a board test)\n");
	t_vectors(); t_init(); t_layout(); t_locks(); t_health(); t_events(); t_eval(); t_c17(); t_c18(); t_c19(); t_c20(); t_c21(); t_c22(); t_c23();
	t_glue(); t_glue_cmds(); t_core1(); t_stall(); t_c5_gaps();
#ifdef CFG_RPI3_CAN_MONITOR_UART
	t_uart();
#endif
#ifdef CFG_RPI3_CAN_MONITOR_TEST_STATS
	t_tstats();
#endif
	t_stress(); t_stress_guard();
	printf("total: %d checks, %d failed\n", nchk, nfail);
	return nfail != 0;
}
