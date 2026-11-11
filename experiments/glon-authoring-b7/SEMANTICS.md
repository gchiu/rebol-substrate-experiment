# B7 semantics investigation

## Is a definitions-only Glon program valid?

**Yes, as a loaded Glon module.** A fresh `f: func [x] [ x ]` source run through
the production `./glon` binary exits successfully and returns `#[func]`.
Production modules such as `jupyter/prelude.glon` and
`demo/shop/strings.glon` are definition modules loaded before a session; they
do not require a top-level invocation.

## Does the benchmark require an invocation?

**Yes, but only to obtain a result.** The B5/B6 tasks specify a top-level call
and an expected value. The benchmark compiler (`b3_compile` in B3, used by the
B5 verifier) has no module mode: it requires a top-level expression, emits it
as the entry, and then `HALT`s. A definitions-only artifact is rejected with
`no top-level expression`.

This is a **benchmark submission/execution contract**, not a Glon language
rule. The compiler does not distinguish a reusable module from an executable
entry point; the singleton benchmark entry is why the invocation is required.

## Why the model omitted it

The B6 A prompt already tells the model to write textual Glon, and each task
specification shows a "Top-level call" line. It does not explicitly say that
the submitted artifact must contain that invocation to be a complete benchmark
solution. A function definition is independently meaningful (a valid module),
so omitting the call is a reasonable module-level interpretation. B6's
`missing_top_level` was therefore partly an underspecified completion contract,
not evidence that the text representation cannot express the program.

## B7 clarification

E adds only this benchmark-contract paragraph:

> The benchmark submission must include the specified top-level invocation
> after the function definitions. Definitions alone are a valid Glon module,
> but this benchmark executes the final top-level expression to obtain its
> result; a definitions-only artifact does not produce a test result.

No new syntax, semantic role, example or reference implementation was added.
The exact diff is in `generation/prompts/DIFF.md`.
