#!/usr/bin/env python3
"""F27/Z7 call-graph audit: functions reachable from a root in a disassembly (direct bl/b-to-function edges); reports indirect calls (blr/br)
inside the reachable set, and whether any forbidden symbol is reachable. usage: callgraph.py <objdump -d file> <root> [forbidden regex]"""
import re, sys, collections
dis, root = sys.argv[1], sys.argv[2]
forb = re.compile(sys.argv[3] if len(sys.argv) > 3 else r'crypto_|hmac|mac_|malloc|free|calloc|realloc|mempool|nex_|tee_pager|trace_printf')
fn = None; body = collections.defaultdict(list); addr2fn = {}
for line in open(dis):
    m = re.match(r'^([0-9a-f]+) <(.+)>:$', line)
    if m: fn = m.group(2); addr2fn[int(m.group(1), 16)] = fn; continue
    if fn: body[fn].append(line)
edges = collections.defaultdict(set); indirect = collections.defaultdict(list)
for f, ls in body.items():
    for l in ls:
        m = re.search(r'\t(bl|b)\t[0-9a-f]+ <([^>+]+)(\+0x[0-9a-f]+)?>', l)
        if m and (m.group(1) == 'bl' or not m.group(3)) and m.group(2) != f: edges[f].add(m.group(2))
        if re.search(r'\t(blr|br)\t', l): indirect[f].append(l.strip())
seen = {root}; q = [root]; parent = {}
while q:
    f = q.pop()
    for g in sorted(edges.get(f, ())):
        if g not in seen: seen.add(g); parent[g] = f; q.append(g)
print("reachable functions from %s: %d" % (root, len(seen)))
for f in sorted(seen): print("  ", f)
print("indirect calls inside the reachable set:")
for f in sorted(seen):
    for l in indirect.get(f, []): print("  ", f, ":", l)
bad = [f for f in seen if forb.search(f)]
print("FORBIDDEN reachable:", bad if bad else "none")
for b in bad:
    c = [b]; 
    while c[-1] in parent: c.append(parent[c[-1]])
    print("   path:", " <- ".join(c))
sys.exit(1 if bad else 0)
