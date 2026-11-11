import re, sys, collections
dis = open(sys.argv[1]).read().split('\n')
func_re = re.compile(r'^([0-9a-f]{16}) <([^>]+)>:$')
ins_re  = re.compile(r'^\s+([0-9a-f]+):\s+(\S+)\s*(.*)$')
funcs = collections.OrderedDict()   # name -> dict
cur = None
for line in dis:
    m = func_re.match(line)
    if m:
        cur = m.group(2)
        funcs[cur] = dict(addr=int(m.group(1),16), ins=[])
        continue
    m = ins_re.match(line)
    if m and cur:
        funcs[cur]['ins'].append((int(m.group(1),16), m.group(2), m.group(3)))
addr2func = {f['addr']: n for n,f in funcs.items()}
def frame_and_calls(name):
    f = funcs[name]; ins = f['ins']
    frame = 0; flags = set(); calls = []
    # prologue: sum sp decrements in the first 24 instructions
    for a,mn,ops in ins[:24]:
        m = re.match(r'(stp|str)\s+.*\[sp,\s*#-(0x[0-9a-f]+|\d+)\]!', mn+' '+ops)
        if m: frame += int(m.group(2),0)
        m = re.match(r'sub\s+sp,\s*sp,\s*#(0x[0-9a-f]+|\d+)', mn+' '+ops)
        if m: frame += int(m.group(1),0)
        if re.match(r'sub\s+sp,\s*sp,\s*x', mn+' '+ops): flags.add('dynamic-sp')
    for a,mn,ops in ins:
        if mn == 'bl':
            m = re.match(r'([0-9a-f]+)\s+<([^>+]+)', ops)
            if m: calls.append(m.group(2))
        elif mn == 'b':
            m = re.match(r'([0-9a-f]+)\s+<([^>+]+)(\+0x[0-9a-f]+)?>', ops)
            if m and m.group(2) != name and not m.group(3):
                calls.append(m.group(2))      # tail call (treated as a call: upper bound)
        elif mn in ('blr','br'):
            flags.add('indirect-call')
        if re.match(r'(ld|st)[a-z0-9]*\s', mn+' ') and re.search(r'\b[qvd]\d+\b|\bv\d+\.', ops):
            flags.add('fp/simd')
    return frame, calls, flags
EXCL = set()
cache = {}
def depth(name, stack=()):
    if name in cache: return cache[name]
    if name not in funcs: return (0, [name], {'unknown-symbol'})
    if name in stack: return (0, [name+'(recursion)'], {'recursion'})
    fr, calls, fl = frame_and_calls(name)
    best = (0, [], set())
    allfl = set(fl)
    for c in dict.fromkeys(calls):
        if c in EXCL: continue
        d, path, cf = depth(c, stack+(name,))
        allfl |= cf
        if d > best[0] or not best[1]: best = (d, path, cf)
    res = (fr + best[0], [name] + best[1], allfl)
    cache[name] = res
    return res
ERRFN = ['__do_panic','_assert_log','_assert_break','_assert_trap','__stack_chk_fail','print_kernel_stack','print_stack_limits','print_thread_id','trace_printf','trace_vprintf','snprintk','vsnprintk','__vsnprintf','kprintf','panic','__panic_at_smc_return','check_canary_fail','print_canary']
if __name__ == '__main__':
    mode = sys.argv[2]
    if mode == 'normal': EXCL.update(ERRFN)
    for ent in sys.argv[3:]:
        d, path, fl = depth(ent)
        fr, _, own = frame_and_calls(ent)
        print(f'== {ent}: own frame {fr} B, deepest static chain {d} B   flags: {sorted(fl) or "-"}')
        tot = 0
        for n in path:
            f = frame_and_calls(n)[0] if n in funcs else 0
            tot += f
            print(f'     {f:5d}  cumulative {tot:5d}  {n}')
