// SPDX-License-Identifier: BSD-2-Clause
/*
 * can_sender - stand-in for the sending ECU: builds the monitor message (id, counter, payload, HMAC tag) and puts it on the CAN controller
 * (can0) as a CAN-FD frame. Nothing here talks to Secure World. The key is a PROTOTYPE key given by the caller.
 *
 *   can_sender [--iface can0] --key-file F once ID COUNTER PAYLOAD [--bad-tag] [--eff]
 *   can_sender [--iface can0] --key-file F run SECONDS PERIOD_MS START_COUNTER
 *
 * The socket turns the kernel's own local echo OFF (CAN_RAW_LOOPBACK 0), so the forwarder can only see a frame if it came back through the
 * CAN controller hardware (chip loopback), not from a software copy.
 */
#include <errno.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <linux/can/raw.h>
#include "canbridge.h"

static int hexval(int c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
static int load_key(const char *path, uint8_t key[32])
{
	char b[256]; size_t n, i; FILE *f = fopen(path, "rb");
	if (!f) return -1;
	n = fread(b, 1, sizeof b - 1, f); fclose(f);
	if (n == 32) { memcpy(key, b, 32); return 0; }
	while (n && (b[n - 1] == '\n' || b[n - 1] == ' ' || b[n - 1] == '\r')) n--;
	if (n != 64) return -1;
	for (i = 0; i < 32; i++) { int h = hexval(b[2 * i]), l = hexval(b[2 * i + 1]); if (h < 0 || l < 0) return -1; key[i] = (uint8_t)(h << 4 | l); }
	return 0;
}

static int open_sock(const char *ifn)
{
	struct sockaddr_can a; struct ifreq ifr; int s, on = 1, off = 0;
	s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
	if (s < 0) { perror("socket(PF_CAN)"); return -1; }
	if (setsockopt(s, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &on, sizeof on)) { perror("CAN_RAW_FD_FRAMES (is the interface in FD mode?)"); return -1; }
	if (setsockopt(s, SOL_CAN_RAW, CAN_RAW_LOOPBACK, &off, sizeof off)) { perror("CAN_RAW_LOOPBACK"); return -1; }
	memset(&ifr, 0, sizeof ifr); snprintf(ifr.ifr_name, sizeof ifr.ifr_name, "%s", ifn);
	if (ioctl(s, SIOCGIFINDEX, &ifr)) { perror("SIOCGIFINDEX"); return -1; }
	memset(&a, 0, sizeof a); a.can_family = AF_CAN; a.can_ifindex = ifr.ifr_ifindex;
	if (bind(s, (struct sockaddr *)&a, sizeof a)) { perror("bind"); return -1; }
	return s;
}

static int send_one(int s, const uint8_t key[32], uint32_t id, uint32_t ctr, uint32_t pl, int bad)
{
	struct canfd_frame f;
	cb_pack(key, id, ctr, pl, bad, &f);
	if (write(s, &f, CANFD_MTU) != CANFD_MTU) { perror("write"); return -1; }
	return 0;
}

int main(int argc, char **argv)
{
	const char *ifn = "can0", *kf = NULL; int a = 1, s; uint8_t key[32];
	while (a < argc && argv[a][0] == '-' && argv[a][1] == '-') {
		if (!strcmp(argv[a], "--iface") && a + 1 < argc) { ifn = argv[a + 1]; a += 2; }
		else if (!strcmp(argv[a], "--key-file") && a + 1 < argc) { kf = argv[a + 1]; a += 2; }
		else break;
	}
	if (!kf || load_key(kf, key) || a >= argc) { fprintf(stderr, "usage: can_sender [--iface can0] --key-file F once ID COUNTER PAYLOAD [--bad-tag] [--eff] | run SECONDS PERIOD_MS START\n"); return 2; }
	s = open_sock(ifn);
	if (s < 0) return 1;
	if (!strcmp(argv[a], "once") && a + 3 < argc) {
		uint32_t id = (uint32_t)strtoul(argv[a + 1], NULL, 0); int bad = 0, i;
		for (i = a + 4; i < argc; i++) { if (!strcmp(argv[i], "--bad-tag")) bad = 1; else if (!strcmp(argv[i], "--eff")) id |= CAN_EFF_FLAG; }
		if (send_one(s, key, id, (uint32_t)strtoul(argv[a + 2], NULL, 0), (uint32_t)strtoul(argv[a + 3], NULL, 0), bad)) return 1;
		printf("sent 1 frame: id 0x%x counter %s payload %s%s\n", id, argv[a + 2], argv[a + 3], bad ? " (one tag bit flipped)" : "");
		return 0;
	}
	if (!strcmp(argv[a], "run") && a + 3 < argc) {
		unsigned secs = (unsigned)strtoul(argv[a + 1], NULL, 0), per = (unsigned)strtoul(argv[a + 2], NULL, 0); uint32_t ctr = (uint32_t)strtoul(argv[a + 3], NULL, 0);
		struct timespec t; unsigned long n = 0; uint64_t next, end;
		if (per < 10) { fprintf(stderr, "period below 10 ms refused\n"); return 2; }
		clock_gettime(CLOCK_MONOTONIC, &t); next = (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec; end = next + (uint64_t)secs * 1000000000ull;
		for (next += (uint64_t)per * 1000000ull; next < end; next += (uint64_t)per * 1000000ull) {
			struct timespec w = { (time_t)(next / 1000000000ull), (long)(next % 1000000000ull) };
			clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &w, NULL);
			if (send_one(s, key, 0x123, ctr, 0xC0DE0000u | (ctr & 0xFFFFu), 0)) return 1;
			ctr++; n++;
		}
		printf("sent %lu frames over %u s, period %u ms, counters %u..%u\n", n, secs, per, (uint32_t)strtoul(argv[a + 3], NULL, 0), ctr - 1);
		return 0;
	}
	fprintf(stderr, "unknown command\n");
	return 2;
}
