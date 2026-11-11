#!/usr/bin/env python3
"""Print a C file with all comments removed (block comments across lines, line comments), keeping string literals and line count."""
import re, sys
s = open(sys.argv[1]).read()
pat = re.compile(r'("(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\')|/\*.*?\*/|//[^\n]*', re.S)
def f(m):
    if m.group(1): return m.group(1)
    return '\n' * m.group(0).count('\n')          # keep line numbers
sys.stdout.write(pat.sub(f, s))
