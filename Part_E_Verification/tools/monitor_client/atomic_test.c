// SPDX-License-Identifier: BSD-2-Clause
/*
 * Check V2 helper: do concurrent exclusive-access increments lose updates on Normal-World memory?
 * Three threads (default: pinned to CPUs 0, 2, 3, the cores Linux owns on the Sep-20 image) each add N to one shared 32-bit word
 * with the same instruction sequence atomic_inc32() uses in OP-TEE (ldaxr / add / stxr / retry). The final value must equal 3 x N.
 * This tests the instruction sequence and the CPU/memory behavior in Normal World only; it does NOT touch Secure World memory.
 * On a non-AArch64 host it falls back to __atomic_fetch_add so the program itself can be tested.
 *
 * usage: atomic_test [iterations-per-thread (default 2000000)] [cpu,cpu,cpu (default 0,2,3; "none" = no pinning)]
 */
#define _GNU_SOURCE
#include <inttypes.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NTHREADS 3

static volatile uint32_t shared __attribute__((aligned(64)));
static uint32_t iters;
static int cpus[NTHREADS] = {0, 2, 3};
static int pin = 1;

static inline void inc32(volatile uint32_t *p)
{
#if defined(__aarch64__)
	uint32_t v, st;
	do {
		__asm__ __volatile__("ldaxr %w0, [%2]\n\tadd %w0, %w0, #1\n\tstxr %w1, %w0, [%2]"
				     : "=&r"(v), "=&r"(st) : "r"(p) : "memory");
	} while (st);
#else
	__atomic_fetch_add(p, 1, __ATOMIC_SEQ_CST);
#endif
}

static void *worker(void *arg)
{
	int id = (int)(intptr_t)arg;
	uint32_t i;

	if (pin) {
		cpu_set_t set;
		CPU_ZERO(&set);
		CPU_SET(cpus[id], &set);
		if (pthread_setaffinity_np(pthread_self(), sizeof(set), &set))
			fprintf(stderr, "warning: could not pin thread %d to cpu %d\n", id, cpus[id]);
	}
	for (i = 0; i < iters; i++)
		inc32(&shared);
	return NULL;
}

int main(int argc, char **argv)
{
	pthread_t t[NTHREADS];
	uint32_t expected;
	int i;

	iters = argc > 1 ? (uint32_t)strtoul(argv[1], NULL, 0) : 2000000U;
	if (argc > 2) {
		if (!strcmp(argv[2], "none")) pin = 0;
		else if (sscanf(argv[2], "%d,%d,%d", &cpus[0], &cpus[1], &cpus[2]) != 3) { fprintf(stderr, "bad cpu list\n"); return 2; }
	}
	shared = 0;
	for (i = 0; i < NTHREADS; i++)
		if (pthread_create(&t[i], NULL, worker, (void *)(intptr_t)i)) { perror("pthread_create"); return 2; }
	for (i = 0; i < NTHREADS; i++)
		pthread_join(t[i], NULL);
	expected = iters * NTHREADS;
	printf("V2 atomic test: %d threads x %u increments, expected %u, got %u: %s\n", NTHREADS, iters, expected, (uint32_t)shared,
	       shared == expected ? "PASS (no lost increments)" : "FAIL (lost increments)");
	return shared == expected ? 0 : 1;
}
