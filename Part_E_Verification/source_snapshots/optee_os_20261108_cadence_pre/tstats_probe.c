// SPDX-License-Identifier: BSD-2-Clause
/*
 * Reader and driver for the TEST-STATISTICS firmware build (commands 24 and 25). Refuses politely on a normal build (NOT_SUPPORTED).
 *   tstats_probe dump            print hold-time, attempt-interval and wait statistics (ticks and microseconds at 19.2 MHz)
 *   tstats_probe reset           zero the statistics
 *   tstats_probe x9 [trials]     X9: ask Core 1 to hold the lock for 10 ms (the clamp) and call the read-only status command at once;
 *                                count BUSY results; then print how long the last exhausted PTA wait lasted and how many attempts it made
 * It changes nothing on the card or the CAN chip; the Core 1 hold only delays Core 1's own monitor pass.
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <tee_client_api.h>
#include <pta_rpi3_can_spi_poc.h>
#include "mon_tstats.h"

#define CMD_STATS 24
#define CMD_CTRL 25
#define US(t) ((double)(t) / 19.2)

static TEEC_Session sess;
static TEEC_Result ctrl(uint32_t op, uint64_t arg, uint32_t *done)
{
	TEEC_Operation o; uint32_t origin = 0; TEEC_Result r;
	memset(&o, 0, sizeof o);
	o.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_INOUT, TEEC_VALUE_INPUT, TEEC_NONE, TEEC_NONE);
	o.params[0].value.a = op; o.params[0].value.b = (uint32_t)arg; o.params[1].value.a = (uint32_t)(arg >> 32);
	r = TEEC_InvokeCommand(&sess, CMD_CTRL, &o, &origin);
	if (r == TEEC_SUCCESS && done) *done = o.params[0].value.a;
	return r;
}
static TEEC_Result get(struct mon_tstats *t)
{
	TEEC_Operation o; uint32_t origin = 0; static struct mon_tstats buf __attribute__((aligned(64))); TEEC_Result r;
	memset(&o, 0, sizeof o);
	o.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_OUTPUT, TEEC_NONE, TEEC_NONE, TEEC_NONE);
	o.params[0].tmpref.buffer = &buf; o.params[0].tmpref.size = sizeof buf;
	r = TEEC_InvokeCommand(&sess, CMD_STATS, &o, &origin);
	if (r == TEEC_SUCCESS) { if (o.params[0].tmpref.size != sizeof buf) return TEEC_ERROR_GENERIC; *t = buf; }
	return r;
}
static void dump(const struct mon_tstats *t)
{
	int k;
	printf("holds: count %" PRIu64 " | mean %.2f us | max %" PRIu64 " ticks = %.2f us | max by PTA %.2f us | max by Core 1 %.2f us\n", t->hold_count,
	       t->hold_count ? US(t->hold_sum) / t->hold_count : 0.0, t->hold_max, US(t->hold_max), US(t->hold_max_ctx[0]), US(t->hold_max_ctx[1]));
	printf("hold histogram (ticks, floor log2 buckets; 19.2 ticks = 1 us):\n");
	for (k = 0; k < MON_TS_HIST; k++) if (t->hold_hist[k]) printf("  %s2^%-2d .. %-7s %10u holds\n", k == MON_TS_HIST - 1 ? ">= " : "   ", k, k == MON_TS_HIST - 1 ? "" : "2^next", t->hold_hist[k]);
	if (t->hold_count) { uint64_t acc = 0, p99 = 0; for (k = 0; k < MON_TS_HIST; k++) { acc += t->hold_hist[k]; if (!p99 && acc * 100 >= t->hold_count * 99) p99 = (k == MON_TS_HIST - 1) ? t->hold_max : (1ull << (k + 1)); }
		printf("p99 hold <= %.2f us (upper edge of the bucket holding the 99th percentile; coarse, factor 2)\n", US(p99)); }
	printf("Core 1 attempt interval: count %" PRIu64 " | min %.2f us | mean %.2f us | max %.2f us\n", t->att_count, US(t->att_min), t->att_count ? US(t->att_sum) / t->att_count : 0.0, US(t->att_max));
	printf("exhausted PTA waits: %u | last: %.2f us waited, %u attempts (%s)\n", t->wait_exhausted, US(t->wait_last_ticks), t->wait_last_attempts,
	       t->wait_last_attempts ? "attempts per ms = attempts / ms waited" : "none yet");
	if (t->wait_exhausted && t->wait_last_ticks) printf("  -> %.0f attempts per ms while waiting\n", t->wait_last_attempts / (US(t->wait_last_ticks) / 1000.0));
	printf("Core 1 hold requests done: %u (pending %" PRIu64 " ticks)\n", t->hold_req_done, t->hold_req_ticks);
}
int main(int argc, char **argv)
{
	TEEC_Context ctx; TEEC_UUID uuid = PTA_RPI3_CAN_SPI_POC_UUID; uint32_t origin = 0, done = 0; struct mon_tstats t; TEEC_Result r;
	const char *cmd = argc > 1 ? argv[1] : "dump";

	if (TEEC_InitializeContext(NULL, &ctx) != TEEC_SUCCESS || TEEC_OpenSession(&ctx, &sess, &uuid, TEEC_LOGIN_PUBLIC, NULL, NULL, &origin) != TEEC_SUCCESS) return 1;
	if (!strcmp(cmd, "reset")) { r = ctrl(0, 0, &done); printf("reset: 0x%08" PRIx32 "\n", (uint32_t)r); }
	else if (!strcmp(cmd, "dump")) { r = get(&t); if (r) printf("not a test-statistics build (0x%08" PRIx32 ")\n", (uint32_t)r); else dump(&t); }
	else if (!strcmp(cmd, "x9")) {
		int n = argc > 2 ? atoi(argv[2]) : 200, i, busy = 0, ok = 0, other = 0; uint8_t buf[128];
		if ((r = ctrl(0, 0, &done))) { printf("not a test-statistics build (0x%08" PRIx32 ")\n", (uint32_t)r); return 1; }
		for (i = 0; i < n; i++) {
			TEEC_Operation o; struct timespec ts = {0, 30000000};		/* 30 ms between trials: the 10 ms hold is over */
			if (ctrl(1, 192000, &done)) { other++; continue; }		/* hold request: 10 ms = 192000 ticks at 19.2 MHz */
			memset(&o, 0, sizeof o); o.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_INOUT, TEEC_MEMREF_TEMP_OUTPUT, TEEC_VALUE_OUTPUT, TEEC_VALUE_OUTPUT);
			o.params[1].tmpref.buffer = buf; o.params[1].tmpref.size = sizeof buf;
			r = TEEC_InvokeCommand(&sess, PTA_RPI3_CAN_SPI_POC_CMD_MON_GET_STATUS, &o, &origin);
			if (r == TEEC_ERROR_BUSY) busy++; else if (r == TEEC_SUCCESS) ok++; else other++;
			nanosleep(&ts, NULL);
		}
		printf("X9 trials %d: status call returned BUSY %d, SUCCESS %d, other %d\n", n, busy, ok, other);
		r = get(&t);
		if (r)
			return 1;
		dump(&t);
	} else { fprintf(stderr, "usage: tstats_probe dump | reset | x9 [trials]\n"); return 2; }
	TEEC_CloseSession(&sess); TEEC_FinalizeContext(&ctx);
	return 0;
}
