// SPDX-License-Identifier: BSD-2-Clause
/*
 * torn_read - does a 64-bit access tear between two A53 cores on this SoC?
 * (Component 2, subcomponent 2.1, G7 review: "torn reads").
 *
 *   usage: torn_read <variant> <writer_cpu> <reader_cpu> <iterations>
 *
 * A writer thread alternates a 64-bit location between two patterns, 0 and ~0.
 * A reader thread on another core loads it as fast as it can. Any value that is
 * neither pattern is a TORN READ (half old, half new).
 *
 *   aligned64     64-bit relaxed atomic store/load at an 8-byte aligned address.  THE CASE UNDER TEST.
 *   two32         CONTROL: the writer stores the two 32-bit halves separately.   MUST tear.
 *   crossline     CONTROL: a 64-bit str/ldr that straddles a 64-byte cache line (address +60).  Expected to tear.
 *   within16      a misaligned 64-bit access that stays inside one 16-byte block (address +4).
 *                 The Arm ARM rule (B2.8.2) says it is still single-copy atomic on shareable write-back memory.
 *
 * The controls exist to show that this test CAN detect tearing; "no tear seen" means nothing otherwise.
 * Runs in Linux user space (Normal, Write-Back, Inner Shareable memory). It does not touch Secure World.
 */
#define _GNU_SOURCE
#include <inttypes.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char buf[256] __attribute__((aligned(64)));
static volatile int done;
static int variant, wcpu, rcpu;
static uint64_t iters;

enum { AL = 0, T32 = 1, CROSS = 2, W16 = 3 };

static void pin(int cpu)
{
	cpu_set_t s;

	CPU_ZERO(&s);
	CPU_SET(cpu, &s);
	if (sched_setaffinity(0, sizeof(s), &s)) {
		perror("sched_setaffinity");
		exit(1);
	}
}

static inline void st64_raw(void *p, uint64_t v)   /* a real 64-bit STR at any alignment (no compiler assumptions) */
{
	__asm__ volatile("str %1, [%0]" :: "r"(p), "r"(v) : "memory");
}
static inline uint64_t ld64_raw(void *p)
{
	uint64_t v;

	__asm__ volatile("ldr %0, [%1]" : "=r"(v) : "r"(p) : "memory");
	return v;
}

static void *writer(void *arg)
{
	uint64_t i;

	(void)arg;
	pin(wcpu);
	for (i = 0; i < iters; i++) {
		uint64_t v = (i & 1) ? ~0ull : 0ull;

		switch (variant) {
		case AL:
			__atomic_store_n((uint64_t *)(buf + 0), v, __ATOMIC_RELAXED);
			break;
		case T32:
			__atomic_store_n((uint32_t *)(buf + 0), (uint32_t)v, __ATOMIC_RELAXED);
			__atomic_store_n((uint32_t *)(buf + 4), (uint32_t)(v >> 32), __ATOMIC_RELAXED);
			break;
		case CROSS:
			st64_raw(buf + 60, v);
			break;
		case W16:
			st64_raw(buf + 4, v);
			break;
		}
	}
	__atomic_store_n(&done, 1, __ATOMIC_RELEASE);
	return NULL;
}

int main(int argc, char **argv)
{
	pthread_t w;
	uint64_t reads = 0, torn = 0, seen0 = 0, seenF = 0;
	uint64_t first_torn = 0;

	if (argc != 5) {
		fprintf(stderr, "usage: %s aligned64|two32|crossline|within16 <writer_cpu> <reader_cpu> <iterations>\n", argv[0]);
		return 1;
	}
	if (!strcmp(argv[1], "aligned64")) variant = AL;
	else if (!strcmp(argv[1], "two32")) variant = T32;
	else if (!strcmp(argv[1], "crossline")) variant = CROSS;
	else if (!strcmp(argv[1], "within16")) variant = W16;
	else { fprintf(stderr, "bad variant\n"); return 1; }
	wcpu = atoi(argv[2]);
	rcpu = atoi(argv[3]);
	iters = strtoull(argv[4], NULL, 10);

	pin(rcpu);
	pthread_create(&w, NULL, writer, NULL);
	while (!__atomic_load_n(&done, __ATOMIC_ACQUIRE)) {
		uint64_t v;

		switch (variant) {
		case AL:    v = __atomic_load_n((uint64_t *)(buf + 0), __ATOMIC_RELAXED); break;
		case T32:   v = __atomic_load_n((uint64_t *)(buf + 0), __ATOMIC_RELAXED); break;
		case CROSS: v = ld64_raw(buf + 60); break;
		default:    v = ld64_raw(buf + 4); break;
		}
		reads++;
		if (v == 0) seen0++;
		else if (v == ~0ull) seenF++;
		else { if (!torn) first_torn = v; torn++; }
	}
	pthread_join(w, NULL);
	printf("%-9s writer cpu%d -> reader cpu%d: %" PRIu64 " writes, %" PRIu64 " reads | saw all-zero %" PRIu64
	       "  all-ones %" PRIu64 "  TORN %" PRIu64 "%s\n",
	       argv[1], wcpu, rcpu, iters, reads, seen0, seenF, torn,
	       torn ? "  (first torn value shown below)" : "");
	if (torn)
		printf("          first torn value: 0x%016" PRIx64 "\n", first_torn);
	return 0;
}
