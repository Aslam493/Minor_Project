#!/usr/bin/env python3
"""Mutation test of the Mode P code (CFG_RPI3_CAN_MONITOR_PULL): plant one bug at a time in a temp copy, rebuild harness_pull, and require a
failed check, a crash or a build failure that is not a missing pattern. usage: mutation_pull.py [-j N]. Touches nothing in the OP-TEE tree."""
import os, shutil, subprocess, sys, tempfile, concurrent.futures as cf
HERE = os.path.dirname(os.path.abspath(__file__))
OS = os.path.abspath(os.path.join(HERE, '../../../optee_full/optee_os')); PL = OS + '/core/arch/arm/plat-rpi3'; GL = OS + '/core/pta/rpi3_can_monitor_pta.c'
C = open(PL + '/rpi3_can_monitor.c').read(); G = open(GL).read()
M = [  # (name, target 'c' or 'g', old, new)
 ("ON_TIME reply does not reset the miss count", 'c', "		l->pq_miss_count = 0;\n		if (l->pq_timeout_raised) {", "		if (l->pq_timeout_raised) {"),
 ("UNISSUED test is seq >= current", 'c', "seq > l->pq_seq) {\n		v = RPI3_MON_V31_UNISSUED", "seq >= l->pq_seq) {\n		v = RPI3_MON_V31_UNISSUED"),
 ("STALE test is seq <= current", 'c', "} else if (seq < l->pq_seq) {\n		v = RPI3_MON_V31_STALE", "} else if (seq <= l->pq_seq) {\n		v = RPI3_MON_V31_STALE"),
 ("a LATE_ANSWERED request is not a duplicate", 'c', "l->pq_state == RPI3_MON_PQ_ANSWERED || l->pq_state == RPI3_MON_PQ_LATE_ANSWERED", "l->pq_state == RPI3_MON_PQ_ANSWERED"),
 ("request issue schedule can burst (next += P)", 'c', "	l->pq_next_issue = now + RPI3_MON_PQ_PERIOD_TICKS;", "	l->pq_next_issue += RPI3_MON_PQ_PERIOD_TICKS;"),
 ("request number can wrap", 'c', "	if (l->pq_seq == 0xFFFFFFFFU)			/* never wraps */\n		return;\n", ""),
 ("packets are judged even for a stale or duplicate request", 'c', "if (reqv != RPI3_MON_V31_ON_TIME && reqv != RPI3_MON_V31_LATE) {", "if (0) {"),
 ("BUSY on the first hold is not reported (nothing returned)", 'c', "			if (i == 0) {\n				rpi3_mon_health_fill(h);\n				return RPI3_MON_R_BUSY;	/* nothing has changed */\n			}\n			break;", "			break;"),
 ("the batch goes on after a BUSY on a later packet", 'c', "			break;\n		}\n		now = MON_PLAT_TIMER();\n		l = rpi3_mon_l();\n		rpi3_mon_evaluate(l, now);\n		if (i == 0) {\n			reqv", "			continue;\n		}\n		now = MON_PLAT_TIMER();\n		l = rpi3_mon_l();\n		rpi3_mon_evaluate(l, now);\n		if (i == 0) {\n			reqv"),
 ("processed count is wrong (+2)", 'c', "			done++;", "			done += 2;"),
 ("batch limit is 33", 'c', "n > RPI3_MON_PQ_MAX_BATCH ||", "n > RPI3_MON_PQ_MAX_BATCH + 1 ||"),
 ("the buffer length is not checked against n", 'c', "len != (size_t)n * RPI3_MON_FRAME_LEN || ", ""),
 ("a missed request is counted at any time after issue", 'c', "rpi3_mon_expired(now, l->pq_issue_ticks, RPI3_MON_PQ_DREQ_TICKS)", "now >= l->pq_issue_ticks"),
 ("packet-request TIMEOUT has detail 2", 'c', "rpi3_mon_ev_append(l, RPI3_MON_EV_TIMEOUT, 0, 4, now);", "rpi3_mon_ev_append(l, RPI3_MON_EV_TIMEOUT, 0, 2, now);"),
 ("packet-request TIMEOUT is raised again and again", 'c', "		l->pq_timeout_raised = true;\n", ""),
 ("a LATE reply repairs the miss count", 'c', "		l->pq_state = RPI3_MON_PQ_LATE_ANSWERED;\n", "		l->pq_state = RPI3_MON_PQ_LATE_ANSWERED;\n		l->pq_miss_count = 0;\n"),
 ("ON_TIME reply does not record the time", 'c', "		l->pq_last_ok_ticks = now;\n", ""),
 ("no recovery event", 'c', "			rpi3_mon_ev_append(l, RPI3_MON_EV_HB_RECOVERED, seq, 1, now);\n", ""),
 ("an empty reply does not answer the request", 'c', "uint32_t rounds = n ? n : 1U;", "uint32_t rounds = n;"),
 ("tag compared over 31 bytes (shared prepare step)", 'c', "if (rpi3_mon_ct_equal(tag, buf + 12, 32))\n			*authentic", "if (rpi3_mon_ct_equal(tag, buf + 12, 31))\n			*authentic"),
 ("command 30 does not evaluate the deadline", 'c', "	rpi3_mon_evaluate(l, MON_PLAT_TIMER());\n	*seq = l->pq_seq;", "	*seq = l->pq_seq;"),
 ("status page does not say Mode P", 'c', "	rpi3_mon.l.mode = RPI3_MON_MODE_P;		/* Mode P (pull)", "	rpi3_mon.l.mode = RPI3_MON_MODE_F;		/* Mode P (pull)"),
 ("Core 1 never issues packet requests", 'c', "#ifdef CFG_RPI3_CAN_MONITOR_PULL\n	rpi3_mon_pq_issue_if_due(l, now);\n#endif\n", ""),
 ("answer marks the request answered before checking it (a stale reply answers the open one)", 'c', "	if (l->pq_state == RPI3_MON_PQ_NONE || seq > l->pq_seq) {", "	if (seq == l->pq_seq && l->pq_state == RPI3_MON_PQ_OPEN) {\n		l->pq_state = RPI3_MON_PQ_ANSWERED;\n	}\n	if (l->pq_state == RPI3_MON_PQ_NONE || seq > l->pq_seq) {"),
 ("glue: command 17 is not refused in Mode P", 'g', "	if (cmd_id == PTA_RPI3_CAN_SPI_POC_CMD_MON_SUBMIT_FRAME)\n		return TEE_ERROR_NOT_SUPPORTED;\n", ""),
 ("glue: command 31 accepts MEMREF_OUTPUT", 'g', "if (pt != PT(VALUE_INOUT, MEMREF_INPUT, VALUE_OUTPUT, VALUE_OUTPUT))\n		return TEE_ERROR_BAD_PARAMETERS;\n	r = rpi3_mon_cmd31", "if (pt != PT(VALUE_INOUT, MEMREF_OUTPUT, VALUE_OUTPUT, VALUE_OUTPUT))\n		return TEE_ERROR_BAD_PARAMETERS;\n	r = rpi3_mon_cmd31"),
 ("glue: command 30 does not copy the batch size to the caller", 'g', "		params[1].value.a = mb;\n", ""),
 ("P6 bucket width is 4 ms", 'c', "uint64_t k = el / RPI3_MON_PQ_BUCKET_TICKS;", "uint64_t k = el / (2 * RPI3_MON_PQ_BUCKET_TICKS);"),
 ("P6 no clamp to the last bucket", 'c', "	if (k >= RPI3_MON_PQ_BUCKETS)\n		k = RPI3_MON_PQ_BUCKETS - 1U;\n", ""),
 ("P6 clamp is off by one", 'c', "	if (k >= RPI3_MON_PQ_BUCKETS)\n		k = RPI3_MON_PQ_BUCKETS - 1U;", "	if (k > RPI3_MON_PQ_BUCKETS)\n		k = RPI3_MON_PQ_BUCKETS - 1U;"),
 ("P6 a LATE answer is counted as ON_TIME", 'c', "		l->pq_n_late++;", "		l->pq_n_ontime++;"),
 ("P6 the bucket is not counted", 'c', "	l->pq_bucket[k]++;\n", ""),
 ("P6 max is never updated", 'c', "	if (el > l->pq_max_lat)\n		l->pq_max_lat = el;\n", ""),
 ("P6 max keeps the smallest", 'c', "if (el > l->pq_max_lat)", "if (el < l->pq_max_lat)"),
 ("P6 last is never updated", 'c', "	l->pq_last_lat = el > 0xFFFFFFFFULL ? 0xFFFFFFFFU : (uint32_t)el;\n", ""),
 ("P6 time measured from the last good answer, not the issue", 'c', "now >= l->pq_issue_ticks ? now - l->pq_issue_ticks : 0;", "now >= l->pq_last_ok_ticks ? now - l->pq_last_ok_ticks : 0;"),
 ("P6 an ON_TIME answer is not timed", 'c', "		rpi3_mon_pq_note_latency(l, now, false);\n", ""),
 ("P6 a LATE answer is not timed", 'c', "		rpi3_mon_pq_note_latency(l, now, true);\n", ""),
 ("P6 page 3 is accepted", 'c', "page >= 3", "page >= 4"),
 ("P6 page 2 returns page 1", 'c', "		rpi3_mon_build_proj2((volatile uint64_t *)loc.w, l);", "		rpi3_mon_build_proj1((volatile uint64_t *)loc.w, l);"),
 ("P6 last bucket is not copied to the page", 'c', "	for (i = 0; i < RPI3_MON_PQ_BUCKETS; i++)\n		p.bucket[i] = l->pq_bucket[i];", "	for (i = 0; i < RPI3_MON_PQ_BUCKETS - 1U; i++)\n		p.bucket[i] = l->pq_bucket[i];"),
 ("P6 reserved word is not zero", 'c', "	p.rsv = 0;\n	p.max_ticks", "	p.rsv = 1;\n	p.max_ticks"),
 ("P6 page has the wrong length field", 'c', "	p.version = 1;\n	p.length = 128;\n	p.n_ontime", "	p.version = 1;\n	p.length = 127;\n	p.n_ontime")
]
def run(k):
    name, tgt, old, new = M[k]; src = C if tgt == 'c' else G
    if src.count(old) < 1: return (name, "SKIP (pattern not found)")
    d = tempfile.mkdtemp(prefix='mutpull_'); pl = d + '/plat'; shutil.copytree(PL, pl); gl = d + '/glue.c'
    open(pl + '/rpi3_can_monitor.c', 'w').write(src.replace(old, new, 1) if tgt == 'c' else C); open(gl, 'w').write(src.replace(old, new, 1) if tgt == 'g' else G)
    exe = d + '/h'
    cc = ["cc", "-std=gnu11", "-Wall", "-Wextra", "-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-pthread", "-DCFG_RPI3_CAN_MONITOR_PULL",
          "-I", HERE + "/mock", "-I", pl, "-I", OS + "/lib/libutee/include", "-I", HERE, "-Wno-misleading-indentation", "-Wno-unused-value", HERE + "/harness.c", HERE + "/mock_platform.c", gl, "-o", exe]
    r = subprocess.run(cc, capture_output=True, text=True)
    if r.returncode:
        shutil.rmtree(d, ignore_errors=True); return (name, "caught (build failed: %s)" % r.stderr.strip().splitlines()[-1][:80] if r.stderr.strip() else "caught (build failed)")
    try: p = subprocess.run([exe], capture_output=True, text=True, timeout=120); out = p.stdout; rc = p.returncode
    except subprocess.TimeoutExpired: shutil.rmtree(d, ignore_errors=True); return (name, "caught (hang)")
    shutil.rmtree(d, ignore_errors=True)
    failed = [l for l in out.splitlines() if l.startswith("FAIL")]
    if rc != 0 or failed: return (name, "caught (%s)" % ("crash/exit %d" % rc if rc and not failed else "failed tests: " + "; ".join(x[5:30] for x in failed[:3])))
    return (name, "SURVIVED")
if __name__ == '__main__':
    j = int(sys.argv[2]) if len(sys.argv) > 2 and sys.argv[1] == '-j' else 3
    with cf.ThreadPoolExecutor(j) as ex: res = list(ex.map(run, range(len(M))))
    for n, r in res: print("%-85s %s" % (r.split(' (')[0] if r.startswith('caught') else r, n) if False else "%-12s %s   [%s]" % (r.split(' (')[0], n, r))
    caught = sum(1 for _, r in res if r.startswith('caught')); skipped = sum(1 for _, r in res if r.startswith('SKIP')); sv = [n for n, r in res if r == 'SURVIVED']
    print("caught %d of %d; skipped %d; survivors: %s" % (caught, len(M) - skipped, skipped, sv))
    sys.exit(1 if sv or skipped else 0)
