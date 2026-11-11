# Component 2.2 host reference model: results (2026-11-06)

**What this is.** A host-only C model of the approved synchronization logic (design sections 4d to 4i, plus the approved
Step 1 and Step 2 command 18 and 20 contracts), with a mock timer, mock lock and mock exception state. Run with `make test`
(AddressSanitizer + UBSan) and `make mutate`. It touches **no OP-TEE source and no board**.

**What this is not.** It is not the OP-TEE implementation. A pass here shows the approved logic is self-consistent and that the
tests detect the bugs they were written for. **It does not pass any of the 53 design checks on the real code**, and the section 4j
statuses stay `NOT RUN`. Real-code checks (disassembly H1, memory visibility W1, board timings, cross-core behaviour) are untouched.

Latest run (`last_run.txt`): 24 test groups, **170 checks, 0 failed**. Mutation run (`last_mutation_run.txt`): **43 of 45 mutants
caught**; the 2 survivors are equivalent mutants (see below). Updated 2026-11-07 for the approved package (DEC-RATE-1, DEC-IF-1 to DEC-IF-15): new event numbering (25 types), the evidence-flood rule, the 300 ms / 1 s / 500 ms / k=3 constants, commands 19, 21 and 23, and command 20 page 1.

## Coverage of the 25 host-side checks of section 4j

| Result | Checks |
|---|---|
| Exercised by the model, passing | V6 V7 V9 V10 V11\* V12 W3 W6 X1 X2 X3 X4 X5 X6 X7 Y1 Y2 Y3 Y4 Y5 Z1\*\* Z4 (22) |
| Only partly exercised | Z5 (once-only is tested; "before direct access" is not), Z6 (the guard is tested; "outside the lock" is not) |
| Not exercised | Z3 (no mock copy function checks the lock flag) |

\* V11: the in-use guard is tested; the compile-time "no CONCURRENT" assertion is not (it needs the real registration macro).
\*\* Z1: tested as a negative control plus every protected access going through a checked accessor; the real check is on the real code.

New interface checks exercised: N1 N2 N3 N4 N6 N7 N8, HP2 HP3 HP4 HP5, Q5 (per-class rate limit), and the command 18 contract (five records, bounded 64-slot
scan, ordering, `more` flag, pinned invariants). N5 is partly covered by static asserts on the projection (size 128, alignment 8,
every field offset). HP1, HP6 and V1 are not asserted in the model.

## Mutation testing

Each mutant injects one bug; a mutant is caught if any check fails or the run hangs. Caught: no attempt cap (hang), fresh budget per
acquisition, increment on success, unmask-before-unlock, unguarded timeout event, late answer resetting the miss count, `skip_total`
reset, `MON_STARVED` below K, Core 1 reading or writing protected state on a failed attempt, cmd 18 allowing 16 records, overwritable
pinned entries, uncounted overflow, page 1 accepted, buffer-before-init precedence, stale size on BUSY, health read before unlock,
a mac guard that never blocks, and the **faithful old R-A2 condition** (`now >= t0 && elapsed > budget`), caught by X4 and a hang
without the cap, which is the exact defect 2.2.3 removed.

Survivors: (1) "no `now < t0` test" and (2) "evidence limit also applies inside the pinned quota" (unreachable: an evidence class cannot have an ordinary entry before its pinned quota is full). The first: It is an **equivalent mutant**: with unsigned arithmetic `now - t0` already wraps to a huge value when
the timer runs backward, so the loop still exits. The explicit `now < t0` in 4f B3 is redundant arithmetically but harmless and
makes the intent visible. What actually matters is the absence of the `now >= t0 &&` guard.

## Things found while modelling (for your decision; the design document is NOT edited)

1. **Diagnostic-event rate limit: RESOLVED 2026-11-07 (DEC-RATE-1).** You approved one event per second **per class**. The model now keeps a
   separate timestamp for every diagnostic class, so one class never suppresses another; suppressed events still increment the exact
   counter; evidence classes are never limited; an event exactly one second after the previous one of its class is allowed. Test group
   Q5 (8 checks) covers this, and four new mutants (shared bucket, no limit, off-by-one at the boundary, uncounted suppression) are caught.
2. **Two of my own tests were vacuous at first** (a tautology on `ev_lost`, and `trylock_calls == 0 + trylock_calls`). They are
   replaced with exact assertions (58 overwrites; the lock is never even tried on a short-buffer reply). Mutation testing is how
   this was checked.
3. **Placeholder values in the model, not approved:** the event type list beyond the six evidence classes, `MON_T_TRAFFIC` (50 ms),
   `MON_D_HB` (500 ms), `MON_K_MISS` (2), `MON_ATTEMPT_CAP` (2000), and `K`. They exist only so the tests can run.
4. **Test-author caveat:** one test failed at first because my expected count ignored a second legitimate traffic timeout; the model
   was right and the test was wrong.

## Not modelled

HMAC, frame validation and the nonce derivation (commands 17 and 22), the real ring layout in OP-TEE memory, the dispatch and legacy-command glue, the D1 variants, cache and
memory ordering, the instrumented-call cost, anything that depends on the board.
