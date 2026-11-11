// SPDX-License-Identifier: BSD-2-Clause
/*
 * PTA latency probe (plan C4.2): N back-to-back calls of the read-only monitor status command (20, page 0) over ONE session, each timed with
 * CLOCK_MONOTONIC_RAW. Prints percentiles, max, call rate and the monitor's own lock-collision counters before and after.
 * Read-only: command 20 only evaluates the monitor's own deadlines. Run from /tmp (RAM). Load generators are separate on purpose:
 * run this alone for the idle number, or beside `stress`/`yes > /dev/null` for the CPU-load number. CAN/SPI flood numbers need a sender.
 * usage: latency_probe [calls (default 100000)]
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <tee_client_api.h>
#include <pta_rpi3_can_spi_poc.h>

static uint64_t ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC_RAW, &t); return (uint64_t)t.tv_sec * 1000000000ull + t.tv_nsec; }
static int cmp(const void *a, const void *b) { uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return x < y ? -1 : x > y; }

int main(int argc, char **argv)
{
	TEEC_Context ctx; TEEC_Session s; TEEC_UUID uuid = PTA_RPI3_CAN_SPI_POC_UUID; TEEC_Operation op; uint32_t origin = 0, busy = 0, other = 0;
	uint8_t buf[128]; size_t n = argc > 1 ? strtoul(argv[1], NULL, 0) : 100000, i; uint64_t *lat, t0, tend, skip0 = 0, skip1 = 0, to0 = 0, to1 = 0;

	lat = malloc(n * sizeof *lat);
	if (!lat || TEEC_InitializeContext(NULL, &ctx) != TEEC_SUCCESS || TEEC_OpenSession(&ctx, &s, &uuid, TEEC_LOGIN_PUBLIC, NULL, NULL, &origin) != TEEC_SUCCESS) return 1;
	for (i = 0; i < n; i++) {
		memset(&op, 0, sizeof op);
		op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_INOUT, TEEC_MEMREF_TEMP_OUTPUT, TEEC_VALUE_OUTPUT, TEEC_VALUE_OUTPUT);
		op.params[1].tmpref.buffer = buf; op.params[1].tmpref.size = sizeof buf;
		t0 = ns();
		TEEC_Result r = TEEC_InvokeCommand(&s, PTA_RPI3_CAN_SPI_POC_CMD_MON_GET_STATUS, &op, &origin);
		lat[i] = ns() - t0;
		if (r == TEEC_ERROR_BUSY) busy++; else if (r != TEEC_SUCCESS) other++;
		if (i == 0) { skip0 = op.params[3].value.b; to0 = op.params[3].value.a; }
		skip1 = op.params[3].value.b; to1 = op.params[3].value.a;
	}
	tend = 0; for (i = 0; i < n; i++) tend += lat[i];
	{ uint64_t sum = tend; qsort(lat, n, sizeof *lat, cmp);
	  printf("calls %zu | BUSY %u | other errors %u | total %.2f s | mean %.0f us | rate %.0f calls/s\n", n, busy, other, sum / 1e9, sum / 1e3 / n, n / (sum / 1e9));
	  printf("latency us: min %.0f p50 %.0f p90 %.0f p99 %.0f p99.9 %.0f max %.0f\n", lat[0] / 1e3, lat[n / 2] / 1e3, lat[n * 90 / 100] / 1e3, lat[n * 99 / 100] / 1e3, lat[n * 999 / 1000] / 1e3, lat[n - 1] / 1e3);
	  printf("monitor counters during the run: skip_total %" PRIu64 " -> %" PRIu64 ", pta_lock_timeouts %" PRIu64 " -> %" PRIu64 "\n", skip0, skip1, to0, to1); }
	TEEC_CloseSession(&s); TEEC_FinalizeContext(&ctx); free(lat);
	return 0;
}
