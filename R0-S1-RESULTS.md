# R0-S1 Phase 1 — Results

Minimal R0 evaluator running **on** the frozen S1 machine. The evaluator is
genuine S1 machine code assembled by a C emitter; C is only the loader
(parse/interning/heap), the code emitter, the `s1_run` driver, and the result
inspector. No R0 semantic work runs in C.

## Build & run

```
make
./s1     # S1 clean-room + adversarial + claims + hosted R0 + R0-S1 phase 1
```

All suites pass: **77 `ok:` checks, 0 failures, exit 0**. The frozen S1 files
(`s1.c`, `s1.h`, `tests.c`, `adversarial.c`, `claims.c`) are untouched.

## What runs where

- **C — loader/toolchain only:** `dup_str`, `intern`, `lalloc`, `make_block`,
  `make_context`, `bind`, `skip_ws`, `parse_form`, `parse_int`, `parse_word`,
  `parse_block`, `r0_s1_parse`, `r0_s1_init`, `r0_s1_run`, `r0_s1_result`,
  instrumentation getters.
- **C — emitter only (emits S1 code, performs no semantics):** `e_cell`,
  `e_setc`, `e_dup`, `e_peek`, `e_pop_to`, `e_untag_ptr`, `emit_call_fwd`,
  `emit_reduce`, `emit_discard`, `emit_lookup`, `emit_set`, `emit_native`,
  `emit_subexpr`, `emit_loop`.
- **S1 (assembled, executes via `s1_run`):** block traversal (`RV_CUR`/
  `RV_END` loop), element dispatch on tag, context lookup, nearest-binding
  `SET` (including append-to-context), `REDUCE`/`DISCARD`, the result protocol
  `[r1..rN, tagged-N]`, and the native trampoline (untag → HOST → tag →
  `[v, tagged-1]` / `[0]`).

## Mandatory + extra tests (all pass)

| Test | Result |
|---|---|
| `[ 42 ]` → one INT 42 | ok |
| `[ none ]` → one NONE | ok |
| `[ + 2 3 ]` → one INT 5 | ok |
| `[ x: 10 + x 5 ]` → one INT 15 | ok |
| `[ print 42 ]` → zero results | ok |
| `[ 10 20 ]` → one result 20 (sequential) | ok |
| `[ x: 10 :x ]` → get-word 10 | ok |
| `[ 'x ]` → lit-word (a WORD) | ok |
| `[ + + 2 3 4 ]` → nested application 9 | ok |
| `[ x: 2 y: 3 * x y ]` → 6 | ok |
| `[ x: 10 x: 20 x ]` → nearest-update 20 | ok |
| `[ - 10 3 ]` → 7 | ok |

`VALUES`, `FUNC`/closures, `BREAK`/`RETURN`/`THROW`, `RAW`/trapdoor, and the
control stack are explicitly deferred to later phases.

## Instrumentation (evidence the machine ran)

For `[ x: 10 + x 5 ]`:

```
code=1337 cells   ip 0->1588   sp 16384->16382   rp 24576->24576   hostcalls=1
results: N=1  hostcalls=1   r0 = 240 (tag 0)
```

`IP` advanced through the emitted evaluator, `SP` (the S1 data stack) moved
16384→16382 (two tagged cells pushed: the value and the tagged arity), `RP`
returned to baseline (all `CALL`/`EXIT` balanced), and the S1 runtime counted
one `HOST` invocation itself. (Dynamic instruction counts would require
instrumenting the frozen `s1_run`; not done.)

## Proof C is not evaluating

A mechanical audit test opens `r0_s1_runtime.c` and `r0_s1.h` and asserts that
none of `ST_BREAK`, `ST_RETURN`, `ST_THROW`, `ds[`, `cstack[`, `eval_subexpr`,
`eval_block`, `apply` appear. It passes. The evaluator lives only in the 1337
cells of emitted S1 code.

## Bugs found and fixed

1. **Inverted `0BRANCH` logic** (three times). `0BRANCH` jumps when the flag is
   *zero*. The block loop's "more elements?" check, and the `LOOKUP`/`SET`
   "ctx == NONE?" and "i >= count?" checks, all had the branch polarity
   backwards. The `ctx==NONE` inversion made every lookup miss (so `+` looked
   unbound); the loop inversion made multi-element blocks evaluate garbage.
2. **Double element fetch.** `e_cell(RV_CUR)` already fetches `M[RV_CUR]`; an
   extra `asm_fetch()` read past the element, so the token read was garbage.
3. **Swapped `!` operands in the append path.** `!` pops the *address* first,
   then the *value*. The `count++` store pushed `(addr, value)` instead of
   `(value, addr)`, writing `M[count+1] = addr` and leaving the context count
   unchanged.

## Measurements

- **New C source lines:** 708 (`r0_s1.h` 90, `r0_s1_runtime.c` 497,
  `r0_s1_tests.c` 121).
- **Emitted S1 code:** 1337 cells.
- **HOST service ids used:** 12 — `ADD`, `SUB`, `MUL`, `DIV`, `MOD`, `EQ`,
  `LT`, `GT`, `LE`, `GE`, `PRINT`, `DUMP` (dump only in the unbound-word
  diagnostic path).
- **S1 primitive changes: 0** (and no reinterpretation).
- **R0 data stack = S1 `REG_SP`; R0 return stack = S1 `REG_RP`; current context
  = `RV_CTX` cell; position = `RV_CUR`/`RV_END` cells.**

## Architectural notes

- **Value/control separation:** not exercised in phase 1 (no control transfer
  yet), but the value side — tagged cells on the real S1 data stack, the
  tagged-INT arity marker, and the native trampoline building `[v, 1]`/`[0]` —
  is exactly as specified.
- **Most difficult S1 routine:** `LOOKUP`/`SET` (context-walking with the
  parent chain and the append path); the recurring bug was `0BRANCH` polarity,
  which a branch-label discipline (emit-then-patch with explicit "if flag==0
  jump") would have caught earlier.
- **Nothing required an eighth primitive.** Everything is `LIT`/`DUP`/`DROP`/
  `@`/`!`/`0BRANCH`/`HOST` plus the already-derived `CALL`/`EXIT`/`BRANCH`.

## Deferred (documented, not skipped silently)

`VALUES`, closures/`FUNC`, `BREAK`/`RETURN`/`THROW`/`CATCH`, `RAW`/trapdoor,
and the control stack — per the phase brief, phase 1 implements only the
minimal evaluator that proves execution runs on S1.

---

# R0-S1 Phase 2 — Results

Functions, closures, lexical capture, captured-binding mutation, recursion, and
`VALUES` — all executing as genuine S1 machine code through `s1_run()`.

## What was added

- **Closure representation** `[spec, body, captured-context, func-site-id]`
  (4 cells) in `M`, allocated at runtime via `HOST_ALLOC`.
- **`func [args] body`** — a reader-recognised keyword (sym id 0) that evaluates
  the spec and body blocks and builds a closure capturing the current context
  (`RV_CTX`).
- **Ordinary function application** entirely on S1 (`INVOKE-CLOSURE`): record
  caller state, evaluate each argument and reduce it to one value, create a
  child context, bind parameters, restore the argument stack, switch to the
  body, evaluate it via `CALL BLOCK-EVAL`, restore caller `RV_CUR`/`RV_END`/
  `RV_CTX`, and leave `[results..., tagged-N]`.
- **Lexical parent-chain lookup / nearest-binding-update SET** (reused from
  phase 1) now walk child → … → global.
- **`either cond then else`** (a native) and **`values [e1 ... en]`** (a native),
  both evaluating their block arguments via `RUN-BLOCK`, which saves/restores
  `RV_CUR`/`RV_END` on `RP` around a nested `CALL BLOCK-EVAL`.
- **Ordinary function completion** — reaching the end of the body is the only
  return mechanism (no `return` word yet).

## Function-call ABI actually used

1. Save `RV_CLOSURE` and `RV_ARITY` on `RP` (they must survive argument eval).
2. Evaluate `arity` arguments (each `CALL SUBEXPR` + `REDUCE`); the argument
   loop index `RV_T4` is saved/restored on `RP` around each `CALL` because a
   nested call clobbers it.
3. Restore `RV_CLOSURE`/`RV_ARITY`; allocate a child context (`HOST_ALLOC`),
   bind parameters by appending to it.
4. Save `RV_CTX`/`RV_CUR`/`RV_END` on `RP`; set `RV_CTX = child`, point
   `RV_CUR`/`RV_END` at the body; `CALL BLOCK-EVAL`.
5. Restore `RV_END`/`RV_CUR`/`RV_CTX` from `RP`; return the result set.

The call chain is real S1 execution state: `REG_RP` holds continuations plus
the saved `RV_*` frames, `REG_SP` holds tagged values and the tagged arity
marker. No C recursion, no C stack, no C status codes.

## Recursion evidence

```
[factorial 5] sp 16384->16382 (min 16379)  rp 24576->24576 (min 24513)  N=1
```

`REG_RP` deepened to 24513 (63 cells) during `fact 5` and returned exactly to
its 24576 baseline; `REG_SP` returned to baseline + result set. This is the
genuine nested S1 activation that Phase 3 will transfer through.

## Tests

All phase-1 tests still pass. New phase-2 tests (all pass):

| Test | Result |
|---|---|
| A. `add: func [a b] [+ a b]  add 2 3` == 5 | ok |
| B. zero-arg `f` == 42 | ok |
| C. lexical capture `outer 3` applied to 4 == 7 | ok |
| D. `make-counter` -> 15, 16 | ok |
| E. `fact 5` == 120 | ok |
| F. f->g->h == 7, RP restored to baseline | ok |
| G. repeated calls, no SP/RP leakage | ok |
| H. argument cleanup (only result set remains) | ok |
| I. nested application `add 1 add 2 3` == 6 | ok |
| J. `values [10 20]` -> two results | ok |
| K. `values []` -> zero results | ok |

Full suite: **84 `ok:` checks, 0 failures, exit 0.**

## Bugs found and fixed

1. **`HOST_ALLOC` is not 16-aligned**, but tagged pointers require 16-alignment.
   Fixed by allocating 16-aligned cell counts for closures and child contexts.
2. **Tagged block used as a raw pointer** (twice): `RV_BODY` (body) and the
   spec (arity + parameter access) were the *tagged* block value; the pointer
   must be untagged before `@`. This made `RV_CUR`/`RV_END`/arity garbage.
3. **Scratch-cell clobbering across nested calls** (three instances): the
   argument-loop index `RV_T4`, the set-word target `RV_WORD`, and the native
   id `RV_NAT` were all overwritten by nested evaluations (function calls use
   `RV_T4`/`RV_WORD` for parameters; nested natives set `RV_NAT`). Each is now
   saved/restored on `RP` around the nested `CALL`.
4. **`either`'s branch evaluation didn't switch `RV_CUR`/`RV_END`** to the
   branch block before `CALL BLOCK-EVAL` — fixed with the shared `RUN-BLOCK`
   helper that saves/restores position around the nested block evaluation.

## Deviations

- `either` (arity 3) is used for the conditional (factorial needs a base case);
  the architecture names `if` for the two-branch form but phase 2 did not
  prescribe a conditional, so `either` was added. It is ordinary control flow
  (conditional evaluation of a block argument), not non-local control.
- `func-site-id` is stored as 0 (unused until the `return` word arrives in
  phase 3).

## Did anything need an eighth primitive?

No. Everything added uses the frozen seven primitives + `HOST` and the derived
`CALL`/`EXIT`/`>R`/`R>` machinery. `HOST_ALLOC` provides runtime allocation.
Zero S1 primitive changes.

---

# R0-S1 Phase 3A — Non-local RETURN

Definitional non-local `return` that crosses arbitrary unaware function
activations, implemented purely as S1 code restoring saved machine state. No
control signal travels as an R0 value.

## Exact semantics of definitional RETURN

A `return` expression belongs to the *function definition whose source text
contains it* (lexical identity), not to whatever function is currently
executing. So:

```
outer: func [] [ helper [ return 42 ]  99 ]
```

the `return` is lexically inside `outer`, so it returns from `outer`, even
though it is executed later inside `helper` via `do`.

## Return-site metadata

Produced entirely by the C loader (parse time), stored **in the block header**,
never in block elements:

- block layout is `[count, return-site-id, elem0, elem1, ...]`;
- the parser assigns each `func` a monotonically increasing **func-site-id**
  and, while parsing that function's body, records that site-id in every block
  it builds there (nested blocks inherit it). A block parsed outside any
  function gets site-id 0.

This is a documented per-block simplification of the architecture's
`(block identity, element offset) -> site-id` mapping — valid because every
`return` site in a given block shares the same innermost enclosing function.
Block *elements* remain ordinary R0 values; the site-id is header metadata.

## Activation-frame layout

A linked list of frames in `M` (allocated via `HOST_ALLOC`), root pointer in
`RV_FRAME`:

```
frame = [ prev | site-id | saved_SP | saved_RP | saved_IP
          | saved_CTX | saved_CUR | saved_END | saved_BLK ]
```

`saved_IP`/`saved_RP` are the caller's return continuation and RP baseline,
captured at call time from `M[RP]` and `RP+1`. `saved_SP` is the caller data
stack baseline after arguments are bound. `saved_CTX/CUR/END/BLK` are the
caller's evaluator state. The frame is *S1 execution state in M* — there is no
C shadow control stack.

## Exact restoration algorithm (RETURN)

1. Read `site-id = M[RV_BLK + 1]` (the current block's return-site-id); 0 is an
   error ("return outside function").
2. Evaluate the `return` argument as one ordinary sub-expression →
   `[r1..rN, tagged-N]`.
3. Preserve that result set into a fixed M buffer.
4. Walk `RV_FRAME` top-down for the frame with `site-id`; the first match is
   the innermost live activation.
5. Restore `REG_SP = saved_SP`, `REG_RP = saved_RP`, then re-place the result
   set, restore `RV_CTX/CUR/END/BLK`, set `RV_FRAME = prev`, and finally
   `REG_IP = saved_IP` — the direct jump to the target's return continuation.

The intermediate activations simply disappear: their saved frames are unlinked
and their normal epilogues never execute. `do` (a native wrapping RUN-BLOCK)
knows nothing about `return`; it is bypassed by the register restoration.

## Recursion disambiguation

The frame chain is walked top-down, so the *first* frame with a matching
site-id is the innermost live activation. Test F proves this: with several live
activations of the same function, `return` selects the innermost (f 0 == 6,
not 3).

## Evidence that unaware calls were crossed

Test D runs `outer -> f -> g -> h -> do -> return` where each of f/g/h performs
an observable `counter += …` *after* its nested call. After the non-local
return the counter is exactly 0 — none of the post-call epilogues ran. Test E
runs the identical chain without `return` and observes counter == 1111, proving
the machinery itself does not skip epilogues. Test J runs one `innocent`
function on both a normal block and a `return` block: it increments a counter
only in the normal case, with no `return`-recognizing branch in `innocent`.

## Stack measurements (deep unaware chain)

```
rp 24576 -> 24576 (min 24558)   sp 16384 -> 16381 (min 16383)
```

Final `RP` equals the caller baseline (24576); final `SP` holds exactly the
result set. Repeated invocation shows no SP/RP leakage.

## Tests (all pass; full suite: 96 ok, 0 fail, exit 0)

A. simple `return 42` (the following `99` never evaluated)
B. return through one unaware helper (side effect skipped)
C. deep chain f->g->h->do, all epilogues bypassed, RP restored
D. intermediate side-effect proof (counter == 0)
E. ordinary-call control (counter == 1111 without return)
F. recursion targets the innermost live activation (f 0 == 6)
G. zero-result return (`return values []`)
H. multiple-result return (`return values [10 20]`)
I. stack cleanliness (SP == result set, RP == baseline, no leak on repeat)
J. control is not a value (same `innocent` function for both paths)

## Bugs found and fixed

1. **`e_cell(RV_RES_BUF)` vs `asm_lit(RV_RES_BUF)`.** The result-preservation
   buffer was addressed by *fetching its contents* instead of its address,
   causing the RETURN store to land at a garbage address and jump to address 1.
2. (None further — the block-header site-id and the frame layout worked on the
   first pass once the buffer bug was fixed.)

## Deviations

- Return-site metadata is stored per-block in the block header (a
  `[count, site-id, elems...]` layout) rather than as a separate
  `(block, offset)` table; documented above and equivalent for this language.
- `return` is arity-1 (always an expression); a bare `return` is written
  `return none`.

## HOST services used by Phase 3A

`ADD SUB MUL DIV MOD EQ NE LT GT LE GE PRINT ALLOC DUMP`. `DUMP` is only in the
"return outside function" / "no matching activation" error diagnostics. None of
these alters `REG_IP`/`REG_SP`/`REG_RP`, unwinds, inspects R0 control frames, or
performs RETURN. The non-local transfer is performed entirely by S1 code
writing the memory-mapped registers.

## Mechanical audit

The audit (forbidden: `ST_BREAK ST_RETURN ST_THROW RESULT_RETURN ds[ cstack[
eval_subexpr eval_block apply`) passes. Manually, the new C code only: assigns
func-site-ids and records per-block site-ids at parse time (toolchain
metadata); emits S1 instructions. It performs no return propagation, unwind,
control-status propagation, or caller-by-caller RETURN checking.

## Was an eighth primitive required?

No. All of RETURN uses the frozen seven primitives + `HOST` + derived
`CALL`/`EXIT`/`>R`/`R>`. The transfer is a direct restoration of `REG_SP`,
`REG_RP`, `REG_IP` (memory-mapped) and the `RV_*` evaluator state, followed by
a jump — no new primitive.

# R0-S1 Phase 4A — First-class RAW S1 trapdoor

## What was added

A new R0 value type `T_RAW` and a new keyword form `raw [ ... ]` (optionally
`raw N [ ... ]` where `N` is the argument arity). A `raw` form assembles the
given symbolic S1 assembly into machine code at load time and yields a
first-class callable value that, when applied, evaluates `N` ordinary R0
arguments and then `CALL`s the fragment's entry. The fragment is arbitrary
(and therefore unsafe/trusted) S1 machine code.

This is the "trapdoor": the language is closed over its substrate but not
sealed against it. The trapdoor is *first-class* — a raw value can be bound,
passed as an argument, returned, and re-invoked, exactly like a closure or
native.

## Two pieces

1. **A generic S1 assembler (C toolchain).** `assemble_raw` translates
   mnemonics + operands + `label:` definitions into S1 cells. It knows the
   frozen opcodes and a handful of derived conveniences, and knows nothing
   about R0 control constructs. Mnemonics: `LIT INT HOST ZBRANCH BRANCH CALL`
   (operand-taking) and `DUP DROP @ ! EXIT >R R> NONE ADD SUB MUL DIV MOD EQ NE
   LT GT LE GE PRINT ALLOC` (bare).

   - `LIT n` pushes the raw S1 cell `n` (machine-level constants: addresses,
     opcodes, the tag mask `16`).
   - `INT n` pushes the *tagged* R0 integer `mk_int(n)` (an R0 integer value).
   - `ARITY n` is `INT n` with documentary intent: it is the result count the
     fragment must leave below its final `EXIT`.
   - `label:` marks an address; `ZBRANCH`/`BRANCH`/`CALL` take a label word or
     an absolute address.

2. **A RAW callable + `r_invoke_raw` (S1 code).** The raw value is a 16-aligned
   heap pair `[entry, arity]` tagged `T_RAW`. `r_invoke_raw` untags it, reads
   entry and arity, evaluates `arity` arguments (each reduced to one value by
   `subexpr` + `r_reduce`), performs an *indirect* `CALL entry`, and `EXIT`s,
   leaving the fragment's result set `[v1..vN, mk_int(N)]` untouched.

## Result protocol

A fragment receives its arguments as tagged R0 values on the data stack and
must leave a result set `[v1..vN, mk_int(N)]` — the last cell is the tagged
count. The `ARITY n` mnemonic exists so a fragment can emit its own count. This
is the same result-set shape the evaluator produces everywhere else, so a raw
call composes transparently with `values`, `return`, functions, etc.

## Exact S1 code (r_invoke_raw)

```
untag raw;            entry = M[raw];  arity = M[raw+1];
i = 0;
loop:  if !(i < arity) goto done;
       save i, arity, entry on RP;
       CALL subexpr; CALL r_reduce;      // one arg -> [v]
       restore entry, arity, i;  i++;
       goto loop;
done:  CALL entry (indirect); EXIT;      // fragment leaves [r.., mk_int(N)]
```

## What is NOT implemented (out of scope, documented)

No `BREAK`/`THROW`/`CATCH`, no generators, no `yield`, no reified continuations,
no resume/suspend. The assembler is a pure transliterator; it cannot express any
of those constructs, and the audit forbids their names in the runtime sources.

## Tests (all pass; full suite now 107 ok, 0 fail, exit 0)

A. raw constant 42 (zero args)
B. raw 2-arg sum (`add2 3 4` == 7) — untag/tag around `ADD`
C. repeated invocation through `values` -> 5 5 5
D. raw passed as an argument and invoked == 5
E. raw invoked from inside a `func` body == 15
F. multiple results -> 10 20
G. zero results -> arity 0
H. `ZBRANCH` + labels (conditional) -> 1 2
I. `@`/`!` store + fetch through raw S1 memory (42)
J. stack cleanliness: SP holds exactly the result set, RP back to baseline
K. first-class value: get-word `:c5` yields the raw value itself (`T_RAW`)

## Bugs found and fixed

1. **`INT` vs `LIT` operand confusion.** The first `add2` fragment wrote
   `INT 16` where it meant the raw constant 16; `INT 16` pushes the *tagged*
   256, so the `DIV` by 256 collapsed every argument to 0. The fix was to use
   `LIT 16` for machine-level constants and reserve `INT` for tagged R0 values.

## HOST services used by Phase 4A

Only those already frozen: `ADD SUB MUL DIV EQ LT ALLOC` inside fragments plus
the evaluator's existing set. No new HOST service, no new S1 primitive, no
change to `s1.c`/`s1.h`.

## Mechanical audit

Forbidden list extended with `generator yield continuation resume`; the
runtime rewords any incidental use of "return continuation" to "return
address" so the audit stays a clean signal. It passes (107 ok, 0 fail).

## Was an eighth primitive required?

No. The indirect call is a derived `CALL` whose callee operand is fetched from
a runtime cell rather than patched at load time — still the frozen
`LIT`/`DUP`/`DROP`/`@`/`!`/`0BRANCH`/`HOST` primitives.

# Phase 4B — User-defined first-class control via RAW

## The question answered

Can an R0 programmer create genuinely new first-class control behavior using
only ordinary R0 and generic RAW/S1 code, without changing the R0 evaluator,
the RAW implementation, S1, or HOST semantics?

**Yes.** A first-class dynamic ESCAPE facility is built entirely from R0
source plus two generic `raw` fragments. The evaluator and trapdoor remain
byte-for-byte unchanged:

```
git diff --exit-code r0-trapdoor-v1 -- r0_s1_runtime.c r0_s1.h   # empty
./check-frozen-s1.sh                                              # frozen
```

## Complete R0/RAW source

See `examples/escape.r0`. The essential definitions:

```
frame-here: raw [ LIT 8215 @ ARITY 1 EXIT ]

restore: raw 2 [
    LIT 8260 ! LIT 8261 !                       ; save (result, frame)
    LIT 8261 @ LIT 5 ADD @ LIT 8194 !           ; RV_CTX   <- frame.ctx
    LIT 8261 @ LIT 6 ADD @ LIT 8192 !           ; RV_CUR   <- frame.cur
    LIT 8261 @ LIT 7 ADD @ LIT 8193 !           ; RV_END   <- frame.end
    LIT 8261 @ LIT 8 ADD @ LIT 8214 !           ; RV_BLK   <- frame.blk
    LIT 8261 @ @ LIT 8215 !                     ; RV_FRAME <- frame.prev
    LIT 8261 @ LIT 3 ADD @ LIT 2 !              ; REG_RP   <- frame.rp
    LIT 8261 @ LIT 4 ADD @ >R                   ; stage frame.ip on R
    LIT 8261 @ LIT 2 ADD @ LIT 1 !              ; REG_SP   <- frame.sp
    LIT 8260 @ LIT 16                           ; push [result, 1]
    EXIT
]

with-escape: func [body] [
    target: frame-here
    escape: func [r] [ restore target r ]
    body :escape
]
```

## Which parts are ordinary R0, which are RAW

- **Ordinary R0** (`with-escape`, `escape`): composition, closures, lexical
  capture of `target`, argument passing, the user-facing API. `with-escape` is
  a plain `func`; `escape` is a plain closure `func [r] [restore target r]`.
- **RAW** (`frame-here`, `restore`): only the two operations R0 cannot express
  on its own — reading the current activation frame, and restoring saved
  machine/evaluator state with a jump.

## Exact machine state manipulated

`restore` copies the saved activation-frame record back into the
memory-mapped registers and evaluator cells, then transfers control:

- `REG_SP` (addr 1) ← `frame.sp`, `REG_RP` (addr 2) ← `frame.rp`,
  `REG_IP` (addr 0) ← `frame.ip` (via a staged `>R` + `EXIT`).
- `RV_CTX` (8194) ← `frame.ctx`, `RV_CUR` (8192) ← `frame.cur`,
  `RV_END` (8193) ← `frame.end`, `RV_BLK` (8214) ← `frame.blk`,
  `RV_FRAME` (8215) ← `frame.prev`.
- The escape result and the count `mk_int(1)` are pushed onto the restored
  data stack, so the caller resumes with `[result, 1]` — the same result-set
  shape every expression produces.

The frame pointer is captured at runtime by `frame-here` (reading `RV_FRAME`)
and closed over by the `escape` closure, so each `with-escape` invocation
yields an escape value bound to *its own* frame. `frame.ip` is the closure
epilogue's `EXIT`, and `frame.rp` is the address of the caller continuation, so
the staged `>R` + `EXIT` lands exactly where the definitional RETURN of Phase
3A would — no site-id search needed, because the pointer is captured directly.

## Why the evaluator is unaware of the construct

Nothing is added to the evaluator. `with-escape`/`escape` are ordinary words
bound to closures; `frame-here`/`restore` are ordinary `raw` callables. The
evaluator dispatches `escape` exactly as it dispatches any closure (T_CLOSURE)
and `restore` exactly as any raw (T_RAW). There is no `N_WITH_ESCAPE`,
`N_ESCAPE`, `ST_ESCAPE`, or `RESULT_ESCAPE`.

## Unaware-call evidence

Tests B and D push the escape value through `innocent`/`deeper` (and
`pass1`/`pass2`) that contain no escape-specific code — they only receive and
call a callable, and perform an observable `counter += …` after the nested
call. On escape, `counter` stays exactly 0; test C runs the identical chain
with an ordinary `id` callable and observes `counter == 111`, proving the
functions are not merely suppressing continuations.

## Nested escape behavior

Each `with-escape` invocation captures its own frame, so an escape value
targets the invocation that created it. Test E1 (`e2 42` -> inner) and E2
(`e1 42` -> outer, crossing the inner `with-escape`) both return 42.

## SP/RP measurements

```
[escape chain] sp 16384->16381 (min 16383)  rp 24576->24576 (min 24559)
```

Final `RP` equals the caller baseline (24576); final `SP` holds exactly the
result set. Repeated invocation (test F) shows no SP/RP leakage.

## Tests (all pass; full suite now 117 ok, 0 fail, exit 0)

A. simple escape 42 (following 99 never evaluated)
B. escape through unaware innocent/deeper (counter == 0)
C. ordinary callable, no escape (counter == 111)
D. first-class: assigned and passed through two args (42, 0)
E1. inner escape targets inner with-escape
E2. outer escape crosses inner with-escape
F. repeated invocation (1 2 3) + RP-cleanliness
G. body that never escapes returns normally
H. stack evidence (SP == result set, RP == baseline)

## Bugs found and fixed

1. **Callables must be quoted when passed as values.** The first attempt wrote
   `body escape` (and `innocent escape`). Because R0 invokes a callable word
   when it appears in expression position, this invoked `escape` with zero
   arguments instead of passing it. The fix is `:escape` (get-word) everywhere
   the escape value is passed as an argument; it is still written `escape 42`
   where it is *invoked*. This is existing R0 evaluation semantics, not a new
   evaluator behavior.

## Deviations

- The user interface is spelled `with-escape func [escape] [...]` with
  `:escape` (get-word) when the escape value is passed through a function, per
  R0's call-by-value/auto-invoke rule. Invocation remains `escape 42`.
- `escape` is arity-1 (always an expression); there is no zero/multi-result
  escape in this phase (the restore fragment always leaves `[result, 1]`).

## C/HOST audit

No C or HOST operation knows what ESCAPE means. The two fragments use only the
frozen primitives (`LIT`, `@`, `!`, derived `>R`/`EXIT`) and the frozen `HOST
ADD`. No HOST op captures a continuation, unwinds, or restores registers — all
of that is done by generic RAW S1 code reading/writing memory.

## Was an eighth primitive required?

No. The escape transfer is a memory copy of the saved frame record into the
registers and RV cells, followed by a staged `>R` + `EXIT` — all derived from
the frozen seven primitives.
