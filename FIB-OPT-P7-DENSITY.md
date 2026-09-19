# FIB-OPT-P7 — Evaluator density (emit fewer words of S1)

## 1. Hypothesis

P6/P6B established that `fib 25` executes **1,107,100,190** S1 opcode
dispatches — ~4560 per fib activation — and that the dominant opcodes are the
memory-mapped register-access idiom (`LIT` 43.8% + `@` 19.3% + `!` 12.8% = 76%).
P7 tests one architectural hypothesis:

> Can the Glon evaluator be expressed more densely in the **frozen**
> seven-primitive S1 machine, reducing repeated memory-mapped-register traffic
> and total S1 instruction count, without changing S1 semantics or hiding
> evaluator semantics in HOST?

This is an **emitted-evaluator-code** optimisation. No S1 change, no new
opcode, no computed goto, no HOST semantic change, no P8.

## 2. Method: inspect before editing

An IP histogram was added to the P6 profiler (`s1_prof_ip_hist[256]`, 256-cell
code buckets) to attribute dynamic opcode executions to the emitted evaluator
subroutines. For `fib 25` the execution is concentrated as follows (approximate,
rounded):

| emitted subroutine | approx. ops | share |
|---|---|---|
| `lookup` (word resolution) | ~260M | 23% |
| `invoke_closure` (activation) | ~185M | 17% |
| `subexpr` (element tag dispatch) | ~181M | 16% |
| `native` (native dispatch) | ~163M | 15% |
| `append` + `mkctx` (context + binding) | ~123M | 11% |
| `reduce` + `discard` | ~90M | 8% |
| `load_lex` + `hash_insert` + `set` | ~55M | 5% |
| `block_eval` + `main` | ~31M | 3% |
| `run_block` + `return` | ~19M | 2% |

### Dominant recurring idioms

| idiom / operation | executions | S1 ops/use | notes |
|---|---|---|---|
| `CALL X` + `EXIT` (subroutine call/return) | ~10M calls | 25 | pure call overhead; 2 indirect jumps |
| `LIT addr @` / `LIT addr !` (mem-mapped register/cell access) | everywhere | 2 | `e_cell`/`e_setc` |
| `>R` / `R>` (return-stack push/pop) | frame/arg save-restore | 9 each | frozen derived words |
| `e_pop_to(X)` (stack→cell) | reduce/discard/lookup | 5 | move + store + drop |
| tag dispatch `DUP LIT tag EQ 0BRANCH` | every subexpr | 7 per check | 5 tag + 3 value checks |
| `e_cell(RV_Tx); HOST arith; e_setc(RV_Ty)` (load/modify/store) | address arithmetic | ~10 | context/lookup/frame address math |

The largest single pure-overhead sources (work that does no Glon semantic
progress) are the subroutine call/return pairs and the `reduce`/`discard`
round-trips around every argument evaluation.

## 3. Strategy: densify the hot evaluation loop (one coherent change)

The chosen single strategy is **"remove redundant register-memory round-trips
and call overhead in the hot evaluation path"** — keeping already-loaded values
live (via `DUP`/tail-jumps) and eliminating the most repeated `load → operate →
store → reload` sequences. Five mechanical, semantics-preserving changes:

1. **Tail-call elimination.** Every `CALL X; EXIT` (a subroutine that immediately
   returns its callee's result) becomes a single `BRANCH X`. This halves the
   indirect jumps in the value-dispatch path. Sites: `subexpr`'s native/closure/
   raw dispatch, `native`'s `either`/`do`/`values`, and the `return` keyword.

2. **Dense `reduce`.** The previous `reduce` moved the tagged-N marker through
   `RV_T1` and computed `N-1` unconditionally. The dense version keeps N in one
   cell and short-circuits the overwhelmingly common `N == 1` case (no loop, no
   `e_pop_to`/`e_cell` re-read).

3. **Dense `discard` + inline it into `block_eval`.** `discard` now derives N on
   the stack; and since `block_eval` is its only caller, it is emitted inline,
   removing a `CALL`+`EXIT` per non-final element. (Minor for fib — see §5 —
   because `either`/`+` consume their arguments via `subexpr`, not `block_eval`.)

4. **Reorder `subexpr` tag checks** to test `BOUND` (pre-resolved lexical) right
   after `WORD`. Parameter references (`n`, `fib`) then skip the rare
   `SET`/`GET`/`LIT` comparisons.

5. **Reorder the arithmetic-native dispatch chain** to fib's observed frequency
   (`<=`, `+`, `-` first), so the common case exits after one or two comparisons.

All changes emit only the seven frozen primitives; none changes what the
evaluator computes (see §6: every evaluator event counter is unchanged).

## 4. Why frozen-S1 semantics are preserved

- No primitive added, removed, or reinterpreted; `s1.c`/`s1.h` byte-identical.
- `BRANCH X` in place of `CALL X; EXIT` is a standard tail-call transformation:
  the return stack still holds the caller's return address, `X`'s trailing
  `EXIT` pops it, and the result set flows through unchanged.
- `reduce`/`discard` are observably identical: same stack effect, same `NONE`
  for zero results, same single value for `N == 1`, same `N-1` drops for `N > 1`.
- Tag-dispatch and native-dispatch reorders only change the *order* of mutually
  exclusive comparisons, never which branch is taken.

## 5. How RAW-visible / canonical state is preserved

The transformations never cache or delay canonical machine state. `RV_CUR`,
`RV_END`, `RV_CTX`, `RV_FRAME`, `SP`, `RP`, `IP`, and the memory-mapped cell
contents are written at exactly the same points as before; only the number of
*re-reads* of already-loaded values fell. At every RAW entry, HOST boundary,
task yield, debugger pause, and GC root scan, the canonical state is byte-for-
byte the same. No hidden state was introduced.

## 6. Correctness evidence

- Full regression: **326 ok, 0 fail** (`-O2`), `./check-frozen-s1.sh` passes.
- Every evaluator event counter is unchanged (proving the same work is done):
  `calls 242,785`, `subexpr 2,670,637`, `blkeval 485,572`, `lookups 1,092,531`,
  `lex-direct 606,962`, `hash-probes 2,185,061`, `natives 849,746`,
  `allocs 1`, `max-depth 25`.
- One test-infrastructure fix: `r0_s1_tests.c` test "raw @/! store+fetch" used a
  hard-coded address `9000`, which is inside the code region and collides with
  raw fragments once the evaluator code shrank; it now uses the symbolic
  `SCRATCH_A` cell (as the `examples/raw-memory.r0` example already does). This
  is a latent test bug exposed by the code shrink, not an evaluator change.

See `FIB-OPT-P7-RESULTS.md` for before/after instruction counts and timing.
