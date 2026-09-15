# R0 — Architecture Proposal

A very small REBOL-like language above the frozen S1 substrate.

**Status:** proposal only. No implementation exists yet.

**Frozen constraint:** S1 is permanently frozen. Its irreducible primitives are
`LIT`, `DUP`, `DROP`, `@`, `!`, `0BRANCH`, `HOST` (plus `OP_HALT` as a harness
stop). R0 must not modify, reinterpret, extend, or add to this set. Everything
R0 does is built *above* S1, as substrate programs and data conventions over
S1's flat cell memory.

**Standing on prior results.** The clean-room and adversarial phases
established two load-bearing facts that R0 inherits:

1. The three machine registers `IP`/`SP`/`RP` are **memory-mapped** at fixed
   cells (0/1/2), so the ordinary `@`/`!` primitives already read and write
   execution state.
2. Control transfer is a **register jump**, not a value: the clean-room tests
   showed `break`/non-local `return` restoring a saved `{RP, IP}` and `throw`
   restoring `{SP, RP, IP}`. R0 generalises this by recording a data-stack
   **baseline** in every control frame so these transfers unwind soundly
   (§8). The load-bearing fact is unchanged: no control effect travels as a
   value on the data stack.

R0's design rule is therefore not a discovery but a commitment: **ordinary
values and result arity live on the data stack as tagged values; control
effects live in `IP`/`SP`/`RP` and a control stack, and are never R0 values.**

---

## Research question restated

Can something recognisably REBOL-like be implemented above S1 while keeping

- **ordinary values / result arity** (0, one NONE, one value, several values)

separate from

- **control effects / execution transfer** (BREAK, RETURN, THROW, suspension)

without changing the machine? The answer this proposal commits to is *yes*,
and the rest of this document is the precise account of how.

---

## 1. R0 value representation in S1 memory

An R0 value is a single S1 cell (`intptr_t` — a machine word). The cell is
**tagged**: the low 4 bits are a tag, the remaining bits are a payload.

```
value  =  (payload << 4) | tag          for payload kinds
         or
         (pointer)       | tag          for heap kinds (16-aligned pointers)
```

4-bit tagging was chosen (over 3-bit) for headroom, and — critically — it is
implementable with **only the mundane HOST arithmetic already in S1**
(`MUL`, `DIV`, `MOD`, `ADD`, `SUB`):

| tag operation | S1 equivalent |
|---|---|
| tag = `v mod 16` | `MOD 16` |
| untag int = `v / 16` | `DIV 16` |
| tag int = `n * 16` | `MUL 16` |
| pointer `p \| t` (p 16-aligned) | `ADD t` |
| untag pointer | `SUB (p mod 16)` |

No bitwise HOST operation, and no new primitive, is required. This is a
deliberate result worth recording: **the entire value discipline rides on
arithmetic S1 already has.**

## 2. How values are distinguished

The tag is the only distinction. The evaluator (itself substrate code) applies
a fixed set of *type predicates* — `IS-INT`, `IS-NONE`, `IS-WORD`,
`IS-BLOCK`, `IS-CONTEXT`, `IS-CLOSURE`, `IS-NATIVE`, `IS-RAW` — each a short
`MOD`+`EQ`/`LT` sequence. Tagging and untagging happen only through checked
runtime words; raw substrate code (the trapdoor) may violate this, and that is
the documented cost of the trapdoor (§14).

## 3. Exact representation of each type

Tag bits (low 4):

| tag | type | payload |
|---|---|---|
| `0` | `INT` | signed value `<< 4` (small ints only) |
| `1` | `NONE` | none (the unique atom `0x1`) |
| `2` | `WORD` | interned symbol id `<< 4` |
| `3` | `SET-WORD` | symbol id `<< 4` (assignment target `x:`) |
| `4` | `GET-WORD` | symbol id `<< 4` (force value `:x`) |
| `5` | `LIT-WORD` | symbol id `<< 4` (quote `'x`) |
| `6` | `BLOCK` | 16-aligned heap pointer |
| `7` | `CONTEXT` | 16-aligned heap pointer |
| `8` | `CLOSURE` | 16-aligned heap pointer |
| `9` | `NATIVE` | host-function id `<< 4` |
| `10` | `RAW` | 16-aligned heap pointer to an assembled S1 fragment |
| `11..15` | reserved | (future: TEXT, ERROR, …) |

Word subtypes (`SET-`, `GET-`, `LIT-`) are **not** new value concepts; they
are the single `WORD` concept with three syntactic modes, distinguished by tag
for the parser/evaluator's convenience. They exist so that assignment,
first-class function values, and quoting need no separate machinery.

### NONE

The constant `0x1` (tag 1, no payload). Unique. Never produced by arithmetic,
never equal to any integer. `IS-NONE` is `v == 0x1`.

### word

`(sym_id << 4) | tag`. `sym_id` indexes a global **interner** — a table
(itself a block in memory) mapping `sym_id → [length, char-code...]` for
`mold`/debug only. Two words are equal iff their `sym_id`s are equal
(interning gives identity). The spelling is not a value; it is metadata.

### block

A heap allocation:

```
cell[0] = element count K
cell[1..K] = K tagged values
```

Immutable by convention (there is no series mutation in R0; a "changed" block
is a new allocation). A block is simultaneously *data* and *code* — homoiconic
by convention, exactly as in S1 where "code vs data" is just "what `IP` points
at". No VM-level distinction exists.

### context

A heap allocation:

```
cell[0] = parent CONTEXT value (or NONE)
cell[1] = binding count K
cell[2..2+2K-1] = (word_i, value_i) pairs, in insertion order
```

A parent-linked association list. Lookup walks the chain; the root context
chains to `NONE`. This is the entire environment story — there is no
environment *register* in S1, only a value the evaluator carries (§5).

### function / closure

A heap allocation:

```
cell[0] = spec BLOCK   (parameter words + literal doc bits, if any)
cell[1] = body BLOCK
cell[2] = captured CONTEXT   (lexical environment at creation time)
```

A function is `(spec, body, captured-context)`. It is first-class: a CLOSURE
value, which can be passed, returned, and stored. Calling it creates a fresh
child context whose parent is the **captured** context — this is lexical
scoping (§5).

### native

`(host_id << 4) | 9`. Represents a *mundane* primitive implemented directly as
an S1 `HOST` escape (integer arithmetic, comparison, allocation, textual I/O,
diagnostics). The evaluator applies a NATIVE by dispatching on `host_id`
through a trampoline. A NATIVE **must not** implement evaluation, binding, call
semantics, or any control transfer (see the result boundary below). Code that
needs to touch control/execution state is a `RAW` fragment, never a NATIVE (§14).

### raw (trusted unsafe)

`(ptr) | 10`. A 16-aligned heap allocation holding an *assembled S1 fragment*
— a flat run of raw cells (opcodes and operands), **not** tagged R0 values. A
`RAW` value is the only R0 value whose content may manipulate `IP`/`SP`/`RP`
and the control stack directly. It is deliberately separated from `NATIVE` so
that a mundane primitive can never be mistaken for code with control powers. It
is **trusted, unsafe code**, not a security boundary (§14).

### The NATIVE/HOST result boundary

`HOST` services are mundane and know nothing of R0's result protocol: `HOST_ADD`
pops two raw cells and pushes one; `HOST_PRINT` pops one and pushes none. R0's
**NATIVE trampoline** bridges the gap. For each NATIVE it (a) untags the R0
argument values to raw cells, (b) invokes the host, and (c) reconstructs the R0
result protocol from the native's *fixed* result arity: `HOST_ADD` →
`[value, 1]`; `HOST_PRINT` → `[0]`. Language-level arity, `NONE`, and
multi-return therefore live entirely above S1/HOST: the host never produces a
`[values..., N]` frame and the evaluator never asks it to.

---

## 4. The result protocol

The single most important convention in R0. It must distinguish these five
cases unambiguously:

- no result
- one result: NONE
- one result: 42
- two results: 10 20
- one block containing `[10 20]`

**The convention.** When any R0 expression (a function call, a block, a
literal) finishes evaluating *normally*, the S1 data stack holds, bottom to
top:

```
[ r1  r2  ...  rN  |  N ]
                       ^ top of stack
```

`r1..rN` are the N ordinary result values (tagged); the top cell `N` is the
**arity**, an ordinary tagged `INT`. "No result" is `N = 0` (the stack holds
only `[0]`).

The five cases are therefore:

| case | data stack (bottom→top) |
|---|---|
| no result | `[ 0 ]` |
| one result: NONE | `[ NONE, 1 ]` |
| one result: 42 | `[ 42, 1 ]` |
| two results: 10 20 | `[ 10, 20, 2 ]` |
| one block `[10 20]` | `[ <BLOCK>, 1 ]` |

Arity and NONE are separate because the *count* (top cell) is not a value being
returned; it is the protocol's arity marker. "One result whose value is NONE"
is `[NONE, 1]`; "no result" is `[0]`. No value is overloaded with the meaning
"absent".

Multiple ordinary results (`N > 1`) are produced **only** by the `values`
construct (§7), never by juxtaposition. A literal block `[10 20]` is a *single*
`BLOCK` value (`[<BLOCK>, 1]`); `values [10 20]` splices its contents into
*two* results (`[10, 20, 2]`). Sequential evaluation is therefore unambiguous:
only `values` can widen a result set.

**Checkability.** The evaluator's single *consume* operation is uniform:

```
consume:  N = pop();  for i in 1..N: r_i = pop()
```

Because `N` is always on top and always an INT, the evaluator can — and does —
check it before using results (e.g. "this call must yield exactly one value
before I bind it", "this argument must be one value"). A producer that lies
about its arity is a *bug the R0 layer detects by convention*, not something S1
detects; S1 has no arity, so R0 supplies the arity *and* the check.

**Reentrancy.** The count travels *on the stack*, so nested calls do not share
state: each inner evaluation leaves its own `[values..., N]`, which the outer
evaluator consumes before continuing. This is the property the adversarial
phase demonstrated (a global "shape" cell is non-reentrant; a count on the
stack is). R0 chooses the reentrant form.

**Decay rule.** A single-value consumer that receives `N > 1` takes the first
value and discards the rest (REBOL's `x: multi-return` behaviour). A consumer
that receives `N = 0` where it needs one value treats the value as `NONE`
unless it is an arity-checked context. These are documented policies of the
evaluator, not new value types.

---

## 5. Contexts and binding

- A context is a parent-linked association list (§3).
- Binding is **lexical**: a closure captures the context in which its
  `func`/`function` literal was evaluated; calls extend the *captured* context,
  not the caller's dynamic context.
- The evaluator keeps the **current context** as evaluation state. It is
  carried on the return stack alongside return addresses (one cell per active
  call frame), saved/restored by the call machinery — the same mechanism that
  already threads return addresses, so no new register or primitive is needed.
- Lookup: walk the context; on miss, follow the parent link; the root chains to
  `NONE`; an unbound word is an R0 **error** (a THROW of a diagnostic), not a
  value.
- **Assignment (`x: expr` → `set x expr`)** uses *nearest-binding update*:
  `set` walks the lexical chain (current context → parents → root) for an
  existing binding of `x`; if one exists it is updated **in place**; only when
  none exists does `set` create a fresh binding in the current (innermost)
  context. This is what makes lexical closure mutation work — an inner closure
  that assigns to a captured name mutates the captured binding rather than
  shadowing it (see `make-counter` in §12). Fresh bindings are otherwise
  created only by parameter binding at call time.

## 6. Functions: representation and invocation

A function is a CLOSURE `(spec, body, captured-context)`. Invocation is the
evaluator's `apply` substrate word (not a primitive). Its precise ABI:

**The normal-call ABI** (also used, tail-for-tail, by non-local `RETURN`):

1. **Record `caller_SP`** = the current `SP` (the caller's data-stack depth).
2. **Evaluate arguments.** Each argument sub-expression is evaluated (in value
   position) to one value; the values are pushed, in order, onto the data stack
   as *temporaries* above `caller_SP`.
3. **Bind.** Allocate a child context (parent = captured context), check
   spec/arg arity (a mismatch is an R0 error), and *copy* the argument values
   from the data stack into the child context, bound to the spec words.
4. **Restore `SP := caller_SP`.** The argument temporaries are discarded; the
   data stack is exactly as the caller left it.
5. **Install the function frame and switch context.** Push a `kind=1` control
   frame recording `{ id, saved_SP = caller_SP, saved_RP = current RP,
   saved_IP = return continuation }`; save the caller's current-context cell on
   the return stack and set it to the child (§5).
6. **Evaluate the body** in the child context. The body leaves its final result
   set `[r1..rN, N]` on the data stack (body temporaries below it).
7. **Preserve the result set** `[r1..rN, N]` into a buffer.
8. **Restore caller state.** `SP := caller_SP`; `RP := saved_RP`;
   `IP := saved_IP`; pop the function frame; restore the caller's context cell.
9. **Place the result set** `[r1..rN, N]` onto the restored caller stack.

Steps 7–9 are the identical `UNWIND` tail used by non-local `RETURN` (§8); the
only difference is that normal return always targets the current (top) frame,
while non-local `RETURN` selects a frame by lexical identity. Multi-return
passes through ordinary calls untouched because the result set is copied
verbatim in step 9.

**Normal-call stack diagram** (data stack grows down; `caller_SP` is the base):

```
        caller's stack before the call
   ... -------------------------------- caller_SP
        [ arg1 ][ arg2 ]                step 2: argument temporaries
        (discarded)                     step 4
        [ body temporaries ... ]        step 6
        [ r1 ... rN ][ N ]              step 6: final result set
   ... -------------------------------- steps 7-8 restore SP to caller_SP
        [ r1 ... rN ][ N ]              step 9: result set on caller stack
```

**Why nested and recursive calls are reentrant.** A call leaves on the machine
only (a) its `kind=1` frame on the control stack, whose `saved_SP`/`saved_RP`
/`saved_IP` were recorded *at that call's* entry, and (b) its body's
temporaries, all above the call's own `caller_SP`. A nested or recursive call
pushes a new frame and records *its own* `caller_SP` (the depth of the outer
body's stack at the call site). On completion it restores exactly to its own
recorded `caller_SP`, leaving the outer activation's state untouched below it.
No global scratch is involved, so any number of simultaneous activations of the
same closure coexist without interference.

## 7. Evaluation of a block

The evaluator is a substrate program `EVAL(block, ctx)`. Evaluation is prefix,
fixed-arity, and left-to-right, with **no infix operators and no grouping
constructs**. A recursive evaluator needs no parentheses: a callable's argument
extent is determined entirely by its arity.

**The evaluation grammar** (authoritative):

```
eval-subexpr:
  tok = next token
  INT | NONE | BLOCK | CONTEXT | CLOSURE | NATIVE | RAW   -> [ tok, 1 ]
  WORD bound to callable f (arity a):
      args = [ reduce(eval-subexpr) repeated a times ]    -> apply(f, args)
  WORD bound to value v                                    -> [ v, 1 ]
  GET-WORD :w                                             -> [ binding(w), 1 ]
  LIT-WORD 'w                                             -> [ w, 1 ]
  SET-WORD w:                                             -> bind(w, reduce(eval-subexpr));
                                                             [ bound-value, 1 ]
```

`reduce` takes a result set and yields its first value (or `NONE` if the set is
empty). The one place a result set is *not* reduced is a `values` form at the
end of a sentence, or a function body's final result set (§6).

A **BLOCK** is *evaluated as code* only when it is *entered* as code — by the
top level, a `do`, a function `apply` (§6), or a control structure's body. A
block that appears as an *element* of a code block (or as an argument) is a
plain `BLOCK` value and self-evaluates to itself (one result).

When a block is entered as code, it is evaluated as a **left-to-right stream of
sub-expressions**. Each sub-expression's result set *replaces* the running
result set; the block's result is the **last** sub-expression's result set (an
empty block yields one result `NONE`). Strict sequential semantics: one
sub-expression's result set is fully consumed before the next begins, so
`do [10 20]` yields `[20, 1]`, never two results.

An unbound word is an error.

**`values` — the one multi-result form.** `values [ e1 ... en ]` is a native
with exactly one `BLOCK` argument (fixed arity 1). It evaluates each *top-level*
sub-expression of that block independently, requires each to produce exactly
one ordinary value, and returns those `n` values as one multi-result set:

```
values [10 20]   =>  [ 10, 20, 2 ]
values [ ]       =>  [ 0 ]
```

The literal block is distinct: `[10 20]` in value position is one `BLOCK`
value. Only `values` can widen a result set to `N > 1`.

A computed condition is just a nested callable application: `if >= i 4
[ break ]` parses because `if` (arity 2) consumes the sub-expression `>= i 4`
(which itself consumes `i` and `4`) and then the block `[ break ]`. No
parentheses are needed, or present, in R0.

---

## 8. BREAK, RETURN, THROW: control via S1 execution state

Control transfer is **not** an R0 value. It is a register jump, exactly as the
prior phases established.

**The control stack.** R0 maintains a small stack (a block of cells plus a
depth cell, all above S1 via `@`/`!`) of *control frames*:

```
frame = [ kind (INT) | id | saved_SP | saved_RP | saved_IP ]
kind:  0 = loop     (break target;   id unused = 0)
       1 = function (return target;  id = the function's func-site id)
       2 = catch    (throw target;   id unused = 0)
```

Every frame records a **data-stack baseline** (`saved_SP`), a **return-stack
baseline** (`saved_RP`), and a **continuation** (`saved_IP`). `saved_SP` is
the stack depth at the moment the construct was entered; it is what makes
non-local unwind sound (§11).

- `loop`/`while` push a `kind=0` frame on entry, pop on exit.
- `apply` pushes a `kind=1` frame on entry, pop on return.
- `catch` pushes a `kind=2` frame on entry, pop on exit.

**The one transfer primitive.** All three transfers reduce to a single
substrate routine `UNWIND(selector, result-set?)`:

1. Locate the target frame `F` by `selector`:
   - `break` → the nearest `kind=0` frame (innermost enclosing loop);
   - `return` → the nearest `kind=1` frame whose `id` equals the `return`'s
     *lexical* function identity (definitional — see below);
   - `throw` → the nearest `kind=2` frame (innermost enclosing `catch`).
   No frame found → R0 error, never a jump to a stale address.
2. If a result set must survive (`return`, and `throw` with one value): copy
   the top result set `[r1..rN, N]` into a heap buffer of `N + 1` cells.
   `break` preserves nothing.
3. Restore `SP := F.saved_SP`, `RP := F.saved_RP`, `IP := F.saved_IP` —
   discarding every temporary the unwound frames pushed onto the data stack.
4. Pop all control frames above `F`.
5. If a result set was preserved, push it back onto the now-restored stack.
6. Continue at `F.saved_IP`.

The transfer is **atomic** under the single-threaded evaluator: no other
transfer can fire between the copy and the jump, so the buffer cannot be
clobbered mid-unwind.

**`break`** — no argument; `UNWIND(loop, none)`. Preserves nothing; the loop's
exit code produces the loop's own result set.

**`return` is definitional.** `return` is an ordinary `WORD` (symbol `return`)
whose *meaning* — which function frame it unwinds to — is fixed by lexical
position, not by the word alone. R0 represents that binding concretely in a
**return-site table**, keyed by `(block identity, element offset)`:

- Each `func` literal (a *site* in the source) is assigned a **func-site id** at
  parse time — a unique integer, not a value. Every `kind=1` frame records the
  func-site id of the function it is executing.
- The reader records, for each `return` word it places in a block, an entry
  `(that block's identity, element offset) → enclosing func-site id` in the
  return-site table. The table is *side metadata*: block cells hold only
  ordinary values, and nothing control-related lives in the value space.
- When the evaluator reaches a `return` word at offset `k` of block `B` (in
  code position), it looks up `(B, k)` to obtain the target func-site id, then
  `UNWIND`s to the nearest active `kind=1` frame with that id.

The syntax/site therefore *carries metadata identifying its lexical target*, but
the control effect itself still travels purely by `SP`/`RP`/`IP` manipulation
(§8's `UNWIND`) and never as an ordinary result value.

Consequences, each verifiable:
- A `return` in `f`'s body returns from `f` (the ordinary case; acceptance test
  I: a `return` inside a loop in `f` crosses the `kind=0` loop frame to `f`).
- A `return` inside a block passed as data keeps its table entry (blocks are
  immutable and identified by pointer), so `do`ing that block inside an unaware
  helper still targets the original function — crossing the helper (test F).
- A nested `func` literal has a distinct func-site id, so its `return` targets
  the nested function (shadowing).
- Recursion: all activations of a function share its func-site id, so a
  `return` targets the *nearest* (innermost) activation.
- `return` with no argument yields `[NONE, 1]`; `return expr` evaluates `expr`
  to a result set (which may be multi via `values [ ... ]`) and unwinds
  preserving it.

**`throw v`** — evaluates `v`; `UNWIND(catch, [v])`; the handler receives
`[v, 1]`.

`break`, `return`, and `throw` remain **execution-state machinery**: none is a
value that travels as data. The only data involved is the *ordinary result set*
that `return`/`throw` deliberately carry, preserved and re-placed, never tagged
as a control signal.

## 9. How nested scopes know their target

By **walking the control stack**, not by naming cells:

- `BREAK` inside two nested loops finds the *topmost* `kind=0` frame — the
  innermost dynamically enclosing loop. Acceptance tests H (inner-only break)
  and E (break crossing unaware functions) fall out of this walk.
- `RETURN` finds the nearest `kind=1` frame whose `id` matches the `return`'s
  *lexical func-site id* — so it unwinds intervening loop frames (`kind=0`)
  **and** intervening function frames (`kind=1` with a different `id`) back to
  the function that defined it. Acceptance tests I and F.
- `THROW` finds the nearest `kind=2` frame. Acceptance test J.

The nesting that "knows" which transfer belongs to which scope is captured by
*frame kind, order, and (for `return`) the func-site id* on the control
stack — a structural fact, not a naming discipline. This is the single largest
improvement over the hand-built adversarial tests, which scoped transfers by
*which cells they read*.

## 10. Preventing / detecting a wrongly-wired transfer

- **Selection is by kind, never by address.** A transfer cannot target the
  wrong scope because it does not name a scope; it selects the nearest frame
  of the required kind. There is no cell that a programmer could miswire.
- **Frames are typed records**, restored only through the kind-checking
  runtime words; restoring a frame of the wrong kind is impossible by
  construction.
- **Missing target is an error.** `BREAK` with no `kind=0` frame, `RETURN`
  with no `kind=1` frame of matching identity, `THROW` with no `kind=2` frame
  all raise an R0 error instead of jumping to a stale address.
- **Stale frames are eliminated by structure.** A loop/function/catch pops its
  frame on normal exit, so a transfer fired after the construct has ended finds
  nothing (or the next-outer frame) rather than a dangling `IP`.
- **The whole frame is restored as a unit.** A transfer writes `SP`/`RP`/`IP`
  from one typed frame record; a partial or mismatched restore is impossible by
  construction (§11's invariant).
- **Depth bounds.** The control-stack depth cell is checked on every push/pop
  (a comparison in the runtime word), converting overflow into an error.

What S1 *cannot* do — and R0 does not pretend to do — is prevent a stray raw
`!` (from the trapdoor, §14) from corrupting `RP`/`IP`/`SP` directly. The
above is R0-level discipline *above* S1; it is not S1 enforcement.

## 11. Multiple ordinary results coexist with control transfer

Results live on the **data stack**; transfer lives in the **control stack +
`SP`/`RP`/`IP`**. They coexist because a transfer restores the data stack to a
*saved baseline* and then re-places only the ordinary results it deliberately
carries.

**Stack invariant** (why §8's unwind is sound). Every control frame records
`saved_SP`, the data-stack depth at construct entry. Every value pushed onto
the data stack between that entry and a transfer targeting the frame is a
*temporary* owned by the construct's body — an argument, a partial result, an
evaluator intermediate. Therefore:

- a transfer may discard **all** such temporaries by restoring `saved_SP`; and
- the only values that must survive are the *ordinary result set* being
  returned, which is copied out before the restore and re-placed after.

Consequences:

- `BREAK` restores the loop's `saved_SP`, discarding the current iteration's
  temporaries (correct — a break abandons the partial iteration). A loop's
  accumulated state must therefore live in *bound words* (cells), not on the
  data stack; the loop's exit code then produces the loop's result.
- `RETURN` preserves the result set, restores the caller's `saved_SP`
  /`saved_RP`, and re-places the set — so a `return values [10 20]` fired
  several calls deep delivers `[10, 20, 2]` to the caller with no leftover
  temporaries. Acceptance test G works because the break/return path and the
  normal path both run the same exit code, which pushes the multi-result on
  the freshly restored stack.
- `THROW` preserves only the thrown value, restores the catch's `saved_SP`
  /`saved_RP`/`saved_IP`, and delivers `[v, 1]`.

The arity count travels with the result set (it is one of the preserved cells),
so the result protocol stays well-formed across every transfer. Nothing about a
transfer is ever encoded as an R0 value.

## 12. A tiny R0 program (syntax + semantics)

Surface syntax (parsed by the bootstrap reader into R0 structures; the reader
only builds data, never evaluates):

- integers: `42`, `-7`
- `none`
- words: `foo`, `loop`, `sum`
- set-word `x:`, get-word `:x`, lit-word `'x`
- blocks: `[ ... ]`
- function: `func [args] body` (a callable; `args` a block of words)

Evaluation is prefix and fixed-arity (§7). Example program, with the result
protocol shown in comments:

```
; lexical closure mutation (nearest-binding update, §5)
make-counter: func [start] [
    func [delta] [ start: + start delta   start ]
]
c: make-counter 10     ; c: closure capturing the binding of start
c 5                    ; start updated in place: 10 -> 15 ; => [ 15, 1 ]
c 1                    ; 15 -> 16                          ; => [ 16, 1 ]

; multiple ordinary results vs a single block result, via `values`
two: func [] [ values [10 20] ]   ; => [ 10, 20, 2 ]   (two results)
one: func [] [ [10 20] ]          ; => [ <BLOCK>, 1 ]  (one block result)

; NONE vs no-result are distinct
nothing: func [] []               ; empty body => [ NONE, 1 ]
quiet:   func [] [ print 1 ]      ; print yields nothing => [ 0 ]

; loop + break crossing an unaware intermediate function
i: 0
sum: 0
loop 10 [
    i: + i 1
    sum: + sum i
    helper []                     ; helper does not know about break
    if >= i 4 [ break ]
]

; definitional return crossing an unaware function (test F)
f: func [] [
    run: func [body] [ do body ]  ; `run` executes its block argument
    run [ return values [10 20] ] ; `return` is lexically f's, fired via `run`
]
f                                 ; `return` unwinds `run` and `f` => [ 10, 20, 2 ]

; return inside a loop targets the enclosing function (test I)
g: func [] [
    loop 10 [ if >= i 4 [ return 42 ] ]
    0
]
g                                 ; => [ 42, 1 ]

; catch/throw crossing unaware functions
result: catch [ loop 10 [ if = i 3 [ throw 99 ] ] ]   ; => [ 99, 1 ]

; first-class function value via get-word
twice: func [f x] [ f f x ]
inc:   func [x] [ + x 1 ]
twice :inc 10                     ; => [ 12, 1 ]
```

`helper` and `run` above are ordinary functions; `break` and `return` fire
*through* them by rewriting the control stack and `SP`/`RP`/`IP`, never by
passing them a value — acceptance tests E and F.

## 13. A minimal dialect

A dialect is a block interpreted by rules other than R0's normal evaluation.
Because blocks are data that self-evaluate, a dialect is just *a block + a
function that reads it*.

Minimal example — a **map** dialect, where the rule block is evaluated once
per element with a loop word bound in a fresh context:

```
data:  [1 2 3 4 5]
map data [x] [ + x 1 ]        ; => [ 2 3 4 5 6 ]
```

`map` is an ordinary R0 function: it iterates `data`, and for each element
binds `x` in a fresh child context and `do`es the rule block. The block `[+ x 1]`
is interpreted *per-element with rebinding* — a rule normal R0 evaluation
does not apply — yet no machinery beyond contexts + `do` is required. This is
the whole of "dialect": the language is data, and a function is an interpreter.
Acceptance test L.

## 14. The staircase into the basement (the trapdoor)

The escape hatch is the `RAW` value (§3), produced by `substrate [ ... ]`. A
`RAW` value is **trusted, unsafe code** — deliberately *not* a `NATIVE`, because
`NATIVE` is reserved for mundane host functions that must never touch control
state. The trapdoor is explicit and rare; it is **not a security boundary**.

**Representation.** A `RAW` value is a 16-aligned heap block whose cells are
*raw S1 cells* — `OP_LIT`/`OP_DUP`/`OP_DROP`/`OP_FETCH`/`OP_STORE`/
`OP_ZBRANCH`/`OP_HOST`/`OP_HALT` opcodes and their inline operands — laid out
exactly as S1's run loop consumes them. They are **not** tagged R0 values.

**Assembly / loading.** `substrate [ ... ]` takes a block of raw integers (or a
small mnemonic→opcode table over `LIT`/`DUP`/`DROP`/`@`/`!`/`0BRANCH`/`HOST`/
`BRANCH`/`CALL`/`EXIT`), resolves forward `0BRANCH`/`BRANCH`/`CALL` operands to
absolute code addresses, and copies the result into a fresh heap block,
returning the `RAW` value. Nothing is evaluated — the fragment is *loaded*, not
run.

**How R0 calls it.** Applying a `RAW` value goes through a dedicated evaluator
path (`raw-apply`), distinct from `NATIVE` dispatch: it `CALL`s into the
fragment's code and lets it run on the bare machine.

**Result protocol.** A fragment must end by restoring the caller's `RP` and
leaving `[values..., N]` on the data stack (the ordinary result protocol), then
`EXIT` to the saved continuation. Anything else is a bug in the fragment.

**State saved on entry.** `raw-apply` saves `{SP, RP, IP}` and the
control-stack depth into a **trapdoor guard** area before entering.

**What can actually be checked on return.** When the fragment `EXIT`s back to
R0, the runtime checks: `SP`/`RP` within their regions, `IP` pointing into the
code region, and the control-stack depth unchanged. If any check fails, R0
restores the guard state and `HALT`s with a diagnostic rather than continuing
into corruption.

**Honest limit.** Arbitrary raw S1 code **cannot be sandboxed** by R0: a
fragment can overwrite `M[REG_IP]`, `M[REG_SP]`, `M[REG_RP]`, *or the guard
itself*, and R0 has no way to stop it. The checks only catch a fragment that
bugged out but did not destroy the guard. The trapdoor is **trusted unsafe
code**, in the same category as an `unsafe` block in a managed language or an
`asm` block in C — not a security boundary. This is the unavoidable cost of a
dumb substrate, and why the trapdoor is opt-in.

**The mandatory new control construct via the trapdoor.** A **generator**
(`make-gen` / `yield` / `next`): `make-gen` is a `RAW` fragment that allocates
two stack regions and installs a symmetric `{SP,RP,IP}` swap; `yield` and
`next` are `RAW` words that perform the swap. From ordinary R0:

```
g: make-gen [ print 1   yield 10   print 2   yield 20 ]
next g       ; prints 1, => [ 10, 1 ]
next g       ; prints 2, => [ 20, 1 ]
```

This is a control/evaluation construct R0's value semantics cannot express,
built entirely through the trapdoor, and then used from R0 as a normal feature
— the acceptance test for the trapdoor requirement.

---

## 15. Deliberately NOT in R0

- refinements, paths (`a/b`), mutable series, `PARSE`, full objects, `switch`
  /`case`/`foreach` library breadth, `any`/`all` short-circuit niceties
- garbage collection (heap leaks are accepted for the experiment)
- bignums (small ints only), decimals, dates/times/money/tuples
- Unicode sophistication (char codes are integers)
- a user-visible string/TEXT type (words' spelling is interner metadata;
  `print`/`mold` operate on integers, words, blocks)
- infix `op!` operators (`1 + 2` is not R0; `+ 1 2` is)
- a full error/exception *value* system (errors are thrown diagnostics)
- any change to S1

## 16. What of real REBOL this deliberately does NOT reproduce

- **Infix operators.** R0 is prefix and fixed-arity; REBOL's `op!` evaluation
  (`1 + 2`) is dropped for a simpler, unambiguous evaluation rule.
- **Full "do next" semantics.** REBOL's incremental evaluator is replaced by a
  left-to-right, callable-consumes-arity rule (§7).
- **Mutable series.** REBOL's central data type is mutable; R0 blocks are
  immutable by convention (rebuilt above S1 if ever needed).
- **The full word-datatype family** (issue!, refinement!, op!, path!, etc.);
  R0 has `WORD` plus three subtypes only.
- **Objects/contexts as first-class with refinement dispatch**; R0 contexts are
  plain association lists used for binding only.
- **Refinements** (`/ref`), which are a big part of REBOL's calling surface.

These are not failures of R0; they are the *point*. R0 exists to test the
minimum, not to be complete.

## 17. Safety problems introduced by keeping S1 dumb

- **No memory safety.** `@`/`!` can read/write any cell — including
  `IP`/`SP`/`RP` and the code region — so any bug (or trapdoor fragment) can
  corrupt the machine silently.
- **No arity at machine level.** The result protocol is a convention; a
  producer that lies about `N` is not caught by S1.
- **No type safety.** Tags are a convention; a raw cell (e.g. an address) can
  masquerade as a tagged value, and nothing stops the trapdoor from doing so.
- **Stale / double-fired transfers** are possible if the control-stack
  discipline is bypassed (only the trapdoor can bypass it).
- **Fixed stack regions** with no overflow/underflow checks at machine level.
- **No GC** → leaks.

## 18. Safety mechanisms buildable above S1 without changing it

- **Tag discipline** via checked runtime words (all tag/untag through
  `TAG`/`UNTAG`/predicates, never ad hoc).
- **Control stack** with kind-tagged frames + depth bounds + select-by-kind,
  converting stale-marker and wrong-scope hazards into errors (§10).
- **Arity count + checks** on every `consume` (§4).
- **Bounds-checked allocation** (size headers, `HP` limit).
- **A single runtime-state block** so corruption is at least localisable.
- **Trapdoor guard** (save/check `{SP,RP,IP}` around descent) (§14.6).

Every item is comparison arithmetic over the memory-mapped registers — i.e.
already in S1. None requires a new primitive.

## 19. Alternative designs considered

1. **Encode control as values (antiforms/isotopes).** Rejected by the critical
   design rule and by the prior phase's finding that a substrate with writable
   `IP`/`RP`/`SP` makes a control signal a jump, not a value.
2. **Extend S1 (tags, call/return, continuations in the machine).** Rejected:
   S1 is frozen by premise; the whole point is to see what is needed *above* it.
3. **Continuation-based R0 (S0 style).** Rejected: S1 has no continuations,
   and the prior phase showed they are convenience, not necessity.
4. **Arity in a dedicated cell vs on the stack.** Chose on-the-stack for
   reentrancy (the adversarial phase's finding).
5. **High-bit vs low-bit tagging.** Chose low 4-bit: expressible with
   `MUL`/`DIV`/`MOD`, no bitwise HOST needed.
6. **Flat vs parent-linked contexts.** Chose parent-linked for lexical scope.
7. **Evaluator in the host vs in the substrate.** Chose substrate: evaluation
   must be above S1 by requirement, and a substrate evaluator is what the
   "tower of interpreters" thesis demands.
8. **A `VOID`/`UNSET` value distinct from `NONE`.** Rejected: "no result" is
   arity 0, not a value; `NONE` is the only "nothing" value. Keeps the value
   space minimal and keeps arity/control separate from values.
9. **Leave `SP` alone on `BREAK`/`RETURN` (the naive first design).** Rejected:
   unsound when the transfer fires several calls deep with arguments and
   temporaries on the data stack. Replaced with a `saved_SP` baseline per
   frame (§8, §11).
10. **Multi-return by juxtaposition (`[10 20]` → two values).** Rejected: it
    contradicts strict sequential evaluation. Replaced with the explicit
    block-argument `values` form (§7).
11. **Assignment always writes a local.** Rejected: it breaks lexical closure
    mutation (`make-counter`). Replaced with nearest-binding update (§5).
12. **Return-target as a wrapper value vs a side table.** Rejected encoding the
    return target as an internal record *value* in the block's cells (it would
    smuggle control metadata into the value space). Chose a **return-site
    table** keyed by `(block identity, element offset)` so block cells hold
    only ordinary values and the control effect still travels by `SP`/`RP`/`IP`
    (§8).

---

## Acceptance-test mapping

| Test | Mechanism | Result protocol |
|---|---|---|
| A. no result | empty body / `print` | `[ 0 ]` |
| B. one result NONE | `func [] []` | `[ NONE, 1 ]` |
| C. two results | `func [] [ values [10 20] ]` | `[ 10, 20, 2 ]` |
| D. one block | `func [] [ [10 20] ]` | `[ <BLOCK>, 1 ]` |
| E. BREAK crossing unaware fns | `break` → nearest `kind=0` | `[ ... , N ]` from exit code |
| F. RETURN crossing unaware fns | definitional `return` → `kind=1` by identity | `[ 10, 20, 2 ]` |
| G. multi-return + BREAK | break restores `saved_SP`; exit code returns `values` | `[ 10, 20, 2 ]` |
| H. inner-loop BREAK | topmost `kind=0` | — |
| I. RETURN inside inner loop | `return` by identity past `kind=0` | `[ 42, 1 ]` |
| J. THROW crossing unaware fns | `kind=2`, restore `{SP,RP,IP}` | `[ v, 1 ]` |
| K. user-defined control abstraction | new loop form over the control stack | — |
| L. dialect | `map` interpreting a block by non-standard rules | `[ <BLOCK>, 1 ]` |
| M. descend to S1 and return | `substrate [ ... ]` → `RAW`, with guard | `[ ... , N ]` |

---

## Summary: concept count

**R0-level value concepts** (live in the value space, tagged):

1. `INT`
2. `NONE`
3. `WORD` (with subtypes `SET-`/`GET-`/`LIT-`, counted as one concept)
4. `BLOCK`
5. `CONTEXT`
6. `CLOSURE` (function)
7. `NATIVE`
8. `RAW` (trusted unsafe substrate fragment)

Plus one **protocol** (not a type): the **result-arity convention** (the
count-on-stack marker) — this is how zero/one/many/NONE are *represented*,
and it is a value-space convention, not a control concept.

**R0-level control concepts** (live in `IP`/`SP`/`RP` + the control stack,
never values):

1. `BREAK`
2. `RETURN` (definitional: target selected by the `return`'s func-site id)
3. `THROW`
4. `CATCH`
5. the **control stack** (the mechanism that scopes the above by frame
   kind/identity)

The `return` word's **func-site id** (the target of a definitional `return`)
lives in the **return-site table** (§8) — side metadata keyed by `(block
identity, element offset)`, not a value and not user-visible. It is part of the
control machinery, not the value space.

(`IF`, `WHILE`, `LOOP`, `DO`, `SET`, `GET`, `APPLY`, `VALUES`, and the dialect
function `MAP` are *functions/natives* composed from the above, not new control
concepts.)

**Mere safety/ergonomic machinery** (neither value nor control semantics):

1. tag/untag helpers + type predicates
2. symbol interner
3. aligned heap allocator (size header, bounds)
4. control-stack depth/kind/identity checks
5. trapdoor guard (save/check `{SP,RP,IP}`)
6. the result-set preservation buffer (heap, for `RETURN`/`THROW` unwind)
7. the return-site table (`(block identity, offset) → func-site id`)
8. the fixed runtime-state block

**Does anything discovered so far require changing S1?**

No. The value discipline needs only `MUL`/`DIV`/`MOD` (already in `HOST`);
control transfer needs only `@`/`!` over the memory-mapped `IP`/`SP`/`RP`
(already in S1); arity is a convention over the data stack (already present);
contexts, closures, and blocks are conventions over memory (already a block).
The `saved_SP`-baseline unwind and the result-set preservation buffer are pure
substrate code (register stores + a `HOST_ALLOC` for the buffer); the buffer
adds an `O(N)` copy per non-local `RETURN` — a *cost*, not a new primitive.
Nothing in this proposal introduces a new primitive, and nothing discovered so
far appears to require one.
