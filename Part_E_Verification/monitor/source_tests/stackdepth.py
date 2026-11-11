#!/usr/bin/env python3
"""Static worst-case stack depth (C3 / A1.6, OBJECT evidence): from an `objdump -d` listing, the frame size of every function
(sum of `sub sp, sp, #N` and pre-indexed `stp/str ..., [sp, #-N]!` in the prologue, i.e. before the first branch) and the deepest
direct-call path from a root. Reports indirect calls (blr/br), dynamic stack adjustment (`sub sp, x?`/alloca) and recursion, which make the
result a lower bound. usage: stackdepth.py <objdump file> <root> [stack size] [margin] [regex of fatal-path functions to exclude for the NORMAL-path figure]"""
import re, sys, collections
dis, root = sys.argv[1], sys.argv[2]
stack = int(sys.argv[3]) if len(sys.argv) > 3 else 4096
margin = int(sys.argv[4]) if len(sys.argv) > 4 else 2048
excl = re.compile(sys.argv[5] if len(sys.argv) > 5 else r'^(__do_panic|_assert_trap|__stack_chk_fail|print_|trace_|kprintf|snprintk|__vsnprintf|vsnprintk|uuid2str|console_)')
body = collections.defaultdict(list); fn = None
for line in open(dis):
    m = re.match(r'^([0-9a-f]+) <(.+)>:$', line)
    if m: fn = m.group(2); continue
    if fn: body[fn].append(line)
frame = {}; dyn = set(); edges = collections.defaultdict(set); indirect = collections.defaultdict(int)
for f, ls in body.items():
    sz = 0; in_pro = True
    for l in ls:
        if in_pro:
            m = re.search(r'\tsub\tsp, sp, #(0x[0-9a-f]+|\d+)', l)
            if m: sz += int(m.group(1), 0)
            m = re.search(r'\t(?:stp|str)\t.*\[sp, #-(\d+)\]!', l)
            if m: sz += int(m.group(1))
            if re.search(r'\tsub\tsp, (?!sp, #)', l): dyn.add(f)
            if re.search(r'\t(bl|b|blr|br|ret|b\.[a-z]+|cbz|cbnz|tbz|tbnz)\t', l): in_pro = False
        m = re.search(r'\t(bl|b)\t[0-9a-f]+ <([^>+]+)(\+0x[0-9a-f]+)?>', l)
        if m and (m.group(1) == 'bl' or not m.group(3)) and m.group(2) != f: edges[f].add(m.group(2))
        if re.search(r'\t(blr|br)\t', l): indirect[f] += 1
        if re.search(r'\tsub\tsp, (?!sp, #)', l): dyn.add(f)
    frame[f] = sz
memo = {}; onstack = set(); rec = set()
def depth(f):
    if f in memo: return memo[f]
    if f in onstack: rec.add(f); return (0, [])
    onstack.add(f); best = (0, [])
    for g in sorted(edges.get(f, ())):
        d = depth(g)
        if d[0] > best[0] or not best[1]: best = d if d[0] >= best[0] else best
    onstack.discard(f); memo[f] = (frame.get(f, 0) + best[0], [f] + best[1]); return memo[f]
# direct max over callees (re-do explicitly for clarity)
def depth2(f):
    if f in memo: return memo[f]
    if f in onstack: rec.add(f); return (0, [])
    onstack.add(f); bd, bp = 0, []
    for g in sorted(edges.get(f, ())):
        d, p = depth2(g)
        if d > bd: bd, bp = d, p
    onstack.discard(f); memo[f] = (frame.get(f, 0) + bd, [f] + bp); return memo[f]
memo.clear(); d, path = depth2(root)
reach = set(); q = [root]
while q:
    f = q.pop()
    if f in reach: continue
    reach.add(f); q.extend(edges.get(f, ()))
print("root %s: worst-case direct-call depth %d bytes of a %d-byte tmp stack (headroom %d, required margin %d) -> %s" % (root, d, stack, stack - d, margin, "OK" if stack - d >= margin else "BELOW MARGIN"))
print("deepest path:"); [print("  %-44s frame %4d" % (p, frame.get(p, 0))) for p in path]
print("reachable functions: %d; with indirect calls (blr/br): %s; dynamic sp adjustment: %s; recursion: %s" % (len(reach), sorted(f for f in reach if indirect.get(f)) or "none", sorted(f for f in reach if f in dyn) or "none", sorted(rec) or "none"))
print("largest frames in the reachable set:"); [print("  %-44s %4d" % (f, frame[f])) for f in sorted(reach, key=lambda x: -frame.get(x, 0))[:6]]

# NORMAL-path figure: the same walk with the fatal-path functions (panic, assert, trace printing) removed
for f in list(edges): edges[f] = {g for g in edges[f] if not excl.search(g)}
memo.clear(); onstack.clear(); rec.clear(); d2, path2 = depth2(root)
print("\nNORMAL path (fatal-path functions excluded): %d bytes, headroom %d, margin %d -> %s" % (d2, stack - d2, margin, "OK" if stack - d2 >= margin else "BELOW MARGIN"))
print("deepest normal path:"); [print("  %-44s frame %4d" % (p, frame.get(p, 0))) for p in path2]
print("recursion in the normal path: %s" % (sorted(rec) or "none"))
