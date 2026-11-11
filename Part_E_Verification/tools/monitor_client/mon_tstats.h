/* SPDX-License-Identifier: BSD-2-Clause */
/* Client copy of struct rpi3_mon_tstats (TEST-STATISTICS BUILDS ONLY, commands 24 and 25). Layout is checked against the real header by the host test. */
#ifndef MON_TSTATS_H
#define MON_TSTATS_H
#include <stdint.h>
#define MON_TS_HIST 16
struct mon_tstats {
	uint64_t hold_start;
	uint64_t hold_count, hold_sum, hold_max;
	uint64_t hold_max_ctx[2];
	uint32_t hold_hist[MON_TS_HIST];
	uint64_t wait_last_ticks;
	uint32_t wait_last_attempts, wait_exhausted;
	uint64_t att_prev, att_count, att_sum, att_min, att_max;
	uint64_t hold_req_ticks;
	uint32_t hold_req_done, cadence_mask;
} __attribute__((aligned(64)));
#endif
