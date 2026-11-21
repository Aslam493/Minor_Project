/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef USER_TA_HEADER_DEFINES_H
#define USER_TA_HEADER_DEFINES_H
#include <speedprobe_ta.h>
#define TA_UUID			TA_SPEEDPROBE_UUID
#define TA_FLAGS		0
#define TA_STACK_SIZE		(8 * 1024)
#define TA_DATA_SIZE		(1024 * 1024)
#define TA_VERSION		"1.0"
#define TA_DESCRIPTION		"speedprobe: timing loops inside the secure world (burst study root-cause check)"
#endif
