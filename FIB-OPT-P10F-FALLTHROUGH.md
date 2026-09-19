# FIB-OPT-P10F — Sequential fall-through / dispatch elimination

## 1. Hypothesis

P10E still emits an indirect `goto *T[ip-256]` after every S1 instruction —
~917M dynamic indirect jumps for `fib 25`. P10F tests whether eliminating
dispatch for ordinary sequential S1 flow (falling through in generated C, and
only dispatching at genuine control flow) materially reduces execution time.

## 2. Portability principle

> Optimisations P10B onward are properties of the high-performance C backend,
> not requirements of the Glon/S1 architecture.

The portable machine model — implement S1 correctly with the seven primitives
and a memory-mapped register model — remains fully expressible without any of
P10B–P10F. P10F changes only the optional compiled backend.

## 3. Transformation

From the P10E emitter, classify each S1 instruction:

- **Sequential** (LIT, DUP, DROP, `@`, `!`, all inline HOST, NEG, and the
  `LIT 0/1/2 @`/`LIT 1 !` register idioms): emit the operation with **no**
  trailing `ip = next; goto *T[ip-256]` — the generated C falls through to the
  next label.
- **Control flow** — `0BRANCH`, the `LIT 0 !` branch idiom (derived
  BRANCH/CALL/EXIT), `HALT`, error/`L_bad` — keep the explicit indirect
  dispatch (or return).

Every label `L_NNNN:` is still emitted, so any branch/call/return target remains
enterable (labels may sit inside a fall-through sequence). At `HALT`/`HOST_DUMP`
the emitter sets `ip = <address>` first so the recorded/observed IP stays
correct.

The promoted `ip` is only semantically observable at control-flow and boundary
operations, so sequential instructions no longer update it.

## 4. What is NOT changed

S1 instruction set, S1/Glon semantics, frozen S1, RAW ABI, frame ABI, task/
debugger/GC semantics, HOST behaviour, the P10E register/TOS+NOS model, and the
P10E flush/reload rules are all untouched. Only the dispatch structure changed.

## 5. Result

See `FIB-OPT-P10F-RESULTS.md`. In short: the dispatch count collapses ~92%
(4,519 → 354 static sites), but runtime barely changes (~1.05×) — the computed
goto was already well predicted by the CPU's branch predictor for sequential
flow, so it was not the dominant remaining cost.
