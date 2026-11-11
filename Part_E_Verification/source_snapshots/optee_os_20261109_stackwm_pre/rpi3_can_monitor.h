/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Secure CAN Monitor: shared state, synchronisation and health (Component 2.2, stage 1).
 *
 * Authority: Part_E_Verification/monitor/MONITOR_STATE_DESIGN.md (sections 4a to 4k) and
 * INTERFACE_DECISION_REGISTER.md. It holds the shared state, the lock, bounded PTA lock acquisition, the Core 1
 * lock attempt, the exactly-once evaluation, the event ring, and the logic of PTA commands 17 to 23. The PTA glue
 * that maps TEE_Param to these functions is core/pta/rpi3_can_monitor_pta.c.
 *
 * Ownership classes used below (design section 3.2):
 *   C = constant after init (written by rpi3_mon_init on the primary core before Linux and Core 1 run)
 *   L = read and written only while holding the monitor lock
 *   A = lock-free atomic access through the accessors below (G7 words and pta_lock_timeouts)
 */
#ifndef __RPI3_CAN_MONITOR_H
#define __RPI3_CAN_MONITOR_H

#include <rpi3_can_monitor_plat.h>

/* ---- provisional constants (design 4k K11; Component 4 measures the real values) ---- */
#define RPI3_MON_TICKS_PER_MS		19200ULL
#define RPI3_MON_TICKS_PER_SEC		19200000ULL
#define RPI3_MON_BUDGET_TICKS		19200ULL	/* PTA lock-wait budget per command: 1 ms */
#define RPI3_MON_ATTEMPT_CAP		2000U		/* PTA trylock attempts per command (provisional) */
#define RPI3_MON_T_TRAFFIC_MS		300ULL
#define RPI3_MON_H_MS			1000ULL
#define RPI3_MON_D_REQ_MS		500ULL
#define RPI3_MON_G_MS			60000ULL
#define RPI3_MON_K_MISS			3U
#define RPI3_MON_K_STARVE		8U
#define RPI3_MON_IDLE_MAX_MS		60000U
#define RPI3_MON_IDLE_USES_MAX		16U
#define RPI3_MON_SENDER_PERIOD_MS	100ULL
#define RPI3_MON_SENDER_JITTER_MS	50ULL

#define RPI3_MON_T_TRAFFIC_TICKS	(RPI3_MON_T_TRAFFIC_MS * RPI3_MON_TICKS_PER_MS)
#define RPI3_MON_H_TICKS		(RPI3_MON_H_MS * RPI3_MON_TICKS_PER_MS)
#define RPI3_MON_D_REQ_TICKS		(RPI3_MON_D_REQ_MS * RPI3_MON_TICKS_PER_MS)
#define RPI3_MON_G_TICKS		(RPI3_MON_G_MS * RPI3_MON_TICKS_PER_MS)
#define RPI3_MON_FRAME_LEN		44U		/* can_id(4) | counter(4) | payload(4) | tag(32), no padding */
#define RPI3_MON_STREAM_CAN_ID		0x123U		/* the single monitored stream (design K2) */
#define RPI3_MON_CAN_ID_MAX		0x1FFFFFFFU
#define RPI3_MON_CORE1_CADENCE		16384U		/* one lock attempt per 16384 Core 1 passes (about 0.6 ms at the measured 75 ns/pass + attempt cost; chosen from board data 2026-11-09) */
#ifdef CFG_RPI3_CAN_MONITOR_TEST_STATS
/* TEST BUILDS ONLY: the cadence can be changed at run time through test command 25 (0 = the default above). */
extern volatile uint32_t rpi3_mon_ts_cadence_mask;
#define RPI3_MON_CORE1_MASK()		(rpi3_mon_ts_cadence_mask ? rpi3_mon_ts_cadence_mask : (RPI3_MON_CORE1_CADENCE - 1U))
#else
#define RPI3_MON_CORE1_MASK()		(RPI3_MON_CORE1_CADENCE - 1U)
#endif

_Static_assert(RPI3_MON_D_REQ_MS < RPI3_MON_H_MS, "TM1: only one heartbeat request may be outstanding (D_req < H)");
_Static_assert(RPI3_MON_T_TRAFFIC_MS > RPI3_MON_SENDER_PERIOD_MS + RPI3_MON_SENDER_JITTER_MS,
	       "TM1: the traffic timeout must exceed the sender period plus jitter");
_Static_assert(RPI3_MON_BUDGET_TICKS == 1 * RPI3_MON_TICKS_PER_MS, "the approved initial PTA lock budget is 1 ms");

/* ---- events (design 4k K10): 6 evidence classes, then 19 diagnostic classes ---- */
enum rpi3_mon_event_type {
	RPI3_MON_EV_AUTH_FAIL = 0, RPI3_MON_EV_REPLAY, RPI3_MON_EV_TIMEOUT,
	RPI3_MON_EV_ALTERED, RPI3_MON_EV_UNACCOUNTED, RPI3_MON_EV_CHIPB_FAULT,
	RPI3_MON_EV_EVIDENCE_END,
	RPI3_MON_EV_MALFORMED = 6, RPI3_MON_EV_COUNTER_GAP, RPI3_MON_EV_COUNTER_RESYNC,
	RPI3_MON_EV_TRAFFIC_RECOVERED, RPI3_MON_EV_HB_RECOVERED, RPI3_MON_EV_MODE_CHANGED,
	RPI3_MON_EV_LEASE_GRANTED, RPI3_MON_EV_LEASE_RENEWED, RPI3_MON_EV_LEASE_REFUSED,
	RPI3_MON_EV_LOCK_TIMEOUT, RPI3_MON_EV_MON_STARVED, RPI3_MON_EV_HB_SEQ_EXHAUSTED,
	RPI3_MON_EV_LEGACY_REFUSED, RPI3_MON_EV_LEGACY_USED, RPI3_MON_EV_BAD_NONCE,
	RPI3_MON_EV_STALE_ANSWER, RPI3_MON_EV_UNISSUED_ANSWER, RPI3_MON_EV_DUP_ANSWER,
	RPI3_MON_EV_LATE_ANSWER, RPI3_MON_EV_COUNT
};
#define RPI3_MON_EV_N_EVIDENCE		6U
#define RPI3_MON_PINNED_PER_CLASS	4U
#define RPI3_MON_PINNED_SLOTS		(RPI3_MON_EV_N_EVIDENCE * RPI3_MON_PINNED_PER_CLASS)	/* 24 */
#define RPI3_MON_ORDINARY_SLOTS		40U
_Static_assert((unsigned int)RPI3_MON_EV_EVIDENCE_END == RPI3_MON_EV_N_EVIDENCE, "EV1: six evidence classes");
_Static_assert((unsigned int)RPI3_MON_EV_COUNT == 25U, "EV1: 25 event types");

struct rpi3_mon_event {			/* 24 bytes */
	uint32_t seq; uint16_t type; uint16_t flags;
	uint64_t ticks; uint32_t counter; uint32_t detail;
};
_Static_assert(sizeof(struct rpi3_mon_event) == 24, "event record is 24 bytes");
_Static_assert(5 * sizeof(struct rpi3_mon_event) <= 128, "Q2: five records fit the 128-byte copy limit (CS3, H5)");

enum rpi3_mon_hb_state { RPI3_MON_HB_NONE = 0, RPI3_MON_HB_OPEN, RPI3_MON_HB_ANSWERED, RPI3_MON_HB_MISSED,
			 RPI3_MON_HB_LATE_ANSWERED };
enum rpi3_mon_profile { RPI3_MON_PROFILE_HARDENED = 0, RPI3_MON_PROFILE_DEBUG = 1 };
enum rpi3_mon_mode { RPI3_MON_MODE_F = 0, RPI3_MON_MODE_D2 = 1 };

struct rpi3_mon_verdict { uint8_t last_type; uint64_t last_ticks; uint32_t total; };
struct rpi3_mon_ctr {
	bool have_last; uint32_t last_counter;
	uint8_t last_frame44[44];		/* exactly the 44-byte wire message, no padding */
	uint64_t last_accept_ticks;
};

/* Class L: every member below is touched only while holding the monitor lock (design rule R-A1). */
struct rpi3_mon_lstate {
	uint8_t mode;				/* G1: written by rpi3_mon_init only (S5) */
	struct rpi3_mon_ctr ctr_f;		/* G3 */
	struct rpi3_mon_verdict verdict_frame, verdict_hb, verdict_xcheck;	/* G4 */
	uint32_t ev_count[RPI3_MON_EV_COUNT];	/* G4/G8: exact per-type counters */
	bool tr_armed; uint64_t tr_last_valid_ticks; bool tr_timeout_raised;	/* G5 */
	uint64_t idle_until_ticks; uint32_t idle_uses; uint64_t last_eval_ticks;
	uint32_t hb_seq; uint64_t hb_issue_ticks; uint64_t hb_nonce; bool hb_nonce_valid;	/* G6 */
	uint8_t hb_state; uint64_t hb_last_ok_ticks; uint32_t hb_miss_count; bool hb_timeout_raised;
	uint64_t hb_next_issue;
	struct rpi3_mon_event pinned[RPI3_MON_PINNED_SLOTS];	/* G8 */
	uint8_t pinned_n[RPI3_MON_EV_N_EVIDENCE];
	struct rpi3_mon_event ordinary[RPI3_MON_ORDINARY_SLOTS];
	uint8_t ordinary_used[RPI3_MON_ORDINARY_SLOTS];
	uint32_t ordinary_head;
	uint64_t diag_last_ticks[RPI3_MON_EV_COUNT];	/* G8-6: one entry per event type */
	uint8_t diag_have[RPI3_MON_EV_COUNT];
	uint32_t ev_seq, ev_ack, ev_lost, pinned_used;
	uint32_t pta_timeouts_seen;		/* S2: lock-protected */
};

/* G7 health: lock-free, ONE writer (Core 1). 64-byte aligned and a multiple of 64 bytes, so it shares no cache
 * line with lock-protected data (design 4a, rule 4). Access only through the accessors below. */
struct rpi3_mon_health {
	uint64_t poll_count;
	uint64_t last_poll_ticks;
	uint32_t skip_count;
	uint32_t skip_total;
	uint32_t starved;			/* 0 or 1, not bool */
	uint32_t reserved[9];
} __aligned(64);

struct rpi3_mon_state {
	struct { unsigned int lock; uint8_t pad[60]; } lockline __aligned(64);	/* G9: the one lock, on its own line */
	uint32_t init_ok;			/* C: published LAST by rpi3_mon_init (release store) */
	uint32_t boot_id;			/* C: set by a later stage (DEC-IF-15); 0 in stage 1 */
	uint64_t init_ticks;			/* C: secure-timer value at init */
	uint8_t profile;			/* C: enum rpi3_mon_profile */
	void *mac_pta;				/* C: G2 contexts, allocated by a later stage; NULL in stage 1 */
	void *mac_core1;
	uint32_t mac_pta_busy;			/* A: in-use guard for mac_pta (4d S3), used by a later stage */
	struct rpi3_mon_lstate l;		/* L */
	struct rpi3_mon_health health;		/* A: G7 */
	struct { uint32_t pta_lock_timeouts; uint8_t pad[60]; } timeouts __aligned(64);	/* A: multi-writer (4d S1), own line */
} __aligned(64);

/* The state is exported (not static) only because the Core 1 loop (a later stage) must update G7 inline. Every
 * other file uses the functions below. */
extern struct rpi3_mon_state rpi3_mon;
#define rpi3_mon_lock		(rpi3_mon.lockline.lock)
#define rpi3_mon_pta_lock_timeouts	(rpi3_mon.timeouts.pta_lock_timeouts)

#define RPI3_MON_CACHE_LINE	64U
_Static_assert(sizeof(long) == 8, "G7: 64-bit words");
_Static_assert(_Alignof(uint64_t) == 8, "G7: natural 64-bit alignment");
_Static_assert(offsetof(struct rpi3_mon_state, health) % RPI3_MON_CACHE_LINE == 0, "G7 starts on a cache line");
_Static_assert(sizeof(struct rpi3_mon_health) % RPI3_MON_CACHE_LINE == 0, "G7 is padded to whole cache lines");
_Static_assert(offsetof(struct rpi3_mon_health, poll_count) % 8 == 0 &&
	       offsetof(struct rpi3_mon_health, last_poll_ticks) % 8 == 0, "G7 64-bit words are 8-byte aligned");
_Static_assert(offsetof(struct rpi3_mon_state, lockline) % RPI3_MON_CACHE_LINE == 0 &&
	       sizeof(rpi3_mon.lockline) == RPI3_MON_CACHE_LINE, "V1/G9: the lock is alone on its cache line");
_Static_assert(offsetof(struct rpi3_mon_state, timeouts) % RPI3_MON_CACHE_LINE == 0 &&
	       sizeof(rpi3_mon.timeouts) == RPI3_MON_CACHE_LINE, "V1: pta_lock_timeouts is alone on its cache line");
_Static_assert(offsetof(struct rpi3_mon_state, health) + sizeof(struct rpi3_mon_health) <=
	       offsetof(struct rpi3_mon_state, timeouts), "G7 and the timeout counter do not share a line");

/* ---- G7 accessors: forced inline and hook-free; a plain read of these words is a review failure (design 4a) ---- */
static inline __attribute__((always_inline)) __nostackcheck
uint64_t rpi3_mon_load_u64(const uint64_t *p) { return MON_PLAT_LOAD64(p); }
static inline __attribute__((always_inline)) __nostackcheck
void rpi3_mon_store_u64(uint64_t *p, uint64_t v) { MON_PLAT_STORE64(p, v); }
static inline __attribute__((always_inline)) __nostackcheck
uint32_t rpi3_mon_load_u32(const uint32_t *p) { return MON_PLAT_LOAD32(p); }
static inline __attribute__((always_inline)) __nostackcheck
void rpi3_mon_store_u32(uint32_t *p, uint32_t v) { MON_PLAT_STORE32(p, v); }

/* Lock-protected state: the caller MUST hold the monitor lock (checked by the host mock, free in OP-TEE). */
static inline __attribute__((always_inline)) __nostackcheck
struct rpi3_mon_lstate *rpi3_mon_l(void) { MON_PLAT_LOCK_HELD_CHECK(); return &rpi3_mon.l; }

/* ---- bounded PTA lock acquisition (design 4f) ---- */
enum rpi3_mon_rc { RPI3_MON_OK = 0, RPI3_MON_BUSY = 1 };

/* One of these per PTA command, shared by every acquisition in the command (B1). */
struct rpi3_mon_wait { uint64_t wait_ticks_used; uint32_t attempts_used; };

/* The health pair that every monitor command returns in slots 2 and 3 (design 4k K1). */
struct rpi3_mon_health_out { uint32_t p2a, p2b, p3a, p3b; };
#define RPI3_MON_HP_STARVED	0x1U
#define RPI3_MON_HP_INIT_OK	0x2U
#define RPI3_MON_HP_SKIP_SHIFT	8
#define RPI3_MON_HP_AGE_NEVER	0xFFFFFFFFU

TEE_Result rpi3_mon_init(void);			/* registered with driver_init; never fails the OS boot */
bool rpi3_mon_init_ok(void);
void rpi3_mon_wait_init(struct rpi3_mon_wait *w);
/* RPI3_MON_OK: lock held and exceptions masked; give *exc to rpi3_mon_release(). RPI3_MON_BUSY: budget, attempt cap
 * or timer fault; the lock was NOT taken, exceptions are restored and pta_lock_timeouts was incremented once. */
enum rpi3_mon_rc rpi3_mon_acquire(struct rpi3_mon_wait *w, uint32_t *exc);
void rpi3_mon_release(uint32_t exc);		/* unlock first, then restore exceptions */
void rpi3_mon_health_fill(struct rpi3_mon_health_out *h);	/* lock-free reads only; call after unlock or unmask */
void rpi3_mon_health_zero(struct rpi3_mon_health_out *h);


/* ---- results and verdicts (design 4k K1 to K8) ---- */
enum rpi3_mon_res {
	RPI3_MON_R_OK = 0, RPI3_MON_R_BAD_PARAMETERS, RPI3_MON_R_BAD_STATE, RPI3_MON_R_SHORT_BUFFER, RPI3_MON_R_BUSY
};
enum rpi3_mon_v17 {	/* command 17 verdicts */
	RPI3_MON_V17_ACCEPTED = 0, RPI3_MON_V17_GAP, RPI3_MON_V17_RESYNC, RPI3_MON_V17_MALFORMED,
	RPI3_MON_V17_AUTH_FAIL, RPI3_MON_V17_REPLAY, RPI3_MON_V17_DUPLICATE, RPI3_MON_V17_CONFLICT
};
enum { RPI3_MON_MAL_LENGTH = 1, RPI3_MON_MAL_CAN_ID_RANGE = 2, RPI3_MON_MAL_UNLISTED = 3 };
enum rpi3_mon_v21 {	/* command 21 verdicts */
	RPI3_MON_V21_GRANTED = 0, RPI3_MON_V21_RENEWED, RPI3_MON_V21_RETURNED, RPI3_MON_V21_REFUSED_CAP,
	RPI3_MON_V21_NO_CHANGE
};
enum rpi3_mon_v23 {	/* command 23 verdicts */
	RPI3_MON_V23_ON_TIME = 0, RPI3_MON_V23_LATE, RPI3_MON_V23_STALE, RPI3_MON_V23_UNISSUED,
	RPI3_MON_V23_BAD_NONCE, RPI3_MON_V23_DUPLICATE
};

/* ---- command 20 projections, layout version 1, exactly 128 bytes (design 4k K5) ---- */
struct rpi3_mon_proj0 {
	uint32_t version, length;			/* 0x00 */
	uint32_t boot_id, flags;			/* 0x08 */
	uint64_t now_ticks;				/* 0x10 */
	uint64_t core1_poll_age_ticks;			/* 0x18 lock-free */
	uint64_t poll_count;				/* 0x20 lock-free */
	uint32_t skip_count, skip_total;		/* 0x28 lock-free */
	uint32_t pta_lock_timeouts;			/* 0x30 lock-free */
	uint32_t pta_timeouts_seen;			/* 0x34 */
	uint32_t last_counter, hb_seq;			/* 0x38 */
	uint64_t traffic_age_ticks;			/* 0x40 */
	uint64_t hb_last_ok_age_ticks;			/* 0x48 */
	uint32_t hb_miss_count, idle_uses;		/* 0x50 */
	uint64_t lease_remaining_ticks;			/* 0x58 */
	uint32_t ev_seq, ev_ack;			/* 0x60 */
	uint32_t ev_lost, pinned_used;			/* 0x68 */
	uint8_t v_frame, v_hb, v_xcheck, hb_state, mode, rsv[3];	/* 0x70 */
	uint64_t rsv2;					/* 0x78 */
};
struct rpi3_mon_proj1 {
	uint32_t version, length;			/* 0x00 */
	uint32_t ev_count[RPI3_MON_EV_COUNT];		/* 0x08 .. 0x6B */
	uint32_t v_frame_total, v_hb_total, v_xcheck_total;	/* 0x6C 0x70 0x74 */
	uint64_t rsv;					/* 0x78 */
};
#define RPI3_MON_PO(f) offsetof(struct rpi3_mon_proj0, f)
_Static_assert(sizeof(struct rpi3_mon_proj0) == 128 && _Alignof(struct rpi3_mon_proj0) == 8, "N5: page 0 is 128 bytes, 8-aligned");
_Static_assert(RPI3_MON_PO(version) == 0x00 && RPI3_MON_PO(boot_id) == 0x08 && RPI3_MON_PO(now_ticks) == 0x10 &&
	       RPI3_MON_PO(core1_poll_age_ticks) == 0x18 && RPI3_MON_PO(poll_count) == 0x20 && RPI3_MON_PO(skip_count) == 0x28 &&
	       RPI3_MON_PO(pta_lock_timeouts) == 0x30 && RPI3_MON_PO(pta_timeouts_seen) == 0x34 && RPI3_MON_PO(last_counter) == 0x38 &&
	       RPI3_MON_PO(traffic_age_ticks) == 0x40 && RPI3_MON_PO(hb_last_ok_age_ticks) == 0x48 && RPI3_MON_PO(hb_miss_count) == 0x50 &&
	       RPI3_MON_PO(lease_remaining_ticks) == 0x58 && RPI3_MON_PO(ev_seq) == 0x60 && RPI3_MON_PO(ev_lost) == 0x68 &&
	       RPI3_MON_PO(v_frame) == 0x70 && RPI3_MON_PO(rsv2) == 0x78, "N5: every page 0 field offset matches the approved table");
_Static_assert(sizeof(struct rpi3_mon_proj1) == 128 && offsetof(struct rpi3_mon_proj1, ev_count) == 0x08 &&
	       offsetof(struct rpi3_mon_proj1, v_frame_total) == 0x6C && offsetof(struct rpi3_mon_proj1, v_xcheck_total) == 0x74 &&
	       offsetof(struct rpi3_mon_proj1, rsv) == 0x78, "EV5: page 1 layout matches the approved table");
#define RPI3_MON_PFLAG_INIT_OK		0x01U
#define RPI3_MON_PFLAG_HARDENED		0x02U
#define RPI3_MON_PFLAG_TR_ARMED		0x04U
#define RPI3_MON_PFLAG_TR_TIMEOUT	0x08U
#define RPI3_MON_PFLAG_HB_TIMEOUT	0x10U
#define RPI3_MON_PFLAG_LEASE		0x20U
#define RPI3_MON_PFLAG_STARVED		0x40U
_Static_assert(RPI3_MON_HP_SKIP_SHIFT == 8 && (RPI3_MON_HP_STARVED | RPI3_MON_HP_INIT_OK) < 0x100U, "HP6: health word bit layout");

/* ---- shared-state logic ---- */
bool rpi3_mon_mac_enter(void);			/* in-use guard of mac_pta (4d S3); false = busy */
void rpi3_mon_mac_leave(void);
void rpi3_mon_note_legacy(uint32_t cmd_id, bool refused);	/* best effort: one non-spinning trylock, event dropped if busy */

/* ---- PTA command logic (plain C, no TEE_Param; the glue maps parameters) ---- */
enum rpi3_mon_res rpi3_mon_cmd17(const uint8_t *msg, size_t len, uint32_t *verdict, uint32_t *detail,
				 struct rpi3_mon_health_out *h);
enum rpi3_mon_res rpi3_mon_cmd18(uint32_t from_seq, uint32_t max_entries, uint8_t *out, size_t *len,
				 uint32_t *oa, uint32_t *ob, struct rpi3_mon_health_out *h);
enum rpi3_mon_res rpi3_mon_cmd19(uint32_t ack_upto, uint32_t b, uint32_t *oa, uint32_t *ob, struct rpi3_mon_health_out *h);
enum rpi3_mon_res rpi3_mon_cmd20(uint32_t page, uint32_t b, uint8_t *out, size_t *len, uint32_t *oa, uint32_t *ob,
				 struct rpi3_mon_health_out *h);
enum rpi3_mon_res rpi3_mon_cmd21(uint32_t mode, uint32_t req_ms, uint32_t *oa, uint32_t *ob, struct rpi3_mon_health_out *h);
enum rpi3_mon_res rpi3_mon_cmd22(uint32_t *nonce_lo, uint32_t *nonce_hi, uint32_t *hb_seq, uint32_t *hb_state,
				 struct rpi3_mon_health_out *h);
enum rpi3_mon_res rpi3_mon_cmd23(uint32_t seq, uint32_t b, uint32_t nonce_lo, uint32_t nonce_hi, uint32_t *oa, uint32_t *ob,
				 struct rpi3_mon_health_out *h);

/* ---- Core 1 (design 4g; D1 = Option B: FIQ and IRQ stay masked for the whole loop) ---- */
void rpi3_mon_core1_entry_check(void);		/* panics if FIQ or IRQ is unmasked at loop entry */
void rpi3_mon_core1_attempt(void);		/* one non-spinning trylock attempt; see rpi3_can_monitor.c */

/* Per-pass Core 1 update, inline and hook-free (G7 rule 6): the pass counter is local to the caller. */
static inline __attribute__((always_inline)) __nostackcheck
void rpi3_mon_core1_pass(uint32_t pass)
{
	rpi3_mon_store_u64(&rpi3_mon.health.poll_count, rpi3_mon_load_u64(&rpi3_mon.health.poll_count) + 1);
	if ((pass & RPI3_MON_CORE1_MASK()) == 0) {
		rpi3_mon_store_u64(&rpi3_mon.health.last_poll_ticks, MON_PLAT_TIMER());
		rpi3_mon_core1_attempt();
	}
}
_Static_assert((RPI3_MON_CORE1_CADENCE & (RPI3_MON_CORE1_CADENCE - 1U)) == 0, "the cadence is a power of two");

/* PTA glue entry (core/pta/rpi3_can_monitor_pta.c): dispatch, legacy policy and parameter mapping. */
TEE_Result rpi3_mon_pta_invoke(uint32_t cmd_id, uint32_t param_types, TEE_Param params[TEE_NUM_PARAMS], bool *handled);

/* ---------------------------------------------------------------- TEST-ONLY statistics (CFG_RPI3_CAN_MONITOR_TEST_STATS=y)
 * Not part of any release build. The hooks below expand to nothing when the flag is off. When on they are forced-inline, hook-free
 * and bounded, so they add no instrumented call to a lock hold (their own timer reads are part of the measured hold and are small). */
#ifdef CFG_RPI3_CAN_MONITOR_TEST_STATS
#define RPI3_MON_TS_HOLD_MAX_TICKS	192000ULL	/* a Core 1 hold request is clamped to 10 ms */
#define RPI3_MON_TS_HIST		16U
enum { RPI3_MON_TS_CTX_PTA = 0, RPI3_MON_TS_CTX_CORE1 = 1 };

struct rpi3_mon_tstats {
	uint64_t hold_start;				/* written by the current lock holder only */
	uint64_t hold_count, hold_sum, hold_max;	/* every hold of PTA and Core 1, in timer ticks */
	uint64_t hold_max_ctx[2];			/* per context: PTA, Core 1 */
	uint32_t hold_hist[RPI3_MON_TS_HIST];		/* bucket k: floor(log2(ticks)) = k, last bucket: 2^15 ticks and more */
	uint64_t wait_last_ticks;			/* last exhausted PTA acquire: ticks spent waiting and attempts made */
	uint32_t wait_last_attempts, wait_exhausted;
	uint64_t att_prev, att_count, att_sum, att_min, att_max;	/* ticks between successive Core 1 lock attempts */
	uint64_t hold_req_ticks;			/* test control: the next Core 1 attempt holds the lock this long */
	uint32_t hold_req_done, cadence_mask;	/* cadence_mask: copy of the run-time cadence mask (0 = default) when read through command 24 */
} __aligned(64);
extern struct rpi3_mon_tstats rpi3_mon_tstats;

#define RPI3_MON_TS_INLINE static inline __attribute__((always_inline)) __nostackcheck

RPI3_MON_TS_INLINE void rpi3_mon_ts_locked(uint64_t t) { rpi3_mon_tstats.hold_start = t; }

RPI3_MON_TS_INLINE void rpi3_mon_ts_unlocking(unsigned int ctx)
{
	uint64_t now = MON_PLAT_TIMER();
	uint64_t d = (now >= rpi3_mon_tstats.hold_start) ? now - rpi3_mon_tstats.hold_start : 0;
	uint64_t x = d;
	unsigned int k = 0;

	while (x > 1 && k < RPI3_MON_TS_HIST - 1U) { x >>= 1; k++; }
	rpi3_mon_tstats.hold_hist[k]++;
	rpi3_mon_tstats.hold_count++;
	rpi3_mon_tstats.hold_sum += d;
	if (d > rpi3_mon_tstats.hold_max) rpi3_mon_tstats.hold_max = d;
	if (d > rpi3_mon_tstats.hold_max_ctx[ctx]) rpi3_mon_tstats.hold_max_ctx[ctx] = d;
}

RPI3_MON_TS_INLINE void rpi3_mon_ts_exhausted(uint64_t now, uint64_t t0, uint32_t attempts)
{
	rpi3_mon_tstats.wait_last_ticks = (now >= t0) ? now - t0 : 0;
	rpi3_mon_tstats.wait_last_attempts = attempts;
	rpi3_mon_tstats.wait_exhausted++;
}

RPI3_MON_TS_INLINE void rpi3_mon_ts_attempt(void)
{
	uint64_t now = MON_PLAT_TIMER();
	uint64_t d;

	if (rpi3_mon_tstats.att_prev && now >= rpi3_mon_tstats.att_prev) {
		d = now - rpi3_mon_tstats.att_prev;
		rpi3_mon_tstats.att_count++;
		rpi3_mon_tstats.att_sum += d;
		if (!rpi3_mon_tstats.att_min || d < rpi3_mon_tstats.att_min) rpi3_mon_tstats.att_min = d;
		if (d > rpi3_mon_tstats.att_max) rpi3_mon_tstats.att_max = d;
	}
	rpi3_mon_tstats.att_prev = now;
}

/* Called by Core 1 right after it took the lock: if a hold was requested, keep the lock for that long (bounded by the clamp), then clear it. */
RPI3_MON_TS_INLINE void rpi3_mon_ts_core1_hold(void)
{
	uint64_t req = rpi3_mon_tstats.hold_req_ticks;
	uint64_t t0;

	if (req) {
		if (req > RPI3_MON_TS_HOLD_MAX_TICKS) req = RPI3_MON_TS_HOLD_MAX_TICKS;
		t0 = MON_PLAT_TIMER();
		while (MON_PLAT_TIMER() - t0 < req) { }
		rpi3_mon_tstats.hold_req_ticks = 0;
		rpi3_mon_tstats.hold_req_done++;
	}
}

#define RPI3_MON_TS_LOCKED(t)			rpi3_mon_ts_locked(t)
#define RPI3_MON_TS_UNLOCKING(ctx)		rpi3_mon_ts_unlocking(ctx)
#define RPI3_MON_TS_EXHAUSTED(now, t0, att)	rpi3_mon_ts_exhausted((now), (t0), (att))
#define RPI3_MON_TS_ATTEMPT()			rpi3_mon_ts_attempt()
/* W1 (lock memory attributes): the PTA asks, Core 1 answers once, just before its lock attempt (outside any hold). */
struct rpi3_mon_w1 {
	uint64_t va;						/* address probed: the monitor lock */
	uint64_t pta_par, pta_sctlr, pta_mair;			/* PAR_EL1 after AT S1E1R, SCTLR_EL1, MAIR_EL1 on the PTA core */
	uint64_t c1_par, c1_sctlr, c1_mair;			/* the same on Core 1 */
	uint64_t c1_done;					/* 1 once Core 1 has answered */
};
_Static_assert(sizeof(struct rpi3_mon_w1) == 64, "W1 result layout is part of the test interface");
extern volatile uint32_t rpi3_mon_ts_w1_req;
void rpi3_mon_ts_w1_core1(void);
uint32_t rpi3_mon_ts_w1_run(struct rpi3_mon_w1 *out);		/* 0 = both cores answered, 1 = Core 1 did not answer in time (PTA part valid) */
#define RPI3_MON_TS_W1_CORE1()			do { if (rpi3_mon_ts_w1_req) rpi3_mon_ts_w1_core1(); } while (0)
/* W7 (forced Core 1 panic): armed only by test command 25 op 4 with the magic below; Core 1 then panics itself at its next
 * attempt, BEFORE it takes the monitor lock, so the lock is never left held. */
#define RPI3_MON_TS_W7_MAGIC			0x57375737U
extern volatile uint32_t rpi3_mon_ts_w7_req;
void rpi3_mon_ts_w7_core1(void);
#define RPI3_MON_TS_W7_CORE1()			do { if (rpi3_mon_ts_w7_req) rpi3_mon_ts_w7_core1(); } while (0)
#define RPI3_MON_TS_CORE1_HOLD()		rpi3_mon_ts_core1_hold()

/* Not in a hold; lock-free reads/writes of the statistics for the two test commands. */
size_t rpi3_mon_ts_copy(uint8_t *out, size_t room);			/* returns sizeof(struct), or 0 if room is too small (then *needed* is sizeof) */
uint32_t rpi3_mon_ts_ctrl(uint32_t op, uint64_t arg, uint32_t *done);	/* op 0 reset, 1 request a Core 1 hold of arg ticks, 2 read only, 3 set the cadence to arg passes (power of two, 16 to 65536; 0 = default) */
#else
#define RPI3_MON_TS_LOCKED(t)			do { } while (0)
#define RPI3_MON_TS_UNLOCKING(ctx)		do { } while (0)
#define RPI3_MON_TS_EXHAUSTED(now, t0, att)	do { } while (0)
#define RPI3_MON_TS_ATTEMPT()			do { } while (0)
#define RPI3_MON_TS_W1_CORE1()			do { } while (0)
#define RPI3_MON_TS_W7_CORE1()			do { } while (0)
#define RPI3_MON_TS_CORE1_HOLD()		do { } while (0)
#endif

#endif /* __RPI3_CAN_MONITOR_H */
