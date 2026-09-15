# R0-S1 Audit

Comparing commit `07de02a` (the hosted-C R0 prototype) with the approved
architecture (`R0-ARCHITECTURE.md`) and the R0-S1 non-negotiable execution rule:
**R0 execution semantics must run ON S1**, not in C.

`07de02a` is a correct *hosted* interpreter of the R0 data model. It is **not**
an implementation of R0 over the seven-primitive substrate: the evaluator, the
stacks, and all control transfer run as C, with only the generator fragment
executing as genuine S1 code.

## Classification key

- **A. Legitimate bootstrap / toolchain C** — loader, parser, interning
  spelling, assembling/loading S1 code and immutable data into `M`, starting
  `s1_run`, inspecting results.
- **B. Legitimate mundane HOST service** — arithmetic, comparison, allocation,
  textual I/O, diagnostics. (Operations only; the *dispatch/result-protocol
  trampoline* around them is C-class.)
- **C. R0 semantics incorrectly in C — must move to S1**.
- **D. Reusable data representation — independent of the execution layer**.

## Component audit

| Component (in `07de02a`) | Class | Notes |
|---|---|---|
| Tag scheme (low 4 bits) | **D** | Reusable; already expressed as cell tags in `M`. |
| Block layout `[count, elem...]` | **D** | Lives in `M`; reusable. |
| Context layout `[parent, count, cap, (w,v)...]` | **D** | Lives in `M`; reusable. |
| Closure layout `[spec, body, ctx, site]` | **D** | Lives in `M`; reusable. |
| Result-protocol *convention* (`[r..,N]`, tagged-INT arity) | **D** | The convention is architecture-specified and language-agnostic. |
| Control-frame *layout* (`kind, id, saved_SP/RP/IP`) | **D** | The layout is right; `07de02a` does not actually populate `saved_RP`/`saved_IP`. |
| Reader/parser (`r0_parse`, `parse_*`) | **A** | Only builds data + records sites; no evaluation. Reusable as-is. |
| Interning spelling (`r0_intern`, C string table) | **A** | Spelling is toolchain metadata. Symbol *identity* is the only runtime fact. |
| Recording func-site / return-site entries | **A** (data: **D**) | *Loading* the metadata is bootstrap; the *table* itself must live in `M` (currently a C array). |
| Heap allocator (`r0_alloc`) | **B** | Mundane allocation (≈ `HOST_ALLOC`). In R0-S1 its call sites must be S1 code (or `HOST_ALLOC`). |
| Data stack (`ds[]`, `sp`, `push/pop`) | **C** | The R0 value/result stack is a host C array; must be S1's `REG_SP`. |
| Control stack (`cstack[]`, `csp`) | **C** | Host C array; must be a region in `M` traversed by S1 code. |
| Result-protocol *implementation* (`take/put/reduce/discard_result_set`) | **C** | Operate on the C `ds[]`. |
| Evaluator (`eval_block`, `eval_subexpr`, `eval_return`, `eval_func`) | **C** | Core semantics in C. |
| `apply` / `apply_closure` / argument cleanup / context switch | **C** | The §6 ABI is implemented in C recursion. |
| Binding `lookup` / `set_binding` (nearest-binding update) | **C** | R0 semantics in C (though the layout it touches is **D**). |
| `make_context` / `make_closure` | **C** | Called from the C evaluator. |
| `IF` / `LOOP` / `DO` / `VALUES` | **C** | C `switch` cases in `apply`. |
| `BREAK` / `RETURN` / `THROW` / `CATCH` | **C** | The critical violation: propagated as C `return ST_BREAK/ST_RETURN/ST_THROW`, with C-recursion frame-by-frame unwind. |
| Transfer payload (`gT`, `r0_transfer`) | **C** | C struct carrying status + carried values. |
| Arithmetic/comparison natives | **B** (op) / **C** (dispatch) | The raw `a+b`, `a<=b` is mundane; but it runs inside the C evaluator's native switch, not as a `HOST` call from S1 code. |
| `print` / `mold` | **B** | Mundane textual I/O / diagnostics (mold walks tags for display only). |
| NATIVE/HOST trampoline (untag → op → tag → `[v,1]`/`[0]`) | **C** | Result-set construction is R0 semantics and must be S1 code. |
| `r0_init` native-binding loop | **A** | *Populating* the initial global context is bootstrap loading; it currently calls C `set_binding`, which must become S1. |
| `r0_eval` (resets stacks, calls `eval_block`) | **C** | Host driver of the evaluator. |
| Generator `make-gen` / `next` | **C** | Hard-coded C; not the user-visible trapdoor. |
| `build_gen_fragment` (assembles S1 generator code) | **A** | Genuine S1 code, assembled by toolchain — the one part already correct in spirit. |

## The critical control test — where `07de02a` fails

The rule requires that `BREAK`/`RETURN`/`THROW` do **not** propagate as C
status codes, and that an unaware intermediate R0 function genuinely does
nothing to propagate.

In `07de02a`:

- The transfer is an `int status` (`ST_BREAK`/`ST_RETURN`/`ST_THROW`) returned
  up the C call chain.
- Every intermediate `apply_closure` *does* inspect the status and restore its
  `saved_sp` — it is not "unaware"; it is a participant in the C protocol.
- The carried result set travels in a C `r0_transfer` struct (`gT`), not on the
  S1 data stack.
- `saved_RP`/`saved_IP` do not exist: the "return continuation" is the C call
  stack, and the "jump" is `return`-ing to a C caller.

The required behaviour is different in kind: the transfer must **restore actual
S1 `REG_SP`/`REG_RP`/`REG_IP` from a frame and jump** (write `REG_IP`), so that
the code between the fire site and the target is simply *not executed* (its
`CALL` frames are unwound beneath it), not *consulted* for a status code.

## Migration path (smallest)

The smallest faithful path is to **re-host the R0 runtime as an S1 program
assembled by a C assembler**, where the assembler is pure toolchain (emits
`LIT`/`DUP`/`DROP`/`@`/`!`/`0BRANCH`/`HOST`/`CALL`/`EXIT` and higher macros)
and contains no R0 semantics — the semantics live only in the *emitted* S1 code.

1. **Fix the runtime state mapping.**
   - R0 data/result stack = S1's `REG_SP` (real register).
   - R0 return/continuation stack = S1's `REG_RP` (real register; the S1
     evaluator recurses via `CALL`/`EXIT`).
   - R0 control stack = a region in `M` + a depth cell in `M`, with frames
     `[kind, id, saved_SP, saved_RP, saved_IP]`; traversed by S1 code.
   - current context = a cell in `M` (saved/restored by `apply`).
   - func-site / return-site tables = regions in `M` (the S1 evaluator reads
     them; the parser only *loads* them).

2. **Write the evaluator as S1 code.** A dispatch loop over block elements;
   `eval-subexpr` as an S1 subroutine (tag dispatch); `apply` as an S1
   subroutine (record `SP` baseline → evaluate args → bind → push control frame
   → `CALL` body → unwind); `lookup`/`set` as S1 subroutines walking context
   regions; arithmetic/comparison via `HOST_ADD`..`HOST_GE`; `print` via
   `HOST_PRINT` (+ a mold diagnostic).

3. **Reimplement control transfer as register-restore-and-jump.** `break`/
   `return`/`throw` become S1 subroutines that scan the control stack in `M`,
   select the target frame by kind (and lexical id for `return`), copy its
   `saved_SP/RP/IP` into `REG_SP/REG_RP/REG_IP`, and write `REG_IP` — the jump.
   Unaware intermediates are genuinely unaware: they are `CALL`/`EXIT` code
   whose frames are discarded by the `RP` restore, never executed past the fire
   site.

4. **Move the native trampoline into S1.** Untag args (`DIV 16`), invoke
   `HOST`, tag the result (`MUL 16`), push `[v,1]` or `[0]`.

5. **Rebuild the trapdoor as user-visible S1.** An R0 word (e.g. `raw-call`)
   implemented in S1 that takes a `RAW` value (address of an S1 fragment in
   `M`), jumps into it, and lets it end by restoring `RP` and leaving
   `[v..,N]`. Re-implement a genuinely new control behaviour (e.g. the
   generator) *through* this word rather than as hard-coded C natives.

6. **Port the tests.** The observable A–M + additional test programs are
   unchanged; the C harness only loads, `s1_run`s, and inspects results.

## What can remain vs what must be replaced

**Remains (A / B / D):**

- The tag scheme, block/context/closure layouts, result-protocol convention,
  control-frame layout (D).
- The reader/parser and interning spelling (A).
- The idea of an assembler/toolchain for S1 code (A) — extended from the
  existing `asm_*` in `s1.h` to a higher-level macro assembler.
- The mundane HOST operations (arithmetic/comparison/I/O/allocation) (B).
- The test *source* strings (their observable behaviour is the target).

**Replaced (C):**

- `eval_block`, `eval_subexpr`, `eval_return`, `eval_func`, `apply`,
  `apply_closure`, `apply_native` → an S1-code evaluator.
- `ds[]`/`sp` → S1 `REG_SP`; `cstack[]`/`csp` → control stack in `M`.
- `take/put/reduce/discard_result_set` → S1 code over `REG_SP`.
- `lookup`/`set_binding` → S1 subroutines.
- `gT`/`r0_transfer`/status enums → gone; transfers are register writes + jump.
- native dispatch + trampoline → S1 code + `HOST`.
- `make-gen`/`next` → replaced by a user-visible `raw-call` trapdoor + an
  R0-level control construct built through it.

Roughly: **~80–90% of `r0.c` (~600 of ~676 lines)** is class C and must be
reimplemented as emitted S1 code; the parser, data layouts, and the assembler
concept carry over.

## Honest difficulty

The real experiment is large precisely here: a recursive-descent evaluator for a
tagged, block-structured language, written in raw Forth-style S1 machine code,
is thousands of cells of emitted code (the "code blow-up" already measured in
`RESULTS.md`: `CALL` ≈ 20 cells, `>R`/`R>` ≈ 13 each). The substrate assembler
is itself a substantial toolchain, and there is a real risk that debugging
emitted S1 code is slow. This is the point: it is the *cost* of the "one
conditional branch + memory-mapped registers" substrate, measured directly.

Nothing here requires an eighth primitive; nothing requires reinterpreting an
existing primitive. The seven primitives plus `HOST` are, by construction of the
prior phases, sufficient for control transfer via `@`/`!` over the
memory-mapped registers.
