// SPDX-License-Identifier: BSD-2-Clause
/*
 * Command line front end for the Secure CAN Monitor client library. Read-only commands: status, events, nonce.
 * Commands that change monitor state (never the CAN chip, never the SD card): ack, idle, answer, frame.
 * No key is built in; frame needs --key-file or --key-hex. Keys are never printed.
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>
#include <time.h>

#include "mon_client.h"

static int g_retries = 5;

static void usage(void)
{
	fprintf(stderr,
		"usage: mon_client [--retries N] <command>\n"
		"  status [0|1]                         command 20 (page 0 default)\n"
		"  events [--from SEQ] [--max 1..5] [--all] [--ack]   command 18 (and 19 with --ack)\n"
		"  ack SEQ                              command 19 (bookmark only)\n"
		"  idle on MS | idle off                command 21\n"
		"  nonce                                command 22\n"
		"  answer                               commands 22 then 23: answer the open challenge once\n"
		"  frame CANID COUNTER PAYLOAD (--key-file F | --key-hex HEX64) [--bad-tag]   command 17\n"
		"  soak SECONDS --key-file F [--period-ms N] [--rt]   software sender + heartbeat responder in one session (absolute-deadline\n"
		"                                       timing, default 100 ms, optional SCHED_FIFO). NOT CAN-bus evidence: frames are injected through the PTA\n"
		"BUSY results are retried up to --retries times (default 5).\n");
}

static void print_health(const struct mon_health *h)
{
	if (h->p2a == MON_AGE_NEVER)
		printf("  health: core1 never polled, ");
	else
		printf("  health: core1 last poll %u ms ago, ", h->p2a);
	printf("init_ok=%u starved=%u skip_count=%u pta_lock_timeouts=%u skip_total=%u\n",
	       !!(h->p2b & MON_HP_INIT_OK), !!(h->p2b & MON_HP_STARVED), MON_HP_SKIP(h->p2b), h->p3a, h->p3b);
}

static void print_err(const char *what, TEEC_Result r)
{
	const char *n = r == TEEC_ERROR_BUSY ? "BUSY" : r == TEEC_ERROR_BAD_STATE ? "BAD_STATE" : r == TEEC_ERROR_BAD_PARAMETERS ? "BAD_PARAMETERS" :
			r == TEEC_ERROR_NOT_SUPPORTED ? "NOT_SUPPORTED" : r == TEEC_ERROR_SHORT_BUFFER ? "SHORT_BUFFER" : "other";
	fprintf(stderr, "%s failed: 0x%08" PRIx32 " (%s)\n", what, (uint32_t)r, n);
}

static int hexval(int c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

static int parse_hex32(const char *s, uint8_t key[32])
{
	int i = 0, hi = -1;

	for (; *s; s++) {
		int v;
		if (*s == ' ' || *s == '\n' || *s == '\r' || *s == '\t') continue;
		v = hexval(*s);
		if (v < 0) return -1;
		if (hi < 0) hi = v;
		else { if (i >= 32) return -1; key[i++] = (uint8_t)(hi * 16 + v); hi = -1; }
	}
	return (i == 32 && hi < 0) ? 0 : -1;
}

static int load_key_file(const char *path, uint8_t key[32])
{
	char buf[256];
	size_t n;
	FILE *f = fopen(path, "rb");

	if (!f) return -1;
	n = fread(buf, 1, sizeof(buf) - 1, f);
	fclose(f);
	if (n == 32) { memcpy(key, buf, 32); return 0; }		/* raw key */
	buf[n] = 0;
	return parse_hex32(buf, key);					/* 64 hex digits */
}

#define RETRY(call, r) do { int t_; for (t_ = 0; ; t_++) { (r) = (call); if ((r) != TEEC_ERROR_BUSY || t_ >= g_retries) break; \
			     { struct timespec ts_ = {0, 2000000}; nanosleep(&ts_, NULL); } } } while (0)

/* Design K7: while a challenge is OPEN or MISSED, a zero nonce means a new challenge was issued between the monitor's two steps. Ask again (bounded);
 * never submit a zero nonce. Returns the last reply; the caller checks *n != 0. */
static TEEC_Result get_nonce_retry(struct mon_ctx *c, uint64_t *n, uint32_t *seq, uint32_t *st, struct mon_health *h)
{
	TEEC_Result r = TEEC_SUCCESS;
	int t;

	for (t = 0; t < 4; t++) {
		RETRY(mon_get_nonce(c, n, seq, st, h), r);
		if (r || *n != 0 || (*st != 1 && *st != 3))
			return r;
	}
	return r;
}

static int cmd_status(struct mon_ctx *c, int page)
{
	struct mon_health h;
	TEEC_Result r;

	if (page == 0) {
		struct mon_status0 s;
		RETRY(mon_get_status0(c, &s, &h), r);
		if (r) { print_err("status", r); return 1; }
		printf("page 0: version %u length %u boot_id 0x%08x flags 0x%02x mode %u\n", s.version, s.length, s.boot_id, s.flags, s.mode);
		printf("  now_ticks %" PRIu64 " core1_poll_age_ticks %" PRIu64 " poll_count %" PRIu64 "\n", s.now_ticks, s.core1_poll_age_ticks, s.poll_count);
		printf("  skip_count %u skip_total %u pta_lock_timeouts %u pta_timeouts_seen %u\n", s.skip_count, s.skip_total, s.pta_lock_timeouts, s.pta_timeouts_seen);
		printf("  last_counter %u hb_seq %u hb_state %s hb_miss_count %u idle_uses %u\n", s.last_counter, s.hb_seq, mon_hbstate_name(s.hb_state), s.hb_miss_count, s.idle_uses);
		printf("  traffic_age_ticks %" PRIu64 " hb_last_ok_age_ticks %" PRIu64 " lease_remaining_ticks %" PRIu64 "\n", s.traffic_age_ticks, s.hb_last_ok_age_ticks, s.lease_remaining_ticks);
		printf("  ev_seq %u ev_ack %u ev_lost %u pinned_used %u\n", s.ev_seq, s.ev_ack, s.ev_lost, s.pinned_used);
	} else {
		struct mon_status1 s;
		unsigned i;
		RETRY(mon_get_status1(c, &s, &h), r);
		if (r) { print_err("status", r); return 1; }
		printf("page 1: version %u length %u verdict totals frame %u heartbeat %u crosscheck %u\n", s.version, s.length, s.v_frame_total, s.v_hb_total, s.v_xcheck_total);
		for (i = 0; i < MON_EVENT_TYPES; i++)
			if (s.ev_count[i]) printf("  %-20s %u\n", mon_event_name(i), s.ev_count[i]);
	}
	print_health(&h);
	return 0;
}

static int cmd_events(struct mon_ctx *c, uint32_t from, uint32_t max, int all, int ack)
{
	struct mon_event ev[MON_EVENT_MAX];
	struct mon_health h;
	uint32_t n, lost, last = 0;
	int more, rounds = 0;
	TEEC_Result r;

	do {
		unsigned i;
		RETRY(mon_get_events(c, from, max, ev, &n, &more, &lost, &h), r);
		if (r) { print_err("events", r); return 1; }
		for (i = 0; i < n; i++) {
			printf("seq %u %-20s counter %u detail %u ticks %" PRIu64 "\n", ev[i].seq, mon_event_name(ev[i].type), ev[i].counter, ev[i].detail, ev[i].ticks);
			last = ev[i].seq;
		}
		if (n) from = last + 1;
		printf("  returned %u, more=%d, ev_lost=%u\n", n, more, lost);
	} while (all && more && n && ++rounds < 100);
	if (ack && last) {
		uint32_t eff;
		RETRY(mon_ack_events(c, last, &eff, &h), r);
		if (r) { print_err("ack", r); return 1; }
		printf("  acknowledged up to %u (effective %u)\n", last, eff);
	}
	print_health(&h);
	return 0;
}


/* ---- soak: a software frame source (plan C4.3 style) and heartbeat responder in ONE session ---- */
static uint64_t now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec; }
static int cmp_u64(const void *a, const void *b) { uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return x < y ? -1 : x > y; }

static int cmd_soak(struct mon_ctx *c, unsigned seconds, const uint8_t key[32], unsigned period_ms, int rt)
{
	struct mon_status0 s0; struct mon_health h; TEEC_Result r;
	uint64_t period = (uint64_t)period_ms * 1000000ull, start, next, end, t_call;
	size_t cap = (size_t)seconds * (1000u / (period_ms ? period_ms : 1)) + 16, n = 0, i;
	uint64_t *jit = malloc(cap * sizeof *jit), *lat = malloc(cap * sizeof *lat);
	unsigned long verdict[8] = {0}, ans_on = 0, ans_late = 0, ans_other = 0, busy = 0, errs = 0, ticks = 0;
	uint32_t counter, v, d, seq, st, last_answered = 0xFFFFFFFFu; uint64_t nonce;
	uint32_t timeouts0, skips0;

	if (!jit || !lat || period_ms < 10) { fprintf(stderr, "bad soak parameters\n"); free(jit); free(lat); return 2; }
	RETRY(mon_get_status0(c, &s0, &h), r);
	if (r) { print_err("status", r); free(jit); free(lat); return 1; }
	counter = (s0.flags & 0x04U) ? s0.last_counter + 1U : 1000U;		/* continue the monitor's own counter when traffic is armed */
	timeouts0 = h.p3a; skips0 = h.p3b;
	if (rt) { struct sched_param sp = { .sched_priority = 50 }; if (sched_setscheduler(0, SCHED_FIFO, &sp)) perror("sched_setscheduler (continuing without real-time priority)"); }
	start = now_ns(); end = start + (uint64_t)seconds * 1000000000ull; next = start + period;
	while (next < end && n < cap) {
		struct timespec ts = { (time_t)(next / 1000000000ull), (long)(next % 1000000000ull) };
		uint8_t f[MON_FRAME_LEN];
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
		jit[n] = now_ns() - next;						/* how late the wake-up was against the absolute deadline */
		mon_build_frame(key, 0x123, counter, 0xC0DE0000u | (counter & 0xFFFFu), f);
		t_call = now_ns();
		RETRY(mon_submit_frame(c, f, &v, &d, &h), r);
		lat[n] = now_ns() - t_call; n++;
		if (r == TEEC_ERROR_BUSY) busy++;
		else if (r) errs++;
		else { if (v < 8) verdict[v]++; if (v == 0 || v == 1 || v == 2) counter++; }
		/* heartbeat: answer the open challenge once, promptly */
		r = get_nonce_retry(c, &nonce, &seq, &st, &h);
		if (!r && (st == 1 || st == 3) && nonce != 0 && seq != last_answered) {
			RETRY(mon_submit_answer(c, seq, nonce, &v, &d, &h), r);
			if (!r) { last_answered = seq; if (v == 0) ans_on++; else if (v == 1) ans_late++; else ans_other++; }
		}
		ticks++; next += period;
	}
	RETRY(mon_get_status0(c, &s0, &h), r);
	qsort(jit, n, sizeof *jit, cmp_u64); qsort(lat, n, sizeof *lat, cmp_u64);
	printf("soak: %u s, period %u ms, %lu ticks%s\n", seconds, period_ms, ticks, rt ? ", SCHED_FIFO" : "");
	printf("frame verdicts: ACCEPTED %lu GAP %lu RESYNC %lu MALFORMED %lu AUTH_FAIL %lu REPLAY %lu DUPLICATE %lu CONFLICT %lu | BUSY %lu errors %lu\n",
	       verdict[0], verdict[1], verdict[2], verdict[3], verdict[4], verdict[5], verdict[6], verdict[7], busy, errs);
	printf("heartbeat answers: ON_TIME %lu LATE %lu other %lu\n", ans_on, ans_late, ans_other);
	if (n) {
		printf("send lateness vs absolute deadline (us): p50 %.0f p99 %.0f max %.0f\n", jit[n / 2] / 1e3, jit[n * 99 / 100] / 1e3, jit[n - 1] / 1e3);
		printf("frame submit latency (us): p50 %.0f p99 %.0f max %.0f\n", lat[n / 2] / 1e3, lat[n * 99 / 100] / 1e3, lat[n - 1] / 1e3);
	}
	if (!r) printf("end state: flags 0x%02x last_counter %u hb_state %s miss_count %u | pta_lock_timeouts +%u skip_total +%u starved %u\n", s0.flags, s0.last_counter,
	       mon_hbstate_name(s0.hb_state), s0.hb_miss_count, h.p3a - timeouts0, h.p3b - skips0, !!(h.p2b & MON_HP_STARVED));
	free(jit); free(lat);
	(void)i;
	return (errs || busy || verdict[4] || verdict[3] || (!r && (h.p2b & MON_HP_STARVED))) ? 1 : 0;	/* anomaly: any error, BUSY, AUTH_FAIL, MALFORMED or starvation */
}

int main(int argc, char **argv)
{
	struct mon_ctx c;
	struct mon_health h;
	TEEC_Result r;
	int rc = 1, a = 1;

	if (argc > 2 && !strcmp(argv[1], "--retries")) { g_retries = atoi(argv[2]); a = 3; }
	if (a >= argc) { usage(); return 2; }
	r = mon_open(&c);
	if (r) { print_err("open session", r); return 1; }

	if (!strcmp(argv[a], "status")) {
		int page = (a + 1 < argc) ? atoi(argv[a + 1]) : 0;
		rc = (page == 0 || page == 1) ? cmd_status(&c, page) : (usage(), 2);
	} else if (!strcmp(argv[a], "events")) {
		uint32_t from = 0, max = MON_EVENT_MAX; int all = 0, ack = 0, i;
		for (i = a + 1; i < argc; i++) {
			if (!strcmp(argv[i], "--from") && i + 1 < argc) from = (uint32_t)strtoul(argv[++i], NULL, 0);
			else if (!strcmp(argv[i], "--max") && i + 1 < argc) max = (uint32_t)strtoul(argv[++i], NULL, 0);
			else if (!strcmp(argv[i], "--all")) all = 1;
			else if (!strcmp(argv[i], "--ack")) ack = 1;
			else { usage(); mon_close(&c); return 2; }
		}
		rc = cmd_events(&c, from, max, all, ack);
	} else if (!strcmp(argv[a], "ack") && a + 1 < argc) {
		uint32_t eff;
		RETRY(mon_ack_events(&c, (uint32_t)strtoul(argv[a + 1], NULL, 0), &eff, &h), r);
		if (r) print_err("ack", r); else { printf("effective ev_ack %u\n", eff); print_health(&h); rc = 0; }
	} else if (!strcmp(argv[a], "idle") && a + 1 < argc) {
		uint32_t v, rem; int on = !strcmp(argv[a + 1], "on");
		if (on && a + 2 >= argc) { usage(); mon_close(&c); return 2; }
		RETRY(mon_set_idle(&c, on ? 1 : 0, on ? (uint32_t)strtoul(argv[a + 2], NULL, 0) : 0, &v, &rem, &h), r);
		if (r) print_err("idle", r); else { printf("verdict %s, remaining %u ms\n", mon_verdict21_name(v), rem); print_health(&h); rc = 0; }
	} else if (!strcmp(argv[a], "nonce")) {
		uint64_t n; uint32_t seq, st;
		RETRY(mon_get_nonce(&c, &n, &seq, &st, &h), r);
		if (r) print_err("nonce", r); else { printf("hb_seq %u state %s nonce 0x%016" PRIx64 "\n", seq, mon_hbstate_name(st), n); print_health(&h); rc = 0; }
	} else if (!strcmp(argv[a], "answer")) {
		uint64_t n; uint32_t seq, st, v, d;
		r = get_nonce_retry(&c, &n, &seq, &st, &h);
		if (r) { print_err("nonce", r); }
		else if (st != 1 && st != 3) { printf("no open challenge (state %s, hb_seq %u): nothing to answer\n", mon_hbstate_name(st), seq); rc = 0; }
		else if (n == 0) { fprintf(stderr, "could not obtain a nonce for hb_seq %u (the challenge kept changing)\n", seq); rc = 1; }
		else {
			RETRY(mon_submit_answer(&c, seq, n, &v, &d, &h), r);
			if (r) print_err("answer", r); else { printf("hb_seq %u answered: %s (detail %u)\n", seq, mon_verdict23_name(v), d); print_health(&h); rc = 0; }
		}
	} else if (!strcmp(argv[a], "frame") && a + 3 < argc) {
		uint8_t key[32], f[MON_FRAME_LEN]; int have = 0, bad = 0, i; uint32_t v, d;
		for (i = a + 4; i < argc; i++) {
			if (!strcmp(argv[i], "--key-file") && i + 1 < argc) have = !load_key_file(argv[++i], key);
			else if (!strcmp(argv[i], "--key-hex") && i + 1 < argc) have = !parse_hex32(argv[++i], key);
			else if (!strcmp(argv[i], "--bad-tag")) bad = 1;
		}
		if (!have) { fprintf(stderr, "frame needs a valid --key-file or --key-hex (32 bytes)\n"); mon_close(&c); return 2; }
		mon_build_frame(key, (uint32_t)strtoul(argv[a + 1], NULL, 0), (uint32_t)strtoul(argv[a + 2], NULL, 0), (uint32_t)strtoul(argv[a + 3], NULL, 0), f);
		memset(key, 0, sizeof(key));
		if (bad) f[12] ^= 1;
		RETRY(mon_submit_frame(&c, f, &v, &d, &h), r);
		if (r) print_err("frame", r); else { printf("verdict %s (detail %u)\n", mon_verdict17_name(v), d); print_health(&h); rc = 0; }
	} else if (!strcmp(argv[a], "soak") && a + 1 < argc) {
		uint8_t key[32]; int have = 0, rt = 0, i; unsigned period = 100;
		for (i = a + 2; i < argc; i++) {
			if (!strcmp(argv[i], "--key-file") && i + 1 < argc) have = !load_key_file(argv[++i], key);
			else if (!strcmp(argv[i], "--period-ms") && i + 1 < argc) period = (unsigned)strtoul(argv[++i], NULL, 0);
			else if (!strcmp(argv[i], "--rt")) rt = 1;
		}
		if (!have) { fprintf(stderr, "soak needs a valid --key-file (32 bytes raw or 64 hex digits)\n"); mon_close(&c); return 2; }
		rc = cmd_soak(&c, (unsigned)strtoul(argv[a + 1], NULL, 0), key, period, rt);
		memset(key, 0, sizeof(key));
	} else {
		usage(); rc = 2;
	}
	mon_close(&c);
	return rc;
}
