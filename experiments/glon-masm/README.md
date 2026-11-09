# GLON-MASM — code density and low-level composition (investigation)

> **Status (2026-11-09): investigation, nothing deployed.**
> - No change to frozen S1, the runtime, the ABI or the `mem/` library.
> - Every number comes from `results.txt` (`make measure`, `make stack`),
>   native `-O1` against the unchanged runtime, unless marked **[E]**
>   (estimate).
> - The microbenchmarks pass 55/55 natively and 55/55 in `glon.wasm` (`make
>   test`, `make wasm-test`).
>
> **Question.** `mem/` showed code space, call overhead and composition to be
> tighter limits than the heap. Are they inherent in S1, or consequences of the
> assembler, the code layout and library design?

## 1. How masm code is generated and run

**S1 encoding (frozen, `s1.h`/`s1.c`).** Seven primitives:
- `LIT v` takes 2 cells; `DUP DROP @ !` 1 cell each;
- `ZBRANCH t` 2 cells; `HOST id` 2 cells, so every arithmetic or comparison
  op costs 2.

There is no call, return, jump or stack-pointer instruction. IP, SP, RP and HP
are memory-mapped cells, so control flow is derived by storing into them.

**Derived forms (frozen `s1.c` assembler; masm uses them):**

| Form | Expansion | Cells |
|---|---|---|
| `BRANCH t` | `LIT t LIT REG_IP !` | 5 |
| `>R` | `LIT REG_RP @ LIT 1 SUB LIT REG_RP ! LIT REG_RP @ !` | 14 |
| `R>` | `LIT REG_RP @ @ LIT REG_RP @ LIT 1 ADD LIT REG_RP !` | 14 |
| `EXIT` | `R@` (4) + `RP := RP + 1` (10) + `LIT REG_IP !` (3) | **17** |
| `CALL x` | `LIT continuation` (2) + `>R` (14) + `LIT x LIT REG_IP !` (5) | **21** |
| `ARITY n` | `LIT n` | 2 |

**Where the cost comes from.** The 21 and 17 cells are the price of keeping a
return stack in memory with only `LIT @ !` and arithmetic: each push or pop of
RP is a fetch, an add and a store.
- **Instruction encoding:** none of that comes from S1's encoding.
- **Assembler expansion:** it is the frozen assembler's expansion of a
  general, re-entrant call.
- **Runtime requirement:** the return stack is shared with the evaluator.

**Measured execution:** a `CALL`/`EXIT` pair executes 24 S1 instructions
(`x/sub` − `x/inl`, divided by 4).

**A masm word** (`r0_s1_runtime.c`):
- **Assembly.** `name: masm N [ ... ]` is assembled at load time,
  contiguously at the frozen assembler's cursor. Its value is a T_RAW pointer
  to a 2-cell loader-heap record, `[entry, arity]`. The masm tokens are not
  kept.
- **Call.** `r_invoke_raw` evaluates `arity` arguments onto the data stack,
  then makes an indirect `CALL entry`.
- **Return.** The word ends with `ARITY n EXIT`: it leaves n results and the
  count n, and returns.
- **Fixed cost.** Each word therefore costs at least 19 cells (`x/min`).
  Calling one from Glon executes about 227 S1 instructions (`x/min`), and a
  Glon function wrapped around it about 1,400 more (`x/glon`).

**Calls, operands and labels:**
- **Labels.** `CALL label` reaches only labels of the same block. Labels are
  per block (`raw_labels[64]`, `raw_refs[128]`).
- **Named runtime routines.** `CALL` also accepts five: `alloc`, `collect`,
  `lookup`, `ESC_ANY`, `ESC_TRANSPORT`.
- **`LIT` operands** are integers or the exported symbol names, never labels.
  So **masm cannot name a code address**: the only return addresses it can
  create are those its own `CALL` pushes.
- **Block size.** A block holds at most 510 tokens (`MASM_TOK_CAP`). These are
  limits of the assembler in `r0_s1_runtime.c`, not of S1.

**Where code goes:**
- **The cursor.** The assembler's cursor `here` is a `static` in frozen `s1.c`,
  with only `asm_reset` (to `CODE_BASE`) and `asm_here`. Runtime code and
  every masm library are therefore laid down contiguously from cell 256.
- **The runtime's share.** The runtime alone is 12,670 cells, ending at
  12,926.
- **The shared budget.** Libraries grow up to the main data stack, which
  grows down from 16,384 (`DS_INIT`, frozen). Every masm library in a session
  shares those **3,458 cells** with the data stack.

## 2. Microbenchmarks

Every variant maps a tagged integer through the same 20-cell operation U (or W)
four times. Source: `results.txt`.
- **Instruction counts** are exact: the `-DCOUNT` build links the instrumented
  S1 copy `s1_prof.c` instead of the frozen `s1.c`.
- **Wall time** on this machine varies by several µs per call between runs
  (the balanced Glon loop around each call costs ~40 µs), so it is only
  indicative.

| Variant | Tokens | Code cells | S1 instructions per call (from Glon) |
|---|---|---|---|
| `x/min`: a word returning its argument | 3 | 19 | 227 |
| `x/inl`: U ×4 inline | 63 | **99** | **267** |
| `x/sub`: U a local subroutine, 4 `CALL` sites | 28 | 140 | 363 |
| `x/loop`: U in a counted loop | 41 | **64** | 321 |
| `x/sel`: U reached by `BRANCH`, return through a selector cell | 86 | 126 | 348 |
| `x/inl2`: U W U W inline | 63 | 99 | 267 |
| `x/sub2`: U, W subroutines, an `EXIT` each | 45 | 177 | 363 |
| `x/sub2x`: the same, sharing one `EXIT` | 47 | 165 | 369 |
| `x/u`: U as its own word | 18 | 39 | 237 |
| `x/two`: control, a 2-argument word (the second a get-word), U ×4 inline | 64 | 100 | 675 |
| `x/xc`: calls `x/u`, passed as a value, 4 times | 64 | 176 | 812 |
| `x/glon`: a Glon `func` around `x/inl` | — | 0 | 1,673 |
| `x/g4`: four Glon-level calls of `x/u` | — | 0 | 3,023 |

**What the measurements show:**
- **Every cell count is the expansion sum.** For example `x/sub` = 4×21 + 19 +
  20 + 17 = 140.
- **Local call and return** cost 24 instructions per pair.
- **A cross-word call** costs about 34 instructions (`x/xc` − `x/two`, minus
  validation, divided by 4), barely more than a local call.
- **Passing the callee as a Glon argument** costs about 408 instructions per
  call of the caller (`x/two` − `x/inl`), the lookup of the get-word. A
  library should obtain a callee once, not per call.

**Break-even, inline against a subroutine (cells).** k call sites of a body of
b cells:
- inline: k·b;
- a subroutine: 21k + b + 17, the 17 shared by every subroutine of a block
  that branches to one `EXIT`, 5 cells each.

So a subroutine saves space only when **b > (21k + 17)/(k − 1)**:

| Call sites | Body must exceed |
|---|---|
| 2 | 59 cells |
| 4 | 34 cells |
| 8 | 27 cells |
| any number | 21 cells |

U (20 cells) never qualifies (`x/sub` 140 against `x/inl` 99). A loop beats
both when the repetition is regular (`x/loop` 64).

**Stack use** (`make stack`: high-water marks, found by poisoning the free
stack cells before each file and scanning afterwards):

| Workload | Deepest data stack | Deepest return stack |
|---|---|---|
| `mem/` tests | 21 cells | 1,884 cells |
| DEFLATE unit tests | 28 | 2,862 |
| `iter` tests | 78 | 7,386 |
| 80 nested Glon calls | **87** | **7,783 of 8,192** |
| masm variants | 12 | 323 |

masm itself uses almost no stack: `CALL` pushes one return-stack cell. The
data stack never exceeded 87 cells.

## 3. Sharing code between separately assembled words

**Possible today, with existing mechanisms** (`x/xc`, tested natively and in
WASM). A caller word receives another masm word as a value (`:x/u`), then:
1. checks that it is a T_RAW value whose loader record has the expected arity;
2. reads its entry address from the record;
3. calls it through a 3-cell trampoline (`tr: LIT REG_IP !`), reached by
   `CALL tr` so that the continuation is on the return stack;
4. drops the result count after the callee's ordinary `ARITY 1 EXIT` returns.

The callee is an unmodified word, still callable from Glon. Per call site:
`LIT SCRATCH_B @ CALL tr DROP`, 25 cells. Per caller: the check and the
trampoline, about 50 cells.

**The safety rule, and why internal helpers cannot be shared:**
- **No authentication.** A caller cannot authenticate its callee. A T_RAW value
  is always a genuine masm word, but which one is chosen by whoever passes it,
  and libraries have no trusted fixed cell to find each other through.
- **What sharing is safe.** Sharing is safe only when the call passes ordinary
  tagged Glon values and the callee validates its own arguments. Calling it
  then grants nothing a Glon program could not do by calling it directly. So a
  public validating word can be shared, for example `mem/` v002's `mem/acc` by
  another library.
- **What cannot be shared.** An internal helper taking raw pointers (region
  validation, byte addressing, an unchecked copy) cannot be. A substituted
  callee would receive raw pointers.
- **The constraint responsible.** It lies in the masm assembler, not frozen
  S1: masm can name no code address (`LIT label` is refused), and the loader
  resolves no name across blocks.
- **The caller-save cost.** The callee also clobbers the scratch registers
  (A, B, C, F), so callers must keep live state in a workspace across a call.

## 4. Code space

**Why code competes with the data stack.**
- **The layout.** The frozen assembler lays code down contiguously from 256
  upward, and the main data stack grows down from 16,384. The cursor cannot be
  moved: it is a `static` of frozen `s1.c`.
- **The figures** (measured code ends): the runtime ends at 12,926; with
  prelude and strings, 13,646; with strings-ext, 14,150; with `mem/` v002,
  15,727, leaving 649 cells.

**How much room is really needed.**
- **The data stack** used at most 87 cells in every workload measured. So
  about 550 of `mem/`'s remaining 649 cells could still hold code in practice.
- **The return stack** is the tight one: 7,783 of 8,192 cells at 80 nested
  calls.

**Two safety gaps found** (no frozen change needed to close them). Both have
since been repaired: see GLON-ARCHITECTURE-MEMORY.md, "Code/stack boundary
repaired", and `r0_s1_masm_bound_tests.c`. The measurements below were made
before the repair.
1. **The assembler does not check its bound.** `mem/` v002 followed by
   `variants.glon` was assembled to 16,932, 548 cells **past** the data-stack
   top and into the return stack, with no error (`make stack`, last run).
2. **The stack sentry does not cover library code.** It compares SP with
   `code_end`, the runtime's code end captured at initialisation (12,926), not
   the current end. A data stack that grows over masm library code is not
   detected.

The poison measurement also caught a data-stack underflow in this
experiment's own `x/xc` failure path (fixed). masm stack balance is checked by
nothing.

**Unused capacity:**
- **The M1 task arena** (10,200 cells) is unused when no task runs.
- **The loader heap** has spare room in most sessions (4,000–8,000 cells).

Code cannot be placed there today: the assembler writes only at the frozen
cursor. A runtime-level second emitter writing elsewhere is possible in
principle (S1 executes any address, and the collector's scans ignore code
outside the managed heap), but it is a memory-map decision [E].

**Coexistence.** Separately loaded masm libraries coexist if their code sum
fits:
- there is no relocation, so labels are private to each block;
- the scratch cells A, B, C, F are shared, so no library may keep state in
  them across calls; D and E are reserved.

`mem/` v002 (1,577 cells) and these variants (≈1,200) do **not** fit together.

**A larger memory profile does not help code space** unless it moves the
code/data-stack boundary. That boundary is `DS_INIT` in frozen `s1.h`, also
used by `s1_reset`. More heap leaves the 3,458-cell code budget unchanged.

## 5. Proof of concept: what can be demonstrated today

| Technique (available today, existing instructions) | Measured effect |
|---|---|
| a loop instead of repeated code or calls | `x/loop` 64 against `x/inl` 99 and `x/sub` 140 cells: **−35%/−54%**, at +20% instructions |
| one shared `EXIT` per block | −12 cells per extra subroutine (`x/sub2x` 165 against `x/sub2` 177) |
| `BRANCH` + selector return instead of `CALL`/`EXIT` | `x/sel` 126 against `x/sub` 140: −10%; worse than inlining here |
| no subroutines, one return point per word | already applied in `mem/` v002 |
| **cross-word reuse of a public validating word** (`x/xc`) | a large primitive is assembled once per session instead of once per library. For a 589-cell word like `mem/acc`, a second library pays about 50 + 25 per call site instead of 589 [E] |

The cross-word call is the one mechanism here that changes what can be built:
libraries can compose at the masm level without duplicating primitives. It is
demonstrated by `x/xc` with tests, natively and in WASM.

The other techniques are modest, and `mem/` v002 already uses those that pay.

**What would help substantially needs the assembler (non-frozen
`r0_s1_runtime.c`) to change** — [PROPOSAL]s, not built:
- **(a) a compact call** through a per-block call stub: `LIT continuation LIT
  target BRANCH stub` (9 cells) plus a `BRANCH` to a shared `EXIT` (5), against
  38 [E]. It needs the assembler to emit a label as a literal, which it already
  does inside `CALL`.
- **(b) load-time linking** of a name across blocks, so that internal helpers
  can be shared with a fixed, trusted identity.
- **(c) the two bound checks of §4**, in the assembler and the stack sentry.
- **(d) the 510-token, 64-label and 128-reference caps**: assembler buffers,
  raised or streamed.

## 6. Answers

1. **Is the 38-cell call/return overhead fundamental?** No.
   - **What is fundamental** is that S1 derives calls from `LIT @ !` and
     arithmetic on a memory-mapped return stack. Any re-entrant call pays a
     push and a pop of RP, about 24 executed instructions.
   - **What is not** is 38 cells per site: that is the frozen assembler's
     inline expansion. A shared stub would bring a call to about 14 cells per
     site [E], with an assembler change and no S1 change.
   - **Today**, masm code avoids the cost by loops, inlining and one return
     point.
2. **Can masm code be shared across library boundaries?** Yes, for public
   words that validate their own arguments: through their RAW value, a
   trampoline and their ordinary return (measured; native and WASM). Not for
   internal raw-pointer helpers, because masm cannot name a code address and
   the loader links no names across blocks. That is an assembler limit, not
   S1.
3. **Can the current machine support multiple substantial low-level libraries
   at once?** Only a few.
   - **The budget:** 3,458 cells of code are shared by every masm library in a
     session. strings-ext uses 504 and `mem/` v002 1,577, and v002 cannot
     coexist with another 1,200-cell library.
   - **What helps:** sharing public primitives across libraries (§3) reduces
     duplication, and about 550 cells of data-stack "headroom" are usable code
     in practice.
   - **What must be fixed first:** the assembler's missing bound check (§4)
     must be fixed before relying on that margin.
4. **Is the 510-token block limit fundamental?** No. It is the assembler's
   token buffer (`MASM_TOK_CAP`, sized like the Glon reader's block limit).
   The real cost of a large block is its code cells, not its tokens.
5. **Is code space, rather than the heap, the binding constraint for
   low-level libraries?** Yes (this question was cut off in the request; this
   is the evident question).
   - The heap can be enlarged by a profile.
   - The code budget is fixed by the frozen `DS_INIT` and the frozen
     contiguous assembler cursor.
   - It is consumed at 17–21 cells per call or return, 19 per word, 2 per
     arithmetic op.

## Files

| File | Purpose |
|---|---|
| `variants.glon` | the microbenchmark words (one definition per line) |
| `tests.glon` | every variant computes the same function; `x/xc` refuses non-words and wrong arities |
| `measure.c` | tokens and code cells per definition; time per call; exact S1 instructions per call with `-DCOUNT` |
| `stackwater.c` | data- and return-stack high-water marks by poisoning |
| `results.txt` | the recorded `make measure` and `make stack` output |
| `Makefile` | `make test`, `make wasm-test`, `make measure`, `make stack` |
