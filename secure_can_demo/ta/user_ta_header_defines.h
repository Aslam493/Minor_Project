/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef USER_TA_HEADER_DEFINES_H
#define USER_TA_HEADER_DEFINES_H

#include <secure_can_ta.h>

#define TA_UUID TA_SECURE_CAN_UUID
#define TA_FLAGS 0
#define TA_STACK_SIZE (2 * 1024)
#define TA_DATA_SIZE (32 * 1024)
#define TA_VERSION "1.0"
#define TA_DESCRIPTION "Minor Project Secure CAN demonstration TA"

#endif
