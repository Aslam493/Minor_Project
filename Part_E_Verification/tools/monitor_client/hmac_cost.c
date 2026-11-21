// SPDX-License-Identifier: BSD-2-Clause
/*
 * hmac_cost - times the Secure World cost of command 31 (submit packets) for batches of n packets, with two kinds of packet:
 *
 *   hmac    can_id 0x123 and an INVALID tag: Secure World computes the HMAC, judges AUTH_FAIL and changes no state (the counter does not move)
 *   nohmac  can_id 0x124: rejected as MALFORMED BEFORE the HMAC is computed, no state change either
 *
 *   usage: hmac_cost [--cpu N] [--reps R]          (Mode P image only; stop can_forwarder first; no key needed; writes nothing persistent)
 *
 * For every kind and every n in {0, 1, 2, 4, 8, 16, 32} it waits for a new OPEN packet request (one comes every 50 ms), answers it at once with n packets and times the
 * call. It prints, per kind and n, the median and maximum call time in microseconds over R requests (default 30), and a least-squares line time = base + slope * n.
 * The difference of the two slopes is the cost of one HMAC; the base of "nohmac" is the cost of a call that does no packet work.
 * Side effects: AUTH_FAIL and MALFORMED counters/events in the monitor's RAM log and missed-heartbeat events while it runs (nobody answers the heartbeat). Not a CAN test.
 */
#define _GNU_SOURCE
#include <inttypes.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "mon_client.h"

static struct mon_ctx C;

static uint64_t now_us(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000u + (uint64_t)t.tv_nsec / 1000u; }
static void msleep(unsigned ms) { struct timespec t = { 0, (long)ms * 1000000L }; nanosleep(&t, NULL); }
static int cmp_u32(const void *a, const void *b) { uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b; return (x > y) - (x < y); }

static void put_le32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

int main(int argc, char **argv)
{
	static const unsigned ns[] = { 0, 1, 2, 4, 8, 16, 32 };
	static const char *const kind_name[2] = { "hmac  ", "nohmac" };
	unsigned reps = 30, k, i, j, a; int cpu = -1;
	static uint8_t pk[MON_PQ_MAX_BATCH * MON_FRAME_LEN];
	static uint32_t t[2][7][512];
	double slope[2], base[2];
	struct mon_health h; struct mon_status0 z; TEEC_Result r; uint32_t last = 0xFFFFFFFFu;

	for (a = 1; a < (unsigned)argc; a++) {
		if (!strcmp(argv[a], "--cpu") && a + 1 < (unsigned)argc) cpu = atoi(argv[++a]);
		else if (!strcmp(argv[a], "--reps") && a + 1 < (unsigned)argc) reps = (unsigned)atoi(argv[++a]);
		else { fprintf(stderr, "usage: hmac_cost [--cpu N] [--reps R]\n"); return 2; }
	}
	if (reps < 3 || reps > 500) { fprintf(stderr, "--reps 3..500\n"); return 2; }
	if (cpu >= 0) {
		cpu_set_t set; CPU_ZERO(&set); CPU_SET(cpu, &set);
		if (sched_setaffinity(0, sizeof set, &set)) { perror("sched_setaffinity"); return 2; }
	}
	r = mon_open(&C); if (r) { fprintf(stderr, "cannot open the monitor (0x%08x)\n", (unsigned)r); return 1; }
	memset(&z, 0, sizeof z); if (mon_get_status0(&C, &z, &h) || z.mode != 2) { printf("NOT the Mode P image: stopping\n"); mon_close(&C); return 2; }

	for (k = 0; k < 2; k++) {
		for (j = 0; j < 7; j++) {
			for (i = 0; i < reps; i++) {
				uint32_t seq, st, mb, due, v, proc; unsigned n = ns[j], p; uint64_t t0, t1; int tries;
				for (p = 0; p < n; p++) {					/* 44 bytes: can_id | counter | payload | tag (all zero = invalid) */
					uint8_t *m = pk + (size_t)p * MON_FRAME_LEN; memset(m, 0, MON_FRAME_LEN);
					put_le32(m, k == 0 ? 0x123u : 0x124u); put_le32(m + 4, 0x7FFF0000u + p); put_le32(m + 8, p);
				}
				for (tries = 0; tries < 2000; tries++) {			/* wait for a NEW open request */
					r = mon_get_packet_request(&C, &seq, &st, &mb, &due, &h);
					if (!r && (st == 1 || st == 3) && seq != last) break;
					msleep(2);
				}
				if (tries == 2000) { fprintf(stderr, "no open request\n"); mon_close(&C); return 1; }
				t0 = now_us(); r = mon_submit_packets(&C, seq, n, pk, &v, &proc, &h); t1 = now_us();
				if (r) { i--; msleep(5); continue; }				/* BUSY or error: repeat this sample */
				last = seq; t[k][j][i] = (uint32_t)(t1 - t0);
			}
			qsort(t[k][j], reps, sizeof(uint32_t), cmp_u32);
		}
		{ double sx = 0, sy = 0, sxx = 0, sxy = 0; for (j = 0; j < 7; j++) { double x = ns[j], y = t[k][j][reps / 2]; sx += x; sy += y; sxx += x * x; sxy += x * y; }
		  slope[k] = (7 * sxy - sx * sy) / (7 * sxx - sx * sx); base[k] = (sy - slope[k] * sx) / 7; }
	}
	printf("hmac_cost: %u requests per cell, cpu %d, Mode P image\n", reps, cpu);
	printf("  kind     n   median_us   max_us\n");
	for (k = 0; k < 2; k++) for (j = 0; j < 7; j++) printf("  %s %2u   %9u  %8u\n", kind_name[k], ns[j], t[k][j][reps / 2], t[k][j][reps - 1]);
	for (k = 0; k < 2; k++) printf("fit %s: time = %.0f us + %.1f us per packet\n", kind_name[k], base[k], slope[k]);
	printf("one HMAC (difference of the slopes) = %.1f us\n", slope[0] - slope[1]);
	mon_close(&C);
	return 0;
}
