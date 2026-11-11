#!/usr/bin/env python3
"""C4.6: derive and check the monitor's timing parameters from MEASURED values and write mon_params.h.
Reads the real constants from rpi3_can_monitor.h (never copies them), the measurements from measured_*.json, and checks every rule the plan lists:
  (a) D_req < H            (b) 32T >= 1.25 (grace + R)      (c) record ring >= N_req = ceil((grace + R + 32T)/T) x 1.25 (plan I-28/O-22; default 128 records)
NOTE (2026-11-12): rules (b) and (c) are DESIGN-2 / Component 7 rules about the chip-B RECORD ring (not implemented yet). T here is the FRAME PERIOD (plan R14: 50 ms),
NOT the traffic deadline T_TRAFFIC_MS (300 ms). R = Normal-World report interval, grace = time before a record's absence is judged, 32 = entries per report.
The implemented 64-slot ring is the EVENT ring and is not compared against N_req.
plus the timing relations the design depends on. R, grace and T (frame period) are REQUIRED inputs (--r-ms, --grace-ms, --t-frame-ms; definitions in monitor/design/ORIGINAL_PLAN_v3.4_*.md), never defaulted;
without them rules (b) and (c) are reported NOT CHECKED, not passed.
usage: derive_params.py [--measured FILE] [--header FILE] [--r-ms N] [--grace-ms N] [--t-frame-ms N] [--record-ring N] [--out mon_params.h]
exit code 0 = every CHECKED rule passes (unchecked rules are listed); 1 = a checked rule fails."""
import argparse, json, math, os, re, sys
ap = argparse.ArgumentParser()
here = os.path.dirname(os.path.abspath(__file__))
ap.add_argument('--measured', default=os.path.join(here, 'measured_20261109.json'))
ap.add_argument('--header', default=os.path.join(here, '../../../optee_full/optee_os/core/arch/arm/plat-rpi3/rpi3_can_monitor.h'))
ap.add_argument('--r-ms', type=float); ap.add_argument('--grace-ms', type=float); ap.add_argument('--t-frame-ms', type=float)
ap.add_argument('--record-ring', type=int, default=128, help='record-ring slots to check against N_req (plan default 128; NOT the 64-slot event ring)')
ap.add_argument('--out', default=os.path.join(here, 'mon_params.h'))
a = ap.parse_args()
hdr = open(a.header).read()
def const(name):
    m = re.search(r'#define\s+' + name + r'\s+\(?(\d+)[UL]*\)?', hdr)
    if not m: sys.exit("constant %s not found in %s" % (name, a.header))
    return int(m.group(1))
C = {n: const('RPI3_MON_' + n) for n in ['T_TRAFFIC_MS', 'H_MS', 'D_REQ_MS', 'G_MS', 'K_MISS', 'K_STARVE', 'BUDGET_TICKS', 'ATTEMPT_CAP', 'CORE1_CADENCE', 'SENDER_PERIOD_MS', 'SENDER_JITTER_MS', 'PINNED_PER_CLASS', 'EV_N_EVIDENCE', 'ORDINARY_SLOTS', 'TICKS_PER_MS']}
C['PINNED_SLOTS'] = C['PINNED_PER_CLASS'] * C['EV_N_EVIDENCE']	# the header defines it as this product
M = {k: v for k, v in json.load(open(a.measured)).items() if not k.startswith('_')}
ring = C['PINNED_SLOTS'] + C['ORDINARY_SLOTS']
interval_us = C['CORE1_CADENCE'] * M['pass_ns_base_hardened'] / 1000.0 + M['attempt_us']
budget_ms = C['BUDGET_TICKS'] / C['TICKS_PER_MS']
res = []   # (id, text, status, detail)
def chk(i, text, ok, detail): res.append((i, text, 'PASS' if ok else 'FAIL', detail))
def unchecked(i, text, why): res.append((i, text, 'NOT CHECKED', why))
chk('a', 'D_req < H', C['D_REQ_MS'] < C['H_MS'], '%d ms < %d ms' % (C['D_REQ_MS'], C['H_MS']))
chk('d1', 'D_req >= 20 x worst measured frame/answer submit latency', C['D_REQ_MS'] * 1000 >= 20 * M['frame_submit_latency_max_us'], 'D_req %d ms vs 20 x %.1f ms = %.0f ms' % (C['D_REQ_MS'], M['frame_submit_latency_max_us'] / 1000, 20 * M['frame_submit_latency_max_us'] / 1000))
chk('d2', 'T >= sender period + jitter + 5 x worst measured submit latency', C['T_TRAFFIC_MS'] * 1000 >= (C['SENDER_PERIOD_MS'] + C['SENDER_JITTER_MS']) * 1000 + 5 * M['frame_submit_latency_max_us'], 'T %d ms vs %d + %d + %.0f ms' % (C['T_TRAFFIC_MS'], C['SENDER_PERIOD_MS'], C['SENDER_JITTER_MS'], 5 * M['frame_submit_latency_max_us'] / 1000))
chk('d3', 'Core 1 attempt interval << T and << D_req (>= 100x)', C['T_TRAFFIC_MS'] * 1000 >= 100 * interval_us and C['D_REQ_MS'] * 1000 >= 100 * interval_us, 'interval %.0f us (cadence %d x %.1f ns + %.1f us)' % (interval_us, C['CORE1_CADENCE'], M['pass_ns_base_hardened'], M['attempt_us']))
chk('d4', 'PTA lock wait budget >= 100 x worst measured PTA hold', budget_ms * 1000 >= 100 * M['hold_max_us_pta'], 'budget %.2f ms vs 100 x %.2f us = %.0f us' % (budget_ms, M['hold_max_us_pta'], 100 * M['hold_max_us_pta']))
chk('d5', 'attempt cap not the limiting bound (cap >= 2 x attempts per budget measured)', C['ATTEMPT_CAP'] >= 2 * M['attempts_per_ms_while_waiting'] * budget_ms, 'cap %d vs 2 x %.0f x %.2f ms = %.0f' % (C['ATTEMPT_CAP'], M['attempts_per_ms_while_waiting'], budget_ms, 2 * M['attempts_per_ms_while_waiting'] * budget_ms))
chk('d6', 'starvation window K_starve x interval >= 100 x worst PTA hold (a healthy PTA never starves Core 1)', C['K_STARVE'] * interval_us >= 100 * M['hold_max_us_pta'], 'K=%d x %.0f us = %.0f us vs 100 x %.2f us' % (C['K_STARVE'], interval_us, C['K_STARVE'] * interval_us, M['hold_max_us_pta']))
chk('d7', 'heartbeat timeout K_miss x H', True, 'TIMEOUT after %d misses = %d ms (information, no limit stated in the plan)' % (C['K_MISS'], C['K_MISS'] * C['H_MS']))
if a.r_ms is None or a.grace_ms is None or a.t_frame_ms is None:
    why = 'R, grace and the frame period T are required inputs (--r-ms --grace-ms --t-frame-ms); see monitor/design/ORIGINAL_PLAN_v3.4_*.md (R14, R6, I-28, O-22)'
    unchecked('b', '[D2/C7] 32T >= 1.25 (grace + R)', why); unchecked('c', '[D2/C7] record ring >= N_req', why)
else:
    Tf = a.t_frame_ms
    chk('b', '[D2/C7] 32T >= 1.25 (grace + R)', 32 * Tf >= 1.25 * (a.grace_ms + a.r_ms), '32T = %.0f ms vs 1.25 x (%.0f + %.0f) = %.0f ms (T = frame period %.0f ms)' % (32 * Tf, a.grace_ms, a.r_ms, 1.25 * (a.grace_ms + a.r_ms), Tf))
    n_base = math.ceil((a.grace_ms + a.r_ms + 32 * Tf) / Tf); n_req = math.ceil(n_base * 1.25)
    chk('c', '[D2/C7] record ring >= N_req', a.record_ring >= n_req, 'N_req = ceil((grace+R+32T)/T) = %d, with 25%% margin %d; record ring (plan default, NOT implemented yet) %d slots. The implemented 64-slot EVENT ring is not compared.' % (n_base, n_req, a.record_ring))
with open(a.out, 'w') as f:
    f.write('/* GENERATED by derive_params.py from %s and the measurements in %s. Do not edit.\n * Not included by the firmware: the firmware constants live in rpi3_can_monitor.h and are CHECKED against these values. */\n#ifndef MON_PARAMS_H\n#define MON_PARAMS_H\n' % (os.path.basename(a.header), os.path.basename(a.measured)))
    f.write('#define MON_PARAM_CORE1_ATTEMPT_INTERVAL_US\t%.0f\t/* cadence %d x %.1f ns + %.1f us */\n' % (interval_us, C['CORE1_CADENCE'], M['pass_ns_base_hardened'], M['attempt_us']))
    f.write('#define MON_PARAM_PTA_HOLD_MAX_US_MEASURED\t%.2f\n#define MON_PARAM_RING_SLOTS\t%d\n#define MON_PARAM_TIMEOUT_AFTER_MS\t%d\t/* K_miss x H */\n' % (M['hold_max_us_pta'], ring, C['K_MISS'] * C['H_MS']))
    f.write('_Static_assert(%d < %d, "D_req < H");\n_Static_assert(%d >= 100 * %d, "T >= 100 x Core 1 attempt interval (us)");\n#endif\n' % (C['D_REQ_MS'], C['H_MS'], C['T_TRAFFIC_MS'] * 1000, round(interval_us)))
w = max(len(r[1]) for r in res)
for i, t, s, d in res: print('%-3s %-*s  %-11s %s' % (i, w, t, s, d))
print('wrote', a.out)
sys.exit(1 if any(r[2] == 'FAIL' for r in res) else 0)
