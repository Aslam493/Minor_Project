#!/usr/bin/env python3
"""Mutation testing of the REAL monitor sources (rpi3_can_monitor.c / .h and the PTA glue): inject one bug at a time into a
temp copy, rebuild the host harness against it, and require a failed check, a crash, a hang or a compile-time static assertion.
Survivors are listed and must be explained (equivalent mutants) or fixed. Touches nothing in the OP-TEE tree.
usage: mutation_real.py [-j N] [name-substring]"""
import os, shutil, subprocess, sys, tempfile, concurrent.futures as cf
HERE = os.path.dirname(os.path.abspath(__file__))
OS = os.path.abspath(os.path.join(HERE, '../../../optee_full/optee_os'))
PLAT = OS + '/core/arch/arm/plat-rpi3'
SRC = {'c': open(PLAT + '/rpi3_can_monitor.c').read(), 'h': open(PLAT + '/rpi3_can_monitor.h').read(), 'g': open(OS + '/core/pta/rpi3_can_monitor_pta.c').read()}
E = True  # expected equivalent
M = [  # (name, file, [(old, new[, count])], expected_equivalent)
 # ---- bounded acquisition
 ("old fail-open test (now>=t0 && elapsed>budget)", 'c', [("if (now < t0 ||", "if (0 ||"), ("w->wait_ticks_used + (now - t0) > RPI3_MON_BUDGET_TICKS ||", "(now >= t0 && w->wait_ticks_used + (now - t0) > RPI3_MON_BUDGET_TICKS) ||")], False),
 ("no 'now < t0' term (unsigned wrap already fails closed)", 'c', [("if (now < t0 ||", "if (0 ||")], E),
 ("no attempt cap", 'c', [("w->attempts_used >= RPI3_MON_ATTEMPT_CAP)\t/* timer-independent backstop */", "0)")], False),
 ("fresh budget per acquisition", 'c', [("w->wait_ticks_used + (now - t0) > RPI3_MON_BUDGET_TICKS ||", "(now - t0) > RPI3_MON_BUDGET_TICKS ||")], False),
 ("counter incremented on success", 'c', [("*exc = e;\n\t\t\treturn RPI3_MON_OK;", "*exc = e;\n\t\t\tMON_PLAT_INC32(&rpi3_mon_pta_lock_timeouts);\n\t\t\treturn RPI3_MON_OK;")], False),
 ("no increment on exhaustion", 'c', [("\tMON_PLAT_INC32(&rpi3_mon_pta_lock_timeouts);\n\treturn RPI3_MON_BUSY;", "\treturn RPI3_MON_BUSY;")], False),
 ("double increment on exhaustion", 'c', [("\tMON_PLAT_INC32(&rpi3_mon_pta_lock_timeouts);\n\treturn RPI3_MON_BUSY;", "\tMON_PLAT_INC32(&rpi3_mon_pta_lock_timeouts);\n\tMON_PLAT_INC32(&rpi3_mon_pta_lock_timeouts);\n\treturn RPI3_MON_BUSY;")], False),
 ("unmask before unlock on release", 'c', [("\tMON_PLAT_UNLOCK_RESTORE(&rpi3_mon_lock, exc);\n}", "\tMON_PLAT_UNMASK(exc);\n\tMON_PLAT_UNLOCK_RESTORE(&rpi3_mon_lock, exc);\n}")], False),
 ("exceptions not restored after exhaustion", 'c', [("\tMON_PLAT_UNMASK(e);\n\nexhausted:", "exhausted:")], False),
 ("exceptions never masked while trying", 'c', [("\t/* 2. mask every exception, then read the timer */\n\te = MON_PLAT_MASK_ALL();", "\te = 0;")], False),
 ("pre-check removed", 'c', [("\tif (w->wait_ticks_used >= RPI3_MON_BUDGET_TICKS || w->attempts_used >= RPI3_MON_ATTEMPT_CAP)\n\t\tgoto exhausted;", "")], False),
 ("backward timer after success charges nothing", 'c', [(": RPI3_MON_BUDGET_TICKS;", ": 0;")], False),
 # ---- init
 ("init_ok published before the extra steps", 'c', [("\tres = rpi3_mon_init_crypto();", "\tMON_PLAT_STORE_RELEASE32(&rpi3_mon.init_ok, 1);\n\tres = rpi3_mon_init_crypto();")], False),
 ("init failure fails the OS boot", 'c', [("(unsigned int)res);\n\t\treturn TEE_SUCCESS;", "(unsigned int)res);\n\t\treturn res;")], False),
 ("init failure still publishes init_ok", 'c', [("(unsigned int)res);\n\t\treturn TEE_SUCCESS;", "(unsigned int)res);\n\t\tMON_PLAT_STORE_RELEASE32(&rpi3_mon.init_ok, 1);\n\t\treturn TEE_SUCCESS;")], False),
 ("init failure not reported", 'c', [("\t\tMON_PLAT_EMSG(", "\t\t(void)(")], False),
 ("profile inverted", 'c', [("MON_PLAT_PROFILE_DEBUG ? RPI3_MON_PROFILE_DEBUG : RPI3_MON_PROFILE_HARDENED", "MON_PLAT_PROFILE_DEBUG ? RPI3_MON_PROFILE_HARDENED : RPI3_MON_PROFILE_DEBUG")], False),
 ("init does not clear the lock explicitly (memset already zeroes it)", 'c', [("\trpi3_mon_lock = SPINLOCK_UNLOCK;\t\t/* explicit, in addition to the .bss zero */", "")], E),
 ("init does not clear the state", 'c', [("\tmemset(&rpi3_mon, 0, sizeof(rpi3_mon));\n", "")], False),
 ("init_ticks not recorded", 'c', [("rpi3_mon.init_ticks = MON_PLAT_TIMER();", "rpi3_mon.init_ticks = 0;")], False),
 ("boot_id from the wrong key", 'c', [("res = rpi3_mon_hmac(rpi3_mon_key_hb, in, sizeof(in), dig);", "res = rpi3_mon_hmac(rpi3_mon_key_frame, in, sizeof(in), dig);")], False),
 ("init ignores an RNG failure", 'c', [("res = MON_PLAT_RNG(prng, sizeof(prng));\n\tif (res != TEE_SUCCESS)\n\t\treturn res;", "res = MON_PLAT_RNG(prng, sizeof(prng));")], False),
 ("init ignores a boot_id HMAC failure", 'c', [("res = rpi3_mon_hmac(rpi3_mon_key_hb, in, sizeof(in), dig);\n\tif (res != TEE_SUCCESS)\n\t\treturn res;", "res = rpi3_mon_hmac(rpi3_mon_key_hb, in, sizeof(in), dig);")], False),
 # ---- health
 ("health age not saturated", 'c', [("\t\tif (age > RPI3_MON_HP_AGE_NEVER)\n\t\t\tage = RPI3_MON_HP_AGE_NEVER;", "")], False),
 ("health 'never polled' sentinel removed", 'c', [("if (lp == 0) {", "if (0) {")], False),
 ("health future stamp wraps (O-1 broken)", 'c', [("} else if (now >= lp) {", "} else {")], False),
 ("health skip_count not saturated", 'c', [("(sk > 255U ? 255U : sk)", "sk")], False),
 ("health init_ok bit missing", 'c', [("\tif (rpi3_mon_init_ok())\n\t\tb |= RPI3_MON_HP_INIT_OK;", "")], False),
 ("health starved bit wrong", 'c', [("\tif (st)\n\t\tb |= RPI3_MON_HP_STARVED;", "\tif (!st)\n\t\tb |= RPI3_MON_HP_STARVED;")], False),
 ("health p3a reads the wrong field", 'c', [("h->p3a = rpi3_mon_load_u32(&rpi3_mon_pta_lock_timeouts);", "h->p3a = rpi3_mon_load_u32(&rpi3_mon.health.skip_total);")], False),
 ("health age in ticks instead of ms", 'c', [("age = (now - lp) / RPI3_MON_TICKS_PER_MS;", "age = (now - lp);")], False),
 # ---- event ring
 ("event overwrite not counted", 'c', [("\t\tl->ev_lost++;\t\t\t\t/* an unread entry is overwritten: always counted */\n", "")], False),
 ("pinned_used not maintained", 'c', [("\t\tl->pinned_used++;\n", "")], False),
 ("limit boundary off by one", 'c', [("now - l->diag_last_ticks[type] < RPI3_MON_TICKS_PER_SEC)\n\t\treturn;", "now - l->diag_last_ticks[type] <= RPI3_MON_TICKS_PER_SEC)\n\t\treturn;")], False),
 ("evidence flood rule reverted (evidence never limited)", 'c', [("if (l->diag_have[type] && now >= l->diag_last_ticks[type] &&", "if (type >= RPI3_MON_EV_N_EVIDENCE && l->diag_have[type] && now >= l->diag_last_ticks[type] &&")], False),
 ("one shared limit bucket for all classes", 'c', [("l->diag_have[type]", "l->diag_have[0]", -1), ("l->diag_last_ticks[type]", "l->diag_last_ticks[0]", -1)], False),
 ("exact counter not kept", 'c', [("\tl->ev_count[type]++;\n\tif (l->ev_seq == 0xFFFFFFFFU)", "\tif (l->ev_seq == 0xFFFFFFFFU)")], False),
 ("pinned slots can be overwritten", 'c', [("type < RPI3_MON_EV_N_EVIDENCE && l->pinned_n[type] < RPI3_MON_PINNED_PER_CLASS", "type < RPI3_MON_EV_N_EVIDENCE && l->pinned_n[type] < RPI3_MON_PINNED_PER_CLASS && l->ev_seq < 8")], False),
 ("seq space exhaustion guard removed", 'c', [("\tif (l->ev_seq == 0xFFFFFFFFU)\t\t\t/* seq space exhausted: counters keep counting, no more records */\n\t\treturn;\n", "")], False),
 # ---- evaluation
 ("traffic recovery does not clear the flag", 'c', [("\t\tl->tr_timeout_raised = false;\n\t\trpi3_mon_ev_append(l, RPI3_MON_EV_TRAFFIC_RECOVERED", "\t\trpi3_mon_ev_append(l, RPI3_MON_EV_TRAFFIC_RECOVERED")], False),
 ("lease expiry does not restart the window", 'c', [("\t\tl->idle_until_ticks = 0;\n\t\tl->tr_last_valid_ticks = now;\n\t\trpi3_mon_ev_append(l, RPI3_MON_EV_MODE_CHANGED, 0, 0, now);\n\t}\n\tlease_active", "\t\tl->idle_until_ticks = 0;\n\t\trpi3_mon_ev_append(l, RPI3_MON_EV_MODE_CHANGED, 0, 0, now);\n\t}\n\tlease_active")], False),
 ("traffic timeout ignores the lease", 'c', [("if (l->tr_armed && !lease_active && !l->tr_timeout_raised &&", "if (l->tr_armed && !l->tr_timeout_raised &&")], False),
 ("traffic timeout not exactly once", 'c', [("!lease_active && !l->tr_timeout_raised &&", "!lease_active &&")], False),
 ("heartbeat miss counted on every evaluation", 'c', [("if (l->hb_state == RPI3_MON_HB_OPEN && rpi3_mon_expired(now, l->hb_issue_ticks", "if ((l->hb_state == RPI3_MON_HB_OPEN || l->hb_state == RPI3_MON_HB_MISSED) && rpi3_mon_expired(now, l->hb_issue_ticks")], False),
 ("heartbeat timeout raised before the grace period", 'c', [("now - rpi3_mon.init_ticks > RPI3_MON_G_TICKS &&", "1 &&")], False),
 ("heartbeat timeout after a single miss", 'c', [("l->hb_miss_count >= RPI3_MON_K_MISS ||", "l->hb_miss_count >= 1 ||")], False),
 ("heartbeat detail codes swapped", 'c', [("l->hb_last_ok_ticks == 0 ? 3 : 2", "l->hb_last_ok_ticks == 0 ? 2 : 3")], False),
 ("challenge burst (next issue relative to the old one)", 'c', [("l->hb_next_issue = now + RPI3_MON_H_TICKS;", "l->hb_next_issue += RPI3_MON_H_TICKS;")], False),
 ("a new challenge keeps the old nonce valid", 'c', [("\tl->hb_nonce_valid = false;\t\t\t/* the previous nonce is invalid (G6-4) */\n", "")], False),
 ("challenge sequence can wrap", 'c', [("if (l->hb_seq == 0xFFFFFFFFU) {\t\t\t/* never wraps (G6-5) */", "if (0) {")], False),
 # ---- command 17
 ("older counter accepted as duplicate class (<=)", 'c', [("} else if (counter < c->last_counter) {", "} else if (counter <= c->last_counter) {")], False),
 ("no duplicate/conflict distinction", 'c', [("rpi3_mon_frame_equal(buf, c->last_frame44) ? RPI3_MON_V17_DUPLICATE : RPI3_MON_V17_CONFLICT", "RPI3_MON_V17_DUPLICATE")], False),
 ("counter wrap guard removed", 'c', [("c->last_counter != 0xFFFFFFFFU && counter == c->last_counter + 1U", "counter == c->last_counter + 1U")], E),
 ("gap detail off by one", 'c', [("*det = counter - c->last_counter - 1U;", "*det = counter - c->last_counter;")], False),
 ("every frame refreshes the state", 'c', [("\tif (accept) {\n\t\tc->have_last = true;", "\tif (1) {\n\t\tc->have_last = true;")], False),
 ("DUPLICATE mapped to the wrong event detail", 'c', [("case RPI3_MON_V17_DUPLICATE:\n\t\trpi3_mon_ev_append(l, RPI3_MON_EV_REPLAY, counter, 2, now);", "case RPI3_MON_V17_DUPLICATE:\n\t\trpi3_mon_ev_append(l, RPI3_MON_EV_REPLAY, counter, 1, now);")], False),
 ("frame verdict total not counted", 'c', [("\tl->verdict_frame.total++;\n", "")], False),
 ("HMAC over the whole frame", 'c', [("rpi3_mon_hmac(rpi3_mon_key_frame, buf, 12, tag)", "rpi3_mon_hmac(rpi3_mon_key_frame, buf, 44, tag)")], False),
 ("tag compared over 31 bytes", 'c', [("rpi3_mon_ct_equal(tag, buf + 12, 32)", "rpi3_mon_ct_equal(tag, buf + 12, 31)")], False),
 ("stream list not enforced", 'c', [("} else if (can_id != RPI3_MON_STREAM_CAN_ID) {", "} else if (0) {")], False),
 ("can_id range not enforced", 'c', [("if (can_id > RPI3_MON_CAN_ID_MAX) {", "if (0) {")], False),
 ("length not enforced (buffer read)", 'c', [("if (len != RPI3_MON_FRAME_LEN) {", "if (0) {")], False),
 ("mac guard never released in command 17", 'c', [("tag);\t/* outside the lock */\n\t\t\trpi3_mon_mac_leave();", "tag);\t/* outside the lock */")], False),
 ("unauthenticated frames are applied", 'c', [("if (authentic)\n\t\trpi3_mon_frame_apply(", "if (1)\n\t\trpi3_mon_frame_apply(")], False),
 ("HMAC failure not fail-closed", 'c', [("\t\t\t\treturn RPI3_MON_R_BAD_STATE;\t/* cannot authenticate: fail closed, change nothing */", "\t\t\t\tauthentic = true;")], False),
 ("mac guard not used in command 17", 'c', [("if (!rpi3_mon_mac_enter()) {\t\t/* mac_pta in use: nothing has changed, resubmit */", "if (0) {")], False),
 # ---- commands 18 to 21
 ("command 18 allows 6 entries", 'c', [("max_entries < 1 || max_entries > 5)\n\t\treturn RPI3_MON_R_BAD_PARAMETERS;\n\tif (out == NULL && *len > 0)\n\t\treturn RPI3_MON_R_BAD_PARAMETERS;\n\tif (!rpi3_mon_init_ok())\n\t\treturn RPI3_MON_R_BAD_STATE;\n\tif (*len < sizeof(struct rpi3_mon_event) * max_entries)", "max_entries < 1 || max_entries > 6)\n\t\treturn RPI3_MON_R_BAD_PARAMETERS;\n\tif (out == NULL && *len > 0)\n\t\treturn RPI3_MON_R_BAD_PARAMETERS;\n\tif (!rpi3_mon_init_ok())\n\t\treturn RPI3_MON_R_BAD_STATE;\n\tif (*len < sizeof(struct rpi3_mon_event) * max_entries)")], False),
 ("command 18 'more' flag missing", 'c', [("*oa = n | (total > n ? 0x100U : 0U);", "*oa = n;")], False),
 ("command 18 order broken", 'c', [("loc[j - 1].seq > t.seq", "loc[j - 1].seq < t.seq")], False),
 ("command 18 reports no ev_lost", 'c', [("\t*ob = lost;", "\t*ob = 0;")], False),
 ("command 18 copies to the caller before the unlock", 'c', [("\trpi3_mon_release(exc);\n\n\tif (n)\n\t\tmemcpy(out, loc, sizeof(struct rpi3_mon_event) * n);\t/* to the caller, after the unlock */", "\tif (n)\n\t\tmemcpy(out, loc, sizeof(struct rpi3_mon_event) * n);\n\trpi3_mon_release(exc);")], False),
 ("command 19 not clamped", 'c', [("\tif (ack_upto > l->ev_seq)\t\t\t/* clamp */\n\t\tack_upto = l->ev_seq;\n", "")], False),
 ("command 19 can move backwards", 'c', [("if (ack_upto > l->ev_ack)\t\t\t/* monotonic; never touches ev_lost or the overwrite victim */\n\t\tl->ev_ack = ack_upto;", "l->ev_ack = ack_upto;")], False),
 ("command 19 resets the lost counter", 'c', [("\t\tl->ev_ack = ack_upto;\n\teff", "\t\tl->ev_ack = ack_upto;\n\tl->ev_lost = 0;\n\teff")], False),
 ("command 20 accepts page 2", 'c', [("if (b != 0 || page >= 2)\n\t\treturn RPI3_MON_R_BAD_PARAMETERS;\n\tif (out == NULL && *len > 0)\n\t\treturn RPI3_MON_R_BAD_PARAMETERS;\n\tif (!rpi3_mon_init_ok())\n\t\treturn RPI3_MON_R_BAD_STATE;\n\tif (*len < 128)", "if (b != 0 || page >= 3)\n\t\treturn RPI3_MON_R_BAD_PARAMETERS;\n\tif (out == NULL && *len > 0)\n\t\treturn RPI3_MON_R_BAD_PARAMETERS;\n\tif (!rpi3_mon_init_ok())\n\t\treturn RPI3_MON_R_BAD_STATE;\n\tif (*len < 128)")], False),
 ("command 20 wrong length field", 'c', [("\tp.version = 1;\n\tp.length = 128;\n\tp.boot_id = boot_id;", "\tp.version = 1;\n\tp.length = 120;\n\tp.boot_id = boot_id;")], False),
 ("command 20 hardened flag inverted", 'c', [("(profile == RPI3_MON_PROFILE_HARDENED ? RPI3_MON_PFLAG_HARDENED : 0)", "(profile != RPI3_MON_PROFILE_HARDENED ? RPI3_MON_PFLAG_HARDENED : 0)")], False),
 ("command 20 pinned_used missing", 'c', [("\tp.pinned_used = l->pinned_used;\n", "")], False),
 ("command 20 lock-free fields read before the unlock", 'c', [("\trpi3_mon_release(exc);\n\n\tif (page == 0) {\t\t\t\t/* the six lock-free fields, read only after the unlock */", "\tif (page == 0) {\t\t\t\t/* the six lock-free fields, read only after the unlock */"), ("\tmemcpy(out, loc.w, 128);", "\trpi3_mon_release(exc);\n\tmemcpy(out, loc.w, 128);")], False),
 ("command 20 page 1 counters wrong", 'c', [("p.ev_count[i] = l->ev_count[i];", "p.ev_count[i] = 0;")], False),
 ("command 21 renewal is additive", 'c', [("l->idle_until_ticks = now + (uint64_t)req_ms * RPI3_MON_TICKS_PER_MS;", "l->idle_until_ticks = (active ? l->idle_until_ticks : now) + (uint64_t)req_ms * RPI3_MON_TICKS_PER_MS;")], False),
 ("command 21 request not clamped", 'c', [("req_ms = RPI3_MON_IDLE_MAX_MS;", "(void)0;")], False),
 ("command 21 cap not enforced", 'c', [("if (l->idle_uses >= RPI3_MON_IDLE_USES_MAX) {", "if (0) {")], False),
 ("command 21 return does not restart the window", 'c', [("\t\tl->tr_last_valid_ticks = now;\t\t/* the traffic window restarts (G5-4) */\n", "")], False),
 ("command 21 return consumes a use", 'c', [("\t\tv = RPI3_MON_V21_RETURNED;", "\t\tl->idle_uses++;\n\t\tv = RPI3_MON_V21_RETURNED;")], False),
 ("command 21 grant clears a raised timeout", 'c', [("\t\t\tl->idle_uses++;", "\t\t\tl->idle_uses++;\n\t\t\tl->tr_timeout_raised = false;")], False),
 ("command 21 NO_CHANGE for a lease it just expired", 'c', [("active = l->idle_until_ticks && now <= l->idle_until_ticks;\n\tif (mode == 1) {", "active = true;\n\tif (mode == 1) {")], False),
 # ---- command 22 and 23
 ("command 22 returns no nonce in MISSED", 'c', [("if (st != RPI3_MON_HB_OPEN && st != RPI3_MON_HB_MISSED) {", "if (st != RPI3_MON_HB_OPEN) {")], False),
 ("command 22 always recomputes", 'c', [("\tif (!valid) {\n\t\tif (!rpi3_mon_mac_enter())", "\tif (1) {\n\t\tif (!rpi3_mon_mac_enter())")], False),
 ("command 22 stores without the sequence check", 'c', [("store_ok = l->hb_seq == seq && (l->hb_state == RPI3_MON_HB_OPEN || l->hb_state == RPI3_MON_HB_MISSED);", "store_ok = true;")], False),
 ("command 22 input layout: boot_id missing", 'c', [("rpi3_mon_put_le32(in, rpi3_mon.boot_id);", "rpi3_mon_put_le32(in, 0);")], False),
 ("command 22 nonce words swapped", 'c', [("nonce = (uint64_t)rpi3_mon_le32(dig) | ((uint64_t)rpi3_mon_le32(dig + 4) << 32);", "nonce = (uint64_t)rpi3_mon_le32(dig + 4) | ((uint64_t)rpi3_mon_le32(dig) << 32);")], False),
 ("command 22 mac guard not used", 'c', [("if (!rpi3_mon_mac_enter()) {\t\t/* mac_pta in use: nothing changed, retry */", "if (0) {")], False),
 ("command 23 compares nothing", 'c', [("!l->hb_nonce_valid || l->hb_nonce != given", "!l->hb_nonce_valid")], False),
 ("command 23 accepts a never-fetched nonce", 'c', [("!l->hb_nonce_valid || l->hb_nonce != given", "l->hb_nonce != given")], False),
 ("command 23 duplicate answer accepted again", 'c', [("} else if (l->hb_state == RPI3_MON_HB_ANSWERED || l->hb_state == RPI3_MON_HB_LATE_ANSWERED) {", "} else if (0) {")], False),
 ("command 23 stale answer accepted", 'c', [("} else if (seq < l->hb_seq) {", "} else if (0) {")], False),
 ("command 23 never-issued answer accepted", 'c', [("if (l->hb_state == RPI3_MON_HB_NONE || seq > l->hb_seq) {", "if (0) {")], False),
 ("command 23 on-time answer keeps the miss count", 'c', [("\t\tl->hb_last_ok_ticks = now;\n\t\tl->hb_miss_count = 0;\n", "\t\tl->hb_last_ok_ticks = now;\n")], False),
 ("command 23 on-time answer does not clear the raised timeout", 'c', [("if (l->hb_timeout_raised) {\n\t\t\tl->hb_timeout_raised = false;\n\t\t\trpi3_mon_ev_append(l, RPI3_MON_EV_HB_RECOVERED, seq, 0, now);\n\t\t}", "(void)0;")], False),
 ("command 23 late answer resets the miss count", 'c', [("\t\tl->hb_state = RPI3_MON_HB_LATE_ANSWERED;\n\t\trpi3_mon_ev_append(l, RPI3_MON_EV_LATE_ANSWER", "\t\tl->hb_miss_count = 0;\n\t\tl->hb_state = RPI3_MON_HB_LATE_ANSWERED;\n\t\trpi3_mon_ev_append(l, RPI3_MON_EV_LATE_ANSWER")], False),
 ("command 23 bad nonce changes state", 'c', [("\t\tv = RPI3_MON_V23_BAD_NONCE;", "\t\tl->hb_state = RPI3_MON_HB_ANSWERED;\n\t\tv = RPI3_MON_V23_BAD_NONCE;")], False),
 # ---- command 23 event types (added with the interface-check mapping)
 ("command 23 stale answer logs the wrong event", 'c', [("rpi3_mon_ev_append(l, RPI3_MON_EV_STALE_ANSWER, seq, 0, now);", "rpi3_mon_ev_append(l, RPI3_MON_EV_UNISSUED_ANSWER, seq, 0, now);")], False),
 ("command 23 unissued answer logs the wrong event", 'c', [("rpi3_mon_ev_append(l, RPI3_MON_EV_UNISSUED_ANSWER, seq, 0, now);", "rpi3_mon_ev_append(l, RPI3_MON_EV_STALE_ANSWER, seq, 0, now);")], False),
 ("command 23 duplicate answer logs the wrong event", 'c', [("rpi3_mon_ev_append(l, RPI3_MON_EV_DUP_ANSWER, seq, 0, now);", "rpi3_mon_ev_append(l, RPI3_MON_EV_STALE_ANSWER, seq, 0, now);")], False),
 ("command 23 bad nonce logs no event", 'c', [("rpi3_mon_ev_append(l, RPI3_MON_EV_BAD_NONCE, seq, detail, now);", "(void)detail;")], False),
 # ---- legacy event and Core 1
 ("legacy event type swapped", 'c', [("refused ? RPI3_MON_EV_LEGACY_REFUSED : RPI3_MON_EV_LEGACY_USED", "refused ? RPI3_MON_EV_LEGACY_USED : RPI3_MON_EV_LEGACY_REFUSED")], False),
 ("legacy event path spins for the lock", 'c', [("if (MON_PLAT_TRYLOCK(&rpi3_mon_lock)) {\t\t\t/* ONE attempt;", "while (!MON_PLAT_TRYLOCK(&rpi3_mon_lock)) { }\n\tif (1) {\t\t\t/* ONE attempt;")], False),
 ("Core 1 starvation threshold 1", 'c', [("if (sk >= RPI3_MON_K_STARVE)\n\t\t\trpi3_mon_store_u32(&rpi3_mon.health.starved, 1U);", "if (sk >= 1)\n\t\t\trpi3_mon_store_u32(&rpi3_mon.health.starved, 1U);")], False),
 ("Core 1 resets skip_total", 'c', [("\trpi3_mon_store_u32(&rpi3_mon.health.starved, 0U);\n\tl->last_eval_ticks", "\trpi3_mon_store_u32(&rpi3_mon.health.starved, 0U);\n\trpi3_mon_store_u32(&rpi3_mon.health.skip_total, 0U);\n\tl->last_eval_ticks")], False),
 ("Core 1 logs MON_STARVED below K", 'c', [("if (sk >= RPI3_MON_K_STARVE)\t\t\t\t\t/* one MON_STARVED event per episode that reached K */", "if (sk >= 1)\t\t\t\t\t/* one MON_STARVED event per episode that reached K */")], False),
 ("Core 1 timeout delta not relative", 'c', [("delta = count - l->pta_timeouts_seen;", "delta = count;")], False),
 ("Core 1 never records what it observed", 'c', [("\t\tl->pta_timeouts_seen = count;\n", "")], False),
 ("Core 1 failure path writes protected state", 'c', [("\t\tif (sk >= RPI3_MON_K_STARVE)\n\t\t\trpi3_mon_store_u32(&rpi3_mon.health.starved, 1U);\n\t\treturn;", "\t\tif (sk >= RPI3_MON_K_STARVE)\n\t\t\trpi3_mon_store_u32(&rpi3_mon.health.starved, 1U);\n\t\trpi3_mon.l.ev_lost++;\n\t\treturn;")], False),
 ("Core 1 does not record last_eval_ticks", 'c', [("\tl->last_eval_ticks = now;\n", "")], False),
 ("Core 1 never issues a challenge", 'c', [("\trpi3_mon_hb_issue_if_due(l, now);\n\n\tcount", "\tcount")], False),
 ("Core 1 entry check inverted", 'c', [("if (!MON_PLAT_EXC_FIQ_IRQ_MASKED())", "if (MON_PLAT_EXC_FIQ_IRQ_MASKED())")], False),
 ("Core 1 cadence 8192", 'h', [("#define RPI3_MON_CORE1_CADENCE\t\t16384U", "#define RPI3_MON_CORE1_CADENCE\t\t8192U")], False),
 ("Core 1 pass counts twice", 'h', [("rpi3_mon_load_u64(&rpi3_mon.health.poll_count) + 1);", "rpi3_mon_load_u64(&rpi3_mon.health.poll_count) + 2);")], False),
 ("Core 1 attempts on every pass", 'h', [("if ((pass & RPI3_MON_CORE1_MASK()) == 0) {", "if (1) {")], False),
 # ---- header constants and layout
 ("G7 struct loses its 64-byte alignment", 'h', [("uint32_t reserved[9];\n} __aligned(64);", "uint32_t reserved[9];\n};")], False),
 ("timeout counter shares a line with G7", 'h', [("struct { uint32_t pta_lock_timeouts; uint8_t pad[60]; } timeouts __aligned(64);", "struct { uint32_t pta_lock_timeouts; uint8_t pad[28]; } timeouts;")], False),
 ("lock shares a cache line", 'h', [("struct { unsigned int lock; uint8_t pad[60]; } lockline __aligned(64);", "struct { unsigned int lock; uint8_t pad[12]; } lockline;")], False),
 ("D_req not below H", 'h', [("#define RPI3_MON_D_REQ_MS\t\t500ULL", "#define RPI3_MON_D_REQ_MS\t\t1500ULL")], False),
 ("traffic timeout below period + jitter", 'h', [("#define RPI3_MON_T_TRAFFIC_MS\t\t300ULL", "#define RPI3_MON_T_TRAFFIC_MS\t\t50ULL")], False),
 ("lock budget not 1 ms", 'h', [("#define RPI3_MON_BUDGET_TICKS\t\t19200ULL", "#define RPI3_MON_BUDGET_TICKS\t\t38400ULL")], False),
 ("event record grows to 32 bytes", 'h', [("uint64_t ticks; uint32_t counter; uint32_t detail;\n};", "uint64_t ticks; uint32_t counter; uint32_t detail; uint64_t extra;\n};")], False),
 ("event type count changes", 'h', [("RPI3_MON_EV_LATE_ANSWER, RPI3_MON_EV_COUNT", "RPI3_MON_EV_LATE_ANSWER, RPI3_MON_EV_EXTRA, RPI3_MON_EV_COUNT")], False),
 ("page 0 field offset moves", 'h', [("\tuint64_t core1_poll_age_ticks;\t\t\t/* 0x18 lock-free */", "\tuint32_t shifted;\n\tuint64_t core1_poll_age_ticks;\t\t\t/* 0x18 lock-free */")], False),
 ("page 1 layout moves", 'h', [("\tuint32_t v_frame_total, v_hb_total, v_xcheck_total;\t/* 0x6C 0x70 0x74 */", "\tuint32_t v_hb_total, v_frame_total, v_xcheck_total, extra;\t/* 0x6C 0x70 0x74 */")], False),
 ("pinned quota 5 per class", 'h', [("#define RPI3_MON_PINNED_PER_CLASS\t4U", "#define RPI3_MON_PINNED_PER_CLASS\t5U")], False),
 ("starvation threshold 1", 'h', [("#define RPI3_MON_K_STARVE\t\t8U", "#define RPI3_MON_K_STARVE\t\t1U")], False),
 ("idle use cap 64", 'h', [("#define RPI3_MON_IDLE_USES_MAX\t\t16U", "#define RPI3_MON_IDLE_USES_MAX\t\t64U")], False),
 ("heartbeat grace shortened", 'h', [("#define RPI3_MON_G_MS\t\t\t60000ULL", "#define RPI3_MON_G_MS\t\t\t1ULL")], False),
 # ---- PTA glue
 ("known set extended to 24", 'g', [("if (cmd_id > PTA_RPI3_CAN_SPI_POC_CMD_MON_SUBMIT_HB_ANSWER)", "if (cmd_id > 24)")], False),
 ("WRITE_REG32 no longer refused", 'g', [("\tcase PTA_RPI3_CAN_SPI_POC_CMD_WRITE_REG32:\n\t\trpi3_mon_note_legacy(cmd_id, true);", "\t\trpi3_mon_note_legacy(cmd_id, true);")], False),
 ("hardened profile allows the debug readers", 'g', [("\t\tif (MON_PLAT_PROFILE_DEBUG) {", "\t\tif (1) {")], False),
 ("commands 12 refused", 'g', [("\tcase PTA_RPI3_CAN_SPI_POC_CMD_WAKE_CORE1:\n", "\tcase PTA_RPI3_CAN_SPI_POC_CMD_WAKE_CORE1:\n\tcase 12:\n")], False),
 ("health pair never written", 'g', [("\trpi3_mon_put_health(params, &h);", "\t(void)h;", -1)], False),
 ("command 21 wrong parameter types", 'g', [("static TEE_Result rpi3_mon_pta_cmd21(uint32_t pt, TEE_Param params[TEE_NUM_PARAMS])\n{\n\tstruct rpi3_mon_health_out h;\n\tuint32_t oa = 0;\n\tuint32_t ob = 0;\n\tenum rpi3_mon_res r;\n\n\tif (pt != PT(VALUE_INOUT, NONE, VALUE_OUTPUT, VALUE_OUTPUT))", "static TEE_Result rpi3_mon_pta_cmd21(uint32_t pt, TEE_Param params[TEE_NUM_PARAMS])\n{\n\tstruct rpi3_mon_health_out h;\n\tuint32_t oa = 0;\n\tuint32_t ob = 0;\n\tenum rpi3_mon_res r;\n\n\tif (pt != PT(VALUE_INPUT, NONE, VALUE_OUTPUT, VALUE_OUTPUT))")], False),
 ("BUSY values not written", 'g', [("return r == RPI3_MON_R_OK || r == RPI3_MON_R_BUSY;", "return r == RPI3_MON_R_OK;")], False),
 ("short-buffer size not returned", 'g', [("if (rpi3_mon_has_values(r) || r == RPI3_MON_R_SHORT_BUFFER)", "if (rpi3_mon_has_values(r))", -1)], False),
 ("legacy refusal logs nothing", 'g', [("\t\trpi3_mon_note_legacy(cmd_id, true);\n\t\treturn TEE_ERROR_NOT_SUPPORTED;\n\n\t/* chip A", "\t\treturn TEE_ERROR_NOT_SUPPORTED;\n\n\t/* chip A")], False),
 ("BUSY mapped to a different error", 'g', [("return TEE_ERROR_BUSY;", "return TEE_ERROR_BAD_STATE;")], False),
]
def apply(spec):
    name, f, subs, equiv = spec
    texts = dict(SRC)
    for sub in subs:
        a, b = sub[0], sub[1]; cnt = sub[2] if len(sub) > 2 else 1
        n = texts[f].count(a)
        if (cnt == 1 and n != 1) or (cnt == -1 and n < 1): return None, "anchor count %d" % n
        texts[f] = texts[f].replace(a, b)
    return texts, None
def run(spec):
    name, f, subs, equiv = spec
    texts, err = apply(spec)
    if err: return (name, "SKIP (" + err + ")", equiv)
    d = tempfile.mkdtemp(prefix='mut_')
    try:
        open(d + '/rpi3_can_monitor.c', 'w').write(texts['c']); open(d + '/rpi3_can_monitor.h', 'w').write(texts['h'])
        shutil.copy(PLAT + '/rpi3_can_monitor_keys.h', d); open(d + '/glue.c', 'w').write(texts['g'])
        out = d + '/harness'
        r = subprocess.run(['gcc', '-std=gnu11', '-w', '-O1', '-g', '-fsanitize=address,undefined', '-pthread', '-I', HERE + '/mock', '-I', d, '-I', OS + '/lib/libutee/include', '-I', HERE,
                            HERE + '/harness.c', HERE + '/mock_platform.c', d + '/glue.c', '-o', out], capture_output=True, text=True)
        if r.returncode != 0:
            return (name, "caught (compile-time static assertion)" if 'static assertion' in r.stderr else "COMPILE ERROR: " + r.stderr[:100].replace("\n", " "), equiv)
        try:
            p = subprocess.run([out], capture_output=True, text=True, timeout=120)
            return (name, "caught (failed checks)" if p.returncode else "SURVIVED", equiv)
        except subprocess.TimeoutExpired: return (name, "caught (hang)", equiv)
    finally: shutil.rmtree(d, ignore_errors=True)
if __name__ == '__main__':
    jobs = 4; flt = None; a = sys.argv[1:]
    while a:
        if a[0] == '-j': jobs = int(a[1]); a = a[2:]
        else: flt = a[0]; a = a[1:]
    sel = [m for m in M if not flt or flt in m[0]]
    with cf.ThreadPoolExecutor(jobs) as ex: res = list(ex.map(run, sel))
    caught = 0; bad = []
    for name, r, equiv in res:
        if r.startswith("caught"): caught += 1
        elif r == "SURVIVED": bad.append((name, equiv))
        print("%-40s %s%s" % (r, name, "  [expected equivalent]" if equiv and r == "SURVIVED" else ""))
    print("caught %d of %d; survivors: %s" % (caught, len(sel), [n for n, e in bad]))
    sys.exit(1 if any(not e for n, e in bad) or any(r.startswith(("SKIP", "COMPILE")) for n, r, e in res) else 0)
