# M3B-EVALUATOR-FIX-RESULTS.md — nested-closure argument-evaluation bug (fix)

M3B's rendering code exposed a pre-existing evaluator correctness bug: a closure
invocation nested inside another closure's argument list corrupted the outer
invocation's argument count. This milestone fixes the bug, adds a pure
regression test, and restores M3B's natural helper-closure rendering so the
example now exercises the corrected evaluator.

## 1. Exact root cause

`emit_invoke_closure` (r0_s1_runtime.c) evaluates a closure's arguments with a
loop whose bound is the global cell `RV_ARITY`:

```
e_cell(RV_T4); e_cell(RV_ARITY); asm_host(HOST_LT);   /* i < RV_ARITY ? */
```

`RV_ARITY` is loaded once at invocation entry from the closure's spec and saved
on the return stack **once** (before the loop), then restored **once** (after the
loop). During argument evaluation, a nested closure invocation (`r_invoke_closure`)
reuses the same global `RV_ARITY` cell for its *own* arity, overwriting the outer
value; when the nested invocation returns it restores its own saved value. The
outer loop therefore re-tests `i < RV_ARITY` against the *nested* closure's
arity, terminates early, and the later arguments are never bound — the argument
values shift.

This is purely an evaluator bug, independent of M3 datatypes, genealogy, or
rendering. Reproduced with ordinary closures/natives:

```
g: func [x] [ x ]
f: func [a b] [ values [a b] ]
f g 100 115          -> buggy: N=1 (expected [100 115])
```

## 2. Exact evaluator change

`r0_s1_runtime.c`, `emit_invoke_closure` argument loop only. `RV_ARITY` (the
loop bound) is now saved on the return stack and restored **per argument**, in
the same way the loop counter `RV_T4` already was — so a nested closure
invocation during argument evaluation cannot clobber the outer loop's bound:

```c
e_cell(RV_T4);    asm_toR();
e_cell(RV_ARITY); asm_toR();        /* + preserve loop bound across nested eval */
to_subexpr[n_subexpr++] = emit_call_fwd();
asm_fromR(); e_setc(RV_ARITY);      /* + restore loop bound */
asm_fromR(); e_setc(RV_T4);
```

Two instructions added (plus the explanatory comment). No S1 change, no parser
change, no HOST service, no datatype change, no new global scratch cell — the
invocation-local state is preserved explicitly on the return stack.

## 3. Assertion count

- **Before fix (M3B milestone):** 244 `ok:`.
- **After fix:** **249 `ok:`**, **0 failures**, exit 0 (5 new regression
  assertions).

## 4. Emitted code-size change

`R0-S1 code emitted: 6832 cells` (was 6798 at M3A). **+34 cells** (one
`e_cell + >R` save and one `R> + e_setc` restore of `RV_ARITY`, each ~16–17
cells, inside the argument loop).

## 5. git diff --stat

```
 Makefile        |  4 +++-
 main.c          |  4 ++++
 r0_s1_runtime.c | 10 +++++++++-
 3 files changed, 16 insertions(+), 2 deletions(-)
```

New files (untracked): `r0_s1_nested_closure_tests.c`; plus the M3B files
(`r0_s1_m3b_tests.c`, `examples/m3b-genealogy.svg`,
`M3B-GENEALOGY-GRAPH-DESIGN.md`, `M3B-RESULTS.md`).

## 6. Other evaluator state audited for the same problem

`emit_invoke_closure` holds the following invocation-local state during the
argument loop. Only `RV_ARITY` was vulnerable:

- **`RV_ARITY`** — the argument-loop bound, *read during the loop*. **Vulnerable;
  fixed** (now saved/restored per argument).
- **`RV_T4`** — the loop counter, read during the loop. Already saved/restored
  per argument (`invoke_raw` uses the same discipline). Not vulnerable.
- **`RV_CLOSURE`** — held in the global cell across the loop (saved once before
  the loop, restored once after). It is *not read during the loop* (only by the
  bind loop / child-context / frame setup after it), and it is correctly
  restored from the before-loop save, so a nested invocation's clobber is
  harmless. Not currently vulnerable; noted as the same "frame-level" pattern
  but safe because it is only consumed after the loop.
- **`RV_CHILD`** — set to 0 at entry, consumed only after the loop (body entry).
  A nested invocation overwrites it (to its own child context, then back to 0 in
  its epilogue); the outer never reads it during the loop. Not vulnerable.
- **`RV_CUR` / `RV_END` / `RV_BLK` / `RV_CTX` / `RV_FRAME`** — the evaluation
  position. These are saved/restored by the *activation frame* mechanism, not by
  global-cell save/restore: each nested invocation allocates a frame that
  records the caller's CUR/END/BLK/CTX/FRAME and restores them in its epilogue.
  Not vulnerable.
- **`RV_SIP` / `RV_SRP` / `RV_FNEW` / `RV_WORD` / `RV_T6`** — used only after
  the argument loop (frame capture/fill, parameter binding). Never live during
  the loop. Not vulnerable.

No other evaluator routine was changed. `invoke_raw` already used the correct
per-argument discipline (it saves `RV_T4`/`RV_T5`/`RV_T6` around each argument),
which is why RAW-in-RAW nesting was never affected.

## 7. Regression test

New `r0_s1_nested_closure_tests.c` — pure closures/natives only (no datatypes,
no genealogy, no rendering). Cases A–E all **fail before** the fix and **pass
after**:

```
A: f (g 100) 115 -> [100 115]
B: f (+ (g 100) 30) 115 -> [130 115]
C: f (g 100) (g 200) -> [100 200]
D: f 7 (g 42) 9 -> [7 42 9]
E: nested arg eval leaves RP balanced
```

## 8. M3B workaround removed

`r0_s1_m3b_tests.c` now uses the natural helper-closure rendering — `node-x`,
`node-y`, `node-name` are ordinary closures invoked inside `draw-node`/
`draw-edge` argument lists (e.g. `draw-rect node-x n node-y n 60 30`), exactly
the pattern that previously triggered the bug. The example now exercises the
corrected evaluator.

## 9. Verification

- **Complete native suite:** exit 0, 249 `ok:`, 0 failures.
- **New nested-closure regression:** A–E pass.
- **M3A A–Q:** unchanged and passing.
- **M3B A–H / R / S:** unchanged and passing.
- **`./check-frozen-s1.sh`:** OK (`f90496c26dc45c7a387d8fc8639cd2781507d3d2`).
- **Generated SVG** (`examples/m3b-genealogy.svg`) still contains `Alice`,
  `Bob`, `Charlie`, three `<line>` elements, and the node `<rect>`/`<text>`
  elements; the 59-integer command stream is byte-identical to the golden
  sequence.

No commit or tag was made; `git add .` was not run.
