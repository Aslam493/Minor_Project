#!/usr/bin/env python3
"""Lock-cost evidence from the Monitor-ON tee.elf disassembly: is each function on the lock path instrumented (calls __cyg_profile_func_enter),
and in which order do the lock primitives call each other. Counts function ENTRIES only; no timing. usage: lockcost_check.py <objdump -d file>"""
import re, sys, collections
body = collections.defaultdict(list); fn = None
for l in open(sys.argv[1]):
    m = re.match(r'^[0-9a-f]+ <(.+)>:$', l)
    if m: fn = m.group(1); continue
    if fn: body[fn].append(l)
def calls(n): return [(m.group(1), m.group(2)) for m in (re.search(r'^\s*([0-9a-f]+):.*\tbl\t[0-9a-f]+ <([^>+]+)', l) for l in body.get(n, [])) if m]
def instr(n): return any(c == '__cyg_profile_func_enter' or c.startswith('__cyg_profile_func_enter.') for _, c in calls(n))
path = ['rpi3_mon_hold','rpi3_mon_wait_init','rpi3_mon_acquire','cpu_spin_trylock','spinlock_count_incr','rpi3_mon_release','cpu_spin_unlock_xrestore','cpu_spin_unlock','spinlock_count_decr',
        'rpi3_mon_core1_attempt','thread_mask_exceptions','thread_unmask_exceptions','thread_get_exceptions','thread_set_exceptions','atomic_inc32','have_spinlock','thread_get_core_local','__cpu_spin_trylock','__cpu_spin_unlock','rpi3_mon_evaluate.constprop.0','rpi3_mon_ev_append.constprop.0']
print("function                        instrumented   non-hook calls in order")
for n in path:
    if n not in body: print("%-32s (no separate symbol)" % n); continue
    cs = [c for _, c in calls(n) if not c.startswith('__cyg') and c not in ('_assert_trap', '__stack_chk_fail')]
    print("%-32s %-13s %s" % (n, 'yes' if instr(n) else 'no', ' -> '.join(cs) or '-'))
print()
pta = ['rpi3_mon_acquire','cpu_spin_trylock','spinlock_count_incr','rpi3_mon_release','cpu_spin_unlock_xrestore','cpu_spin_unlock','spinlock_count_decr']
print("PTA lock cycle instrumented entries:", sum(instr(n) for n in pta), "of", len(pta), "; with rpi3_mon_hold and rpi3_mon_wait_init:", sum(instr(n) for n in pta + ['rpi3_mon_hold','rpi3_mon_wait_init']))
core1 = ['cpu_spin_trylock','spinlock_count_incr','cpu_spin_unlock','spinlock_count_decr']
print("Core 1 successful attempt: lock entries", sum(instr(n) for n in core1), "; with rpi3_mon_core1_attempt:", sum(instr(n) for n in core1 + ['rpi3_mon_core1_attempt']))
print("Core 1 failed attempt: lock entries", int(instr('cpu_spin_trylock')), "; with rpi3_mon_core1_attempt:", int(instr('cpu_spin_trylock')) + int(instr('rpi3_mon_core1_attempt')))
