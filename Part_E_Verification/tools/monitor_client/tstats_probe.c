// SPDX-License-Identifier: BSD-2-Clause
/*
 * Reader and driver for the TEST-STATISTICS firmware build (commands 24 and 25). Refuses politely on a normal build (NOT_SUPPORTED).
 *   tstats_probe dump            print hold-time, attempt-interval and wait statistics (ticks and microseconds at 19.2 MHz)
 *   tstats_probe reset           zero the statistics
 *   tstats_probe cadence N       set passes per Core 1 lock attempt (power of two, 16 to 65536; 0 = firmware default 256)
 *   tstats_probe w1              W1: ask the CPU (AT S1E1R) how the monitor lock is mapped on the PTA core and on Core 1 (command 26)
 *   tstats_probe w7 I-ACCEPT-FORCED-PANIC   W7: panic Core 1 on purpose (command 25 op 4); test build only
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
#define CMD_W1 26
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
	printf("Core 1 hold requests done: %u (pending %" PRIu64 " ticks) | cadence: %u passes per lock attempt\n", t->hold_req_done, t->hold_req_ticks, t->cadence_mask ? t->cadence_mask + 1 : 256);
}
int main(int argc, char **argv)
{
	TEEC_Context ctx; TEEC_UUID uuid = PTA_RPI3_CAN_SPI_POC_UUID; uint32_t origin = 0, done = 0; struct mon_tstats t; TEEC_Result r;
	const char *cmd = argc > 1 ? argv[1] : "dump";

	if (TEEC_InitializeContext(NULL, &ctx) != TEEC_SUCCESS || TEEC_OpenSession(&ctx, &sess, &uuid, TEEC_LOGIN_PUBLIC, NULL, NULL, &origin) != TEEC_SUCCESS) return 1;
	if (!strcmp(cmd, "reset")) { r = ctrl(0, 0, &done); printf("reset: 0x%08" PRIx32 "\n", (uint32_t)r); }
	else if (!strcmp(cmd, "cadence") && argc > 2) { r = ctrl(3, strtoull(argv[2], NULL, 0), &done); printf("cadence %s: 0x%08" PRIx32 " (0 = accepted)\n", argv[2], (uint32_t)r); }
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
	} else if (!strcmp(cmd, "stack")) {		/* C3 / A1.6: Core 1 tmp stack watermark (figure is conservative: includes the region above the fill point) */
		uint32_t used = 0, size = 0, atfill = 0; const uint32_t margin = argc > 2 ? (uint32_t)strtoul(argv[2], NULL, 0) : 2048;
		TEEC_Result r5 = ctrl(5, 0, &used), r6 = ctrl(6, 0, &size), r8 = ctrl(8, 0, &atfill);
		if (r5 || r6 || r8) { printf("not a test-statistics build (0x%08" PRIx32 ")\n", (uint32_t)(r5 | r6 | r8)); TEEC_CloseSession(&sess); TEEC_FinalizeContext(&ctx); return 3; }
		if (!size) { printf("stack: Core 1 has not filled its stack yet (it does so at its first lock attempt)\n"); }
		else printf("Core 1 tmp stack: size %u bytes | already in use at the fill point %u | deepest touched %u bytes | headroom %u bytes (required margin %u) -> %s\n", size, atfill, used, size - used, margin, (size - used >= margin) ? "OK" : "BELOW MARGIN");
	} else if (!strcmp(cmd, "w7")) {		/* W7: DELIBERATELY panic Core 1 (test build only). Needs the literal word so it cannot be run by accident. */
		if (argc < 3 || strcmp(argv[2], "I-ACCEPT-FORCED-PANIC")) { fprintf(stderr, "w7 forces a Core 1 panic; run: tstats_probe w7 I-ACCEPT-FORCED-PANIC\n"); TEEC_CloseSession(&sess); TEEC_FinalizeContext(&ctx); return 2; }
		r = ctrl(4, 0x57375737u, &done); if (r == TEEC_SUCCESS) printf("w7 armed: Core 1 will panic at its next lock attempt (within about 1 ms)\n"); else printf("w7 REFUSED (0x%08" PRIx32 "): not a test-statistics build, nothing was armed\n", (uint32_t)r);
	} else if (!strcmp(cmd, "w1")) {		/* W1: memory attributes of the monitor lock, as seen by the PTA core and by Core 1 */
		uint64_t w[8] __attribute__((aligned(64))); TEEC_Operation o; const char *who[2] = {"PTA core ", "Core 1   "}; int k, bad = 0;
		memset(&o, 0, sizeof o); o.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_OUTPUT, TEEC_NONE, TEEC_NONE, TEEC_NONE); o.params[0].tmpref.buffer = w; o.params[0].tmpref.size = sizeof w;
		r = TEEC_InvokeCommand(&sess, CMD_W1, &o, &origin);
		if (r == 0xFFFF000A) { printf("not a test-statistics build (0x%08" PRIx32 ")\n", (uint32_t)r); TEEC_CloseSession(&sess); TEEC_FinalizeContext(&ctx); return 3; }
		printf("W1 result 0x%08" PRIx32 " (0 = both cores answered; 0xffff3001 = Core 1 did not answer in 100 ms)  lock VA 0x%016" PRIx64 "\n", (uint32_t)r, w[0]);
		for (k = 0; k < 2; k++) {
			uint64_t par = w[1 + 3 * k], sctlr = w[2 + 3 * k], mair = w[3 + 3 * k]; unsigned attr = (unsigned)(par >> 56), sh = (unsigned)((par >> 7) & 3), f = (unsigned)(par & 1);
			unsigned in = attr & 0xF, out = attr >> 4; int normal = out != 0 && in != 0, cached = normal && (in >> 2) != 0 && in != 4 && (out >> 2) != 0 && out != 4;
			int ok = (k == 1 && !w[7]) ? 0 : (!f && normal && cached && sh == 3 && (sctlr & 1) && (sctlr & 4));
			printf("  %s PAR_EL1 0x%016" PRIx64 ": F=%u (0 = translated) ATTR=0x%02x SH=%u PA page 0x%" PRIx64 " | SCTLR_EL1 0x%" PRIx64 " (M=%u C=%u) | MAIR_EL1 0x%016" PRIx64 " -> %s\n", who[k], par, f, attr, sh,
			       (uint64_t)(par & 0x0000FFFFFFFFF000ULL), sctlr, (unsigned)(sctlr & 1), (unsigned)((sctlr >> 2) & 1), mair, (k == 1 && !w[7]) ? "NO ANSWER" : (ok ? "Normal memory, cacheable, inner shareable: OK" : "NOT as required"));
			if (!ok) bad = 1;
		}
		printf("W1 verdict: %s\n", bad ? "FAIL / incomplete" : "PASS (lock memory is Normal, cacheable, inner-shareable on both cores; MMU and D-cache on)");
	} else { fprintf(stderr, "usage: tstats_probe dump | reset | cadence N | x9 [trials] | w1 | stack [margin] | w7 I-ACCEPT-FORCED-PANIC\n"); return 2; }
	TEEC_CloseSession(&sess); TEEC_FinalizeContext(&ctx);
	return 0;
}
