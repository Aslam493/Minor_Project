// SPDX-License-Identifier: BSD-2-Clause
/*
 * canmode - read-only query of a SocketCAN interface through rtnetlink.
 *
 * Sends ONE request: RTM_GETLINK for the named interface. That is a pure
 * read. This program never sends RTM_NEWLINK/RTM_SETLINK, never opens a CAN
 * socket, never touches the MCP2518FD registers and never calls OP-TEE.
 *
 * Why it exists: the Pi image only has BusyBox `ip`, which cannot show the
 * CAN bitrate or control-mode flags (Component 1, subcomponent 1.2).
 *
 * Reported fields (a field the kernel did not send is printed as
 * "unavailable", never as 0):
 *   - bit timing (nominal bitrate, sample point, tq, segments, brp)
 *   - CAN clock
 *   - controller state
 *   - ctrlmode flags ACTIVE now          (IFLA_CAN_CTRLMODE .flags)
 *   - ctrlmode flags SUPPORTED by driver (IFLA_CAN_CTRLMODE_EXT -> SUPPORTED)
 *   - the mask word of IFLA_CAN_CTRLMODE, printed as-is. In a GETLINK reply
 *     the kernel leaves it 0 (drivers/net/can/dev/netlink.c can_fill_info()),
 *     so it is NOT the supported set.
 *
 * Exit status: 0 = query answered and interface is a CAN device,
 *              2 = answered but not a CAN device, 1 = error.
 */
#include <errno.h>
#include <net/if.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <linux/can/netlink.h>
#include <linux/if_link.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

/*
 * Build against the kernel's own uapi header (vendored in ./include, copied
 * from optee_full/linux/include/uapi/linux/can/netlink.h). The cross
 * toolchain's sysroot header is older and lacks IFLA_CAN_CTRLMODE_EXT; a
 * hand-written fallback value was wrong once (18 instead of 17), so there is
 * deliberately no fallback any more. The assert fails the build if the header
 * in use does not match the running kernel's enum.
 */
_Static_assert(IFLA_CAN_CTRLMODE_EXT == 17, "enum mismatch vs linux 6.7 uapi");
#define CANMODE_EXT_SUPPORTED 1 /* IFLA_CAN_CTRLMODE_SUPPORTED */
#define NLA_TYPE_MASK_ 0x3fff   /* strip NLA_F_NESTED / NLA_F_NET_BYTEORDER */

struct req {
	struct nlmsghdr nh;
	struct ifinfomsg ifi;
};

struct can_attrs {
	const struct can_bittiming *bt;
	const struct can_bittiming *dbt;
	const struct can_clock *clk;
	const uint32_t *state;
	const struct can_ctrlmode *cm;
	const uint32_t *supported;
	const uint32_t *restart_ms;
	const struct can_berr_counter *bec;
};

struct link_info {
	char name[IFNAMSIZ];
	bool has_name;
	const uint32_t *mtu;
	const uint8_t *operstate;
	unsigned int ifi_flags;
	char kind[16];
	bool has_kind;
	struct can_attrs can;
};

static unsigned int atype(const struct rtattr *a)
{
	return a->rta_type & NLA_TYPE_MASK_;
}

static void parse_can_data(const struct rtattr *data, struct link_info *li)
{
	const struct rtattr *a = data;
	int len = (int)RTA_PAYLOAD(data);

	for (a = RTA_DATA(data); RTA_OK(a, len); a = RTA_NEXT(a, len)) {
		unsigned int t = atype(a);
		unsigned int pl = (unsigned int)RTA_PAYLOAD(a);

		switch (t) {
		case IFLA_CAN_BITTIMING:
			if (pl >= sizeof(struct can_bittiming))
				li->can.bt = RTA_DATA(a);
			break;
		case IFLA_CAN_DATA_BITTIMING:
			if (pl >= sizeof(struct can_bittiming))
				li->can.dbt = RTA_DATA(a);
			break;
		case IFLA_CAN_CLOCK:
			if (pl >= sizeof(struct can_clock))
				li->can.clk = RTA_DATA(a);
			break;
		case IFLA_CAN_STATE:
			if (pl >= sizeof(uint32_t))
				li->can.state = RTA_DATA(a);
			break;
		case IFLA_CAN_CTRLMODE:
			if (pl >= sizeof(struct can_ctrlmode))
				li->can.cm = RTA_DATA(a);
			break;
		case IFLA_CAN_RESTART_MS:
			if (pl >= sizeof(uint32_t))
				li->can.restart_ms = RTA_DATA(a);
			break;
		case IFLA_CAN_BERR_COUNTER:
			if (pl >= sizeof(struct can_berr_counter))
				li->can.bec = RTA_DATA(a);
			break;
		case IFLA_CAN_CTRLMODE_EXT: {
			const struct rtattr *e;
			int elen = (int)pl;

			for (e = RTA_DATA(a); RTA_OK(e, elen);
			     e = RTA_NEXT(e, elen)) {
				if (atype(e) == CANMODE_EXT_SUPPORTED &&
				    RTA_PAYLOAD(e) >= sizeof(uint32_t))
					li->can.supported = RTA_DATA(e);
			}
			break;
		}
		default:
			break;
		}
	}
}

static void parse_linkinfo(const struct rtattr *li_attr, struct link_info *li)
{
	const struct rtattr *a;
	int len = (int)RTA_PAYLOAD(li_attr);
	const struct rtattr *data = NULL;

	for (a = RTA_DATA(li_attr); RTA_OK(a, len); a = RTA_NEXT(a, len)) {
		if (atype(a) == IFLA_INFO_KIND) {
			size_t n = RTA_PAYLOAD(a);

			if (n >= sizeof(li->kind))
				n = sizeof(li->kind) - 1;
			memcpy(li->kind, RTA_DATA(a), n);
			li->kind[n] = '\0';
			li->has_kind = true;
		} else if (atype(a) == IFLA_INFO_DATA) {
			data = a;
		}
	}
	/* Only interpret the data nest as CAN attributes if the kind says so. */
	if (data && li->has_kind && strcmp(li->kind, "can") == 0)
		parse_can_data(data, li);
}

static void parse_link(const struct nlmsghdr *nh, struct link_info *li)
{
	const struct ifinfomsg *ifi = NLMSG_DATA(nh);
	const struct rtattr *a = IFLA_RTA(ifi);
	int len = (int)IFLA_PAYLOAD(nh);

	li->ifi_flags = ifi->ifi_flags;
	for (; RTA_OK(a, len); a = RTA_NEXT(a, len)) {
		switch (atype(a)) {
		case IFLA_IFNAME: {
			size_t n = RTA_PAYLOAD(a);

			if (n >= sizeof(li->name))
				n = sizeof(li->name) - 1;
			memcpy(li->name, RTA_DATA(a), n);
			li->name[n] = '\0';
			li->has_name = true;
			break;
		}
		case IFLA_MTU:
			if (RTA_PAYLOAD(a) >= sizeof(uint32_t))
				li->mtu = RTA_DATA(a);
			break;
		case IFLA_OPERSTATE:
			if (RTA_PAYLOAD(a) >= 1)
				li->operstate = RTA_DATA(a);
			break;
		case IFLA_LINKINFO:
			parse_linkinfo(a, li);
			break;
		default:
			break;
		}
	}
}

static const char *state_name(uint32_t s)
{
	static const char *const n[] = {
		"ERROR-ACTIVE", "ERROR-WARNING", "ERROR-PASSIVE",
		"BUS-OFF",      "STOPPED",       "SLEEPING",
	};

	return s < sizeof(n) / sizeof(n[0]) ? n[s] : "UNKNOWN";
}

static const char *oper_name(uint8_t s)
{
	static const char *const n[] = {
		"UNKNOWN", "NOTPRESENT", "DOWN", "LOWERLAYERDOWN",
		"TESTING", "DORMANT",    "UP",
	};

	return s < sizeof(n) / sizeof(n[0]) ? n[s] : "?";
}

static void print_modes(const char *label, uint32_t v)
{
	static const struct { uint32_t bit; const char *name; } m[] = {
		{ 0x001, "LOOPBACK" },      { 0x002, "LISTEN-ONLY" },
		{ 0x004, "3-SAMPLES" },     { 0x008, "ONE-SHOT" },
		{ 0x010, "BERR-REPORTING" },{ 0x020, "FD" },
		{ 0x040, "PRESUME-ACK" },   { 0x080, "FD-NON-ISO" },
		{ 0x100, "CC-LEN8-DLC" },   { 0x200, "TDC-AUTO" },
		{ 0x400, "TDC-MANUAL" },
	};
	uint32_t known = 0;
	bool first = true;

	printf("  %-22s 0x%08x  <", label, v);
	for (unsigned int i = 0; i < sizeof(m) / sizeof(m[0]); i++) {
		known |= m[i].bit;
		if (v & m[i].bit) {
			printf("%s%s", first ? "" : ",", m[i].name);
			first = false;
		}
	}
	if (v & ~known)
		printf("%sUNKNOWN-BITS(0x%x)", first ? "" : ",", v & ~known);
	printf(">\n");
}

static void print_bt(const char *label, const struct can_bittiming *bt)
{
	if (!bt) {
		printf("  %-22s unavailable\n", label);
		return;
	}
	printf("  %-22s %u bit/s  sample-point %u.%u%%  tq %u ns  "
	       "prop_seg %u  phase_seg1 %u  phase_seg2 %u  sjw %u  brp %u\n",
	       label, bt->bitrate, bt->sample_point / 10, bt->sample_point % 10,
	       bt->tq, bt->prop_seg, bt->phase_seg1, bt->phase_seg2, bt->sjw,
	       bt->brp);
}

static void print_report(const char *want, const struct link_info *li)
{
	const struct can_attrs *c = &li->can;

	printf("canmode (read-only RTM_GETLINK)\n");
	printf("interface requested : %s\n", want);
	printf("interface reported  : %s\n", li->has_name ? li->name : "unavailable");
	printf("link kind           : %s\n", li->has_kind ? li->kind : "unavailable");
	printf("flags               : 0x%x%s\n", li->ifi_flags,
	       (li->ifi_flags & IFF_UP) ? "  (administratively UP)" : "  (admin DOWN)");
	if (li->mtu)
		printf("mtu                 : %u\n", *li->mtu);
	else
		printf("mtu                 : unavailable\n");
	if (li->operstate)
		printf("operstate           : %s\n", oper_name(*li->operstate));
	else
		printf("operstate           : unavailable\n");

	if (!li->has_kind || strcmp(li->kind, "can") != 0) {
		printf("NOT A CAN DEVICE: no CAN attributes to report.\n");
		return;
	}

	printf("CAN attributes:\n");
	print_bt("nominal bit timing", c->bt);
	print_bt("data bit timing", c->dbt);
	if (c->clk)
		printf("  %-22s %u Hz\n", "can clock", c->clk->freq);
	else
		printf("  %-22s unavailable\n", "can clock");
	if (c->state)
		printf("  %-22s %s (%u)\n", "controller state",
		       state_name(*c->state), *c->state);
	else
		printf("  %-22s unavailable\n", "controller state");

	if (c->cm) {
		print_modes("ctrlmode ACTIVE", c->cm->flags);
		printf("  %-22s 0x%08x  (kernel leaves this 0 in a GETLINK reply; "
		       "not the supported set)\n",
		       "ctrlmode mask word", c->cm->mask);
		printf("  %-22s %s\n", "loopback",
		       (c->cm->flags & CAN_CTRLMODE_LOOPBACK) ? "ENABLED" : "disabled");
		printf("  %-22s %s\n", "listen-only",
		       (c->cm->flags & CAN_CTRLMODE_LISTENONLY) ? "ENABLED" : "disabled");
		printf("  %-22s %s\n", "FD mode",
		       (c->cm->flags & CAN_CTRLMODE_FD) ? "ENABLED" : "disabled");
	} else {
		printf("  %-22s unavailable\n", "ctrlmode ACTIVE");
		printf("  %-22s unavailable\n", "loopback");
		printf("  %-22s unavailable\n", "listen-only");
		printf("  %-22s unavailable\n", "FD mode");
	}
	if (c->supported)
		print_modes("ctrlmode SUPPORTED", *c->supported);
	else
		printf("  %-22s unavailable (kernel did not send CTRLMODE_EXT)\n",
		       "ctrlmode SUPPORTED");
	if (c->restart_ms)
		printf("  %-22s %u ms\n", "restart_ms", *c->restart_ms);
	else
		printf("  %-22s unavailable\n", "restart_ms");
	if (c->bec)
		printf("  %-22s tx %u  rx %u\n", "error counters (TEC/REC)",
		       c->bec->txerr, c->bec->rxerr);
	else
		printf("  %-22s unavailable (driver does not report)\n",
		       "error counters");
}

int main(int argc, char **argv)
{
	const char *ifname = argc > 1 ? argv[1] : "can0";
	struct link_info li;
	struct req rq;
	char buf[8192];
	unsigned int idx;
	int fd;
	ssize_t n;

	if (argc > 2) {
		fprintf(stderr, "usage: %s [ifname]   (read-only)\n", argv[0]);
		return 1;
	}
	idx = if_nametoindex(ifname);
	if (!idx) {
		fprintf(stderr, "canmode: no such interface '%s'\n", ifname);
		return 1;
	}

	fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
	if (fd < 0) {
		perror("canmode: socket(AF_NETLINK)");
		return 1;
	}

	memset(&rq, 0, sizeof(rq));
	rq.nh.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
	rq.nh.nlmsg_type = RTM_GETLINK;      /* the ONLY request ever sent */
	rq.nh.nlmsg_flags = NLM_F_REQUEST;
	rq.nh.nlmsg_seq = 1;
	rq.ifi.ifi_family = AF_UNSPEC;
	rq.ifi.ifi_index = (int)idx;

	if (send(fd, &rq, rq.nh.nlmsg_len, 0) < 0) {
		perror("canmode: send");
		close(fd);
		return 1;
	}

	memset(&li, 0, sizeof(li));
	for (;;) {
		struct nlmsghdr *nh;
		int len;

		n = recv(fd, buf, sizeof(buf), 0);
		if (n < 0) {
			perror("canmode: recv");
			close(fd);
			return 1;
		}
		len = (int)n;
		for (nh = (struct nlmsghdr *)buf; NLMSG_OK(nh, len);
		     nh = NLMSG_NEXT(nh, len)) {
			if (nh->nlmsg_type == NLMSG_ERROR) {
				const struct nlmsgerr *e = NLMSG_DATA(nh);

				fprintf(stderr, "canmode: kernel replied error %d (%s)\n",
					e->error, strerror(-e->error));
				close(fd);
				return 1;
			}
			if (nh->nlmsg_type == RTM_NEWLINK) {
				parse_link(nh, &li);
				close(fd);
				print_report(ifname, &li);
				return (li.has_kind && strcmp(li.kind, "can") == 0) ? 0 : 2;
			}
			if (nh->nlmsg_type == NLMSG_DONE) {
				fprintf(stderr, "canmode: no link data returned\n");
				close(fd);
				return 1;
			}
		}
	}
}
