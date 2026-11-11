// Layout check: the client copy of struct mon_tstats equals the real struct rpi3_mon_tstats (build with -DCFG_RPI3_CAN_MONITOR_TEST_STATS).
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <rpi3_can_monitor.h>
#include "mon_tstats.h"
_Static_assert(sizeof(struct mon_tstats) == sizeof(struct rpi3_mon_tstats), "size");
#define S(f) _Static_assert(offsetof(struct mon_tstats, f) == offsetof(struct rpi3_mon_tstats, f), #f)
S(hold_start); S(hold_count); S(hold_sum); S(hold_max); S(hold_max_ctx); S(hold_hist); S(wait_last_ticks); S(wait_last_attempts); S(wait_exhausted);
S(att_prev); S(att_count); S(att_sum); S(att_min); S(att_max); S(hold_req_ticks); S(hold_req_done); S(cadence_mask);
_Static_assert(MON_TS_HIST == RPI3_MON_TS_HIST, "histogram size");
int main(void) { printf("layout equal: size %zu\n", sizeof(struct mon_tstats)); return 0; }
