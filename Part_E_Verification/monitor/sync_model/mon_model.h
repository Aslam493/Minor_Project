#ifndef MON_MODEL_H
#define MON_MODEL_H
#include <string.h>
#include "mon_plat.h"
/* Model parameters (placeholders, NOT final values; Component 4 fixes them). */
#define MON_BUDGET_TICKS 19200u        /* 1 ms at 19.2 MHz */
#define MON_ATTEMPT_CAP  2000u
#define MON_TICKS_PER_SEC 19200000ull
#define MON_T_TRAFFIC    5760000ull    /* 300 ms (DEC-IF-11) */
#define MON_D_HB         9600000ull    /* per-request deadline D_req, 500 ms */
#define MON_H_HB         19200000ull   /* challenge period H, 1 s */
#define MON_K_MISS       3u
#define MON_IDLE_MAX_MS  60000u        /* provisional */
#define MON_IDLE_USES_MAX 16u          /* provisional */
enum { MON_OK = 0, MON_BUSY = 1, MON_BAD_PARAMETERS = 2, MON_BAD_STATE = 3, MON_NOT_SUPPORTED = 4, MON_SHORT_BUFFER = 5 };
enum { EV_AUTH = 0, EV_REPLAY, EV_TIMEOUT, EV_ALTERED, EV_UNACCOUNTED, EV_CHIPB, NEV_EVID /* 6 evidence classes */,
       EV_MALFORMED = 6, EV_COUNTER_GAP, EV_COUNTER_RESYNC, EV_TRAFFIC_RECOVERED, EV_HB_RECOVERED, EV_MODE,
       EV_LEASE_GRANTED, EV_LEASE_RENEWED, EV_LEASE_REFUSED, EV_LOCK_TIMEOUT, EV_STARVED, EV_HB_SEQ_EXHAUSTED,
       EV_LEGACY_REFUSED, EV_LEGACY_USED, EV_BAD_NONCE, EV_STALE_ANSWER, EV_UNISSUED_ANSWER, EV_DUP_ANSWER,
       EV_LATE_ANSWER, NEV = 25 };                    /* 6 evidence + 19 diagnostic classes (DEC-IF-10) */
enum { V23_ON_TIME = 0, V23_LATE, V23_STALE, V23_UNISSUED, V23_BAD_NONCE, V23_DUPLICATE };
enum { V21_GRANTED = 0, V21_RENEWED, V21_RETURNED, V21_REFUSED_CAP, V21_NO_CHANGE };
enum { HB_NONE = 0, HB_OPEN, HB_ANSWERED, HB_MISSED, HB_LATE_ANSWERED };
struct ev { uint32_t seq; uint16_t type; uint16_t flags; uint64_t ticks; uint32_t counter; uint32_t detail; };
_Static_assert(sizeof(struct ev) == 24, "event record is 24 bytes");
_Static_assert(5 * sizeof(struct ev) <= 128, "five records fit the 128-byte copy limit (CS3, H5)");
struct lstate {                         /* every member here is lock-protected (class L) */
	bool tr_armed; uint64_t tr_last_valid; bool tr_timeout_raised;
	uint64_t idle_until; uint32_t idle_uses;
	uint32_t last_counter;
	uint32_t hb_seq; uint64_t hb_issue; int hb_state; uint64_t hb_last_ok; uint32_t hb_miss; bool hb_timeout_raised;
	uint64_t hb_nonce; bool hb_nonce_valid;      /* stored derived nonce (cmd 22 stores it; cmd 23 compares) */
	uint32_t vtot[3];                            /* verdict totals: frame, heartbeat, cross-check */
	uint32_t pta_timeouts_seen;
	struct ev pinned[24]; uint8_t pinned_n[6];
	struct ev ord[40]; bool ord_used[40]; uint32_t ord_head;
	uint32_t ev_seq, ev_ack, ev_lost, pinned_used, ev_count[NEV];
	uint64_t diag_last[NEV]; bool diag_have[NEV];     /* per-class one-per-second limit (DEC-RATE-1) */ uint8_t last_verdict[3]; uint8_t mode;
};
struct health { uint64_t poll_count, last_poll_ticks; uint32_t skip_count, skip_total, starved; };  /* G7: lock-free */
struct mon {
	unsigned lock; bool init_ok; uint32_t boot_id;
	struct lstate l;
	_Alignas(64) struct health h;
	_Alignas(64) uint32_t pta_lock_timeouts;       /* multi-writer, atomic_inc32 (4d S1) */
	_Alignas(64) unsigned mac_pta_busy;            /* mac_pta in-use guard (4d S3) */
};
struct mon_wait { uint64_t wait_ticks_used; uint32_t attempts_used; };
struct core1 { uint32_t pass, K; };
struct health_out { uint32_t p2a, p2b, p3a, p3b; };
/* 128-byte status projection, page 0, layout version 1 (Step 2 contract) */
struct proj {
	uint32_t version, length;                      /* 0x00 */
	uint32_t boot_id, flags;                       /* 0x08 */
	uint64_t now_ticks;                            /* 0x10 */
	uint64_t core1_poll_age_ticks;                 /* 0x18 */
	uint64_t poll_count;                           /* 0x20 */
	uint32_t skip_count, skip_total;               /* 0x28 */
	uint32_t pta_lock_timeouts, pta_timeouts_seen; /* 0x30 */
	uint32_t last_counter, hb_seq;                 /* 0x38 */
	uint64_t traffic_age_ticks;                    /* 0x40 */
	uint64_t hb_last_ok_age_ticks;                 /* 0x48 */
	uint32_t hb_miss_count, idle_uses;             /* 0x50 */
	uint64_t lease_remaining_ticks;                /* 0x58 */
	uint32_t ev_seq, ev_ack;                       /* 0x60 */
	uint32_t ev_lost, pinned_used;                 /* 0x68 */
	uint8_t v_frame, v_hb, v_xcheck, hb_state, mode, rsv[3];   /* 0x70 */
	uint64_t rsv2;                                 /* 0x78 */
};
#define PO(f) offsetof(struct proj, f)
_Static_assert(sizeof(struct proj) == 128 && _Alignof(struct proj) == 8, "projection is 128 bytes, 8-aligned");
_Static_assert(PO(version)==0x00 && PO(boot_id)==0x08 && PO(now_ticks)==0x10 && PO(core1_poll_age_ticks)==0x18 &&
	PO(poll_count)==0x20 && PO(skip_count)==0x28 && PO(pta_lock_timeouts)==0x30 && PO(last_counter)==0x38 &&
	PO(traffic_age_ticks)==0x40 && PO(hb_last_ok_age_ticks)==0x48 && PO(hb_miss_count)==0x50 &&
	PO(lease_remaining_ticks)==0x58 && PO(ev_seq)==0x60 && PO(ev_lost)==0x68 && PO(v_frame)==0x70 && PO(rsv2)==0x78,
	"every field offset matches the approved table");
/* page 1 of command 20: exact counters, layout version 1 (DEC-IF-10) */
struct proj1 {
	uint32_t version, length;                      /* 0x00 */
	uint32_t ev_count[NEV];                        /* 0x08 .. 0x6B */
	uint32_t v_frame_total, v_hb_total, v_xcheck_total;   /* 0x6C 0x70 0x74 */
	uint64_t rsv;                                  /* 0x78 */
};
_Static_assert(sizeof(struct proj1) == 128 && offsetof(struct proj1, ev_count) == 0x08 &&
	offsetof(struct proj1, v_frame_total) == 0x6C && offsetof(struct proj1, v_xcheck_total) == 0x74 &&
	offsetof(struct proj1, rsv) == 0x78, "page 1 layout matches the approved table");
#define PFLAG_INIT_OK 0x01u
#define PFLAG_HARDENED 0x02u
#define PFLAG_TR_ARMED 0x04u
#define PFLAG_TR_TIMEOUT 0x08u
#define PFLAG_HB_TIMEOUT 0x10u
#define PFLAG_LEASE 0x20u
#define PFLAG_STARVED 0x40u
#define HP_STARVED 0x1u
#define HP_INIT_OK 0x2u
#define HP_SKIP_SHIFT 8
struct lstate *mon_l(struct mon *m);
int  mon_acquire(struct mon *m, struct mon_wait *w, uint32_t *exc);
void mon_release(struct mon *m, uint32_t exc);
void mon_evaluate(struct mon *m, uint64_t now);
void mon_note_valid_frame(struct mon *m, uint64_t now);
void mon_hb_issue(struct mon *m, uint64_t now);
void mon_hb_answer(struct mon *m, uint64_t now, bool on_time);
void mon_ev_append(struct mon *m, unsigned type, uint32_t counter, uint32_t detail, uint64_t now);
void core1_pass(struct mon *m, struct core1 *c);
int  mon_cmd18(struct mon *m, uint32_t from_seq, uint32_t maxn, uint8_t *out, size_t outsz, size_t *outlen,
               uint32_t *oa, uint32_t *ob, struct health_out *h);
int  mon_cmd20(struct mon *m, uint32_t page, uint32_t b, uint8_t *out, size_t outsz, size_t *outlen,
               uint32_t *oa, uint32_t *ob, struct health_out *h);
int  mon_cmd19(struct mon *m, uint32_t ack_upto, uint32_t b, uint32_t *oa, uint32_t *ob, struct health_out *h);
int  mon_cmd21(struct mon *m, uint32_t mode, uint32_t req_ms, uint32_t *oa, uint32_t *ob, struct health_out *h);
int  mon_cmd23(struct mon *m, uint32_t seq, uint32_t b, uint32_t nlo, uint32_t nhi, uint32_t *oa, uint32_t *ob, struct health_out *h);
int  mon_mac_enter(struct mon *m);
void mon_mac_leave(struct mon *m);
#endif
