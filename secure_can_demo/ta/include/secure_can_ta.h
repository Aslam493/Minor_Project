/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef SECURE_CAN_TA_H
#define SECURE_CAN_TA_H

#include <stdint.h>

#define TA_SECURE_CAN_UUID \
	{ 0xabecf637, 0xca20, 0x467e, \
		{ 0x8e, 0x6c, 0x0b, 0x87, 0x79, 0xba, 0x6b, 0x2f } }

#define TA_SECURE_CAN_CMD_PROCESS_CHALLENGE 0
#define TA_SECURE_CAN_CMD_PROCESS_FRAME     1

#define SECURE_CAN_CHALLENGE 0x12345678U
#define SECURE_CAN_MASK      0xa5a5a5a5U
#define SECURE_CAN_RESPONSE  (SECURE_CAN_CHALLENGE ^ SECURE_CAN_MASK)
#define SECURE_CAN_MARKER    0x5343414eU /* ASCII: SCAN */

/* Simulated CAN frame: Linux decides when one is "received" and sends it
 * into Secure World for authentication, standing in for a real CAN
 * controller until CAN hardware is available.
 */
#define SECURE_CAN_FRAME_DATA_LEN 8U
#define SECURE_CAN_FRAME_TAG_LEN  8U

struct secure_can_frame {
	uint32_t can_id;
	uint8_t data[SECURE_CAN_FRAME_DATA_LEN];
};

#endif
