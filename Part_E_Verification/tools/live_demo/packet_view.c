// SPDX-License-Identifier: BSD-2-Clause
/*
 * packet_view - runs on the RECEIVER Pi. One command that never stops: it prints every CAN packet that arrives on can0, one line each, and shows what the
 * Secure World decided about it, in colour:
 *    green   accepted           red   tampered / replay / duplicate / conflict      magenta  wrong id
 *    yellow  counter gap        cyan  bus and heartbeat events (silence, recovered, forwarder not answering)
 * How the colour is found (no change in the Secure World): the Secure World logs every alarm as an event with the COUNTER of the packet and the TIME (its own
 * 19.2 MHz counter). Linux reads the packet, stamps the arrival with the same counter (CNTVCT), asks the Secure World for new events a few times per second
 * (PTA command 18) and gives the packet with that counter and that time its colour. A packet with no alarm for 0.7 s is green.
 * Honest limit: the Secure World logs at most one event per second per alarm type (the exact counters in the status line are always exact), so attacks
 * faster than that are counted but not all coloured one by one. The colour appears about 0.7 s after the packet (the time the Secure World needs).
 *
 *   packet_view [--iface can0] [--no-bar]        stop with Ctrl+C
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include "mon_client.h"

#define FIN_TICKS (19200ULL * 700)          /* 0.7 s: after this a packet without an alarm is accepted */
#define MAXI 2048
#define RETRY(call, r) do { int t_; for (t_ = 0; ; t_++) { (r) = (call); if ((r) != TEEC_ERROR_BUSY || t_ >= 40) break; \
			     { struct timespec ts_ = {0, 2000000}; nanosleep(&ts_, NULL); } } } while (0)

/* one colour per packet type (the sender window uses the same): valid green, tampered red, replay orange, duplicate light blue, conflict purple, wrong id pink, gap yellow, bus events grey */
static const char *CGREEN = "\033[32m", *CRED = "\033[1;31m", *CORANGE = "\033[38;5;208m", *CBLUE = "\033[38;5;75m", *CPURPLE = "\033[38;5;135m", *CPINK = "\033[38;5;213m",
		  *CYEL = "\033[1;33m", *CGREY = "\033[38;5;245m", *CEND = "\033[0m";

enum { PKT, MSG };
struct item {
	int kind, state;                  /* state: 0 waiting for the Secure World, 1 final */
	uint64_t ticks;
	const char *col; char why[200];
	uint32_t id, ctr, payload; uint8_t tag[4]; unsigned n; int decode;
};
static struct item q[MAXI]; static int nq;
static volatile sig_atomic_t stop;
static uint64_t t0_ticks; static int bar = 1, rows = 24;
#define TOP 5                                   /* rows of the dashboard at the top */
static int cols = 120; static unsigned cnt[7], n_bus; static double spd[64]; static unsigned nspd;      /* cnt: ok, tampered, replay, duplicate, conflict, wrong id, gap */
static unsigned pkts_total, run_ok, run_bad;                          /* packets shown in this run */
static char alert_txt[200]; static const char *alert_col; static uint64_t alert_ticks;      /* the last detection, shown in the banner */
static struct mon_status0 cs0; static struct mon_status1 cs1; static int have_stats;

static uint64_t cntvct(void) { uint64_t v; __asm__ volatile("isb; mrs %0, cntvct_el0" : "=r"(v)); return v; }
static void on_sig(int s) { (void)s; stop = 1; }

static void insert(const struct item *it)
{
	int i;
	if (nq >= MAXI) return;
	for (i = nq; i > 0 && q[i - 1].ticks > it->ticks; i--) q[i] = q[i - 1];
	q[i] = *it; nq++;
}

static void msg(uint64_t ticks, const char *col, const char *text)
{
	struct item it; memset(&it, 0, sizeof it);
	it.kind = MSG; it.state = 1; it.ticks = ticks; it.col = col; snprintf(it.why, sizeof it.why, "%s", text);
	insert(&it);
}

/* the Secure World's event names are looked up once: type number -> name */
static int is(const char *a, const char *b) { return a && b && !strcmp(a, b); }

/* Which packet does an alarm belong to? Packets with the same counter can be pending together:
 *   tampered / wrong id: the rejected packet comes FIRST, the good packet with the same counter follows it  -> take the earliest
 *   replay / duplicate / conflict: the offender comes AFTER the original                                    -> take the latest (arrived before the event) */
static void mark(const struct mon_event *e, const char *col, const char *text, int latest)
{
	int i, pick = -1;
	for (i = 0; i < nq; i++)
		if (q[i].kind == PKT && q[i].state == 0 && q[i].ctr == e->counter && q[i].ticks <= e->ticks + 38400ULL) {
			pick = i;
			if (!latest) break;
		}
	if (pick >= 0) { q[pick].col = col; snprintf(q[pick].why, sizeof q[pick].why, "%s", text); q[pick].state = 1; return; }
	{ char b[200]; snprintf(b, sizeof b, "alarm for counter %u seen late: %s", e->counter, text); msg(e->ticks, CGREY, b); }
}

static void handle(const struct mon_event *e)
{
	const char *nm = mon_event_name(e->type); char b[140];
	if (is(nm, "AUTH_FAIL")) mark(e, CRED, "TAMPERED: the signature does not fit the data (AUTH_FAIL)", 0);
	else if (is(nm, "REPLAY")) {
		if (e->detail == 1) mark(e, CORANGE, "REPLAY: counter too old (REPLAY detail 1)", 1);
		else if (e->detail == 2) mark(e, CBLUE, "DUPLICATE: the same packet again (REPLAY detail 2)", 1);
		else mark(e, CPURPLE, "CONFLICT: same counter, other data (REPLAY detail 3)", 1);
	} else if (is(nm, "MALFORMED")) mark(e, CPINK, "WRONG ID / format not allowed (MALFORMED)", 0);
	else if (is(nm, "COUNTER_GAP")) { snprintf(b, sizeof b, "GAP: %u counter(s) missing (COUNTER_GAP)", e->detail); mark(e, CYEL, b, 0); }
	else if (is(nm, "COUNTER_RESYNC")) mark(e, CGREEN, "first packet after a boot, counter synchronised (normal)", 0);
	else if (is(nm, "TIMEOUT")) {
		if (e->detail == 1) msg(e->ticks, CGREY, ">>> BUS SILENT for more than 0.3 s (TIMEOUT detail 1)");
		else if (e->detail == 2) msg(e->ticks, CRED, ">>> LINUX FORWARDER NOT ANSWERING the heartbeat (TIMEOUT detail 2)");
		else if (e->detail == 4) msg(e->ticks, CRED, ">>> packet requests not answered (TIMEOUT detail 4)");
		else msg(e->ticks, CGREY, ">>> TIMEOUT");
	} else if (is(nm, "TRAFFIC_RECOVERED")) msg(e->ticks, CGREY, ">>> traffic is back (TRAFFIC_RECOVERED)");
	else if (is(nm, "HEARTBEAT_RECOVERED")) msg(e->ticks, CGREY, ">>> heartbeat answered again (HEARTBEAT_RECOVERED)");
	else if (is(nm, "LATE_ANSWER")) msg(e->ticks, CGREY, e->detail ? ">>> a packet answer was late (LATE_ANSWER)" : ">>> a heartbeat answer was late (LATE_ANSWER)");
}

static const char *speed_bar(double kmh)                      /* speed bar: 12 cells = 0..120 km/h */
{
	static char b[64]; int k, f = (int)(kmh / 10.0 + 0.5); b[0] = 0;
	if (f < 0) f = 0;
	if (f > 12) f = 12;
	for (k = 0; k < 12; k++) strcat(b, k < f ? "\xe2\x96\x88" : "\xe2\x96\x91");
	return b;
}

static void print_item(const struct item *it)
{
	double t = (double)(it->ticks - t0_ticks) / 19200000.0; char line[300];
	if (it->kind == MSG) { n_bus++; printf("%st+%7.2f  %s%s\n", it->col, t, it->why, CEND); return; }
	pkts_total++;
	if (it->decode) spd[nspd++ % 64] = (double)(it->payload >> 16) / 10.0;
	if (it->col == CGREEN) cnt[0]++; else if (it->col == CRED) cnt[1]++; else if (it->col == CORANGE) cnt[2]++; else if (it->col == CBLUE) cnt[3]++;
	else if (it->col == CPURPLE) cnt[4]++; else if (it->col == CPINK) cnt[5]++; else if (it->col == CYEL) cnt[6]++;
	if (it->col == CGREEN) run_ok++;
	else { run_bad++; snprintf(alert_txt, sizeof alert_txt, "%s", it->why); alert_col = it->col; alert_ticks = cntvct(); }
	if (it->decode)
		snprintf(line, sizeof line, "t+%7.2f  #%-5u id 0x%03X  ctr %-6u speed %5.1f km/h %s rpm %-5u  tag %02x%02x%02x%02x..",
			 t, it->n, it->id & 0x1FFFFFFF, it->ctr, (double)(it->payload >> 16) / 10.0, speed_bar((double)(it->payload >> 16) / 10.0), (it->payload & 0xFFFF) * 50, it->tag[0], it->tag[1], it->tag[2], it->tag[3]);
	else
		snprintf(line, sizeof line, "t+%7.2f  #%-5u id 0x%03X  (not a monitor packet)", t, it->n, it->id & 0x1FFFFFFF);
	if (it->col == CGREEN) printf("%s%s   \xe2\x9c\x94 accepted%s%s\n", it->col, line, it->why[0] ? ": " : "", it->why);
	else printf("%s%s   \xe2\x9c\x96 %s%s\n", it->col, line, it->why, CEND);
}

static void flush(uint64_t now)
{
	int k;
	while (nq > 0) {
		if (q[0].kind == PKT && q[0].state == 0) {
			if (now - q[0].ticks < FIN_TICKS) break;
			q[0].state = 1; q[0].col = CGREEN;
		}
		print_item(&q[0]);
		for (k = 1; k < nq; k++) q[k - 1] = q[k];
		nq--;
	}
	fflush(stdout);
}

static void paint(void)
{
	static const char *spark[8] = {"\xe2\x96\x81", "\xe2\x96\x82", "\xe2\x96\x83", "\xe2\x96\x84", "\xe2\x96\x85", "\xe2\x96\x86", "\xe2\x96\x87", "\xe2\x96\x88"};
	static const char *spin[10] = {"\xe2\xa0\x8b", "\xe2\xa0\x99", "\xe2\xa0\xb9", "\xe2\xa0\xb8", "\xe2\xa0\xbc", "\xe2\xa0\xb4", "\xe2\xa0\xa6", "\xe2\xa0\xa7", "\xe2\xa0\x87", "\xe2\xa0\x8f"};
	char L1[400], L2[700], L3[900], L4[600], L5[600], sp[900] = ""; uint64_t now = cntvct(); double age; int k, nn, top;
	static uint64_t t_start, t_last; static unsigned n_last; static double rate; unsigned hb_ok;
	if (!bar || !have_stats) return;
	if (!t_start) { t_start = now; t_last = now; }
	if (now - t_last >= 19200000ULL) { rate = (double)(pkts_total - n_last) * 19200000.0 / (double)(now - t_last); n_last = pkts_total; t_last = now; }
	hb_ok = !strcmp(mon_hbstate_name(cs0.hb_state), "ANSWERED");
	age = (double)(now - alert_ticks) / 19200000.0;
	/* row 1: title */
	snprintf(L1, sizeof L1, "\033[1;38;5;75m\xe2\x95\xad\xe2\x94\x80\033[48;5;24;97m %s  RECEIVER \xc2\xb7 SECURE WORLD  LIVE \033[0m\033[38;5;75m\xe2\x94\x80\033[0m  %s\xe2\x99\xa5 heartbeat %s\033[0m   \033[38;5;245mcore 1 loop %" PRIu64 "   %.0f pkt/s   %.0f s\033[0m",
		 spin[(now / 1920000ULL) % 10], hb_ok ? "\033[32m" : "\033[1;31m", mon_hbstate_name(cs0.hb_state), (uint64_t)cs0.poll_count, rate, (double)(now - t_start) / 19200000.0);
	/* row 2: counters = legend */
	snprintf(L2, sizeof L2, "\033[38;5;75m\xe2\x94\x82\033[0m %s\xe2\x97\x8f valid %u%s  %s\xe2\x97\x8f tampered %u%s  %s\xe2\x97\x8f replay %u%s  %s\xe2\x97\x8f duplicate %u%s  %s\xe2\x97\x8f conflict %u%s  %s\xe2\x97\x8f wrong id %u%s  %s\xe2\x97\x8f gap %u%s  %s\xe2\x97\x8f bus events %u%s",
		 CGREEN, cnt[0], CEND, CRED, cnt[1], CEND, CORANGE, cnt[2], CEND, CBLUE, cnt[3], CEND, CPURPLE, cnt[4], CEND, CPINK, cnt[5], CEND, CYEL, cnt[6], CEND, CGREY, n_bus, CEND);
	/* row 3: speed sparkline (last 48 packets) */
	nn = nspd < 48 ? (int)nspd : 48;
	{
		double mn = 1e9, mx = -1e9, rg;
		for (k = nn; k > 0; k--) { double v = spd[(nspd - k) % 64]; if (v < mn) mn = v; if (v > mx) mx = v; }
		rg = mx - mn < 8.0 ? 8.0 : mx - mn;                                     /* scaled to the recent range so the movement is visible */
		for (k = nn; k > 0; k--) { double v = spd[(nspd - k) % 64]; int l = (int)(7.99 * (v - mn) / rg); if (l < 0) l = 0; if (l > 7) l = 7; strcat(sp, spark[l]); }
	}
	snprintf(L3, sizeof L3, "\033[38;5;75m\xe2\x94\x82\033[0m \033[38;5;245mspeed\033[0m \033[1;38;5;81m%s\033[0m %5.1f km/h", sp, nn ? spd[(nspd - 1) % 64] : 0.0);
	/* row 4: the alert strip */
	if (alert_col && age < 1.8) snprintf(L4, sizeof L4, "\033[38;5;75m\xe2\x94\x82\033[0m %s\033[7m  \xe2\x96\xb6\xe2\x96\xb6 DETECTED  %s  \033[0m", alert_col, alert_txt);
	else if (alert_col && age < 6.0) snprintf(L4, sizeof L4, "\033[38;5;75m\xe2\x94\x82\033[0m %s\xe2\x96\xb6 last detection: %s\033[0m", alert_col, alert_txt);
	else snprintf(L4, sizeof L4, "\033[38;5;75m\xe2\x94\x82\033[0m \033[38;5;245m\xe2\x97\x8f watching the bus ... no alert in the last 6 seconds\033[0m");
	/* row 5: rule */
	top = 0; L5[0] = 0; strcat(L5, "\033[38;5;75m\xe2\x95\xb0");
	for (k = 0; k < cols - 2 && k < 200; k++) strcat(L5, "\xe2\x94\x80");
	strcat(L5, "\033[0m"); (void)top;
	printf("\0337\033[1;1H\033[2K%s\033[2;1H\033[2K%s\033[3;1H\033[2K%s\033[4;1H\033[2K%s\033[5;1H\033[2K%s\033[0m", L1, L2, L3, L4, L5);
	/* bottom line: the Secure World's own totals since it booted */
	{
		unsigned i, v[5] = {0, 0, 0, 0, 0}; char l6[600];
		for (i = 0; i < MON_EVENT_TYPES; i++) {
			const char *n = mon_event_name(i);
			if (is(n, "AUTH_FAIL")) v[0] = cs1.ev_count[i]; else if (is(n, "REPLAY")) v[1] = cs1.ev_count[i]; else if (is(n, "MALFORMED")) v[2] = cs1.ev_count[i];
			else if (is(n, "COUNTER_GAP")) v[3] = cs1.ev_count[i]; else if (is(n, "TIMEOUT")) v[4] = cs1.ev_count[i];
		}
		snprintf(l6, sizeof l6, "\033[48;5;236;97m SECURE WORLD OWN COUNTERS (since boot) \033[48;5;236;32m judged %u \033[38;5;245m\xe2\x94\x82\033[1;31m AUTH_FAIL %u \033[38;5;245m\xe2\x94\x82\033[0;48;5;236;38;5;208m REPLAY %u \033[38;5;245m\xe2\x94\x82\033[38;5;213m MALFORMED %u \033[38;5;245m\xe2\x94\x82\033[1;33m GAP %u \033[38;5;245m\xe2\x94\x82\033[0;48;5;236;38;5;245m TIMEOUT %u ",
			 cs1.v_frame_total, v[0], v[1], v[2], v[3], v[4]);
		printf("\033[%d;1H\033[2K%s\033[K\033[0m\0338", rows, l6);
	}
	fflush(stdout);
}

static void draw_bar(const struct mon_status0 *s0, const struct mon_status1 *s1)
{
	cs0 = *s0; cs1 = *s1; have_stats = 1; paint();
}

int main(int argc, char **argv)
{
	const char *ifn = "can0"; int i, s, on = 1; struct sockaddr_can ad; struct ifreq ifr; struct mon_ctx c; struct mon_health h; struct mon_status0 s0; struct mon_status1 s1; TEEC_Result r;
	uint32_t first_seq; uint64_t last_poll = 0, last_bar = 0; unsigned n = 0; struct winsize w;
	for (i = 1; i < argc; i++) { if (!strcmp(argv[i], "--iface") && i + 1 < argc) ifn = argv[++i]; else if (!strcmp(argv[i], "--no-bar")) bar = 0; }
	signal(SIGINT, on_sig); signal(SIGTERM, on_sig);
	if (ioctl(1, TIOCGWINSZ, &w) == 0 && w.ws_row > 14) { rows = w.ws_row; if (w.ws_col > 40) cols = w.ws_col; } else bar = 0;
	r = mon_open(&c); if (r) { fprintf(stderr, "cannot open the Secure World monitor: 0x%x\n", r); return 1; }
	memset(&s0, 0, sizeof s0); memset(&s1, 0, sizeof s1);
	RETRY(mon_get_status0(&c, &s0, &h), r); if (r) { fprintf(stderr, "status read failed: 0x%x\n", r); return 1; }
	first_seq = s0.ev_seq + 1;
	s = socket(AF_CAN, SOCK_RAW, CAN_RAW); if (s < 0) { perror("socket"); return 1; }
	setsockopt(s, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &on, sizeof on);
	memset(&ifr, 0, sizeof ifr); snprintf(ifr.ifr_name, sizeof ifr.ifr_name, "%s", ifn);
	if (ioctl(s, SIOCGIFINDEX, &ifr)) { perror(ifn); return 1; }
	memset(&ad, 0, sizeof ad); ad.can_family = AF_CAN; ad.can_ifindex = ifr.ifr_ifindex;
	if (bind(s, (struct sockaddr *)&ad, sizeof ad)) { perror("bind"); return 1; }
	fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK);
	t0_ticks = cntvct();
	if (bar) printf("\033[2J\033[%d;%dr\033[%d;1H", TOP + 1, rows - 1, TOP + 1);
	if (!bar) {
	printf("\033[1;38;5;75m\xe2\x95\xad\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80 RECEIVER \xc2\xb7 SECURE WORLD VIEW \xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\033[0m\n");
	printf("\033[1;38;5;75m\xe2\x94\x82\033[0m every packet on %s, judged by the Secure World (Ctrl+C to stop)\n", ifn);
	printf("\033[1;38;5;75m\xe2\x94\x82\033[0m %s\xe2\x97\x8f valid%s  %s\xe2\x97\x8f tampered%s  %s\xe2\x97\x8f replay%s  %s\xe2\x97\x8f duplicate%s  %s\xe2\x97\x8f conflict%s  %s\xe2\x97\x8f wrong id%s  %s\xe2\x97\x8f counter gap%s  %s\xe2\x97\x8f bus events%s\n",
	       CGREEN, CEND, CRED, CEND, CORANGE, CEND, CBLUE, CEND, CPURPLE, CEND, CPINK, CEND, CYEL, CEND, CGREY, CEND);
	printf("\033[1;38;5;75m\xe2\x95\xb0\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\033[0m\n\n");
	}
	while (!stop) {
		struct pollfd pf = { s, POLLIN, 0 }; uint64_t now;
		poll(&pf, 1, 50);
		for (;;) {
			struct canfd_frame f; struct item it; ssize_t k = read(s, &f, sizeof f); uint64_t tk = cntvct();
			if (k < 0) break;
			if ((k != CAN_MTU && k != CANFD_MTU) || (f.can_id & CAN_ERR_FLAG)) continue;
			memset(&it, 0, sizeof it); it.kind = PKT; it.ticks = tk; it.id = f.can_id; it.n = ++n;
			if (k == CANFD_MTU && f.len >= 40) {
				it.decode = 1; it.ctr = f.data[0] | f.data[1] << 8 | f.data[2] << 16 | (uint32_t)f.data[3] << 24;
				it.payload = f.data[4] | f.data[5] << 8 | f.data[6] << 16 | (uint32_t)f.data[7] << 24; memcpy(it.tag, f.data + 8, 4);
			}
			it.col = CGREEN; insert(&it);
		}
		now = cntvct();
		if (now - last_poll >= 19200ULL * 250) {                         /* ask the Secure World for new events 4 times a second */
			struct mon_event ev[MON_EVENT_MAX]; uint32_t cnt = 0, lost = 0; int more = 0; unsigned guard = 0;
			last_poll = now;
			do {
				RETRY(mon_get_events(&c, first_seq, MON_EVENT_MAX, ev, &cnt, &more, &lost, &h), r);
				if (r) break;
				for (i = 0; i < (int)cnt; i++) { handle(&ev[i]); first_seq = ev[i].seq + 1; }
			} while (more && ++guard < 50);
		}
		flush(cntvct());
		{ static uint64_t lp; if (now - lp >= 4800000ULL) { lp = now; paint(); } }
		if (now - last_bar >= 19200000ULL) {
			last_bar = now; RETRY(mon_get_status0(&c, &s0, &h), r); RETRY(mon_get_status1(&c, &s1, &h), r); draw_bar(&s0, &s1);
		}
	}
	if (bar) printf("\033[r\033[%d;1H\n", rows); else printf("\n");
	printf("stopped. packets seen: %u\n", n);
	mon_close(&c);
	return 0;
}
