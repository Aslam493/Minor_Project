#!/usr/bin/env python3
"""H1/H2 object-code check on the ON tee.elf disassembly: every function declared RPI3_MON_CRIT (static __nostackcheck) that still exists
as a symbol must contain no call to __cyg_profile_func_*, memcpy, memcmp, memset, memmove, crypto_*, malloc/free, trace_*.
Functions that were inlined away have no symbol and are covered by their callers. usage: h1_objcheck.py <monitor.c> <objdump -d file>"""
import re, sys, collections
src = open(sys.argv[1]).read()
crit = re.findall(r'^RPI3_MON_CRIT\s+[^(\n]*?\b(rpi3_mon_[a-z0-9_]+)\(', src, re.M)
body = collections.defaultdict(list); fn = None
for l in open(sys.argv[2]):
    m = re.match(r'^[0-9a-f]+ <(.+)>:$', l)
    if m: fn = m.group(1); continue
    if fn: body[fn].append(l)
bad = re.compile(r'^(__cyg_profile_func_(enter|exit).*|mem(cpy|cmp|set|move)|crypto_.*|malloc|calloc|realloc|free|trace_.*|__do_panic)$')
rc = 0
print("RPI3_MON_CRIT functions declared in the source: %d" % len(crit))
for c in crit:
    syms = [s for s in body if s == c or s.startswith(c + '.')]
    if not syms: print("  inlined (no symbol)  %s" % c); continue
    for s in syms:
        calls = set(re.findall(r'\tbl?\t[0-9a-f]+ <([^>+]+)', "".join(body[s]))) - {s}
        hit = sorted(x for x in calls if bad.match(x))
        print("  %-8s %-40s calls: %s" % ("FAIL" if hit else "ok", s, sorted(calls) or "none")); rc |= bool(hit)
sys.exit(rc)
