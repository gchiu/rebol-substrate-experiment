# Architecture Proposal: a delimited-continuation concatenative substrate ("S0")

## 1. Guiding thesis

The smallest substrate that preserves a REBOL-like abstraction is a **concatenative (Forth-shaped) machine whose only special control feature is delimited, first-class continuations**, plus a tagged value model in which code and data are the same kind of thing (blocks). Everything REBOL-specific — evaluation rules, arity, refinements, series, paths, `if`, `while`, `try`, generators — is built *above* the machine as ordinary substrate programs, in a "tower of interpreters."

Why continuations are the load-bearing idea: one mechanism subsumes call/return, exceptions, non-local transfer, `break`/`continue`, suspension, and *user-defined control structures*. If you do not put continuations in the machine, you must instead put five or six purpose-specific control primitives in (violating constraint #2), or you cannot express those features at all. So the answer to "continuations, only if actually necessary" is: **delimited continuations are necessary**, but *only* them — not the surrounding machinery people usually bundle with them.

---

## 2. VM state

The machine is a small CESK-style machine (Control / Environment / Store / Kontinuation), dressed as a concatenative interpreter:

| Component | Role |
|---|---|
| **Data stack `D`** | Operand stack (Forth style). The only channel between operations. |
| **Continuation stack `K`** | A stack of *continuation frames* plus optional *prompt markers*. This is "what happens after the currently running code." |
| **Environment `E`** | The current *context* value used to resolve bindings (`GET`/`SET`). One register, pointing into the store. |
| **Control `C`** | The current instruction stream (a block + instruction pointer). |
| **Store `S`** | The heap: boxes (mutable cells) and contexts. |

`E` is the only piece of "environment machinery," and it is just a value. There are no other registers; there is no call stack *separate from* `K`. A call is `K` growing; a return is `K` shrinking.

---

## 3. Memory / value model

A value is a tagged machine word: `tag + payload`. Payload is either inline (small ints) or a reference into `S`.

- `none`, `true`, `false` — the empty value and truth values.
- `int`, `dec`, `str` — scalars. **Immutable.**
- `word` — an interned symbol. **Inert data** (unlike REBOL, a word here does not auto-evaluate).
- `block` — an immutable sequence of values. **Used both as data and as code** (homoiconic). Immutability enables cheap structural sharing and simplifies reasoning about continuation capture.
- `context` — a mutable map `word → value`, optionally with a *parent* link (for lexical shadowing/objects). This *is* the environment and the object system; there is no separate environment type.
- `closure` — `(block, context)`. A function value: the block is code, the context is its lexical scope.
- `box` — a mutable cell holding one value. The only primitive mutable state.
- `cont` — a reified continuation (a value on the same footing as any other).
- `native` — a reference to a host function. The escape hatch to the outside world.

**Split:** *immutable* things are blocks and scalars; *mutable* things are boxes and contexts. This is the minimal partition that gives "ordinary values and mutable state" with no hidden aliasing surprises. REBOL's mutable series are reconstructed above the VM as `box`-wrapped blocks (or the split is relaxed later if profiling demands it — flagged in §9).

---

## 4. Instruction set

Fourteen instructions. Each line is justified; the right-hand column marks **essential** (the substrate cannot express general computation without it) vs **derivable** (sugar kept only for economy of substrate programs).

| # | Instruction | Effect | Purpose / justification |
|---|---|---|---|
| 1 | `LIT v` | push `v` | **Essential.** The only way to introduce a constant into the machine. |
| 2 | `DUP` | `x → x x` | **Essential.** Reusing a value (test it *and* keep it) requires duplication. |
| 3 | `DROP` | `x →` | **Essential.** Discarding an operand. |
| 4 | `SWAP` | `x y → y x` | *Derivable.* Operand reordering for concatenative composition; kept because removing it makes every substrate program twice as long. `ROT`, `OVER`, etc. are further sugar and are *not* instructions. |
| 5 | `GET` | pop `word`, push its binding in `E` | **Essential.** Reading a binding. Explicit, because the substrate does not auto-evaluate words (keeps REBOL semantics out). |
| 6 | `SET` | pop `value`, `word`; bind `word→value` in `E` | **Essential.** Writing a binding. Together with `GET` and contexts-as-values this is the entire environment/binding story. |
| 7 | `RUN` | pop code value; execute it | **Essential.** The only way to invoke code. Dispatch: `block` → splice it into `C`; `closure` → push current continuation onto `K`, switch `E` to the closure's context, run its block; `native` → call host routine. This single instruction is *call* for blocks/closures and *foreign call* for natives. |
| 8 | `RESET` | push a *prompt marker* onto `K` | **Essential.** Delimits a region whose continuation may be captured. Bounds how much control is reified. |
| 9 | `CAPTURE` | pop `K` frames up to (not including) the nearest prompt; bundle them into a `cont` value `k`; push `k` onto `D`; continue execution *after* the prompt | **Essential.** Reifies the current computation (up to the delimiter) as a first-class value and aborts to the delimiter. This is `shift`-style capture. |
| 10 | `INVOKE` | pop `k` (and an optional value); make `k` the current continuation, resuming it | **Essential.** Resumes a captured continuation. This *is* non-local transfer, exception raise, `break`/`continue`, generator resume, and (with the call stack) return. |
| 11 | `SELECT` | pop `cond`, `then-block`, `else-block`; run one | **Essential.** The single value-inspecting decision point. Some primitive must branch on truth; this is it. Every richer conditional is built above. |
| 12 | `BOX` | pop `v`; allocate a box holding `v`; push the box ref | **Essential.** Creating mutable state. |
| 13 | `UNBOX` | pop box; push its contents | **Essential.** Reading mutable state. |
| 14 | `SETBOX` | pop `v`, box; store `v` | **Essential.** Writing mutable state. |

**Not in the instruction set, because derivable:** `CALL`/`RET` (≈ `RUN` + end-of-block return; early return ≈ `CAPTURE`+`INVOKE`), `PUSH-ENV`/`POP-ENV` (≈ `RUN` on a closure), `THROW`/`CATCH` (≈ `RESET`+`CAPTURE`+`INVOKE`), arithmetic, comparison, I/O, string/series ops (host *words*, not instructions — §6).

This is the whole machine: **3 data-stack ops, 2 binding ops, 4 control ops, 1 branch, 3 mutation ops.** The control group is the interesting one.

---

## 5. Control transfer, calls, and the continuation model

**Call.** `RUN` on a closure pushes the current continuation (everything after the call site) onto `K`, switches `E` to the closure's lexical context, and runs its body.

**Return.** A block "returns" simply by running out of instructions; the machine pops the top of `K` and resumes. There is no `RET` instruction. Early return is a *non-local* act: capture the caller's continuation at the call site and `INVOKE` it, abandoning the current computation.

**The continuation semantics, precisely** (one-shot, delimited):

- `RESET` plants a marker. The computation below the marker is the "outside" continuation; above it is the "inside."
- `CAPTURE` severs everything above the nearest marker, turns it into a value `k`, and transfers control to the outside (just past the `RESET`). The inside is *abandoned* but preserved as `k`.
- `INVOKE k` replaces the current continuation with `k` (and pushes its own continuation underneath, so `k` composes correctly). When `k` is exhausted, control returns to the `INVOKE` site.

Continuations are **one-shot by default** (linear): each `cont` is consumed by a single `INVOKE`. One-shotness makes them cheap (no copying of `K`) and safe (no accidental resource re-entry). Making them **multi-shot** (copyable) is a store-level change, not a new instruction, and it is precisely what enables backtracking / `amb` / Prolog-style search *above* the VM with zero instruction-set changes — a strong demonstration of the substrate's generality. I flag this as the recommended later extension, not the baseline.

---

## 6. Host vocabulary (explicitly *not* instructions)

Arithmetic, comparisons, string/series operations, and all I/O are provided as **`native` words installed in a context** — a pluggable dictionary of host routines. The VM's fixed instruction set deliberately contains no numeric or side-effecting primitives, because none of them are needed for the *generality* the substrate exists to provide, and every one of them is easy to add as a word later. This is the cleanest possible answer to constraint #2: "native" is the one mechanism, and every host feature rides on it.

---

## 7. Building an evaluator above it

The REBOL-like language is a substrate program — a **metacircular evaluator** whose core is a dispatch loop over block values:

- a `word` → `GET` it (and handle "unset" as a policy, not a machine rule);
- a `block` → treat as code or data according to *the evaluator's own* rules (quoting is a high-level decision);
- a `closure` → `RUN` it;
- etc.

**Functions** are closures. "Calling" a function means: build a fresh child `context` (a store value), bind the parameter words into it with `SET`, then `RUN` the closure body with that context as `E`. Arity, refinements, default arguments, and evaluation order are all *code in the evaluator*, not machine rules. The machine never knows what a "function call with refinements" is; it only knows `RUN`.

**Control structures** are closures that use `RESET`/`CAPTURE`/`INVOKE`. For example, `loop` is an ordinary closure that: installs a `RESET`, captures the "continue-point" continuation into a box, runs its body; the words `break` and `continue` are bound to closures that `INVOKE` the stored continuation. The evaluator treats `loop`, `break`, `continue` exactly as it treats any other function — it has no idea they are "control structure."

This is the point of the whole design: **the machine has no idea which functions are "normal" and which are "control flow."** Control flow is an emergent property of closures manipulating continuations.

---

## 8. Defining a genuinely new control structure without touching the VM

A programmer defines a new control structure by writing a closure (in high-level code) that arranges `RESET`/`CAPTURE`/`INVOKE` to produce the desired flow, and publishing it as a word. Examples that require **zero** VM changes:

- **`try`/`throw`** — a handler installs a `RESET`, captures the "unwind-to-handler" continuation, stores it; `throw` `INVOKE`s it.
- **`while`/`until`** — capture the "top of loop" continuation; `break`/`continue`/`repeat` `INVOKE` it.
- **`foreach`** — a loop that also captures and *exposes* the element-injection continuation.
- **Generators / `yield`** — `yield` `CAPTURE`s the current continuation into a box and returns to the consumer; resuming `INVOKE`s that box.
- **Non-local `return` from a nested function** — `INVOKE` a continuation captured at the outer call site.
- **A `choose`/backtracking operator** — with multi-shot continuations, capture-and-retry.
- **Coroutines / `async`/`await`-like scheduling** — capture continuations and hand them to a scheduler built as ordinary substrate code.

A new control structure is *just a library function.* The language's normal abstraction is not "punctured"; the substrate is *reached through* it.

---

## 9. Deliberate descent into the substrate

The high-level language exposes one escape hatch: a marker form (e.g. `substrate [ ... ]` or `do/native`) that bypasses the high-level evaluator and hands a block directly to the machine's `RUN`. This lets a programmer, when the normal abstraction is insufficient, write raw concatenative code (`DUP`/`GET`/`SET`/`RESET`/`CAPTURE`/`INVOKE`/...) that manipulates stacks and continuations directly, or define a new `native` bridge. It also enables a *nested* tower: a custom evaluator written above the substrate can be swapped in for a restricted dialect, a different evaluation strategy (lazy, backtracking), or a debugger/profiler — all without VM modification. The substrate is the fixed point the whole tower bottoms out on.

---

## 10. Deliberately left out (and why)

- **Arithmetic/comparison/I/O/strings/series** — host `native` words, not instructions (§6).
- **Explicit `CALL`/`RET`, `PUSH-ENV`, `THROW`/`CATCH`, `BREAK`/`CONTINUE`** — all derivable from `RUN`/`RESET`/`CAPTURE`/`INVOKE`.
- **Any notion of arity, refinement, evaluation order, quoting, or word-as-value** — REBOL semantics; lives in the evaluator.
- **A mutable "series" type** — mutable state is boxes + contexts only; REBOL series are rebuilt above.
- **Special environments as machine machinery** — contexts are just values.
- **A real `RET`/return value** — returns are continuations; results are passed on `D`.
- **Recursion depth limits / GC / JIT** — implementation concerns, not architectural ones (but see §11).
- **Typed dispatch on argument kinds** — `RUN` dispatches only on the *value's* tag, nothing else.

---

## 11. Weaknesses and trade-offs

1. **Continuation capture cost.** `CAPTURE` must freeze the portion of `K` above the prompt. Bounded by the delimiter, but still real. Mitigation: one-shot continuations + stack chunking; the high-level evaluator caches continuation frames where it can.
2. **Verbose substrate code.** Explicit `GET`/`SET`/`SWAP` makes substrate programs long. Accepted: substrate code is rare and machine-generated (the evaluator is written once).
3. **Immutable blocks vs. REBOL's mutable series.** The split keeps the model clean but pushes series mutation into `box`-wrapping; this may show up in microbenchmarks and in ergonomics. It is the single most likely thing to revisit.
4. **Continuation ↔ GC interaction.** A `cont` pins store objects reachable from `K`. Needs precise (or at least continuation-aware) GC, or copied stack segments. Non-trivial engineering.
5. **Power without safety.** `INVOKE` of a stale continuation is a real hazard (double-resume, dangling boxes). One-shot linearity mitigates; a stronger type/stability discipline may be wanted above the VM.
6. **Performance.** A tower of interpreters over a small interpreted machine is slow. The substrate is designed for *correctness and generality first*; a later JIT or partial-evaluation pass is anticipated, not built in.
7. **The "one more convenience" temptation.** The instruction set wants to grow (`ROT`, `OVER`, `>R`…). Discipline: additions must be justified as *generality*, not convenience; conveniences are library words.

---

## 12. Alternative designs considered

1. **Pure Forth (threaded code, no continuations).** Faithful to "Forth-like" but *cannot* express non-local control, exceptions, or suspension without adding exactly the special instructions the constraints forbid. Rejected as insufficiently general.
2. **SECD machine.** Clean for the lambda calculus, and the `Dump` doubles as continuations. But environments and mutation are more awkward than in a concatenative machine, and it fits REBOL's imperative, series-oriented flavor poorly.
3. **Register machine + program counter (a "real" CPU).** Too low-level; loses the composability that lets a control structure be a *value*. You'd rebuild concatenation anyway.
4. **SKI / graph reduction.** Elegant and minimal, but a poor fit for imperative mutation and REBOL-style evaluation; hard to make efficient without a serious runtime.
5. **Full undelimited `call/cc`.** Simpler (drop `RESET`), but composes badly: each capture reaches the whole universe, so handlers and generators cannot be cleanly nested or reused. Delimited continuations chosen specifically for *composability*.
6. **A purpose-built instruction per high-level feature** (`IF`, `LOOP`, `THROW`, `YIELD`…). Directly violates constraint #2; rejected on principle.
7. **No substrate at all (self-hosted REBOL only).** Then there is nowhere for the language to "descend into"; the experiment's core question becomes unanswerable. Rejected by the premise.

---

## 13. Answer to the research question

The smallest programmable substrate that preserves a REBOL-like abstraction while remaining a genuine escape hatch is:

> **an immutable-value concatenative machine (tagged words + blocks + contexts + boxes) whose fixed instruction set is only data movement, explicit binding, and *delimited one-shot continuations* (`RUN`/`RESET`/`CAPTURE`/`INVOKE`), with everything else — including the evaluator itself, and every control structure — expressed as words above it.**

Continuations are the one "expensive" idea, and they are justified because they are the single mechanism from which *all* non-local and user-extensible control flow is derived. Everything else the experiment is about (calls, environments, loops, exceptions, suspension, and new control structures) falls out of ordinary substrate code rather than out of the VM.
