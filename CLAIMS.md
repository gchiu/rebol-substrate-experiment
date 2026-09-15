# S1 — Testing the designer's claims (the "isotope/antiform" argument)

This document treats the source text as a set of *claims to be tested*, not as
facts. The S1 VM and the clean-room tests are frozen; nothing here modifies,
extends, reinterprets, or adds a primitive to S1. The new tests are in
`claims.c` (`run_claim_tests`), built entirely from the frozen primitives
(`LIT`, `DUP`, `DROP`, `@`, `!`, `0BRANCH`, `HOST`) plus the derived macros in
`s1.h`.

The question, per the experiment's own constraint 9, is not elegance but:

> **"Impossible without a primitive"** — the substrate has no *mechanical* way
> to reach the behaviour. **"Inconvenient without a primitive"** — it can, but
> only through verbose, bug-prone, or slow bookkeeping.

Every claim below is graded on six axes: **mechanical necessity, safety,
composability, ergonomics, efficiency, semantic clarity.** "S1 can express it"
is *not* read as "S1 is better", and S1's awkwardness is *not* read as proof
that richer semantics are necessary.

---

## 1. The central structural claim

**Neutral restatement.** A control/effect (e.g. `BREAK`) must travel as a
*value* in order to cross code that does not know about it (e.g. `FOR-BOTH`),
because values are the only thing that passes through unaware code. Once the
signal is a value, one must ask what kind of value it is and how to stop it
being confused with data — which is exactly the isotope model. Hence: "you
can't patch a representational gap with a procedural addition."

**Requirement asserted.** That the *value space* is the only up-channel between
a callee and its caller, so any out-of-band signal is forced into the value
space and requires a value-level distinction (isotopes/antiforms).

**Does frozen S1 reproduce it?** No — and this is the one place the claim is
mechanically false, not merely inconvenient. S1 has a second up-channel the
value model does not: the **memory-mapped machine registers** `IP`/`SP`/`RP`,
writable through the ordinary `!` primitive. A control signal crosses unaware
code by *overwriting the return-stack pointer and the program counter*, not by
passing a value through the unaware frames.

**Test.** `claim2_signal_not_a_value` puts a value (`42`) on the data stack,
then fires a `break` from `leaf` through three unaware pass-through functions
(`f1`→`f2`→`f3` are byte-for-byte `CALL next; EXIT`). The test asserts that
the break moved `RP`/`IP` but did **not** move `SP`, and that `42` survived
untouched. The signal carried no value; nothing was pushed, popped, or tagged
as "break". The unaware code never saw it.

**Categorisation.**

- *Mechanical necessity:* **none.** The signal does not need a value to cross
  unaware code; it needs writable control state, which the frozen set already
  has (`@`/`!` over memory-mapped `IP`/`RP`).
- *Safety:* the break is a raw `RP`/`IP` store. A stale marker, a miswired
  cell, or a stray `!` corrupts the machine silently. The value model would
  prevent some of this; S1 does not.
- *Composability:* the signal composes with *values* (see §4) precisely
  because it lives on a different channel. But it does not compose *safely* —
  two effects coexist only because the programmer keeps their cells apart.
- *Ergonomics/efficiency/semantic clarity:* the value model wins all three;
  S1's version is manual, opaque, and untyped.

**The precise refutation.** The claim "values are the only thing that travels
through code that doesn't know about them" is a theorem about a *class* of
machines — those whose only inter-frame channel is the value space. S1 is not
in that class: its registers are ordinary memory, so a signal can travel
through the *machine* (by rewriting `RP`/`IP`) rather than through the *code*.
The designer's argument is true of Ren-C's evaluator; it is not a property of
substrates in general, and S1 is the counterexample.

---

## 2. The designer's falsifiable challenge

**Neutral restatement.** "Write one function that hits two distinctions at once
— a loop wrapper that can both be broken out of and return multiple values —
and show the two mechanisms composing without either one knowing about the
other."

**Requirement asserted.** That a single function threading two out-of-band
channels (a `BREAK` and a multi-value return) requires the two mechanisms to
know about each other, or is otherwise impossible/impractical.

**Does frozen S1 reproduce it?** Yes.

**Test.** `claim1_for_both` builds `for-both ( N -- i acc )`: a loop wrapper
that returns *two* values and can be broken out of. A `leaf` reached through an
unaware `body` breaks when the accumulator reaches a target. Both the break and
the exhaustion path return `(i, acc)`. The break restores only `RP`/`IP`; the
two return values ride the data stack. The two mechanisms never touch each
other's channel, so neither needs to know the other exists.

**Categorisation.**

- *Mechanical necessity:* none. The two mechanisms are independent channels
  (`SP` vs `RP`/`IP`), so there is no "threading" of two unrelated out-of-band
  channels to get right — they are disjoint by construction.
- *Safety:* the arity (two) is in the programmer's head. A producer/consumer
  mismatch silently reads past the stack. The break marker is an unchecked raw
  integer.
- *Composability:* composes *because* the channels are disjoint, but that
  disjointness is a discipline the programmer maintains, not a guarantee.

This directly answers the challenge in the affirmative, but the "without either
knowing about the other" part is satisfied by *channel separation*, not by any
language-level guarantee.

---

## 3. The eight distinctions

The source lists eight distinctions it says must exist, each "demonstrated with
a broken example". Graded one by one against frozen S1.

| # | Distinction | S1 mechanically distinguishes? | Category | Test |
|---|---|---|---|---|
| 1 | broke out of a loop vs body evaluated to none | **Yes** — break is a jump (no value); "none" is a value on `SP` | control: mechanical | claim 1 |
| 2 | loop never ran vs loop produced nothing | **Yes, by convention** — a loop can set a "ran" flag; not enforced | control, but convention | (doc) |
| 3 | several return values vs one return value that's a block | **Partially** — S1 counts cells (`SP` before/after) but has no block type, so it cannot tell a block *reference* from a scalar | value/arity: convention only | claim 4 |
| 4 | no value at all vs the value NONE | **Partially** — `0` vs `1` cells is observable, but "NONE" is just one cell; nothing marks it as NONE | value/arity: convention only | claim 4 |
| 5 | failed vs returned something falsey | **Yes** — failure is a non-local jump (restores `SP`/`RP`); falsey is a value on `SP` | control: mechanical | claim 3 |
| 6 | vanished vs produced nothing | **No** — both are "no cell pushed"; S1's single word type has no representation for the distinction | value: not representable | (doc) |
| 7 | spliced a block's contents vs appended the block | **Partially** — N cells vs 1 cell is countable, but "block" is not a type, so splice-vs-append is a count convention, not a semantic fact | value/arity: convention only | (doc) |
| 8 | branch was taken vs branch wasn't | **Yes** — this is exactly the `0BRANCH` primitive | control: mechanical | (tests.c #2) |

**The pattern.** The eight distinctions split cleanly along one line:

- **Control-flow distinctions** (#1, #2, #5, #8) are *mechanical* in S1,
  because they are encoded in the machine's own control state (`IP`/`RP`/`SP`
  and the single `0BRANCH` decision). "Jumped vs didn't jump" and "unwound the
  return stack vs didn't" are the machine's own distinctions.
- **Value/arity distinctions** (#3, #4, #6, #7) are *not* mechanical in S1.
  Its value space is flat (one word type) and arity is not tracked, so these
  distinctions must be imposed as conventions — and the machine cannot check
  them. `claim4_arity_observable_not_enforced` shows S1 can *count* results
  (0/1/2) by reading `SP`, but it cannot *type* them: a block reference and a
  scalar are the same word.

**What this means for the source's claim.** "The defect isn't in the evaluator;
it's in the value space" is *correct for the value/arity half* of the list. S1
confirms that those four distinctions genuinely cannot be made mechanical in a
flat value space. But it is *incorrect for the control half*: S1 makes those
mechanical by keeping control out of the value space entirely (as jumps).

The source also says "you can't patch a representational gap with a procedural
addition." S1 supports the general principle, but not the specific conclusion:
the *control* effects do not *have* a representational gap in S1, because S1
does not route them through the value space to begin with. The gap S1 exposes
is real and is *precisely* the value/arity half — and for that half, a richer
value model is not mechanically *necessary* (S1 can count cells), only *safer
and clearer*.

---

## 4. "Eight mechanisms, twenty-eight pairs" (composability)

**Neutral restatement.** Under option (B), the eight distinctions are eight
separate mechanisms with 28 pairwise interactions, each of which a programmer
must get right and none of which the language can check; under (A) they are
positions on one ladder and compose because there is nothing to compose.

**Frozen S1 verdict.** S1 does *not* have eight mechanisms, and it does *not*
have one ladder. It has **two channels** (the data stack for values, the
memory-mapped registers for control) plus the fact that the whole machine state
is transparent memory. A `break` and a `return` are *the same instruction
sequence* differing only in which cells they read (see `ADVERSARIAL.md`, cases
3 and 6). So S1 does reduce the "mechanisms" to one move — `restore RP; jump`
— but the *scoping* (which cells) is nominal, not structural, and the *arity*
of values is untyped.

- *Composability:* two effects coexist because they touch disjoint cells and
  disjoint register subsets. That is a property the programmer preserves by
  hand; S1 does not guarantee it (`ADVERSARIAL.md` ADV 6).
- *Safety:* the "28 pairs, none checkable" burden is *not removed* by S1 — it
  is *relocated* to raw stack/cell bookkeeping, and it is *still uncheckable*.
  S1 pays the same combinatorial cost, but as manual, silent corruption rather
  than as compile-time-visible constructs.

So the source's "count the edges, not the nouns" point is *largely vindicated*
against S1: S1's edges are the pairwise bookkeeping hazards, and they are real.
The isotope model's "one ladder" is not mechanically necessary, but it *is* a
genuine safety/ergonomics win over S1's "two channels + hand discipline".

---

## 5. "Decay: a caller who doesn't care never finds out"

**Neutral restatement.** The value model lets an unconcerned caller be unaware
of the light/heavy distinction (e.g. `x: multi-return` takes the first value;
`if cond [...]` tests nullity only), so beginners see one falsey form and only
metaprogrammers learn the ladder. Under (B) there is no decay rule, so every
primitive is visible everywhere.

**Frozen S1 verdict.** This is an *ergonomics/decay* claim about the high-level
language, not about the substrate. S1 neither confirms nor refutes it at the
mechanical level: S1 has no multi-return, no `if`, and no falsey *concept* —
it has raw cells and one conditional branch. The claim is *about the layer
above the substrate*. It is consistent with S1 (nothing about S1 forbids a
higher language from building exactly the Ren-C decay rules), but S1 offers no
evidence either way, because decay is a property of the evaluator, and S1 is
deliberately *below* the evaluator.

This is worth saying explicitly to avoid overclaiming: S1 does **not** refute
the decay argument. It simply does not reach it.

---

## 6. Summary verdict

| Claim | S1 mechanically reproduces it? | The honest reading |
|---|---|---|
| Signal must be a *value* to cross unaware code | **No** (claim 2) | **False in general.** A substrate with writable control state (memory-mapped `IP`/`RP`) lets the signal cross as a jump, not a value. The claim is true only for machines whose sole up-channel is the value space. |
| Loop wrapper that breaks *and* multi-returns, composing | **Yes** (claim 1) | Expressible via channel separation. But "without either knowing about the other" is satisfied by *disjoint channels*, not by a language guarantee. |
| The eight distinctions need a value ladder | **Half** (claims 3, 4 + doc) | The control half is mechanical in S1. The value/arity half is not representable as *mechanical* distinctions — S1 can count cells but cannot type them. |
| "Defect is in the value space, not the evaluator" | — | **Correct for the value/arity distinctions.** S1's flat value space genuinely cannot encode "none vs NONE", "block vs its contents", "vanished vs void" as machine-checkable facts. |
| "28 pairs, none checkable" | — | **Not refuted.** S1 relocates the pairwise burden to raw bookkeeping; it is still uncheckable and unsafe. |
| "One ladder is minimal for the set, not for any one" | — | **Consistent with S1.** S1 shows each distinction is *individually* expressible; the isotope model's advantage is only for the *set*, and only as safety/ergonomics/clarity — not mechanical necessity. |

### What the experiment adds

1. The designer's strongest falsifiable claim — "a signal must be a value to
   cross unaware code" — is **false for a substrate that exposes its registers
   as memory**. This is the load-bearing finding, and it is demonstrated by a
   passing test (`claim2_signal_not_a_value`), not asserted.

2. The designer's *other* claim — that the value-space distinctions (block vs
   contents, none vs NONE, vanished vs void) are a *representational* gap that
   procedures can't patch — is **vindicated**, with the qualification that it
   is a gap in *safety, ergonomics, and semantic clarity*, not in *mechanical
   capability*. S1 can count results (`claim4`); it cannot type them.

3. Both the designer's "one ladder" and S1's "two channels + hand discipline"
   pay a pairwise cost. The isotope model pays it in *language features*; S1
   pays it in *silent, uncheckable corruption*. That is a real, measured
   difference in safety — and it is *not* a mechanical necessity.

4. A honest methodological note: writing `claim4` revealed the S1 aliasing
   subtlety live — the first version read `SP` *after* pushing a bookkeeping
   value, which perturbed the very register it was measuring, off by one. That
   is exactly the kind of unforced error a value/arity model would make
   impossible. It is *evidence for* the designer's ergonomics/safety thesis,
   and it is *not* evidence of mechanical impossibility (the fix was a
   reordering, not a new primitive).

### What was deliberately *not* claimed

- That "S1 can express it" makes S1 the better language. It does not; it makes
  S1 the weaker *safety* instrument with the same *mechanical* reach.
- That S1's inconvenience is proof richer semantics are unnecessary. It is
  proof they are *not necessary for expressiveness*; it is not proof they are
  not *worthwhile for safety and clarity*.
- That the isotope model is overbuilt. It is not minimal for any one
  distinction; the source concedes this, and S1's data is consistent with the
  concession.

---

## 7. Honouring the constraint

Ren-C's implementation and its isotope/antiform solution were **not** inspected
before these tests were written and committed. The tests and this document are
derived only from the claims text and from S1's frozen behaviour.
