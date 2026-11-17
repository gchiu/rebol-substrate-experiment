# Glon AI Authoring Guide

A short entry point for AI coding agents writing or checking Glon programs.
Read this first; do not infer the language from C source.

## Required reading order

1. **`GLON-ALPHA-LAWS.md`** — normative contract. If anything conflicts with
   it, the laws win.
2. **`GLON-LANGUAGE-SPEC.md`** — complete descriptive reference: syntax,
   values, evaluation, vocabulary, failures and limits.
3. **`GLON-SYNTAX-DIAGRAMS.md`** — verified reader grammar and the
   item/form distinction.
4. **`GLON-EVALUATION-MODEL.md`** — resolution, calls, result sets, closures,
   `return`, `judge`, escape law, tasks.
5. **`GLON-IMPLEMENTER-GUIDE.md`** — minimum implementation checklist.
6. Only for host integration: **`GLON-HOST-ABI.md`**, then the original
   **`GLON-WASM-ABI.md`** for binary detail.

## Where definitions live

| Need | Read |
|---|---|
| Grammar / accepted source | `GLON-SYNTAX-DIAGRAMS.md`; generated reference `docs/grammar/index.html` |
| Canonical machine-readable grammar | `docs/grammar/glon-grammar.ebnf` |
| Values, truth, equality, blocks, strings, SIN! | `GLON-LANGUAGE-SPEC.md` §2–§6 |
| Evaluation, binding, closures, `return`, `judge` | `GLON-EVALUATION-MODEL.md` |
| Core words and library vocabulary | `GLON-LANGUAGE-SPEC.md` §4–§5 |
| Failures and limits | `GLON-LANGUAGE-SPEC.md` §6–§7 |
| WASM/C host boundary | `GLON-HOST-ABI.md`, `GLON-WASM-ABI.md` |
| Known gaps | `GLON-SPEC-VERIFICATION.md` §7 |

## Provenance: what is what

- **Frozen S1:** `LIT DUP DROP @ ! 0BRANCH HOST`. These are not ordinary Glon
  words; reachable only through `masm`/`raw`.
- **R0 natives over S1 HOST:** `+ - * / = < > <= >= print`.
- **Evaluator keywords/mechanics:** `func`, `return`, `either`, `do`, `invoke`,
  `reduce`, `values`, SIN! words.
- **Glon libraries:** `mk-string`, `get`, `lambda`, `does`, `block-len`,
  `block-at`, `select`, `case`, `parse`, `s/...`, tasks. They must be loaded;
  they are not core.
- **Host-specific:** `glon_*` exports, `host_*` imports, route/event dispatch,
  files/network/DOM/time. Never put application routing or domain rules in the
  host.

## High-value traps

- Prefix only. No infix, parentheses or commas. Arity groups expressions.
- `'x` evaluates to a WORD, not a LIT-WORD.
- `none` is a literal token; `none:`, `:none`, `'none` are word forms.
- String literals expand to `mk-string [bytes]`; a bare core runtime has no
  `mk-string` and halts.
- Strings are immutable bytes. Use `s/+`, `s/=`, `s/length`, `s/print`.
- `=` is identity; not string content equality.
- `values` needs exactly one result per source form; arguments and `reduce`
  keep the first of several results.
- `do` runs in the current context. A block that captures a function's locals
  cannot escape that activation; use a closure.
- `return` is definitional and non-local. `judge` catches raised SIN!s, not
  machine fail-stops.
- `;;` comments are host-stripped even inside strings; the reader has no
  comment syntax.
- Tokens are truncated to 63 bytes. A leading block ignores trailing source.
- `func` is special inside a block, not in unwrapped top-level source.
- Missing/extra arguments and non-block `do`/`either` are unchecked. Do not
  rely on them.
- `case`, `parse`, strings and tasks are optional libraries, not natives.

## Validate generated programs

1. Check the source shape against `GLON-SYNTAX-DIAGRAMS.md`.
2. Check vocabulary provenance in `GLON-LANGUAGE-SPEC.md` §4.0; load needed
   libraries before using their words.
3. For semantics, trace through `GLON-EVALUATION-MODEL.md` before running.
4. Run the narrowest native/wasm check available:
   - `./s1` groups or the named test in `GLON-LANGUAGE-SPEC.md` §10;
   - `make test` for the full native suite (note the known traffic-test-8
     discrepancy in this environment);
   - `make wasm-primer-test` for primer examples on the real WASM module.
5. Prefer executable examples from `GLON-PRIMER.md` or tests over guessed
   behaviour.

## Before searching implementation source

Consult the specification and verification notes first. Search C only for:

- exact `masm` mnemonics/ABI;
- GC object layout or addresses;
- host pointer formats;
- cases listed as UNRESOLVED in `GLON-SPEC-VERIFICATION.md` §7.

Do not promote an implementation accident to a language contract.

## Reporting gaps

If the documents do not define a behaviour:

1. Say it is unspecified/UNRESOLVED; do not invent it.
2. Cite the closest document section and, if needed, the exact implementation
   location.
3. Propose a focused conformance test or a specification clarification.
4. Do not change runtime, parser, prelude or frozen S1 to make a guess true.
