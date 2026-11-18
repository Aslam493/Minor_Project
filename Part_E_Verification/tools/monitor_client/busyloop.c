// SPDX-License-Identifier: BSD-2-Clause
#define _GNU_SOURCE
/* busyloop - CPU load generator for the Mode P timing test: spins on one core for SECONDS. usage: busyloop CORE SECONDS. Only cores 0, 2, 3
 * are accepted (core 1 belongs to Secure World). Needs no TEE access. */
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
int main(int argc, char **argv)
{
	int core; unsigned secs; cpu_set_t set; struct timespec t; volatile unsigned long x = 0; unsigned long long end;
	if (argc != 3) { fprintf(stderr, "usage: busyloop CORE(0|2|3) SECONDS\n"); return 2; }
	core = atoi(argv[1]); secs = (unsigned)strtoul(argv[2], NULL, 0);
	if (core != 0 && core != 2 && core != 3) { fprintf(stderr, "busyloop: only cores 0, 2 and 3 are allowed\n"); return 2; }
	CPU_ZERO(&set); CPU_SET(core, &set);
	if (sched_setaffinity(0, sizeof set, &set)) { perror("sched_setaffinity"); return 1; }
	clock_gettime(CLOCK_MONOTONIC, &t); end = (unsigned long long)t.tv_sec + secs;
	for (;;) { unsigned i; for (i = 0; i < 1000000; i++) x++; clock_gettime(CLOCK_MONOTONIC, &t); if ((unsigned long long)t.tv_sec >= end) break; }
	return 0;
}
