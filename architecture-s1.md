# Adversarial critique of S0, and a retraction

You are right, and I want to correct my own claim before describing S1.

My S0 proposal made an assertion it did not actually support: that delimited continuations are *necessary*. That claim conflated three different things:

- **expressiveness** (can the substrate compute every control flow the high level needs?),
- **cost** (how much work does the high level pay for non-local control?), and
- **ergonomics/safety** (does the high level have to reason about raw stacks?).

Continuations buy you the second and third. They buy you **nothing** for the first. Anything a continuation can express can be expressed by a machine that lets you *read and write its execution state* (program counter + stack pointers + stack contents) as ordinary memory. That is a mechanical capability, not a semantic one — it is what every real CPU already has, and what Forth multitaskers have used for decades.

So the honest verdict on my own design: **S0 solved the problem the way a language designer would** — it noticed that all the desired features reduce to "capture the rest of the computation," invented a beautiful universal value for that ("the continuation"), and *burned it into the substrate as first-class semantics*. That is precisely the failure mode this experiment exists to test against. I overclaimed. Continuations are powerful and elegant; they are not necessary.

---

## The load-bearing distinction (constraint 9)

The whole S0-vs-S1 question turns on one distinction, so I will state it up front and keep using it:

> **"Impossible without a primitive"** means: the substrate as specified has no *mechanical* way to reach the behavior — e.g. no way to observe a value, no way to leave the machine, no way to alter the program counter.

> **"Inconvenient without a primitive"** means: the substrate *can* reach the behavior, but only through boilerplate (stack-depth bookkeeping, copying a stack to a buffer, saving three registers by hand) that is verbose, bug-prone, or slow.

My claim about S0 should have been: *delimited continuations are a convenience primitive, not a necessity primitive.* Almost everything S0 does with them, S1 does with a saved `{IP, SP, RP}` triple and stack copies/swaps. The differences are cost and ergonomics, not possibility.

---

# S1: a plain Forth-style machine, no continuations

## 1. VM state

Everything lives in one flat, cell-addressed memory `M`. The machine has three registers, and — this is the one deliberate design move — **they are memory-mapped at fixed addresses** so that the ordinary load/store primitives can read and write them:

| Register | Meaning | Memory-mapped |
|---|---|---|
| `IP` | instruction pointer | yes (e.g. `M[0]`) |
| `SP` | data-stack pointer | yes (e.g. `M[1]`) |
| `RP` | return/control-stack pointer | yes (e.g. `M[2]`) |

- **Data stack `D`** — a region of `M`, grows down, `SP` points at top.
- **Return stack `R`** — a second region of `M`, grows down, `RP` points at top. Used for return addresses, loop bookkeeping, and *anything the runtime wants to stash*.
- **Heap** — the rest of `M`; a single allocation pointer (or a few) managed entirely *above* the VM.
- **`IP`** — points into `M` at the current instruction.

There is **no environment register**. There is **no typed value model**. There are **no closures, contexts, or blocks** as machine concepts. A "value" is a machine word (an integer, which is also an address). Structure is a convention the high level imposes, not a fact the machine knows.

Memory-mapping the registers is the key to satisfying constraints 6 and 7 with *zero new primitives*: `@` and `!` already read and write `IP`/`SP`/`RP`. To save execution state you save three words and (if needed) copy a stack region. To resume, you restore them.

*(Variant worth noting: one could use dedicated `@IP`/`!IP`/`@SP`… instructions instead of memory-mapping. Memory-mapping is chosen because it makes the machine transparent — the entire state is inspectable with the two instructions you already have — at the cost of some aliasing hazards. The experiment is better served by maximum transparency.)*

## 2. Instruction set

Only nine instructions are irreducible. A few conveniences are listed because Forth programmers expect them; every one is derivable from the irreducible set and marked as such.

| # | Instruction | Effect | Essential or derivable |
|---|---|---|---|
| 1 | `LIT n` | push inline literal onto `D` | **Essential** — only way to introduce a constant |
| 2 | `DUP` | duplicate top of `D` | **Essential** — reuse a value |
| 3 | `DROP` | discard top of `D` | **Essential** — discard a value |
| 4 | `@` | pop `a`, push `M[a]` | **Essential** — read memory; *also reads `IP`/`SP`/`RP` because they are memory* |
| 5 | `!` | pop `v`, `a`; `M[a] := v` | **Essential** — write memory; *also writes the registers, so it is jump, stack-pointer move, and store in one* |
| 6 | `0BRANCH` | pop flag; if false, `IP :=` inline addr | **Essential** — the *only* value-inspecting decision in the machine |
| 7 | `HOST` | pop a native id; invoke host code (host has access to `D`, `R`, `M`) | **Essential** — the only escape from the machine (I/O, arithmetic, FFI) |
| 8 | `SWAP` | exchange top two | *Derivable* |
| 9 | `>R` | move `D`→`R` | *Derivable* from `@`/`!` + memory-mapped `SP`/`RP` (decrement `RP`, store) |
| 10 | `R>` | move `R`→`D` | *Derivable* |
| 11 | `CALL` | pop `a`; push `IP` onto `R`; `IP := a` | *Derivable* from `@`/`!`/`>R` — kept for speed and readability |
| 12 | `EXIT` | pop `R` into `IP` | *Derivable* |
| 13 | `BRANCH` | `IP :=` inline addr | *Derivable* — unconditional jump is just a store to `IP` |

**The entire control semantics of S1 is a single conditional branch.** Call, return, and jump are *not* semantic decisions — they are three different ways of storing to the program counter. That is the crucial contrast with S0, where `RUN`/`RESET`/`CAPTURE`/`INVOKE` each encode a *semantic* theory of control flow.

## 3. Why context, closure, and block are not needed (constraint 4)

- **Context** is an association table (word → value). A table in memory built with `@`/`!` (or a parent-linked frame) *is* a context. The VM never resolves a word; the high-level evaluator does the lookup itself. No environment register, no lookup instruction.
- **Closure** is a pair `(code-address, environment-pointer)` — two adjacent cells. Calling a closure is `CALL` of the code address after arranging the environment pointer via the stack. No closure type needed.
- **Block** is a contiguous region of memory. Memory is *already* a block. "Code" vs "data" is a matter of what `IP` points at — homoiconicity is a *convention*, not a machine feature.

None of these require a VM-level type because the machine's single value type (a word) is *already* an address, and an address is *already* all three of those things. The high level imposes tags by convention (e.g. the low bits of a pointer), and the VM stays ignorant.

## 4. Building the high-level features in S1 (constraint 8)

Everything below uses only `@`, `!`, `>R`/`R>`, and the memory-mapped `IP`/`SP`/`RP`.

**Loops.** A `while` compiles to an unconditional backward branch plus a `0BRANCH`. Static `break`/`continue` compile to forward branches (the compiler knows the exit address). This is exactly how Forth compiles `BEGIN…WHILE…REPEAT`.

**Dynamic non-local exit** (`break`/`continue`/`return` that must fire from arbitrary nested calls). At loop entry, save `RP` (a depth marker) and the exit `IP`. `break` is a substrate word that resets `RP` to the marker and stores the exit address into `IP` — i.e. it unwinds the return stack by writing a saved pointer and jumps. Two stores. No primitive.

**Exceptions (`CATCH`/`THROW`).** `CATCH` saves `{SP, RP, IP}` (the handler continuation), then runs the protected code. `THROW` restores the three saved words and pushes the error value. Because `SP`/`RP`/`IP` are memory-mapped, "restore the state" is literally three `!` operations. This is the classic Forth user-level `CATCH`/`THROW`, and it is fully buildable — indeed Forth programs have built it for years.

**Generators / suspend-resume.** This is the one place S1 pays a real cost. To suspend a generator you must preserve not just `IP`/`SP`/`RP` but the *contents* of its `D` and `R` stacks (its live data sits on them). Two techniques:

- **Copy**: allocate a heap buffer, copy the used slice of `D` and `R`, store `IP`. Resume = copy back and restore the three registers. Cost `O(stack size)` per suspend.
- **Per-generator stack regions** (the Forth multitasker trick): give each generator its own `D`/`R` region in memory. Suspend = save three words and switch the region pointers; resume = restore three words. Cost `O(1)`, at the price of stack-region allocation (fixed or segmented).

An async scheduler is just a substrate program round-robining among saved `{IP, SP, RP}` triples — this is how Forth cooperative multitasking has worked since the 1970s.

All of these are *possible*; none of them is elegant; several of them are footguns if done by hand.

---

## 5. Impossible vs. inconvenient (constraint 9)

| Capability | S1 verdict |
|---|---|
| Read/write memory; read/write the program counter, data pointer, return pointer | **Native** (memory-mapped registers) |
| Conditional decision | **Impossible without `0BRANCH`** — this is the one irreducible control semantic |
| Reach the outside world | **Impossible without `HOST`** — a closed machine observes nothing |
| Call/return | **Inconvenient** — `CALL`/`EXIT` are stores to `IP`; kept as sugar |
| Unconditional jump | **Inconvenient** — a store to `IP` |
| Non-local exit / `break` / exceptions | **Possible, inconvenient** — save/restore `IP`/`SP`/`RP` |
| Generators / suspension | **Possible, costly** — stack copy, or stack-region management |
| Delimited control composition | **Possible, very inconvenient** — manual marker nesting; no type discipline prevents corrupting the stacks |
| Backtracking / multi-shot capture | **Possible, costly** — copy the state per retry |
| Closures / contexts / blocks | **Not needed** — conventions over memory |

The *only* two things that are genuinely impossible without a primitive are **a conditional branch** and **a host escape**. Continuations are not among them.

---

## 6. S0 vs S1 (constraint 10)

| Axis | S0 (delimited continuations) | S1 (plain Forth + memory-mapped state) |
|---|---|---|
| **Irreducible primitives** | ~13 (`LIT`, `DUP`, `DROP`, `GET`, `SET`, `RUN`, `RESET`, `CAPTURE`, `INVOKE`, `SELECT`, `BOX`, `UNBOX`, `SETBOX`) | ~7 (`LIT`, `DUP`, `DROP`, `@`, `!`, `0BRANCH`, `HOST`) |
| **Semantic assumptions in the VM** | Typed values (block/context/closure/box/cont); environment semantics; call/return semantics; delimited control; one-shot linearity | One value type (word); exactly one control semantic (conditional branch). Call/return/jump are mechanism, not semantics |
| **Implementation complexity** | High: tag-checked values, GC, continuation frames, prompt delimiters, linearity tracking | Low: three registers, two stacks, one branch, one host call; near-trivial to verify |
| **Cost on ordinary programs** | Constant tax: every value tagged, every call dispatched, GC pressure — subsidizes the *rare* case (non-local control) | Minimal for ordinary code: untagged words, direct jumps. Non-local control pays extra (copy or region management) |
| **Ease of extending the high-level language** | Easy to add *control* abstractions (continuations are pre-built); hard to change the *value model* (baked into the VM) | Easy to change *everything*, since the VM commits to almost nothing; but each control abstraction re-derives stack bookkeeping |
| **New control abstraction needs a VM change?** | No | No |

Both answers share the headline result — **a new control abstraction never requires a VM change.** The difference is *what the substrate pre-commits to*. S0 pre-commits to a rich theory of values and control; S1 pre-commits to almost nothing and forces the high level to own all of it.

---

## 7. Revisiting the "necessary" claim

My S0 proposal was wrong to say continuations are *necessary*. The accurate statement is:

> **Delimited continuations are the most elegant and cheapest way to give a high-level language composable non-local control, but they are not necessary. A substrate that exposes its program counter, stack pointers, and stack contents as ordinary memory — S1 — can express every one of the required features with no continuation primitive, at the cost of stack bookkeeping, stack copying, and the loss of the type safety that S0's first-class continuations provide.**

Moreover, S1 is the better *instrument* for this experiment, because it does not presuppose the answer. S0 already *believes* that control flow should be a first-class, composable semantic object; it would be hard to tell, from S0's results, whether that belief is load-bearing. S1 starts from the null hypothesis — "a conditional branch and a memory-mapped program counter are all the control the substrate owes anyone" — and lets the experiment discover how much of S0's machinery the high level actually has to *reconstruct by hand*, and at what cost.

If I were running the experiment, I would build **S1** first, measure precisely where it hurts (generator suspension cost, exception boilerplate, stack-corruption bugs), and only then ask whether a single *convenience* primitive — and which one — is worth promoting into the substrate. That question is answerable from data only if you start dumb.
