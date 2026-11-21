// SPDX-License-Identifier: BSD-2-Clause
/*
 * sockcap - MEASURES how many CAN-FD frames a raw CAN socket can hold when its reader is slow. It changes nothing in the system: it opens ONE extra raw socket
 * on can0, does NOT read for WAIT seconds (a stand-in for a slow reader such as can_forwarder), then reads whatever is queued and prints how many frames there
 * were, the first and last packet counter in the queue (bytes 0..3 of the data, little-endian) and the kernel's own drop counter for this socket (SO_RXQ_OVFL).
 *
 *   sockcap WAIT_SECONDS [RCVBUF_BYTES_FORCED]
 *
 * With the second argument the socket buffer of THIS socket only is set with SO_RCVBUFFORCE (the kernel doubles the value) to show what a larger buffer would hold.
 * Nothing else is touched (no sysctl, no interface setting). Needs root for SO_RCVBUFFORCE only.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <net/if.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <linux/can.h>
#include <linux/can/raw.h>

#ifndef SO_RXQ_OVFL
#define SO_RXQ_OVFL 40
#endif

int main(int argc, char **argv)
{
	int wait = argc > 1 ? atoi(argv[1]) : 6, force = argc > 2 ? atoi(argv[2]) : 0, on = 1, rb = 0, s;
	socklen_t l = sizeof rb; struct ifreq ifr; struct sockaddr_can ad;
	unsigned long count = 0, drops = 0, classic = 0; uint32_t first = 0, last = 0;

	s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
	if (s < 0) { perror("socket"); return 1; }
	if (setsockopt(s, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &on, sizeof on)) { perror("CAN_RAW_FD_FRAMES"); return 1; }
	if (setsockopt(s, SOL_SOCKET, SO_RXQ_OVFL, &on, sizeof on)) { perror("SO_RXQ_OVFL"); return 1; }
	if (force && setsockopt(s, SOL_SOCKET, SO_RCVBUFFORCE, &force, sizeof force)) { perror("SO_RCVBUFFORCE"); return 1; }
	getsockopt(s, SOL_SOCKET, SO_RCVBUF, &rb, &l);
	memset(&ifr, 0, sizeof ifr); snprintf(ifr.ifr_name, sizeof ifr.ifr_name, "can0");
	if (ioctl(s, SIOCGIFINDEX, &ifr)) { perror("SIOCGIFINDEX"); return 1; }
	memset(&ad, 0, sizeof ad); ad.can_family = AF_CAN; ad.can_ifindex = ifr.ifr_ifindex;
	if (bind(s, (struct sockaddr *)&ad, sizeof ad)) { perror("bind"); return 1; }
	printf("sockcap: SO_RCVBUF reported by the kernel = %d bytes%s; not reading for %d s\n", rb, force ? " (forced)" : " (default)", wait);
	fflush(stdout);
	sleep((unsigned)wait);
#define DRAIN() do { for (;;) { \
		struct canfd_frame f; char ctl[CMSG_SPACE(sizeof(uint32_t))]; struct iovec iov = { &f, sizeof f }; \
		struct msghdr m; struct cmsghdr *c; ssize_t n; \
		memset(&m, 0, sizeof m); m.msg_iov = &iov; m.msg_iovlen = 1; m.msg_control = ctl; m.msg_controllen = sizeof ctl; \
		n = recvmsg(s, &m, MSG_DONTWAIT); \
		if (n < 0) break; \
		if (n != (ssize_t)CANFD_MTU) { classic++; continue; } \
		for (c = CMSG_FIRSTHDR(&m); c; c = CMSG_NXTHDR(&m, c)) \
			if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SO_RXQ_OVFL) drops = *(uint32_t *)CMSG_DATA(c); \
		{ uint32_t ctr = (uint32_t)f.data[0] | (uint32_t)f.data[1] << 8 | (uint32_t)f.data[2] << 16 | (uint32_t)f.data[3] << 24; if (!count) first = ctr; last = ctr; } \
		count++; } } while (0)
	DRAIN();
	printf("sockcap: frames that were queued = %lu (classic %lu); first counter %u, last counter %u; kernel drop counter of this socket = %lu\n", count, classic, first, last, drops);
	if (count) printf("sockcap: buffer charged per frame about %lu bytes (SO_RCVBUF %d / %lu frames)\n", (unsigned long)rb / count, rb, count);
	fflush(stdout);
	{ unsigned long c1 = count; sleep(4);		/* the sender now sends ONE marker frame: it carries the final drop counter */
	  DRAIN();
	  printf("sockcap: after the marker frame: %lu more frame(s); FINAL kernel drop counter of this socket = %lu (so %lu + %lu offered = %lu)\n", count - c1, drops, count, drops, count + drops); }
	return 0;
}
