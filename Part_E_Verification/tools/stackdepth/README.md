# stackdepth.py: static stack-depth estimate from a disassembly

Usage:
    aarch64-none-linux-gnu-objdump -d --no-show-raw-insn optee_os/out/arm/core/tee.elf > tee.dis
    python3 -I stackdepth.py tee.dis normal|all <function> [<function> ...]

Method: each function's frame is the sum of the stack-pointer decrements in its first 24 instructions
(`stp ..., [sp, #-N]!` and `sub sp, sp, #N`, decimal or hex). It follows direct `bl` calls and tail `b` calls
and reports the deepest chain. `normal` leaves out a fixed list of error handlers (panic, assert, trace).

Limits (the two errors go in opposite directions, so the result is an ESTIMATE, not a bound):
- calls through function pointers (`blr`, `br`) cannot be followed, so those chains are too SHORT;
- every call to a function is assumed to run the stack-check hook at its deepest, and all branches are assumed
  taken in the worst order, so chains are too LONG;
- functions with a frame larger than the first 24 instructions can show, or variable `sub sp, sp, x` frames, are flagged or missed;
- it says nothing about what actually executes. A run-time watermark (pattern-filled stack) is the real test (plan item A1.6).
