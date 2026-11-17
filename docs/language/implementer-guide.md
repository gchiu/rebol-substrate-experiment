# Glon Independent-Implementer Reading Guide

This is the shortest reading path for an implementer who must build a Glon
reader/evaluator or a compatible host without consulting the C source for
ordinary language behaviour.

## 1. Read in this order

| Order | Document | Why |
|---|---|---|
| 1 | `GLON-ALPHA-LAWS.md` | Normative contract: values, words, blocks, closures, equality, truth, reduce, parse, SIN!, escape law, limits. |
| 2 | `GLON-LANGUAGE-SPEC.md` | Complete descriptive surface: source rules, value model, evaluation, core/library vocabulary, failure model, memory/limits. |
| 3 | `GLON-SYNTAX-DIAGRAMS.md` | Verified lexical grammar and word-form precedence, including the item/form split. |
| 4 | `GLON-EVALUATION-MODEL.md` | How words resolve, how calls consume arguments, result sets, closures, `return`, `judge`, escape law, tasks. |
| 5 | `GLON-HOST-ABI.md` | Native C session/embedding boundary and host-specific capabilities. |
| 6 | `GLON-WASM-ABI.md` | Authoritative WASM ABI detail (original contract; the host guide is a summary). |
| 7 | `GLON-ARCHITECTURE-MEMORY.md` | Decisions, memory map and measured limits. |

Use the named tests in `GLON-LANGUAGE-SPEC.md` §10 as the conformance corpus
for each behaviour. The implementation source is needed only for unresolved or
trapdoor details: exact `masm` mnemonics, GC object layout, host pointer
formats and raw S1 addresses.

## 2. Minimum semantic checklist

An implementation is not Glon-compatible unless it has:

1. **Tagged values** with the Alpha tags and type-safe identity `=`.
2. **Prefix, fixed-arity evaluation** with eager left-to-right arguments and
   no operator precedence.
3. **Result sets**: block evaluation keeps the last set; `do` returns it;
   `values` returns all; `reduce` builds a managed block.
4. **Contexts**: dynamic lexical-chain lookup, nearest-binding set-word,
   local creation when absent, 16-per-function and 256-global limits.
5. **Closures**: literal-body capture, computed-body origin capture, depth
   bias, stack-context promotion on escape, portable closure bodies.
6. **Definitional `return`** that unwinds to the matching function frame and
   through `judge`/CASE action boundaries.
7. **SIN!** as a value, `raise` as per-task control flow, `judge` as the
   nearest catcher; machine fail-stops are not catchable.
8. **Escape law** at return, argument, store, capture and transport
   boundaries.
9. **Immutable byte strings** with the library operations in §5 of the spec.
10. **Library words** (`mk-string`, `get`, `lambda`, `does`, `block-len`,
    `block-at`, `select`, `case`, `parse`, `s/...`, tasks) only when the
    session actually loads them.

## 3. Non-negotiable lexical facts

- `[` and `]` are the structural delimiters; whitespace is the C `isspace`
  set. There are no parentheses, commas or parser comments.
- A token is a maximal run excluding whitespace/`[`/`]`, truncated to 63
  bytes, then interpreted in this order: exact `none`; trailing `:`;
  leading `:`; leading `'`; otherwise word.
- `'x` evaluates to a WORD, not a LIT-WORD.
- A string literal is recognized at item start only, has no escapes, is
  limited to 512 bytes, and expands to `mk-string [bytes]`.
- `func` is special-cased inside a block, not by the top-level program loop.
- `masm`/`raw` arity is an optional non-negative decimal integer.
- If source begins with a block, trailing source bytes are ignored.

## 4. Host implementer checklist

- Separate portable language semantics from host capabilities.
- Do not put routing, application names or domain rules in the host.
- For WASM, implement the exact import set and export set in
  `GLON-WASM-ABI.md`; honour the one-execution-entry `glon_alloc` lifetime.
- Copy `glon_run` result text before the next `glon_run`.
- Recreate linear-memory views after calls because memory may grow.
- Do not re-enter the runtime from a host callback.
- Treat `** halted` as a machine fail-stop, not a catchable SIN!.
- Use `GLON-HOST-ABI.md` for the native session facade and the original
  `GLON-WASM-ABI.md` for binary detail.

## 5. Known documentation limits

Do not guess these from the current docs; they are recorded as unresolved or
unchecked in `GLON-SPEC-VERIFICATION.md` §7:

- non-integer arithmetic/`print`, division by zero;
- non-block `do`/`either`, missing or extra arguments;
- non-block/duplicate/overfull `func` specs;
- `return` with no following expression;
- nested/multi-result `values` and result-set collapse;
- token truncation collisions and host `;;` stripping inside strings;
- `masm` empty-block validation;
- form adjacency (`a[1]`, `1"x"`, `a"b"`).

If strict compatibility with the current implementation is required for those
cases, read the specific test and the corresponding reader/evaluator routine;
do not infer a contract from this guide.
