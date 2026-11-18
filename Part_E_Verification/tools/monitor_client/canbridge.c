// SPDX-License-Identifier: BSD-2-Clause
#include <stdlib.h>
#include <string.h>
#include "canbridge.h"

void cb_pack(const uint8_t key[32], uint32_t can_id, uint32_t counter, uint32_t payload, int bad_tag, struct canfd_frame *f)
{
	uint8_t msg[MON_FRAME_LEN];

	mon_build_frame(key, can_id, counter, payload, msg);
	if (bad_tag)
		msg[12] ^= 1;
	memset(f, 0, sizeof(*f));
	f->can_id = can_id;
	f->len = CB_DATA_LEN;
	f->flags = CANFD_BRS;
	memcpy(f->data, msg + 4, MON_FRAME_LEN - 4);			/* counter | payload | tag, 40 bytes; the last 8 bytes stay zero */
}

int cb_unpack(const struct canfd_frame *f, uint8_t msg[MON_FRAME_LEN])
{
	if (f->can_id & CAN_ERR_FLAG)
		return CB_ERR_FRAME;
	if (f->can_id & CAN_RTR_FLAG)
		return CB_RTR;
	if (f->len != CB_DATA_LEN)
		return CB_BAD_LEN;
	msg[0] = (uint8_t)f->can_id; msg[1] = (uint8_t)(f->can_id >> 8); msg[2] = (uint8_t)(f->can_id >> 16); msg[3] = (uint8_t)(f->can_id >> 24);
	memcpy(msg + 4, f->data, MON_FRAME_LEN - 4);
	return CB_OK;
}

static int hv(int ch) { return ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1; }

int cb_parse_line(const char *line, uint32_t *delay_ms, struct canfd_frame *f)
{
	char *e; unsigned long d, id; const char *p = line; int i;

	while (*p == ' ' || *p == '\t') p++;
	if (*p == 0 || *p == '#' || *p == '\n' || *p == '\r') return 1;
	d = strtoul(p, &e, 10);
	if (e == p || d > 3600000ul) return -1;
	p = e;
	id = strtoul(p, &e, 16);
	if (e == p || id > 0xFFFFFFFFul) return -1;
	p = e;
	while (*p == ' ' || *p == '\t') p++;
	memset(f, 0, sizeof(*f));
	for (i = 0; i < CB_DATA_LEN; i++) {
		int a = hv(p[2 * i]), b = a < 0 ? -1 : hv(p[2 * i + 1]);
		if (a < 0 || b < 0) return -1;
		f->data[i] = (uint8_t)(a << 4 | b);
	}
	p += 2 * CB_DATA_LEN;
	while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
	if (*p != 0 && *p != '#') return -1;
	*delay_ms = (uint32_t)d; f->can_id = (uint32_t)id; f->len = CB_DATA_LEN; f->flags = CANFD_BRS;
	return 0;
}

const char *cb_reason(int r)
{
	switch (r) {
	case CB_OK: return "ok";
	case CB_ERR_FRAME: return "error frame";
	case CB_RTR: return "remote frame";
	case CB_BAD_LEN: return "data length is not 48";
	default: return "?";
	}
}
