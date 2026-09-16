/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef SECURE_CAN_TA_H
#define SECURE_CAN_TA_H

#include <stdint.h>

#define TA_SECURE_CAN_UUID \
	{ 0xabecf637, 0xca20, 0x467e, \
		{ 0x8e, 0x6c, 0x0b, 0x87, 0x79, 0xba, 0x6b, 0x2f } }

#define TA_SECURE_CAN_CMD_PROCESS_CHALLENGE 0
#define TA_SECURE_CAN_CMD_PROCESS_FRAME     1
#define TA_SECURE_CAN_CMD_VERIFY_FRAME      2
#define TA_SECURE_CAN_CMD_CLASSIFY_FRAME    3

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

/* Lower CAN IDs win bus arbitration, so real CAN systems already put
 * safety-critical traffic (braking, steering) on low IDs and lower-priority
 * traffic (telemetry, infotainment) on high ones. Secure World reuses that
 * convention to classify each frame before deciding whether it needs full
 * authentication or can be handled directly in Normal World.
 */
#define SECURE_CAN_CRITICAL_ID_MAX 0x102U

#define SECURE_CAN_CLASS_NORMAL 0U
#define SECURE_CAN_CLASS_SECURE 1U

#endif
