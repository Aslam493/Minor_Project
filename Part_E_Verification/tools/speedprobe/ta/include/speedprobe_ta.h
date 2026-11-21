/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef SPEEDPROBE_TA_H
#define SPEEDPROBE_TA_H
/* UUID of the speedprobe TA: 5a3f9d10-7c2b-4e61-9b1a-3d0e5f7a2c88 */
#define TA_SPEEDPROBE_UUID { 0x5a3f9d10, 0x7c2b, 0x4e61, { 0x9b, 0x1a, 0x3d, 0x0e, 0x5f, 0x7a, 0x2c, 0x88 } }
/* Command 0: run the timing loops. Output values (milliseconds):
 *   params[0].a = (A) register-only loop, 2,000,000 iterations      params[0].b = (B) 200 passes over 16 KiB (L1)
 *   params[1].a = (C) 20 passes over 256 KiB (L2)                    params[1].b = (D) 2000 calls of TEE_GetSystemTime
 *   params[2].a = (E) 50 HMAC-SHA256 of 12 bytes (TEE API)           params[2].b = (F) 50 SHA-256 of 64 bytes (TEE API)
 *   params[3].a = checksum (keeps the loops from being optimised away), params[3].b = distinct millisecond values seen in a short loop */
#define TA_SPEEDPROBE_CMD_RUN 0
#endif
