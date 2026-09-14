/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef SECURE_CAN_TA_H
#define SECURE_CAN_TA_H

#define TA_SECURE_CAN_UUID \
	{ 0xabecf637, 0xca20, 0x467e, \
		{ 0x8e, 0x6c, 0x0b, 0x87, 0x79, 0xba, 0x6b, 0x2f } }

#define TA_SECURE_CAN_CMD_PROCESS_CHALLENGE 0

#define PTA_SECURE_CAN_PHASE_A_UUID \
	{ 0x7cb1a48e, 0x3b5f, 0x4b73, \
		{ 0x91, 0xe8, 0x73, 0x31, 0xc4, 0x7a, 0xd2, 0x10 } }

#define PTA_SECURE_CAN_PHASE_A_GET_STATUS 0

#define SECURE_CAN_CHALLENGE 0x12345678U
#define SECURE_CAN_MASK      0xa5a5a5a5U
#define SECURE_CAN_RESPONSE  (SECURE_CAN_CHALLENGE ^ SECURE_CAN_MASK)
#define SECURE_CAN_MARKER    0x5343414eU /* ASCII: SCAN */

#endif
