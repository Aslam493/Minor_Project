#!/usr/bin/env python3
"""EV1/EV4/TM2/KY1 evidence: compare the approved tables K10 (event types) and K11 (constants) in MONITOR_STATE_DESIGN.md with the values the REAL
header compiles to (host compile of rpi3_can_monitor.h with the mock bindings), and check that the three prototype keys are pairwise distinct."""
import re, subprocess, tempfile, os, sys
D = open('../MONITOR_STATE_DESIGN.md').read()
OS = os.path.abspath('../../../optee_full/optee_os'); PLAT = OS + '/core/arch/arm/plat-rpi3'
k10 = D.split('### K10.')[1].split('### K11.')[0]; k11 = D.split('### K11.')[1].split('### K12.')[0]
ev = {}
for m in re.finditer(r'(\d+) ([A-Z_]+)(?: \(|[,;.])', k10): ev[int(m.group(1))] = m.group(2)
ev = {k: v for k, v in ev.items() if k <= 24}
# abbreviations the implementation uses (recorded deviation): design name -> header suffix
abbr = {'HEARTBEAT_RECOVERED': 'HB_RECOVERED', 'LEGACY_CMD_REFUSED': 'LEGACY_REFUSED', 'LEGACY_CMD_USED': 'LEGACY_USED', 'DUPLICATE_ANSWER': 'DUP_ANSWER'}
names = [abbr.get(ev[i], ev[i]) for i in sorted(ev)]
src = '#include <stdio.h>\n#include <stddef.h>\n#include <stdint.h>\n#include <rpi3_can_monitor.h>\nint main(void){\n'
for n in names: src += 'printf("EV %%s %%d\\n","%s",(int)RPI3_MON_EV_%s);\n' % (n, n)
for c in ['T_TRAFFIC_MS', 'H_MS', 'D_REQ_MS', 'G_MS', 'K_MISS', 'K_STARVE', 'IDLE_MAX_MS', 'IDLE_USES_MAX', 'BUDGET_TICKS', 'ATTEMPT_CAP', 'CORE1_CADENCE']:
    src += 'printf("C %s %%llu\\n",(unsigned long long)RPI3_MON_%s);\n' % (c, c)
src += 'printf("C EV_COUNT %d\\n",(int)RPI3_MON_EV_COUNT);return 0;}\n'
d = tempfile.mkdtemp(); open(d + '/t.c', 'w').write(src)
r = subprocess.run(['gcc', '-std=gnu11', '-w', '-I', 'mock', '-I', PLAT, '-I', OS + '/lib/libutee/include', '-I', '.', d + '/t.c', 'mock_platform.c', '-pthread', '-o', d + '/t'], capture_output=True, text=True)
if r.returncode: print(r.stderr[:500]); sys.exit(2)
out = subprocess.run([d + '/t'], capture_output=True, text=True).stdout.split('\n')
rc = 0; got = {}
for l in out:
    p = l.split()
    if p[:1] == ['EV']: got[p[1]] = int(p[2])
    if p[:1] == ['C']: got[p[1]] = int(p[2])
print("K10: %d event types parsed from the design" % len(ev))
for i in sorted(ev):
    n = abbr.get(ev[i], ev[i]); ok = got.get(n) == i
    if not ok: rc = 1
    print("  %2d %-22s header %-4s %s%s" % (i, ev[i], got.get(n), "ok" if ok else "MISMATCH", "  (header name %s)" % n if n != ev[i] else ""))
print("header event count:", got['EV_COUNT'], "(design: 25)"); rc |= got['EV_COUNT'] != 25
exp = {'T_TRAFFIC_MS': 300, 'H_MS': 1000, 'D_REQ_MS': 500, 'G_MS': 60000, 'K_MISS': 3, 'K_STARVE': 8, 'IDLE_MAX_MS': 60000, 'IDLE_USES_MAX': 16, 'BUDGET_TICKS': 19200, 'ATTEMPT_CAP': 2000, 'CORE1_CADENCE': 256}
for k, v in exp.items():
    ok = got[k] == v; rc |= (not ok); print("K11 %-14s design %-6d header %-6d %s" % (k, v, got[k], "ok" if ok else "MISMATCH"))
# the K11 text itself must contain these numbers (guards the expected table above against my own typing)
for pat in ['T = \\*\\*300 ms\\*\\*', 'H = \\*\\*1 s\\*\\*', 'D_req = \\*\\*500 ms\\*\\*', 'G = 60 s', 'k = 3', 'K = 8', '`IDLE_MAX` = 60 s', '`IDLE_USES_MAX` = 16', 'lock budget 19,200 ticks', 'attempt cap 2,000', 'cadence 256 passes']:
    ok = re.search(pat, k11) is not None; rc |= (not ok); print("  K11 text contains %-28s %s" % (pat.replace('\\', ''), "yes" if ok else "NO"))
ksrc = open(PLAT + '/rpi3_can_monitor_keys.h').read().replace('\\\n', ' '); keys = {}
for n in ['FRAME', 'HB', 'TEST']:
    m = re.search(r'#define RPI3_MON_KEY_%s_INIT\s*\{(.*?)\}' % n, ksrc, re.S); keys[n] = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})', m.group(1)))
print("KY1 key lengths:", {k: len(v) for k, v in keys.items()}, "| pairwise distinct:", len(set(keys.values())) == 3)
rc |= len(set(keys.values())) != 3 or any(len(v) != 32 for v in keys.values())
sys.exit(rc)
