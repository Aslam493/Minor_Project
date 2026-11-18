// SPDX-License-Identifier: BSD-2-Clause
/*
 * canfdcfg - set the mode of a SocketCAN interface through rtnetlink (the Pi image has only BusyBox `ip`, which cannot).
 * It changes ONLY the named CAN interface: link down / link up, and (while down) bit rates, CAN-FD on/off, chip loopback on/off.
 * It never opens a CAN socket, never touches the MCP2518FD registers directly, never calls OP-TEE. A reboot restores the boot defaults.
 *
 *   canfdcfg IFACE down
 *   canfdcfg IFACE up
 *   canfdcfg IFACE set BITRATE DBITRATE fd|nofd loopback|noloopback      (interface must be down; DBITRATE ignored with nofd)
 *   canfdcfg IFACE test      = down, set 500000 2000000 fd loopback, up   (the mode used by the canbridge tests)
 *   canfdcfg IFACE normal    = down, set 500000 - nofd noloopback, up     (back to the boot mode)
 */
#include <errno.h>
#include <net/if.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <linux/can/netlink.h>
#include <linux/if_link.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

struct msg { struct nlmsghdr nh; struct ifinfomsg ifi; char buf[512]; };

static struct rtattr *add(struct msg *m, int type, const void *data, int len)
{
	struct rtattr *a = (struct rtattr *)((char *)m + NLMSG_ALIGN(m->nh.nlmsg_len));
	a->rta_type = (unsigned short)type; a->rta_len = (unsigned short)RTA_LENGTH(len);
	if (len) memcpy(RTA_DATA(a), data, (size_t)len);
	m->nh.nlmsg_len = NLMSG_ALIGN(m->nh.nlmsg_len) + RTA_ALIGN(a->rta_len);
	return a;
}
static void nest_end(struct msg *m, struct rtattr *a) { a->rta_len = (unsigned short)((char *)m + m->nh.nlmsg_len - (char *)a); }

static int talk(struct msg *m)
{
	int s = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE); char b[1024]; ssize_t n; struct nlmsghdr *h;
	if (s < 0) { perror("socket(NETLINK_ROUTE)"); return -1; }
	if (send(s, m, m->nh.nlmsg_len, 0) < 0) { perror("send"); close(s); return -1; }
	n = recv(s, b, sizeof b, 0); close(s);
	if (n < (ssize_t)sizeof(struct nlmsghdr)) { fprintf(stderr, "no answer\n"); return -1; }
	h = (struct nlmsghdr *)b;
	if (h->nlmsg_type == NLMSG_ERROR) { int e = ((struct nlmsgerr *)NLMSG_DATA(h))->error; if (e) { fprintf(stderr, "kernel refused: %s\n", strerror(-e)); return -1; } return 0; }
	return 0;
}

static void init(struct msg *m, int idx)
{
	memset(m, 0, sizeof *m);
	m->nh.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg)); m->nh.nlmsg_type = RTM_NEWLINK; m->nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK; m->nh.nlmsg_seq = 1;
	m->ifi.ifi_family = AF_UNSPEC; m->ifi.ifi_index = idx;
}

static int updown(int idx, int up)
{
	struct msg m; init(&m, idx); m.ifi.ifi_change = IFF_UP; m.ifi.ifi_flags = up ? IFF_UP : 0;
	return talk(&m);
}

static int setmode(int idx, uint32_t br, uint32_t dbr, int fd, int loop)
{
	struct msg m; struct rtattr *li, *data; struct can_bittiming bt, dbt; struct can_ctrlmode cm; init(&m, idx);
	memset(&bt, 0, sizeof bt); memset(&dbt, 0, sizeof dbt); bt.bitrate = br; dbt.bitrate = dbr;
	cm.mask = CAN_CTRLMODE_LOOPBACK | CAN_CTRLMODE_FD; cm.flags = (loop ? CAN_CTRLMODE_LOOPBACK : 0) | (fd ? CAN_CTRLMODE_FD : 0);
	li = add(&m, IFLA_LINKINFO, NULL, 0);
	add(&m, IFLA_INFO_KIND, "can", 4);
	data = add(&m, IFLA_INFO_DATA, NULL, 0);
	add(&m, IFLA_CAN_BITTIMING, &bt, sizeof bt);
	if (fd) add(&m, IFLA_CAN_DATA_BITTIMING, &dbt, sizeof dbt);
	add(&m, IFLA_CAN_CTRLMODE, &cm, sizeof cm);
	nest_end(&m, data); nest_end(&m, li);
	return talk(&m);
}

int main(int argc, char **argv)
{
	int idx;
	if (argc < 3 || strncmp(argv[1], "can", 3)) { fprintf(stderr, "usage: canfdcfg canN down|up|set BR DBR fd|nofd loopback|noloopback|test|normal\n"); return 2; }
	idx = (int)if_nametoindex(argv[1]);
	if (!idx) { fprintf(stderr, "no interface %s\n", argv[1]); return 1; }
	if (!strcmp(argv[2], "down")) return updown(idx, 0) ? 1 : 0;
	if (!strcmp(argv[2], "up")) return updown(idx, 1) ? 1 : 0;
	if (!strcmp(argv[2], "set") && argc >= 7)
		return setmode(idx, (uint32_t)strtoul(argv[3], NULL, 0), (uint32_t)strtoul(argv[4], NULL, 0), !strcmp(argv[5], "fd"), !strcmp(argv[6], "loopback")) ? 1 : 0;
	if (!strcmp(argv[2], "test") || !strcmp(argv[2], "normal")) {
		int t = !strcmp(argv[2], "test");
		if (updown(idx, 0)) return 1;
		if (setmode(idx, 500000, t ? 2000000 : 0, t, t)) { updown(idx, 1); return 1; }
		return updown(idx, 1) ? 1 : 0;
	}
	fprintf(stderr, "unknown command\n");
	return 2;
}
