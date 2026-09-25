# Glon Alpha — Language Laws

This is the authoritative contract for the Glon Alpha language. It is not a
tutorial. A fresh machine should read this before writing Glon.

Frozen S1 substrate: `f90496c…` (s1.c / s1.h / tests.c / adversarial.c / claims.c).
The runtime (`r0_s1_runtime.c`) is emitted S1 above that substrate.

## 1. Value model

Every value is one cell: `payload * 16 + tag`, where `tag` is the low 4 bits.

| tag | kind | payload |
|---|---|---|
| 0 | INT | signed integer |
| 1 | NONE | 0 |
| 2 | WORD | symbol id |
| 3 | SET-WORD | symbol id |
| 4 | GET-WORD | symbol id |
| 5 | LIT-WORD | symbol id |
| 6 | BLOCK | 16-aligned block payload address |
| 7 | CONTEXT | context payload address |
| 8 | CLOSURE | 16-aligned closure payload address |
| 9 | NATIVE | native id |
| 10 | RAW | raw payload address |
| 11 | USER | user object payload address |
| 12 | STRING | 16-aligned string payload address |
| 13 | BOUND | (depth*16 + slot) |
| 14 | SIN | 16-aligned SIN! payload address (see §6a) |

## 2. Words

- A `WORD` is looked up dynamically in the current context (lexical parent
  chain), not re-lexicalised.
- A `BOUND` value is a pre-resolved relative lexical reference
  `(depth, slot)` against the current context, biased by the activation's
  `FRAME_BIAS` ("Guard of Binding").
- Word-like values (`WORD` / `SET` / `GET` / `LIT`) compare by symbol id.
- **Qualified names.** A `/` inside a word is part of the word: `s/+` is one
  word (`s/+:` sets it, `:s/+` gets it, `'s/+` is its literal form), and a bare
  `/` is still integer division. An interior `/` is reserved for qualified,
  path-like names (`domain/operation`). For now such names are ordinary flat
  words in the usual contexts; no path evaluation, qualified lookup,
  namespaces or contexts exist yet.

## 3. Blocks

- A block is structured data / a description. Its layout is
  `[count, site_id, elem…]`; loader blocks and managed blocks (from `reduce`)
  share this operational layout.
- A block does **not** carry activation identity — only a static `BLK_SITE`.
- A bare block is **not a portable lexical executable object**. A bare block
  containing `BOUND` references is valid only while executed under an activation
  whose `FRAME_SITE` equals its `BLK_SITE`. Executing it elsewhere fails loudly
  (fail-stop), never silently resolving against an unrelated activation.
- Known limitation: under same-site recursion, two live activations share a
  `FRAME_SITE`, so a travelling bare bound block resolves against the innermost
  matching activation. The same holds for a block whose creating activation is
  dead but which is `do`ne inside a *later* activation of the same function: it
  resolves against that later activation. Use a closure when
  activation-specific capture is required.
- `do` runs a block in the **current** context. A block passed into another
  function and `do`ne there resolves its plain (`T_WORD`) words dynamically in
  that function's context, so they can be captured by the callee's names. User
  control forms should turn caller blocks into closures (as `case` does), not
  `do` them.

## 4. Closures

- A closure is an executable value: `[spec, body, captured-ctx, site, bias]`.
- `CLOSURE_CTX` captures the **actual** lexical context identity; a captured
  stack-local context is promoted to the managed heap on escape. Promotion
  redirects the owning activation onto the managed copy, so the activation and
  all its closures share one context (mutation is visible both ways).
- A closure is the **portable executable form**; it keeps its captured ancestry
  even after its creator returns and across same-site recursion.
- Closure-origin binding (the Guard of Binding), by body kind:
  - a **literal** body (`func SPEC [...]`) is the new func's own scope:
    capture the current context, bias 0;
  - a **computed** body (`func :spec :body`, `func [] block-at rows i`) keeps
    the lexical meaning it was parsed with. A top-level block binds to the
    global context. Otherwise the nearest live frame of the block's site is its
    origin: that activation's context (bias-0 frame), or the origin captured by
    a closure made from one of its blocks (bias-1 frame). Capture it, bias +1;
  - a computed body whose origin activation is **dead** yields a closure whose
    `T_BOUND` references fail-stop (it never resolves against another context).
- A func spec's words are never lexically resolved: an inner parameter shadows
  a same-named outer parameter.

## 5. func / lambda / does

- `func` is the base closure constructor (a reserved keyword).
- `lambda` and `does` are ordinary Glon built from `func`; they imply no new
  primitive.
- `func` fails loudly when its body is not a block.

## 5a. case

- `case [ [cond-1] [action-1] [cond-2] [action-2] ... ]` is ordinary Glon
  (`demo/shop/case.glon`, an optional library loaded after bootstrap like
  `parse.glon`), built from `func`, `invoke`, `either`,
  `block-at`/`block-len`. It implies no new primitive.
- Conditions are evaluated in order; the first truthy one runs its action, and
  CASE returns the action's result. Later conditions/actions are not evaluated.
  No match returns `NONE`. A condition without an action fails loudly.
- Conditions are **blocks**, not inline expressions: Glon has no way to evaluate
  one expression of a block at a time from Glon code.
- Each condition/action runs as a zero-argument closure bound to its lexical
  origin, so words resolve exactly as if written in the enclosing function,
  including assignment. `RETURN` inside an action returns from that action
  (CASE yields the value), not from the enclosing function.
- Each tested clause allocates one closure, and the first one promotes the
  enclosing activation's context to the managed heap. Recursion through
  CASE-using functions is therefore bounded by live promoted contexts (about
  25 levels with the current heap).

## 6. invoke

- `invoke` evaluates one closure-producing expression and hands control to the
  resulting closure, which consumes its own arity from the same expression
  stream. Non-closure invocation fails safely.

## 6a. SIN!, raise, judge

An error value and error propagation are separate things.

- **SIN! is a value.** `create-sin type id arg` builds one: `type` and `id`
  are conventionally words, `arg` is any value. Holding, passing, storing,
  returning or comparing a SIN! never raises. `sin? v` is 1 for a SIN!
  and 0 otherwise; `sin-type`, `sin-id` and `sin-arg` read its fields.
  A SIN! equals only itself (§9).
- **`raise err` propagates.** It abandons the current operation and unwinds
  through any number of unaware callers to the nearest `judge` in the same
  task. `raise` of a non-SIN! raises `'type 'raise` carrying that value.
- **`judge block`** runs `block` in place, in the current activation, like
  `do`. If the block completes, `judge` returns its result. If anything in it
  raises, `judge` returns the SIN! and execution continues after the `judge`.
  Nested judges: the nearest one catches.
- `return` inside a judge body returns from the enclosing function, unwinding
  through the judge.
- **Uncaught:** with no judge, a raise ends the run exactly as a fail-stop
  does (the run is not clean). The host can read the uncaught error.
- **Tasks:** judges are per task. An error raised in a task is caught only by
  a judge in that task; unjudged, it ends the run. A judge in the main world
  does not catch a task's error.
- **Runtime errors.** An accessor applied to a non-SIN! raises `'type
  'sin-field`; `judge` of a non-block raises `'type 'judge`. The escape-time
  binding law raises `'escape` with id `'return`, `'argument`, `'store`,
  `'capture` or `'transport` (the boundary that detected the violation) and
  arg = the offending block's site id. The violating operation is abandoned
  before it completes; a judge never receives the illegal value.
- Machine-integrity failures (heap/stack corruption, out of memory,
  return-stack exhaustion) are not SIN!s: they still fail-stop and cannot
  be judged.

## 7. reduce

- `reduce block` evaluates each source expression once, left-to-right, reduces
  each to a single value, and returns a managed block of those values.

## 8. parse

- `parse` is optional pure-Glon structural recognition. Input and rules are
  blocks. Operators: `skip`, `alt [A] [B]`, `opt [A]`, `some [A]`, `any [A]`;
  any other rule value is a literal matched with `=`.
- Full-match law: `parse input rules` is true only if the rules succeed and
  consume the whole input.
- `-1` is the internal impossible-cursor failure sentinel.
- Alpha PARSE has no actions, captures, string parsing, or `into`.

## 9. Equality

- `=` is type-safe identity:
  - word-like values (`WORD`/`SET`/`GET`/`LIT`) compare by symbol id;
  - every other value compares by full tagged cell identity.
- Consequences: `NONE != 0`; a word never equals an integer; a block/closure
  equals only itself (identity). String *content* equality is `s/=` (also
  spelled `str-eq`), not `=`.

## 9a. STRING!

- A STRING! is an **immutable** sequence of bytes: a managed payload
  `[length, byte0 .. byteN-1]` with one byte (0..255) per cell. Any byte value,
  including 0, can be stored; there are no C-string or Unicode semantics. A
  quoted literal `"..."` is built by `mk-string` (up to 512 bytes per literal).
- No operation changes an existing string:
  - `s/+ a b` returns a **new** string holding `a`'s bytes then `b`'s;
  - `s/= a b` is 1 if `a` and `b` hold the same bytes, else 0 (`str-eq` is the
    same word);
  - `s/length a` is the number of stored **bytes**;
  - `s/print a` writes `a`'s bytes and then a newline to the output and
    returns no value (`print` prints integers only).
- A non-STRING! argument raises a SIN! of type `type` whose id is the
  operation's name and whose arg is the offending value. These are ordinary
  functions: both arguments are evaluated first, then the first non-string is
  reported.
- These are library code, not runtime natives: `str-eq` (the one equality
  algorithm) is in `demo/shop/common.glon`, and `s/+ s/= s/length s/print` are
  in `demo/shop/strings.glon`, built on the runtime's general mechanisms
  (managed allocation, byte access, frozen S1's byte output). Jupyter and
  Saturnine sessions load `strings.glon`; a page that needs only `str-eq`
  (the shop, the traffic simulator) does not.

## 10. Truth

- `NONE` and integer `0` are falsey; every other value is truthy.

## 11. Memory

- The S1 substrate is frozen; the runtime lives above it.
- The managed heap is a non-moving mark/sweep collector; the loader heap is
  permanent and never swept.
- GC-safepoint invariant: at every GC-capable allocation, all GC-scanned state
  (data stack, return stack, roots) holds only valid tagged Glon values.
- **Context capacity (an implementation limit):** contexts have fixed
  capacities: the global context holds 256 bindings and a function's context
  holds 16 (`R0S1_CTX_CAP`). Adding a new binding (a set-word with no existing
  binding, or a bound parameter) to a full context is detected **before any
  mutation**: the machine fail-stops with the structured reason `CONTEXT_FULL`
  (`r0_s1_halt_reason`). This is a machine/resource failure, not a `SIN!`, so
  `judge` cannot catch it, and the rejected write leaves existing state
  uncorrupted. The capacities are implementation limits intended for later
  removal, not language semantics.
- **Known limit:** the current 256-binding global-context ceiling can still be
  reached by sufficiently large combined workloads (for example, a session
  that loads traffic plus two extra models needs 280 bindings; the shipped
  traffic page does not offer that session shape). The runtime detects this
  before writing and fail-stops with `CONTEXT_FULL`; the ceiling remains an
  implementation limit scheduled for later removal.

## 12. Tasks / tuple space

- Cooperative, deterministic single-world tasks; Linda-style tuple
  coordination. No claim of multicore/distributed execution.
- Every shipped task-creation word (`mnew-task` in linda, merchant-flow,
  tuple-space and examples/multitasking.r0) calls the runtime's shared
  `ESC_TRANSPORT` check on the still-tagged body before touching the task
  table: an activation-dependent body raises `'escape 'transport` and no task
  is created (escape law boundary 5).

## 13. Visual host

- The graph-scene / canvas protocol is emitted by Glon; the JS/browser is a
  host, not application semantics.

## 14. Explicit non-features (deferred)

`SWITCH`, `EACH`/`FOREACH`, Rebol-style inline-condition `CASE` and `/ALL`,
full Rebol PARSE, string PARSE, PARSE
`into`, `type?`, Rebol-style portable bound blocks, distributed closures.
Post-Alpha language changes require evidence from real machine-written
applications or a demonstrated correctness defect.
