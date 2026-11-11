#!/usr/bin/env python3
"""Control-flow check on the ON tee.elf disassembly: which calls (default memcpy/memset/memcmp/memmove) sit INSIDE a lock hold?
A hold starts after 'bl rpi3_mon_hold' / 'bl rpi3_mon_acquire' when its result is zero (the first conditional branch on w0 after the call is the
failure path and is NOT followed) and ends at 'bl rpi3_mon_release'. Reports every such call per function.
usage: hold_memcpy_check.py <objdump -d file> [function ...]"""
import re, sys
dis = sys.argv[1]; funcs = sys.argv[2:] or ['rpi3_mon_cmd17','rpi3_mon_cmd18','rpi3_mon_cmd19','rpi3_mon_cmd20','rpi3_mon_cmd21','rpi3_mon_cmd22','rpi3_mon_cmd23','rpi3_mon_note_legacy','rpi3_mon_core1_attempt']
BAD = re.compile(r'^(mem(cpy|set|cmp|move)|crypto_.*|rpi3_mon_hmac|malloc|free|trace_printf)$')
def load(name):
    out = []; f = False
    for l in open(dis):
        if re.match(r'^[0-9a-f]+ <%s>:$' % re.escape(name), l): f = True; continue
        if f and l.strip() == '': break
        if f:
            m = re.match(r'\s*([0-9a-f]+):\s+[0-9a-f]+\s+(\S+)\s*(.*)', l)
            if m: out.append((int(m.group(1), 16), m.group(2), m.group(3)))
    return out
rc = 0
for name in funcs:
    ins = load(name)
    if not ins: print("%-26s (no symbol)" % name); continue
    addr = {a: i for i, (a, _, _) in enumerate(ins)}
    def tgt(i):
        m = re.search(r'\b([0-9a-f]{8,}) <', ins[i][2]); return addr.get(int(m.group(1), 16)) if m else None
    def callee(i):
        if ins[i][1] != 'bl': return None
        m = re.search(r'<([^>+]+)', ins[i][2]); return m.group(1) if m else None
    def succ(i, skip_taken=False):
        op = ins[i][1]
        if op == 'ret': return []
        if op == 'b': t = tgt(i); return [t] if t is not None else []
        if op.startswith('b.') or op in ('cbz', 'cbnz', 'tbz', 'tbnz'):
            r = [i + 1] if i + 1 < len(ins) else []
            if not skip_taken and tgt(i) is not None: r.append(tgt(i))
            return r
        return [i + 1] if i + 1 < len(ins) else []
    starts = [k for k in range(len(ins)) if callee(k) in ('rpi3_mon_hold', 'rpi3_mon_acquire', 'cpu_spin_trylock')]
    inside = {}
    for h in starts:
        # find the first conditional branch within 4 instructions: its taken edge is the failure path
        first = None
        for j in range(h + 1, min(h + 5, len(ins))):
            if ins[j][1].startswith('b.') or ins[j][1] in ('cbz', 'cbnz', 'tbz', 'tbnz'): first = j; break
        seen = {h}; q = [h]
        while q:
            x = q.pop()
            for s in succ(x, skip_taken=(x == first)):
                if s in seen: continue
                seen.add(s)
                c = callee(s)
                if c == 'rpi3_mon_release' or (c in ('cpu_spin_unlock', 'cpu_spin_unlock_xrestore')): continue
                if c and BAD.match(c): inside[s] = c
                q.append(s)
    allbad = [(k, callee(k)) for k in range(len(ins)) if callee(k) and BAD.match(callee(k))]
    for k, c in allbad:
        where = "INSIDE a hold" if k in inside else "outside any hold"
        print("%-26s %-8s at %x : %s" % (name, c, ins[k][0], where)); rc |= (k in inside)
    if not allbad: print("%-26s no memcpy/memset/memcmp/crypto call" % name)
sys.exit(rc)
