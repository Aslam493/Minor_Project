// SPDX-License-Identifier: BSD-2-Clause
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
