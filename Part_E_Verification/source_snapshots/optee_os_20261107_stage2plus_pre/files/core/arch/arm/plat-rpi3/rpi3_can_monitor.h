/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Secure CAN Monitor: shared state, synchronisation and health (Component 2.2, stage 1).
 *
 * Authority: Part_E_Verification/monitor/MONITOR_STATE_DESIGN.md (sections 4a to 4k) and
 * INTERFACE_DECISION_REGISTER.md. This stage implements ONLY: the state structures and their ownership, the
 * lock and its initialisation, init failure handling, bounded PTA lock acquisition and release, the G7
 * accessors, and the health pair. No PTA command, no Core 1 hook, no HMAC, no events logic yet.
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

#endif /* __RPI3_CAN_MONITOR_H */
