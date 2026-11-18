/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * canbridge: how the monitor's 44-byte message travels over a real CAN-FD frame (design 2026-11-18, replaces "mon_client builds the packet").
 *
 *   sender (can_sender)  ->  can0 (MCP2518FD, chip loopback)  ->  Linux SocketCAN  ->  can_forwarder  ->  PTA command 17
 *
 * Monitor message (44 bytes, little endian):  can_id(4) | counter(4) | payload(4) | tag(32), tag = HMAC-SHA256(key, first 12 bytes).
 * On the bus:   CAN-FD frame, identifier = can_id (flags such as EFF kept), data field = 48 bytes:
 *                   counter(4) | payload(4) | tag(32) | 8 bytes of zero padding   (CAN-FD has no 40-byte length: 32 -> 48)
 * The padding is NOT authenticated (it is not under the tag). The forwarder never needs the key: it only repacks the bytes.
 */
#ifndef CANBRIDGE_H
#define CANBRIDGE_H

#include <linux/can.h>
#include <stdint.h>
#include "mon_client.h"

#define CB_DATA_LEN 48

enum { CB_OK = 0, CB_ERR_FRAME = 1, CB_RTR = 2, CB_BAD_LEN = 3 };

/* Sender side: fill an FD frame. bad_tag flips one bit of the tag (test only). */
void cb_pack(const uint8_t key[32], uint32_t can_id, uint32_t counter, uint32_t payload, int bad_tag, struct canfd_frame *f);

/* Forwarder side: rebuild the 44-byte message from a received frame. Returns CB_OK or the reason the frame cannot be a monitor message
 * (error frame, remote frame, data length other than 48). The identifier is copied with its flag bits, so an extended-id frame reaches
 * the monitor as an id with bit 31 set, which the monitor judges MALFORMED. */
int cb_unpack(const struct canfd_frame *f, uint8_t msg[MON_FRAME_LEN]);

const char *cb_reason(int r);

#endif
