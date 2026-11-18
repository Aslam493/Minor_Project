// SPDX-License-Identifier: BSD-2-Clause
/*
 * can_replay - sends ready-made packets through the CAN controller. It has NO key and NO HMAC code: the packets (message + tag) were made
 * BEFORE Linux was involved, on the laptop, by packet_gen.py, and are only turned into CAN-FD frames here.
 *
 *   can_replay [--iface can0] FILE        (FILE format: see canbridge.h, one packet per line)
 *
 * The socket turns the kernel's local echo OFF, so the forwarder can only see a frame if it came back through the CAN controller hardware.
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

int main(int argc, char **argv)
{
	const char *ifn = "can0", *file; struct sockaddr_can a; struct ifreq ifr; int s, on = 1, off = 0, ln = 0; FILE *fp; char line[512];
	unsigned long sent = 0, bad = 0; struct timespec t; uint64_t next;

	if (argc >= 3 && !strcmp(argv[1], "--iface")) { ifn = argv[2]; argv += 2; argc -= 2; }
	if (argc != 2) { fprintf(stderr, "usage: can_replay [--iface can0] FILE\n"); return 2; }
	file = argv[1];
	fp = fopen(file, "r");
	if (!fp) { perror(file); return 1; }
	s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
	if (s < 0) { perror("socket(PF_CAN)"); return 1; }
	if (setsockopt(s, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &on, sizeof on)) { perror("CAN_RAW_FD_FRAMES (is the interface in FD mode?)"); return 1; }
	if (setsockopt(s, SOL_CAN_RAW, CAN_RAW_LOOPBACK, &off, sizeof off)) { perror("CAN_RAW_LOOPBACK"); return 1; }
	memset(&ifr, 0, sizeof ifr); snprintf(ifr.ifr_name, sizeof ifr.ifr_name, "%s", ifn);
	if (ioctl(s, SIOCGIFINDEX, &ifr)) { perror("SIOCGIFINDEX"); return 1; }
	memset(&a, 0, sizeof a); a.can_family = AF_CAN; a.can_ifindex = ifr.ifr_ifindex;
	if (bind(s, (struct sockaddr *)&a, sizeof a)) { perror("bind"); return 1; }
	clock_gettime(CLOCK_MONOTONIC, &t); next = (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
	while (fgets(line, sizeof line, fp)) {
		struct canfd_frame f; uint32_t d = 0; int r; ln++;
		r = cb_parse_line(line, &d, &f);
		if (r == 1) continue;
		if (r < 0) { fprintf(stderr, "%s:%d: malformed line, skipped\n", file, ln); bad++; continue; }
		next += (uint64_t)d * 1000000ull;
		{ struct timespec w = { (time_t)(next / 1000000000ull), (long)(next % 1000000000ull) }; clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &w, NULL); }
		if (write(s, &f, CANFD_MTU) != CANFD_MTU) { perror("write"); return 1; }
		sent++;
	}
	fclose(fp);
	printf("can_replay: %lu packets sent through %s from %s (no key, no HMAC in this program), %lu malformed lines skipped\n", sent, ifn, file, bad);
	return bad ? 1 : 0;
}
