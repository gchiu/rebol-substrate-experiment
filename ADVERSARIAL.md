# S1 — Adversarial results: combinations of effects

The S1 primitive set is frozen (7 primitives: `LIT`, `DUP`, `DROP`, `@`, `!`,
`0BRANCH`, `HOST`; plus the derived macros `SWAP`, `>R`, `R>`, `R@`, `CALL`,
`EXIT`, `BRANCH`). `tests.c` exercised one control behaviour at a time.
`adversarial.c` exercises **combinations** — the cases where a language design
that models effects as values (antiforms / isotopes / a richer value model)
would claim plain concatenative code stops composing.

The question is not elegance. It is: *can the frozen substrate express and
compose these behaviours at all, and at what cost?*

All six cases pass. None required a VM change. Nothing here is mechanically
impossible. But several things are unsafe or manual in ways that point exactly
at what a richer value/effect model would buy.

---

## Shared vocabulary (how execution state is represented)

Every mechanism below is made of the same three ingredients, all available in
the frozen set:

- **ordinary values** live on the **data stack** (`SP`);
- **call depth / return addresses** live on the **return stack** (`RP`);
- **"where to jump next"** is `IP`, and all three registers are memory-mapped,
  so `@`/`!` can read and write them.

There are exactly two non-local primitives *in the substrate*, and both are
just register stores:

| Mechanism | Saved at install | Restored at fire | Also |
|---|---|---|---|
| `break` / non-local `return` | `{RP, exit IP}` | `RP`, `IP` | leaves `SP` alone |
| `throw` (exception) | `{SP, RP, IP}` | all three | re-pushes the error value |

`break` and non-local `return` are **the same instruction sequence**; they
differ only in which memory cells they read. That identity is the load-bearing
fact for cases 3 and 6 below.

Each test allocates its own bookkeeping cells (a disjoint block of host-visible
memory below `CODE_BASE`), so mechanisms that must not interfere use distinct
cells by construction.

---

## ADV 1 — user-defined loop, body calls a 4-deep chain, break from the leaf

- **Implementable with frozen VM?** Yes.
- **Execution state.** The loop is a user-built loop: a counter in cell `A_I`,
  a backward `BRANCH`, and a `0BRANCH`. The loop's `break` frame is `{A_BRK_RP,
  A_BRK_IP}` saved at loop entry: `A_BRK_RP` is a return-stack depth marker,
  `A_BRK_IP` the exit address. Each of the four nested calls is a `CALL`
  (continuation pushed on `R`). The leaf fires `break` by restoring `RP` to the
  marker (unwinding all four frames in one store) and storing the exit address
  into `IP`.
- **Unaware intermediate code modified?** No. `f1`/`f2`/`f3` are ordinary
  `CALL next; EXIT` words. They neither know nor care that a transfer passed
  through them.
- **Bookkeeping.** Two cells (`A_BRK_RP`, `A_BRK_IP`) installed once at loop
  entry; the leaf must know which cells to read.
- **Values vs control state confused?** No. The loop index and accumulator are
  values in cells; the transfer is a register jump. The break carries no value,
  so there is nothing to confuse with a value.
- **Interference.** Only one mechanism here; n/a.
- **Size / complexity.** 277 assembled cells for the whole test (loop + 4
  nested words + install/fire). Manual and verbose because every `CALL` is 20
  cells and every register access goes through `@`/`!`.
- **Safety.** Nothing stops a stray `!` from writing `A_BRK_RP`/`A_BRK_IP`, or
  `IP`/`RP` directly, corrupting the machine. The break marker is a raw integer
  with no validity check; firing it after the loop has already exited (stale
  marker) would jump into a now-meaningless address.

## ADV 2 — the same loop + a function returning multiple ordinary values

- **Implementable with frozen VM?** Yes.
- **Execution state.** The multiple values are simply **two cells on the data
  stack** (`I` and `I*2`) pushed by the leaf and left there through the
  pass-through chain. The break frame is again `{A_BRK_RP, A_BRK_IP}`. The two
  mechanisms live on different channels: values on `SP`, transfer in cells +
  `RP`/`IP`.
- **Unaware intermediate code modified?** No. `f1`/`f2`/`f3` are the same
  `CALL next; EXIT` pass-throughs; the two values ride the data stack through
  them untouched.
- **Bookkeeping.** One temp cell `A_TMP` to accumulate both values; the break
  frame cells. The caller must know *how many* values the leaf returned (here,
  two) — that arity is in the programmer's head, not in the machine.
- **Values vs control state confused?** No — the transfer and the values use
  different channels. The *arity* of the returned values, however, is not
  represented anywhere; the consumer hard-codes "consume two".
- **Interference.** The break unwinds the return stack but leaves the data
  stack alone; because the consumer drains the two values *before* the break
  can fire, there is no leftover-value hazard. (If a break fired with values
  still on the data stack, they would simply be abandoned — correct but
  implicit.)
- **Size / complexity.** 329 cells. Arity is a convention; nothing enforces
  that the producer and consumer agree.
- **Safety.** A producer/consumer arity mismatch silently pops past the stack
  (reads or writes beyond `SP` into adjacent memory). No type system or arity
  check exists.

## ADV 3 — nested structures: break targets the inner loop, non-local return targets the outer function

- **Implementable with frozen VM?** Yes.
- **Execution state.** Two independent frames, each `{RP marker, exit IP}`, in
  **distinct cells**: `A_BRK_RP/A_BRK_IP` for the inner loop, `A_RET_RP/A_RET_IP`
  for the outer function. `break` restores the inner marker (unwinds only the
  loop's frames, leaves the outer frame intact); `return` restores the outer
  marker (unwinds both). The nesting is captured by the fact that the break
  marker was saved *after* (deeper than) the return marker.
- **Unaware intermediate code modified?** No. `inner` and `leaf` are ordinary
  words; `trigger` merely calls the `return` word.
- **Bookkeeping.** Four cells, installed at two different dynamic levels. The
  programmer must keep the two frames in distinct cells and know which transfer
  belongs to which level.
- **Values vs control state confused?** No. Both transfers are register jumps;
  they carry no value that could be confused with data.
- **Interference.** None, precisely because the cells are distinct. This is the
  key result: the substrate *scopes* a transfer by *which cells it reads*, not
  by any language notion of a target.
- **Size / complexity.** 288 cells. Two nearly identical transfer words
  (`do_brk`, `do_ret`) differ only in their cell operands.
- **Safety.** Nothing prevents wiring `break` to the return cells by mistake —
  the wrong scope would be unwound with no error. The "scoping" is purely
  nominal (a naming discipline over shared memory), not structural.

## ADV 4 — exception crossing functions that are completely unaware of it

- **Implementable with frozen VM?** Yes.
- **Execution state.** The `catch` frame is `{A_C_SP, A_C_RP, A_C_IP}`. The leaf
  computes a partial result (`13`) on the data stack, then `throw`s: it stashes
  the error in `A_TMP`, restores `SP`/`RP` to the catch frame (discarding the
  partial `13` and all three call frames), re-pushes the error, and jumps to
  the handler.
- **Unaware intermediate code modified?** No — this is the point. `f1`/`f2`/`f3`
  are byte-for-byte ordinary functions. They need no `try`, no error
  propagation, no cleanup hook. The throw tears through them by overwriting
  `RP` and `SP`.
- **Bookkeeping.** Three cells for the catch frame, one temp cell for the error
  value (because `SP` is restored before the value is re-pushed).
- **Values vs control state confused?** No. The exception is a register jump
  that *also* restores `SP`, so the partial ordinary value `13` is abandoned by
  construction, not by a value/error tag.
- **Interference.** n/a (one mechanism).
- **Size / complexity.** 205 cells; the smallest test because throw is a
  three-store sequence.
- **Safety.** `A_TMP` is a shared scratch: a `throw` that re-entered before
  another `throw` finished could clobber it. The handler must know to expect
  the error value on the stack; if the transfer is triggered from a context
  where `SP` was not as the catch frame expected, the handler sees garbage.

## ADV 5 — one function with four outcomes, without per-case propagation

- **Implementable with frozen VM?** Yes, but this is where the "value/effect
  model" question lands hardest.
- **Execution state.** The function `produce` uses a **count-on-stack arity
  convention**: it always returns the arity as an integer on top of the data
  stack, followed by the values below it (`0`, `42 1`, `20 10 2`). The fourth
  outcome — the non-local transfer — is *not* a value at all: it is a `break`
  (register jump) that bypasses the caller entirely.
- **Unaware intermediate code modified?** No. `g`/`h` are pass-throughs. The
  four outcomes pass through them without any `switch`/`if`/`match` on their
  part. The *consumer* reads the arity and dispatches.
- **Bookkeeping.** The arity count itself (on the stack), plus the break frame
  cells. The dispatch in the consumer is three `0BRANCH` tests.
- **Values vs control state confused?** Partly, and this is the crux:
  - The **control transfer** is *not* confused with a value — it never appears
    on the data stack; it is a jump. There is no integer that "means break".
  - The **"no result / one result / two results" distinction** is *not*
    represented by the substrate at all. It is a manual count. The machine
    cannot tell "this function returned nothing" from "this function returned
    the integer `0`" — both are just a `0` on the stack. The arity count is a
    self-imposed convention, invisible to the VM.
- **Interference.** The break (outcome 4) and the arity convention (outcomes
  1–3) do not interfere because the break bypasses the consumer; it never
  coexists with a half-read arity.
- **Size / complexity.** 249 cells. The arity convention is composable and
  reentrant *because the count travels on the stack* (no global). A naive
  alternative — a global "shape" cell — would be **non-reentrant** (two nested
  `produce` calls would overwrite each other's shape), which is exactly the
  non-composability the richer model is meant to prevent.
- **Safety.** The arity is *trusted*, not checked. A buggy `produce` that
  pushed count `2` but one value would make the consumer pop past the stack.
  Nothing in the VM links the count to the actual number of pushed values. This
  is the single clearest gap a typed value/effect model would close.

## ADV 6 — two independent mechanisms active simultaneously, targeting different scopes

- **Implementable with frozen VM?** Yes.
- **Execution state.** A `catch` frame (`A_C_SP/A_C_RP/A_C_IP`) is installed
  *outside* a loop; a `break` frame (`A_BRK_RP/A_BRK_IP`) is installed *inside*
  it. Both are live at once. The leaf routes by reading `A_SEL`: one path
  `throw`s (restores `SP`/`RP`/`IP` to the catch frame), the other `break`s
  (restores `RP`/`IP` to the loop frame).
- **Unaware intermediate code modified?** No.
- **Bookkeeping.** Five cells across the two frames; the routing decision is a
  single `0BRANCH` on `A_SEL`.
- **Values vs control state confused?** No. There is no "signal value" that two
  mechanisms compete to interpret. Each mechanism is a *word* (`throw` vs
  `break`); the programmer routes by choosing the word, not by tagging a value.
- **Interference.** None, in both directions:
  - `break` restores only `RP`/`IP`, so it stops at the loop and **does not**
    disturb the still-installed catch frame (the catch's `SP`/`RP`/`IP` cells
    remain valid and unused).
  - `throw` restores `SP`/`RP`/`IP` to the catch frame, correctly bypassing the
    loop and its break frame in one jump; the break frame's stale cells are
    simply never read again.
  The two mechanisms do not interfere because they touch disjoint cells and
  disjoint register subsets. This is a property the programmer must preserve by
  hand; the substrate does not guarantee it.
- **Size / complexity.** 149 cells — the cheapest case, because it is *just*
  two installed frames plus a one-bit route.
- **Safety.** The break frame left stale after a `throw` is a latent hazard: if
  any later code resumed the loop (or fired the stale break), it would jump to
  an address that is no longer a valid loop exit. Stale-marker reuse is the
  dominant safety risk of the whole approach.

---

## Summary

| Case | Expressible? | Unaware code changed? | Values vs control confused? | Mechanisms interfere? |
|---|---|---|---|---|
| 1. break N-deep in a loop | yes | no | no | n/a |
| 2. loop + multi-value return | yes | no | no (arity is manual) | no |
| 3. break + return at different levels | yes | no | no | no |
| 4. exception crossing unaware code | yes | no | no | n/a |
| 5. four-outcome function | yes | no | **partly** (arity untyped) | no |
| 6. two mechanisms at once | yes | no | no | no |

### What this tells us about the "richer value/effect model" claim

The claim under test is that certain semantic/control behaviours become
**non-composable** without a richer value/effect model. The frozen S1 substrate
can *express* all six cases, and it can *compose* them — provided the
programmer supplies, by hand, the discipline that a richer model would provide
by construction:

1. **Effects are not values.** S1's answer to "is this a value or a control
   signal?" is "control is a jump, never a value." That works, but it means
   there is no uniform way to *pass* a control effect around or store it; each
   effect is a dedicated word plus dedicated cells. Two effects coexist only
   because the programmer keeps them apart.

2. **Arity is not represented.** The "no result / one / many" distinction is a
   count-on-stack convention that the VM cannot see. It composes (count travels
   on the stack, so it is reentrant), but it is unenforced: a mismatch silently
   corrupts the stack. This is the closest S1 comes to a genuine failure, and it
   is a *type-safety* failure, not a *mechanical* one.

3. **Scoping is nominal, not structural.** A `break` targets the loop whose
   marker cells it reads. Nothing connects a `break` word to a loop *except the
   programmer's choice of cells*. Wire it to the wrong cells and it silently
   unwinds the wrong scope.

### Genuinely impossible without a new primitive?

**No.** Nothing in these six cases required adding, removing, or reinterpreting
a primitive. The frozen 7-primitive set, plus its derived macros, expressed
every behaviour. The one irreducible capability S1 leans on — the ability to
read and write `IP`/`SP`/`RP` as ordinary memory via `@`/`!` — was already in
the frozen set from the start.

What a richer value/effect model would *buy* is not expressiveness but
**safety and ergonomics**: machine-checked arity, effect values that cannot be
confused with data, and structural (not nominal) scoping. Those are real gaps,
and they are measured here as the source of every "Safety" note above — but
they are gaps in the *type discipline above the VM*, not in the *mechanical
capability of the VM*.
