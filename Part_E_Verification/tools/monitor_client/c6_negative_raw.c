/*
 * c6_negative_raw: board negative tests (plan unit U2, T25 and bad parameters) through RAW TEEC_InvokeCommand calls.
 *
 *   usage: c6_negative_raw
 *
 * SAFETY: every call that could act (legacy ids 0,3,6,7,13,14 and the chip-A debug readers) is sent with DELIBERATELY WRONG parameter
 * types. The legacy handlers check their parameter types before they do anything (pta/rpi3_can_spi_poc.c), so even if a refusal were
 * missing the call returns BAD_PARAMETERS and nothing is armed, written, transmitted or woken. Result codes tell the two apart:
 *   refused        -> TEE_ERROR_NOT_SUPPORTED (0xFFFF000A) plus an exact LEGACY_CMD_REFUSED count
 *   NOT refused    -> TEE_ERROR_BAD_PARAMETERS (0xFFFF0006) = FAIL, but harmless.
 * Writes nothing persistent. RAM-side effect only: the exact LEGACY_CMD_REFUSED counter and its (rate-limited) events.
 * Prints PASS / FAIL / NOTE per check; exit 1 if any FAIL.
 */
#define _GNU_SOURCE
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mon_client.h"

#define EV_LEGACY_REFUSED 18
#define EV_LEGACY_USED 19
#define NOT_SUPPORTED 0xFFFF000Au
#define BAD_PARAMETERS 0xFFFF0006u
#define SHORT_BUFFER 0xFFFF0010u

static struct mon_ctx C;
static int n_pass, n_fail, n_note;
static uint8_t bufs[4][64];

static void PASSF(int ok, const char *id, const char *fmt, ...)
{ va_list a; char b[300]; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a); printf("  %s  %-5s %s\n", ok ? "PASS" : "FAIL", id, b); if (ok) n_pass++; else n_fail++; }
static void NOTE(const char *id, const char *fmt, ...)
{ va_list a; char b[300]; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a); printf("  note  %-5s %s\n", id, b); n_note++; }

/* parameter-type patterns that are wrong for every command in this PTA */
#define PT_ALL_MEMREF TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INOUT, TEEC_MEMREF_TEMP_INOUT, TEEC_MEMREF_TEMP_INOUT, TEEC_MEMREF_TEMP_INOUT)
#define PT_NONE4 TEEC_PARAM_TYPES(TEEC_NONE, TEEC_NONE, TEEC_NONE, TEEC_NONE)

static uint32_t raw(uint32_t cmd, uint32_t ptypes)
{
	TEEC_Operation op; uint32_t origin = 0; TEEC_Result r;
	memset(&op, 0, sizeof op); memset(bufs, 0, sizeof bufs);
	op.paramTypes = ptypes;
	for (int i = 0; i < 4; i++) { op.params[i].tmpref.buffer = bufs[i]; op.params[i].tmpref.size = sizeof bufs[i]; }
	r = TEEC_InvokeCommand(&C.session, cmd, &op, &origin);
	return (uint32_t)r;
}
static void ev_counts(uint32_t out[MON_EVENT_TYPES]) { struct mon_status1 s; struct mon_health h; TEEC_Result r; memset(&s, 0, sizeof s); r = mon_get_status1(&C, &s, &h); if (r) printf("  !! status1 failed 0x%x\n", (unsigned)r); memcpy(out, s.ev_count, MON_EVENT_TYPES * 4); }
struct snap { uint32_t ev[MON_EVENT_TYPES]; uint32_t last_counter, idle_uses, ev_ack, vf, vh, vx; };
static void take(struct snap *s)
{
	struct mon_status0 a; struct mon_status1 b; struct mon_health h; memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
	if (mon_get_status0(&C, &a, &h))
		printf("  !! status0 failed\n");
	if (mon_get_status1(&C, &b, &h))
		printf("  !! status1 failed\n");
	memcpy(s->ev, b.ev_count, sizeof s->ev); s->last_counter = a.last_counter; s->idle_uses = a.idle_uses; s->ev_ack = a.ev_ack; s->vf = b.v_frame_total; s->vh = b.v_hb_total; s->vx = b.v_xcheck_total;
}
static int same(const struct snap *a, const struct snap *b)
{ return !memcmp(a->ev, b->ev, sizeof a->ev) && a->last_counter == b->last_counter && a->idle_uses == b->idle_uses && a->ev_ack == b->ev_ack && a->vf == b->vf && a->vh == b->vh && a->vx == b->vx; }

int main(void)
{
	static const uint32_t refused[] = { 0, 3, 6, 7, 13, 14 }, readers[] = { 4, 5, 8, 9, 10, 11, 15, 16 }, kept[] = { 1, 2, 12 };
	struct mon_status0 s0; struct mon_health h; uint32_t e0[MON_EVENT_TYPES], e1[MON_EVENT_TYPES]; struct snap a, b; unsigned i, ok;
	if (mon_open(&C)) { fprintf(stderr, "cannot open the PTA session\n"); return 2; }
	memset(&s0, 0, sizeof s0); if (mon_get_status0(&C, &s0, &h)) { fprintf(stderr, "status failed\n"); return 2; }
	int hardened = (s0.flags & 0x02) != 0;
	printf("c6_negative_raw: boot_id 0x%08x flags 0x%02x profile %s, init_ok=%d starved=%d\n", s0.boot_id, s0.flags, hardened ? "HARDENED" : "DEBUG", !!(h.p2b & MON_HP_INIT_OK), !!(h.p2b & MON_HP_STARVED));

	/* R1: legacy ids refused in both profiles. Two wrong-type patterns each. */
	printf("== legacy ids 0,3,6,7,13,14 (wrong parameter types: harmless even if a refusal were missing)\n");
	ev_counts(e0); ok = 1; for (i = 0; i < 6; i++) { uint32_t r1 = raw(refused[i], PT_ALL_MEMREF), r2 = raw(refused[i], PT_NONE4);
		if (r1 != NOT_SUPPORTED || r2 != NOT_SUPPORTED) { ok = 0; printf("     id %u: 0x%08x / 0x%08x\n", refused[i], r1, r2); } } ev_counts(e1);
	PASSF(ok, "R1", "ids 0,3,6,7,13,14 x 2 patterns -> all NOT_SUPPORTED (a BAD_PARAMETERS would mean the refusal is missing)");
	PASSF(e1[EV_LEGACY_REFUSED] - e0[EV_LEGACY_REFUSED] == 12 && e1[EV_LEGACY_USED] == e0[EV_LEGACY_USED], "R1b", "LEGACY_CMD_REFUSED +%u (expected exactly 12: the counter is exact even though the ring limits events to 1/s), LEGACY_CMD_USED +%u (expected 0)", e1[EV_LEGACY_REFUSED] - e0[EV_LEGACY_REFUSED], e1[EV_LEGACY_USED] - e0[EV_LEGACY_USED]);

	/* R2: chip-A debug readers: refused in the hardened profile */
	printf("== chip-A debug readers 4,5,8,9,10,11,15,16\n");
	if (hardened) {
		ev_counts(e0); ok = 1; for (i = 0; i < 8; i++) { uint32_t r1 = raw(readers[i], PT_ALL_MEMREF), r2 = raw(readers[i], PT_NONE4); if (r1 != NOT_SUPPORTED || r2 != NOT_SUPPORTED) { ok = 0; printf("     id %u: 0x%08x / 0x%08x\n", readers[i], r1, r2); } } ev_counts(e1);
		PASSF(ok, "R2", "8 readers x 2 patterns -> NOT_SUPPORTED in the hardened profile");
		PASSF(e1[EV_LEGACY_REFUSED] - e0[EV_LEGACY_REFUSED] == 16, "R2b", "LEGACY_CMD_REFUSED +%u (expected 16)", e1[EV_LEGACY_REFUSED] - e0[EV_LEGACY_REFUSED]);
	} else NOTE("R2", "debug profile: the readers are allowed there; not run");

	/* R3: ids beyond 23 */
	printf("== unknown ids\n");
	{ static const uint32_t unk[] = { 24, 25, 26, 27, 28, 29, 30, 40, 100, 0x1000, 0xFFFFFFFFu }; take(&a); ok = 1;
	  for (i = 0; i < sizeof unk / sizeof unk[0]; i++) { uint32_t r1 = raw(unk[i], PT_ALL_MEMREF), r2 = raw(unk[i], PT_NONE4); if (r1 != NOT_SUPPORTED || r2 != NOT_SUPPORTED) { ok = 0; printf("     id 0x%x: 0x%08x / 0x%08x\n", unk[i], r1, r2); } }
	  take(&b); PASSF(ok && same(&a, &b), "R3", "ids 24-30, 40, 100, 0x1000, 0xFFFFFFFF x 2 patterns -> NOT_SUPPORTED, monitor state unchanged"); }

	/* R4: monitor commands 17..23 with wrong parameter types */
	printf("== monitor commands 17-23 with wrong parameter types\n");
	{ take(&a); ok = 1; for (uint32_t c = 17; c <= 23; c++) { uint32_t r1 = raw(c, PT_ALL_MEMREF), r2 = raw(c, PT_NONE4); if (r1 != BAD_PARAMETERS || r2 != BAD_PARAMETERS) { ok = 0; printf("     cmd %u: 0x%08x / 0x%08x\n", c, r1, r2); } }
	  take(&b); PASSF(ok, "R4", "commands 17-23 x 2 wrong patterns -> BAD_PARAMETERS"); PASSF(same(&a, &b), "R4b", "no counter, verdict total, event, last_counter, idle use or acknowledgement changed"); }

	/* R5: short buffers deliver the required size */
	printf("== short buffers\n");
	{ TEEC_Operation op; uint32_t origin = 0; uint8_t small[8]; memset(&op, 0, sizeof op);
	  op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_INOUT, TEEC_MEMREF_TEMP_OUTPUT, TEEC_VALUE_OUTPUT, TEEC_VALUE_OUTPUT);
	  op.params[0].value.a = 1; op.params[0].value.b = 5; op.params[1].tmpref.buffer = small; op.params[1].tmpref.size = sizeof small;
	  uint32_t r = (uint32_t)TEEC_InvokeCommand(&C.session, 18, &op, &origin);
	  PASSF(r == SHORT_BUFFER && op.params[1].tmpref.size == 5 * sizeof(struct mon_event), "R5", "command 18 with an 8-byte buffer for 5 events -> SHORT_BUFFER, required size %zu (expected %zu)", (size_t)op.params[1].tmpref.size, 5 * sizeof(struct mon_event));
	  memset(&op, 0, sizeof op); op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_INOUT, TEEC_MEMREF_TEMP_OUTPUT, TEEC_VALUE_OUTPUT, TEEC_VALUE_OUTPUT);
	  op.params[0].value.a = 0; op.params[0].value.b = 0; op.params[1].tmpref.buffer = small; op.params[1].tmpref.size = sizeof small;
	  r = (uint32_t)TEEC_InvokeCommand(&C.session, 20, &op, &origin);
	  PASSF(r == SHORT_BUFFER && op.params[1].tmpref.size == 128, "R5b", "command 20 with an 8-byte buffer -> SHORT_BUFFER, required size %zu (expected 128)", (size_t)op.params[1].tmpref.size); }
	/* R6: command 18 max out of range */
	{ TEEC_Operation op; uint32_t origin = 0; uint8_t big[160]; uint32_t r1, r2; memset(&op, 0, sizeof op);
	  op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_INOUT, TEEC_MEMREF_TEMP_OUTPUT, TEEC_VALUE_OUTPUT, TEEC_VALUE_OUTPUT);
	  op.params[0].value.a = 1; op.params[0].value.b = 0; op.params[1].tmpref.buffer = big; op.params[1].tmpref.size = sizeof big; r1 = (uint32_t)TEEC_InvokeCommand(&C.session, 18, &op, &origin);
	  op.params[0].value.a = 1; op.params[0].value.b = 6; op.params[1].tmpref.size = sizeof big; r2 = (uint32_t)TEEC_InvokeCommand(&C.session, 18, &op, &origin);
	  PASSF(r1 == BAD_PARAMETERS && r2 == BAD_PARAMETERS, "R6", "command 18 with max_entries 0 and 6 -> BAD_PARAMETERS (0x%08x, 0x%08x)", r1, r2); }

	/* R7: ids 1, 2, 12 still reach the unchanged legacy path with their correct types */
	printf("== legacy ids kept: 1, 2, 12\n");
	{ ok = 1; for (i = 0; i < 3; i++) { TEEC_Operation op; uint32_t origin = 0; memset(&op, 0, sizeof op); op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_OUTPUT, TEEC_NONE, TEEC_NONE, TEEC_NONE);
		uint32_t r = (uint32_t)TEEC_InvokeCommand(&C.session, kept[i], &op, &origin); if (r != 0) { ok = 0; printf("     id %u: 0x%08x\n", kept[i], r); } }
	  PASSF(ok, "R7", "ids 1 (FIQ count), 2 (FIQ control), 12 (Core 1 heartbeat) -> SUCCESS with VALUE_OUTPUT"); }

	memset(&s0, 0, sizeof s0); (void)mon_get_status0(&C, &s0, &h);
	PASSF((h.p2b & MON_HP_INIT_OK) && !(h.p2b & MON_HP_STARVED), "H", "monitor still healthy: init_ok=1 starved=0, Core 1 last poll %u ms ago", h.p2a);
	printf("SUMMARY: %d PASS, %d FAIL, %d note\n", n_pass, n_fail, n_note);
	mon_close(&C);
	return n_fail ? 1 : 0;
}
