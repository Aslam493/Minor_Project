import subprocess,shutil,os,sys
D=__import__('os').path.dirname(__import__('os').path.abspath(__file__))
S=__import__('tempfile').mkdtemp()
src=open(D+'/mon_model.c').read()
muts=[
("equivalent mutant: unsigned wrap already fails closed (expected to survive)","if (now < t0 || w->wait_ticks_used","if (w->wait_ticks_used"),
("faithful OLD R-A2 test (now>=t0 && elapsed>budget)","if (now < t0 || w->wait_ticks_used + (now - t0) > MON_BUDGET_TICKS || w->attempts_used >= MON_ATTEMPT_CAP) break;","if ((now >= t0 && w->wait_ticks_used + (now - t0) > MON_BUDGET_TICKS) || w->attempts_used >= MON_ATTEMPT_CAP) break;"),
("no attempt cap in the loop"," || w->attempts_used >= MON_ATTEMPT_CAP) break;",") break;"),
("budget is fresh per acquisition","w->wait_ticks_used + (now - t0) > MON_BUDGET_TICKS","(now - t0) > MON_BUDGET_TICKS"),
("increment counter on success too","*exc = e; return MON_OK;","MON_ATOMIC_INC32(&m->pta_lock_timeouts); *exc = e; return MON_OK;"),
("unmask before unlock","void mon_release(struct mon *m, uint32_t exc) { MON_UNLOCK(&m->lock); MON_UNMASK(exc); }","void mon_release(struct mon *m, uint32_t exc) { MON_UNMASK(exc); MON_UNLOCK(&m->lock); }"),
("timeout event not guarded (fires every evaluation)","&& !l->tr_timeout_raised &&\n\t    expired","&&\n\t    expired"),
("late answer resets miss count (old path)","else if (l->hb_state == HB_MISSED) l->hb_state = HB_LATE_ANSWERED;","else if (l->hb_state == HB_MISSED) { l->hb_state = HB_LATE_ANSWERED; l->hb_miss = 0; }"),
("skip_total reset on success","MON_STORE32(&m->h.skip_count, 0); MON_STORE32(&m->h.starved, 0);","MON_STORE32(&m->h.skip_count, 0); MON_STORE32(&m->h.starved, 0); MON_STORE32(&m->h.skip_total, 0);"),
("STARVED event below K","if (sk >= c->K) mon_ev_append(m, EV_STARVED","if (sk >= 1) mon_ev_append(m, EV_STARVED"),
("Core 1 reads protected state before locking","if (!MON_TRYLOCK(&m->lock)) {                          /* one attempt, never spins; touches only G7 */","if (!MON_TRYLOCK(&m->lock)) { (void)mon_l(m)->ev_seq;"),
("Core 1 failed attempt writes a protected member","if (n >= c->K) MON_STORE32(&m->h.starved, 1);","if (n >= c->K) MON_STORE32(&m->h.starved, 1); m->l.ev_lost++;"),
("cmd 18 allows 16 records","if (maxn < 1 || maxn > 5) return","if (maxn < 1 || maxn > 16) return"),
("pinned entries can be overwritten","if (type < NEV_EVID && l->pinned_n[type] < 4) {","if (type < NEV_EVID && l->pinned_n[type] < 4 && l->ev_seq < 8) {"),
("overflow not counted","if (l->ord_used[i]) { l->ev_lost++; e.flags |= 1; }","if (l->ord_used[i]) { e.flags |= 1; }"),
("rate limit shares ONE bucket across classes","if (l->diag_have[type] && now >= l->diag_last[type] && now - l->diag_last[type] < MON_TICKS_PER_SEC) return;\n\tl->diag_have[type] = true; l->diag_last[type] = now;","if (l->diag_have[0] && now >= l->diag_last[0] && now - l->diag_last[0] < MON_TICKS_PER_SEC) return;\n\tl->diag_have[0] = true; l->diag_last[0] = now;"),
("rate limit never applied","if (l->diag_have[type] && now >= l->diag_last[type] && now - l->diag_last[type] < MON_TICKS_PER_SEC) return;","if (0) return;"),
("rate limit off by one at the second boundary","now - l->diag_last[type] < MON_TICKS_PER_SEC) return;","now - l->diag_last[type] <= MON_TICKS_PER_SEC) return;"),
("suppressed events not counted exactly","l->ev_count[type]++;                                   /* exact counter, even when suppressed */","if (type < NEV_EVID) l->ev_count[type]++;"),
("evidence classes never limited after their quota (flood rule reverted)","if (l->diag_have[type] && now >= l->diag_last[type] && now - l->diag_last[type] < MON_TICKS_PER_SEC) return;","if (type >= NEV_EVID && l->diag_have[type] && now >= l->diag_last[type] && now - l->diag_last[type] < MON_TICKS_PER_SEC) return;"),
("evidence limit also applies inside the pinned quota","e.seq = 0; e.type = (uint16_t)type; e.flags = 0; e.ticks = now; e.counter = counter; e.detail = detail;\n\tif (type < NEV_EVID && l->pinned_n[type] < 4) {","e.seq = 0; e.type = (uint16_t)type; e.flags = 0; e.ticks = now; e.counter = counter; e.detail = detail;\n\tif (type < NEV_EVID && l->pinned_n[type] < 4 && !(l->diag_have[type] && now - l->diag_last[type] < MON_TICKS_PER_SEC)) {"),
("cmd 20 page 2 accepted","if (b != 0 || page >= 2) return MON_BAD_PARAMETERS;","if (b != 0 || page >= 3) return MON_BAD_PARAMETERS;"),
("cmd 20 reports the wrong page 1 length","p1.version = 1; p1.length = 128;","p1.version = 1; p1.length = 96;"),
("cmd 20 checks buffer before init","if (b != 0 || page >= 2) return MON_BAD_PARAMETERS;\n\tif (!m->init_ok) return MON_BAD_STATE;","if (b != 0 || page >= 2) return MON_BAD_PARAMETERS;\n\tif (outsz < 128) { *outlen = 128; return MON_SHORT_BUFFER; }\n\tif (!m->init_ok) return MON_BAD_STATE;"),
("BUSY reply leaks stale size","*oa = 0; *ob = 0; *outlen = 0; fill_health(m, h); return MON_BUSY; }\n\tnow = MON_TIMER();\n\tmon_evaluate(m, now);\n\tl = mon_l(m);\n\tif (page == 1)","*oa = 0; *ob = 0; *outlen = 128; fill_health(m, h); return MON_BUSY; }\n\tnow = MON_TIMER();\n\tmon_evaluate(m, now);\n\tl = mon_l(m);\n\tif (page == 1)"),
("health read before unlock","\tmon_release(m, exc);\n\t/* lock-free fields, read only after unlocking, through the accessors */\n\tlp =","\tfill_health(m, h);\n\tmon_release(m, exc);\n\t/* lock-free fields, read only after unlocking, through the accessors */\n\tlp ="),
("mac guard never blocks","return __atomic_compare_exchange_n(&m->mac_pta_busy, &exp, 1u, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED) ? MON_OK : MON_BUSY;","return MON_OK;"),
("ack changes the lost counter","if (ack_upto > l->ev_ack) l->ev_ack = ack_upto;","if (ack_upto > l->ev_ack) { l->ev_ack = ack_upto; l->ev_lost = 0; }"),
("ack is not clamped","if (ack_upto > l->ev_seq) ack_upto = l->ev_seq;","(void)0;"),
("ack can move backwards","if (ack_upto > l->ev_ack) l->ev_ack","if (1) l->ev_ack"),
("lease renewal is additive","l->idle_until = now + dur;","l->idle_until = (active ? l->idle_until : now) + dur;"),
("returning to EXPECTED consumes a use","l->idle_until = 0; l->tr_last_valid = now; v = V21_RETURNED;","l->idle_until = 0; l->tr_last_valid = now; l->idle_uses++; v = V21_RETURNED;"),
("lease is not clamped to IDLE_MAX","if (req_ms > MON_IDLE_MAX_MS) req_ms = MON_IDLE_MAX_MS;","(void)0;"),
("lease cap not enforced","if (l->idle_uses >= MON_IDLE_USES_MAX) {","if (0) {"),
("grant clears a raised timeout","l->idle_until = now + dur;                        /* replaces the old expiry, never additive */","l->idle_until = now + dur; l->tr_timeout_raised = false;"),
("return to EXPECTED does not restart the window","l->idle_until = 0; l->tr_last_valid = now; v = V21_RETURNED;","l->idle_until = 0; v = V21_RETURNED;"),
("cmd 23 late answer resets the miss count","else { v = V23_LATE; l->hb_state = HB_LATE_ANSWERED;","else { v = V23_LATE; l->hb_miss = 0; l->hb_state = HB_LATE_ANSWERED;"),
("cmd 23 nonce comparison skipped","!l->hb_nonce_valid || l->hb_nonce != given","!l->hb_nonce_valid"),
("cmd 23 accepts a nonce that was never fetched","!l->hb_nonce_valid || l->hb_nonce != given","l->hb_nonce != given"),
("cmd 23 duplicate answer re-accepted","else if (l->hb_state == HB_ANSWERED || l->hb_state == HB_LATE_ANSWERED) {","else if (0) {"),
("cmd 23 stale answer accepted","else if (seq < l->hb_seq) {","else if (0) {"),
("cmd 23 never-issued answer accepted","if (l->hb_state == HB_NONE || seq > l->hb_seq) {","if (0) {"),
("cmd 23 on-time answer does not clear the raised timeout","if (l->hb_timeout_raised) { l->hb_timeout_raised = false; mon_ev_append(m, EV_HB_RECOVERED, seq, 0, now); }","(void)0;"),
("cmd 23 bad nonce changes state","v = V23_BAD_NONCE; detail = l->hb_nonce_valid ? 2 : 1;","v = V23_BAD_NONCE; l->hb_state = HB_ANSWERED; detail = l->hb_nonce_valid ? 2 : 1;"),
("new challenge keeps the old nonce valid","l->hb_state = HB_OPEN; l->hb_nonce_valid = false; }","l->hb_state = HB_OPEN; }"),
]
caught=0
for name,a,b in muts:
    if src.count(a)!=1: print("SKIP (anchor %d): %s"%(src.count(a),name)); continue
    open(S+'/m.c','w').write(src.replace(a,b))
    shutil.copy(D+'/mon_model.h',S); shutil.copy(D+'/mon_plat.h',S); shutil.copy(D+'/test_sync.c',S)
    c=subprocess.run(['gcc','-std=c11','-O1','-w','-I',S,S+'/m.c',S+'/test_sync.c','-o',S+'/mt'],capture_output=True,text=True)
    if c.returncode: print("COMPILE-ERR:",name,c.stderr[:100]); continue
    try:
        r=subprocess.run([S+'/mt'],capture_output=True,text=True,timeout=10); res="caught (failed checks)" if r.returncode else "SURVIVED"
    except subprocess.TimeoutExpired: res="caught (hang/timeout)"
    if res.startswith("caught"): caught+=1
    print("%-24s %s"%(res,name))
print("caught",caught,"of",len(muts))
