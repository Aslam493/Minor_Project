// SPDX-License-Identifier: BSD-2-Clause
/*
 * pta_latency - time Linux -> PTA calls (Component 2, subcomponent 2.1, G9 review).
 *
 *   usage: pta_latency <cpu> <calls>
 *
 * Pins itself to <cpu>, opens ONE session to the project PTA and times <calls> calls of the
 * harmless status command 12 (read Core 1's heartbeat). Read-only: no other command is used.
 * Prints min / median / p90 / p99 / max / mean in microseconds, the elapsed time and calls/s.
 * Run it once alone, then three at the same time on different cores, to see whether the
 * calls are serialised by OP-TEE (single instance, no TA_FLAG_CONCURRENT).
 * Preliminary figures only: the full measurement matrix (load, SPI, CAN) belongs to Component 4.
 */
#define _GNU_SOURCE
#include <inttypes.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <tee_client_api.h>

#include <pta_rpi3_can_spi_poc.h>

static int cmp_u32(const void *a, const void *b)
{
	uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;

	return (x > y) - (x < y);
}

static uint64_t now_ns(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

int main(int argc, char **argv)
{
	TEEC_UUID uuid = PTA_RPI3_CAN_SPI_POC_UUID;
	TEEC_Context ctx;
	TEEC_Session s;
	TEEC_Operation op;
	uint32_t origin, *lat;
	unsigned long n, i;
	int cpu;
	cpu_set_t set;
	uint64_t t0, t1, sum = 0;

	if (argc != 3) {
		fprintf(stderr, "usage: %s <cpu> <calls>\n", argv[0]);
		return 1;
	}
	cpu = atoi(argv[1]);
	n = strtoul(argv[2], NULL, 10);
	if (n < 100 || n > 200000) {
		fprintf(stderr, "calls must be 100..200000\n");
		return 1;
	}
	CPU_ZERO(&set);
	CPU_SET(cpu, &set);
	if (sched_setaffinity(0, sizeof(set), &set)) {
		perror("sched_setaffinity");
		return 1;
	}
	lat = malloc(n * sizeof(*lat));
	if (!lat || TEEC_InitializeContext(NULL, &ctx) != TEEC_SUCCESS ||
	    TEEC_OpenSession(&ctx, &s, &uuid, TEEC_LOGIN_PUBLIC, NULL, NULL,
			     &origin) != TEEC_SUCCESS) {
		fprintf(stderr, "setup failed\n");
		return 1;
	}
	memset(&op, 0, sizeof(op));
	op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_OUTPUT, TEEC_NONE,
					 TEEC_NONE, TEEC_NONE);
	for (i = 0; i < 200; i++)		/* warm up */
		TEEC_InvokeCommand(&s, PTA_RPI3_CAN_SPI_POC_CMD_GET_CORE1_HEARTBEAT,
				   &op, &origin);
	t0 = now_ns();
	for (i = 0; i < n; i++) {
		uint64_t a = now_ns();

		if (TEEC_InvokeCommand(&s,
				PTA_RPI3_CAN_SPI_POC_CMD_GET_CORE1_HEARTBEAT,
				&op, &origin) != TEEC_SUCCESS) {
			fprintf(stderr, "call failed\n");
			return 1;
		}
		lat[i] = (uint32_t)(now_ns() - a);
		sum += lat[i];
	}
	t1 = now_ns();
	qsort(lat, n, sizeof(*lat), cmp_u32);
	printf("cpu %d: %lu calls in %.3f s = %.0f calls/s | us: min %.1f  p50 %.1f  p90 %.1f  p99 %.1f  max %.1f  mean %.1f\n",
	       cpu, n, (t1 - t0) / 1e9, n / ((t1 - t0) / 1e9), lat[0] / 1e3,
	       lat[n / 2] / 1e3, lat[n * 9 / 10] / 1e3, lat[n * 99 / 100] / 1e3,
	       lat[n - 1] / 1e3, sum / (double)n / 1e3);
	TEEC_CloseSession(&s);
	TEEC_FinalizeContext(&ctx);
	return 0;
}
