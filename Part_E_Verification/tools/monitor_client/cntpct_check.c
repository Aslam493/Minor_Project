// SPDX-License-Identifier: BSD-2-Clause
/*
 * C4.5 CNTPCT sanity (prerequisite of C5), read-only. Samples command 20 page 0 every PERIOD ms for SECONDS seconds and checks, over the whole run:
 *   1. now_ticks (the secure counter as read by whichever core serves the PTA call) never goes backwards and is strictly increasing between samples;
 *   2. the counter rate against CLOCK_MONOTONIC_RAW (expected 19.2 MHz; note: both come from the same architected timer, so this checks the 19.2 MHz
 *      assumption, not the crystal);
 *   3. Core 1's own stamp, last_poll = now_ticks - core1_poll_age_ticks: never in the future (age <= now), monotonic non-decreasing, and age stays small
 *      (a Core 1 clock that disagreed with the PTA cores would show a large or negative age);
 *   4. poll_count never decreases.
 * usage: cntpct_check [seconds (default 300)] [period ms (default 100)]    Exit code 0 = all checks pass.
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "mon_client.h"

static uint64_t ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC_RAW, &t); return (uint64_t)t.tv_sec * 1000000000ull + t.tv_nsec; }

int main(int argc, char **argv)
{
	unsigned secs = argc > 1 ? (unsigned)strtoul(argv[1], NULL, 0) : 300, per = argc > 2 ? (unsigned)strtoul(argv[2], NULL, 0) : 100;
	uint64_t start_ns; struct mon_ctx c = {0}; struct mon_status0 s; struct mon_health h; uint64_t t_first_ns = 0, tk_first = 0, prev_now = 0, prev_last = 0, prev_poll = 0, next, age_max = 0, age_min = ~0ull, pc_first = 0;
	unsigned long n = 0, back = 0, notinc = 0, future = 0, last_back = 0, poll_back = 0, fail = 0; double age_sum = 0;
	uint64_t t_ns = 0, tk_last = 0, pc_last = 0;

	if (per < 10 || mon_open(&c) != TEEC_SUCCESS) { fprintf(stderr, "cannot open the PTA\n"); return 2; }
	next = ns(); start_ns = next;
	for (; ns() - start_ns < (uint64_t)secs * 1000000000ull; ) {
		uint64_t now_ns; TEEC_Result r;
		now_ns = ns();
		r = mon_get_status0(&c, &s, &h);
		if (r != TEEC_SUCCESS) { fail++; continue; }
		if (!n) { t_first_ns = now_ns; tk_first = s.now_ticks; pc_first = s.poll_count; next = now_ns; }
		else {
			if (s.now_ticks < prev_now) back++;
			else if (s.now_ticks == prev_now) notinc++;
			if (s.poll_count < prev_poll) poll_back++;
		}
		if (s.core1_poll_age_ticks > s.now_ticks) future++;
		else {
			uint64_t last = s.now_ticks - s.core1_poll_age_ticks;
			if (n && last < prev_last) last_back++;
			prev_last = last;
			if (s.core1_poll_age_ticks > age_max) age_max = s.core1_poll_age_ticks;
			if (s.core1_poll_age_ticks < age_min) age_min = s.core1_poll_age_ticks;
			age_sum += (double)s.core1_poll_age_ticks;
		}
		prev_now = s.now_ticks; prev_poll = s.poll_count; t_ns = now_ns; tk_last = s.now_ticks; pc_last = s.poll_count; n++;
		next += (uint64_t)per * 1000000ull;
		while (ns() < next) { struct timespec d = {0, 200000}; nanosleep(&d, NULL); }
	}
	mon_close(&c);
	if (n < 2) { printf("too few samples\n"); return 1; }
	{
		double dt = (double)(t_ns - t_first_ns) / 1e9, mhz = (double)(tk_last - tk_first) / dt / 1e6, ppm = (mhz / 19.2 - 1.0) * 1e6;
		printf("samples %lu over %.1f s (period %u ms), failed calls %lu\n", n, dt, per, fail);
		printf("1. now_ticks: went backwards %lu times, not increasing %lu times\n", back, notinc);
		printf("2. counter rate vs CLOCK_MONOTONIC_RAW: %.6f MHz (%+.1f ppm from 19.2 MHz)\n", mhz, ppm);
		printf("3. Core 1 stamp: age > now %lu times, last_poll went backwards %lu times; age min %.2f us, mean %.2f us, max %.2f us\n", future, last_back,
		       (double)age_min / 19.2, age_sum / (double)(n - future) / 19.2, (double)age_max / 19.2);
		printf("4. poll_count went backwards %lu times; Core 1 passes %" PRIu64 " in %.1f s (%.3f M/s)\n", poll_back, pc_last - pc_first, dt, (double)(pc_last - pc_first) / dt / 1e6);
		{
			int ok = !back && !notinc && !future && !last_back && !poll_back && !fail && mhz > 19.0 && mhz < 19.4 && age_max < 19200ull * 5;
			printf("CNTPCT sanity: %s\n", ok ? "PASS" : "FAIL");
			return ok ? 0 : 1;
		}
	}
}
