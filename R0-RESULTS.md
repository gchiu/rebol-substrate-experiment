# R0 — Implementation Results

R0 (a very small REBOL-like language) implemented above the frozen S1
substrate. Commit `2e21310` is the normative `R0-ARCHITECTURE.md`; this file
records what was actually built, where it deviates, and what the experiment
showed.

## Build & run

```
make
./s1        # runs S1 clean-room + adversarial + claims + R0 tests
```

All suites pass: **64 `ok:` checks, 0 failures, exit 0**. The original S1
clean-room, adversarial, and claims tests are untouched and still pass.

## What was implemented

- **Values (all 9 tag families).** `INT`, `NONE`, `WORD` (with `SET-`/`GET-`/
  `LIT-` subtypes), `BLOCK`, `CONTEXT`, `CLOSURE`, `NATIVE`, `RAW` — tagged
  cells (low 4 bits) living in S1's flat memory `M`.
- **Symbol interning** (identity by `sym_id`), **heap allocator** (bump,
  16-aligned, in `M`), **immutable blocks** `[count, elem...]`, **parent-linked
  lexical contexts** `[parent, count, cap, (word,value)...]`, **closures**
  `[spec, body, captured-ctx, func-site-id]`.
- **Nearest-binding-update `SET`** (walks the lexical chain; updates in place).
- **The §6 normal-call ABI**: record `caller_SP` → evaluate args → bind into a
  fresh child context → restore `SP` → install `kind=1` frame → evaluate body →
  preserve result set → restore `SP` → place result set.
- **Result protocol** `[r1..rN, N]` on a data stack; `reduce` for argument
  positions; arity `N` is an ordinary tagged INT on top.
- **Fixed-arity prefix evaluator** (the §7 `eval-subexpr` grammar), with `func`
  and `return` as reader-recognised keywords.
- **`values [e1 ... en]`** (one BLOCK argument), **`if`**, **`loop`**,
  **`break`**, **`catch`/`throw`**, **`do`**.
- **Definitional `RETURN`** via parse-time func-site ids and a return-site
  table keyed by `(block identity, element offset)`.
- **Natives** `+ - * / = != < > <= >= print`, plus `len`/`pick` (read-only
  block access, added for the dialect).
- **RAW trapdoor**: a generator (`make-gen` / `next`) whose body is genuine S1
  machine code (built with the S1 assembler), suspending/resuming by swapping
  S1's actual `SP`/`RP`/`IP`.

## Deviations from R0-ARCHITECTURE.md

1. **The evaluator is host C code, not S1 machine code.** The architecture
   describes the evaluator as "a substrate program `EVAL(block, ctx)`". It is
   implemented as C that walks R0 objects in `M`. Consequence: the control
   frame's `saved_SP` (data-stack baseline) is explicit, but `saved_RP`/
   `saved_IP` are folded into the C call stack. Non-local transfer is realised
   *frame-by-frame* (each frame restores its own `saved_SP` and propagates),
   which is observably equivalent to the architecture's atomic `UNWIND`. This
   is the largest deviation, chosen for clarity and testability.
2. **`return` is arity-1.** R0 is fixed-arity prefix, so a bare `return` is not
   a separate form; "return no value" is written `return none` (yields
   `[NONE,1]`), realising the architecture's "return with no argument" case.
3. **`len` and `pick` natives added.** Any dialect must read a block's length
   and elements. These are *read-only* access, not the mutable-series model the
   architecture excludes.
4. **Truthiness.** There is no distinct `true`/`false`; comparisons yield
   `INT` 0/1, and `if` treats `NONE` and `INT 0` as falsey, everything else as
   truthy.
5. **`substrate [...]` is not a user-facing assembler.** The RAW fragment is
   built in C with the S1 assembler and bound as `make-gen`/`next`; there is a
   generic RAW apply path but no source-level opcode syntax.
6. **A single generator** (fixed state cells), not multiple concurrent
   generators.

## Complete test results (all pass)

| # | Test | Result |
|---|---|---|
| A | no ordinary result (`print 1` → `[0]`) | ok |
| B | one NONE (`none`, empty body) | ok |
| C | `values [10 20]` → two results | ok |
| D | one literal block `[10 20]` | ok |
| E | BREAK crossing unaware functions | ok |
| F | definitional RETURN through an unaware helper | ok |
| G | `return values [7 8 9]` surviving the unwind | ok |
| H | nested loops: BREAK selects only the inner | ok |
| I | RETURN inside a loop targets the enclosing function | ok |
| J | THROW crossing functions unaware of THROW | ok |
| K | user-defined `while` (control abstraction) | ok |
| L | `sum-list` dialect (folds a block as data) | ok |
| M | RAW trapdoor generator yields 1,2,3 then NONE | ok |
| — | recursion (factorial 5 = 120) | ok |
| — | nested closures (lexical capture) | ok |
| — | make-counter (captured-binding mutation → 15,16) | ok |
| — | zero-argument function | ok |
| — | argument cleanup after normal calls | ok |
| — | argument/temporary cleanup after non-local RETURN | ok |
| — | repeated calls leave no stale frames (csp_max stable) | ok |
| — | missing BREAK/THROW targets → controlled errors | ok |
| — | `return` outside function → parse error | ok |
| — | `print` arity 0; `+` one tagged INT result | ok |
| — | `values []` → zero results | ok |

## Bugs found during implementation

1. **`-` misparsed as a unary minus.** `- 5 1` parsed `-` as a negative-number
   literal (`0`), so subtraction was silently wrong and broke factorial.
   Fixed: `-` is a sign only when immediately followed by a digit.
2. **Generator done-sentinel collision.** The NONE sentinel `0x1` equals the
   yielded value `1`, so the first `next` returned NONE. Fixed with a distinct
   `-1` sentinel.
3. **Context capacity vs arity conflated.** The 22-native global context
   overflowed a 16-binding cap. Separated `R0_MAX_BINDINGS` (context capacity)
   from `R0_MAX_ARITY`.

## Architectural weaknesses / awkwardness

- **Suspension of *R0* evaluation is not reachable via the S1 trapdoor.** R0's
  evaluator runs as C, not on S1's machine, so a generator whose *body is R0
  code* cannot be suspended by swapping S1's `SP`/`RP`/`IP`. The generator
  body is therefore S1 substrate code. This is the single clearest place the
  C-evaluator choice diverges from the architecture's "the evaluator is a
  substrate program" framing.
- **`func`/`return` are reader keywords**, so func-site assignment is a
  parse-time special case; the reader must understand `func`'s shape.
- The trapdoor is **trusted unsafe code**; the guard is best-effort (checks the
  generator stack pointer range), not a sandbox.

## Did value/control separation survive?

**Yes.** `break`/`return`/`throw` travel as transfer statuses plus frame-by-frame
`SP`-baseline unwind — never as tagged values. The result protocol `[v..,N]` is
purely data. The func-site ids live in a side table keyed by block identity +
offset; block cells contain only ordinary values. The ordinary-result
distinctions (0 / NONE / one / several / a block) are carried by arity, exactly
as specified.

## Did the trapdoor genuinely extend R0?

**Yes.** Suspension/resume is not expressible in R0's value model or its
one-shot control transfer. The generator demonstrates it through a genuine S1
`SP`/`RP`/`IP` swap, packaged as `make-gen`/`next` and used from ordinary R0
code — a control construct R0 cannot express by itself.

## Did anything demonstrate a need for an eighth S1 primitive?

**No.** Zero S1 primitive changes; zero S1 `HOST` service ids invoked (the R0
natives are C functions equivalent to `HOST_ADD`/`HOST_PRINT`, with the R0
trampoline constructing result sets — the NATIVE/HOST boundary is preserved as
a *separation of concerns*).

## Measurements

- **New C source lines:** 1039 (`r0.h` 127, `r0.c` 676, `r0_tests.c` 236).
- **R0 concepts:** 8 value tag-families (`INT`, `NONE`, `WORD`+subtypes,
  `BLOCK`, `CONTEXT`, `CLOSURE`, `NATIVE`, `RAW`) + the result-arity protocol;
  4 control transfers (`BREAK`, `RETURN`, `THROW`, `CATCH`) + the control
  stack + func-site ids + return-site table (machinery).
- **HOST service ids used:** 0.
- **S1 primitive changes:** 0.
- **Max data-stack depth observed:** 5 cells.
- **Max control-stack depth observed:** 5 frames.
- **Non-local RETURN result-copy cost:** `O(N)` (N ≤ 16) into a fixed buffer.
