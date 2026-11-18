// SPDX-License-Identifier: BSD-2-Clause
/*
 * can_forwarder - the Linux side of the real path: reads CAN-FD frames from can0 (they came back through the CAN controller), rebuilds the
 * 44-byte monitor message (canbridge) and submits it to the monitor (PTA command 17). It also answers the monitor's heartbeat, like the
 * soak responder. It holds no key and builds no packet: the tag is judged only in Secure World.
 *
 *   can_forwarder [--iface can0] [--retries N] run SECONDS [--verbose] [--no-heartbeat]
 *
 * Frames the forwarder cannot turn into a message (error frame, remote frame, wrong length) are dropped and counted here: the monitor's
 * command 17 takes exactly 44 bytes, so a wrong-length frame is only visible in these forwarder counters.
 */
#include <errno.h>
#include <inttypes.h>
#include <net/if.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <linux/can/raw.h>
#include "canbridge.h"

static int g_retries = 20;
#define RETRY(call, r) do { int t_; for (t_ = 0; ; t_++) { (r) = (call); if ((r) != TEEC_ERROR_BUSY || t_ >= g_retries) break; \
			     { struct timespec ts_ = {0, 2000000}; nanosleep(&ts_, NULL); } } } while (0)

static uint64_t now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u; }

static unsigned long rx_packets(const char *ifn)
{
	char p[128]; unsigned long v = 0; FILE *f;
	snprintf(p, sizeof p, "/sys/class/net/%s/statistics/rx_packets", ifn);
	f = fopen(p, "r"); if (f) { if (fscanf(f, "%lu", &v) != 1) v = 0; fclose(f); }
	return v;
}

int main(int argc, char **argv)
{
	const char *ifn = "can0"; int a = 1, verbose = 0, hb = 1, s, on = 1, i; unsigned secs;
	struct sockaddr_can ad; struct ifreq ifr; struct mon_ctx c; struct mon_health h; struct mon_status0 s0; TEEC_Result r;
	unsigned long verdict[8] = {0}, got = 0, drop[4] = {0}, ans_on = 0, ans_late = 0, ans_other = 0, busy = 0, errs = 0, rx0, rx1;
	uint32_t seq, st, last_answered = 0xFFFFFFFFu, v, d; uint64_t nonce, end;
	static const char *const vn[8] = {"ACCEPTED", "GAP", "RESYNC", "MALFORMED", "AUTH_FAIL", "REPLAY", "DUPLICATE", "CONFLICT"};

	while (a < argc && argv[a][0] == '-' && argv[a][1] == '-') {
		if (!strcmp(argv[a], "--iface") && a + 1 < argc) { ifn = argv[a + 1]; a += 2; }
		else if (!strcmp(argv[a], "--retries") && a + 1 < argc) { g_retries = atoi(argv[a + 1]); a += 2; }
		else break;
	}
	if (a + 1 >= argc || strcmp(argv[a], "run")) { fprintf(stderr, "usage: can_forwarder [--iface can0] [--retries N] run SECONDS [--verbose] [--no-heartbeat]\n"); return 2; }
	secs = (unsigned)strtoul(argv[a + 1], NULL, 0);
	for (i = a + 2; i < argc; i++) { if (!strcmp(argv[i], "--verbose")) verbose = 1; else if (!strcmp(argv[i], "--no-heartbeat")) hb = 0; }

	s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
	if (s < 0) { perror("socket(PF_CAN)"); return 1; }
	if (setsockopt(s, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &on, sizeof on)) { perror("CAN_RAW_FD_FRAMES (is the interface in FD mode?)"); return 1; }
	memset(&ifr, 0, sizeof ifr); snprintf(ifr.ifr_name, sizeof ifr.ifr_name, "%s", ifn);
	if (ioctl(s, SIOCGIFINDEX, &ifr)) { perror("SIOCGIFINDEX"); return 1; }
	memset(&ad, 0, sizeof ad); ad.can_family = AF_CAN; ad.can_ifindex = ifr.ifr_ifindex;
	if (bind(s, (struct sockaddr *)&ad, sizeof ad)) { perror("bind"); return 1; }
	r = mon_open(&c);
	if (r) { fprintf(stderr, "cannot open the monitor session (0x%08x)\n", (unsigned)r); return 1; }
	memset(&s0, 0, sizeof s0); RETRY(mon_get_status0(&c, &s0, &h), r);
	rx0 = rx_packets(ifn);
	printf("can_forwarder: %s, %u s%s; monitor last_counter %u\n", ifn, secs, hb ? ", heartbeat answered" : ", no heartbeat", s0.last_counter);
	end = now_ms() + (uint64_t)secs * 1000u;
	while (now_ms() < end) {
		struct pollfd pf = { s, POLLIN, 0 };
		int pr = poll(&pf, 1, 50);
		if (pr > 0 && (pf.revents & POLLIN)) {
			struct canfd_frame f; uint8_t msg[MON_FRAME_LEN]; ssize_t n; int why;
			memset(&f, 0, sizeof f);
			n = read(s, &f, sizeof f);
			if (n == (ssize_t)CAN_MTU) { f.len = (uint8_t)((struct can_frame *)&f)->can_dlc; }		/* classic frame: its length is below 48 */
			else if (n != (ssize_t)CANFD_MTU) continue;
			why = cb_unpack(&f, msg);
			if (why != CB_OK) { drop[why]++; if (verbose) printf("  dropped by the forwarder: %s (id 0x%x, len %u)\n", cb_reason(why), f.can_id, f.len); }
			else {
				RETRY(mon_submit_frame(&c, msg, &v, &d, &h), r);
				if (r == TEEC_ERROR_BUSY) busy++;
				else if (r) errs++;
				else { if (v < 8) verdict[v]++; got++;
					if (verbose) printf("  frame id 0x%x counter %u -> %s%s (detail %u)\n", f.can_id, (unsigned)(msg[4] | msg[5] << 8 | msg[6] << 16 | (uint32_t)msg[7] << 24), v < 8 ? vn[v] : "?", "", d); }
			}
		}
		if (hb) {
			r = mon_get_nonce(&c, &nonce, &seq, &st, &h);
			if (!r && (st == 1 || st == 3) && nonce != 0 && seq != last_answered) {
				RETRY(mon_submit_answer(&c, seq, nonce, &v, &d, &h), r);
				if (!r) { last_answered = seq; if (v == 0) ans_on++; else if (v == 1) ans_late++; else ans_other++; }
			}
		}
	}
	rx1 = rx_packets(ifn);
	RETRY(mon_get_status0(&c, &s0, &h), r);
	printf("frames submitted to the monitor: %lu | verdicts:", got);
	for (i = 0; i < 8; i++) printf(" %s %lu", vn[i], verdict[i]);
	printf(" | BUSY %lu errors %lu\n", busy, errs);
	printf("dropped by the forwarder: error frame %lu, remote frame %lu, wrong length %lu\n", drop[CB_ERR_FRAME], drop[CB_RTR], drop[CB_BAD_LEN]);
	if (hb) printf("heartbeat answers: ON_TIME %lu LATE %lu other %lu\n", ans_on, ans_late, ans_other);
	printf("can0 rx_packets counter (frames received by the CAN controller): %lu -> %lu (+%lu)\n", rx0, rx1, rx1 - rx0);
	if (!r) printf("end state: flags 0x%02x last_counter %u hb_state %s miss_count %u starved %u\n", s0.flags, s0.last_counter, mon_hbstate_name(s0.hb_state), s0.hb_miss_count, !!(h.p2b & MON_HP_STARVED));
	mon_close(&c);
	return 0;
}
