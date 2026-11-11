/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Normal-World client library for the Secure CAN Monitor PTA commands 17 to 23 (rpi3_can_spi_poc).
 * One function per command. Parameter types follow design 4k exactly. No key is built in: tag generation takes
 * the key from the caller. Nothing here touches the legacy commands.
 */
#ifndef MON_CLIENT_H
#define MON_CLIENT_H

#include <stddef.h>
#include <stdint.h>
#include <tee_client_api.h>

#define MON_FRAME_LEN 44
#define MON_EVENT_MAX 5
#define MON_AGE_NEVER 0xFFFFFFFFU

/* Health pair returned in slots 2 and 3 of every monitor command (zero unless the call returned SUCCESS or BUSY). */
struct mon_health {
	uint32_t p2a;	/* age of the last Core 1 poll in ms, MON_AGE_NEVER if never */
	uint32_t p2b;	/* bit 0 starved, bit 1 init_ok, bits 8..15 skip_count (saturated at 255) */
	uint32_t p3a;	/* pta_lock_timeouts */
	uint32_t p3b;	/* skip_total */
};
#define MON_HP_STARVED 0x1U
#define MON_HP_INIT_OK 0x2U
#define MON_HP_SKIP(p2b) (((p2b) >> 8) & 0xFFU)

/* Records and pages: byte-for-byte copies of the Secure World layouts (checked by the host integration test). */
struct mon_event {
	uint32_t seq; uint16_t type; uint16_t flags;
	uint64_t ticks; uint32_t counter; uint32_t detail;
};
struct mon_status0 {
	uint32_t version, length;
	uint32_t boot_id, flags;
	uint64_t now_ticks;
	uint64_t core1_poll_age_ticks;
	uint64_t poll_count;
	uint32_t skip_count, skip_total;
	uint32_t pta_lock_timeouts;
	uint32_t pta_timeouts_seen;
	uint32_t last_counter, hb_seq;
	uint64_t traffic_age_ticks;
	uint64_t hb_last_ok_age_ticks;
	uint32_t hb_miss_count, idle_uses;
	uint64_t lease_remaining_ticks;
	uint32_t ev_seq, ev_ack;
	uint32_t ev_lost, pinned_used;
	uint8_t v_frame, v_hb, v_xcheck, hb_state, mode, rsv[3];
	uint64_t rsv2;
};
#define MON_EVENT_TYPES 25
struct mon_status1 {
	uint32_t version, length;
	uint32_t ev_count[MON_EVENT_TYPES];
	uint32_t v_frame_total, v_hb_total, v_xcheck_total;
	uint64_t rsv;
};

struct mon_ctx {
	TEEC_Context context;
	TEEC_Session session;
	int open;
};

/* Open and close a session to the PTA by its UUID. */
TEEC_Result mon_open(struct mon_ctx *c);
void mon_close(struct mon_ctx *c);

/* Command 17. verdict: 0 ACCEPTED 1 GAP 2 RESYNC 3 MALFORMED 4 AUTH_FAIL 5 REPLAY 6 DUPLICATE 7 CONFLICT. BUSY: resubmit. */
TEEC_Result mon_submit_frame(struct mon_ctx *c, const uint8_t frame[MON_FRAME_LEN], uint32_t *verdict, uint32_t *detail, struct mon_health *h);
/* Command 18. At most 5 records per call. *count records are stored in ev[]. *more is 1 if more are available. */
TEEC_Result mon_get_events(struct mon_ctx *c, uint32_t first_seq, uint32_t max, struct mon_event ev[MON_EVENT_MAX], uint32_t *count, int *more, uint32_t *lost, struct mon_health *h);
/* Command 19. Bookmark only. *effective is the value the monitor stored. */
TEEC_Result mon_ack_events(struct mon_ctx *c, uint32_t upto_seq, uint32_t *effective, struct mon_health *h);
/* Command 20, page 0 or 1. */
TEEC_Result mon_get_status0(struct mon_ctx *c, struct mon_status0 *out, struct mon_health *h);
TEEC_Result mon_get_status1(struct mon_ctx *c, struct mon_status1 *out, struct mon_health *h);
/* Command 21. mode 1 = request a lease of ms; mode 0 = return (ms must be 0). verdict: 0 GRANTED 1 RENEWED 2 RETURNED 3 REFUSED_CAP 4 NO_CHANGE. */
TEEC_Result mon_set_idle(struct mon_ctx *c, uint32_t mode, uint32_t ms, uint32_t *verdict, uint32_t *remaining_ms, struct mon_health *h);
/* Command 22. A zero nonce with state ANSWERED/LATE_ANSWERED/NONE means "no open challenge". states: 0 NONE 1 OPEN 2 ANSWERED 3 MISSED 4 LATE_ANSWERED. */
TEEC_Result mon_get_nonce(struct mon_ctx *c, uint64_t *nonce, uint32_t *seq, uint32_t *state, struct mon_health *h);
/* Command 23. verdict: 0 ON_TIME 1 LATE 2 STALE 3 UNISSUED 4 BAD_NONCE 5 DUPLICATE. */
TEEC_Result mon_submit_answer(struct mon_ctx *c, uint32_t seq, uint64_t nonce, uint32_t *verdict, uint32_t *detail, struct mon_health *h);

/* Frame helpers: can_id | counter | payload (little endian) | HMAC-SHA256(key, first 12 bytes), 44 bytes. The key is supplied by the caller. */
void mon_build_frame(const uint8_t key[32], uint32_t can_id, uint32_t counter, uint32_t payload, uint8_t out[MON_FRAME_LEN]);
void mon_hmac_sha256(const uint8_t *key, size_t keylen, const uint8_t *msg, size_t len, uint8_t out[32]);

/* Names for printing. */
const char *mon_event_name(unsigned type);
const char *mon_verdict17_name(unsigned v);
const char *mon_verdict21_name(unsigned v);
const char *mon_verdict23_name(unsigned v);
const char *mon_hbstate_name(unsigned s);

#endif
