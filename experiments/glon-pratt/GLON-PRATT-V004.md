# GLON-PRATT v004 — a Pratt parser written in Glon

> **Scope (2026-11-07).** v004 is an **experiment**: a native Glon Pratt
> parser for a **subset** of the v003 grammar. It is **not** a complete
> replacement for v003. The deployed `/latex/` page still uses the
> JavaScript translator `experiments/glon-latex/web/infix.js`, and v004 is
> not deployed. Re-verified on 2026-11-07 against the runtime of `96a0d13`
> with no change to any frozen file: native 38/38, 24/24 and 40/40; WASM
> differential suite 1,500/1,500 (CI-built `glon.wasm`).
>
> **Naming difference from the standard string API.** This experiment's
> `s-extra.glon` predates `demo/shop/strings-ext.glon` and keeps its original
> names. Here, `s/span s i set` is the **byte-set scanner**. In the standard
> module that operation is `s/scan-set s i set`, and `s/span s start end`
> means something else: a new string of the bytes in `[start, end)`. Do not
> load `s-extra.glon` and `strings-ext.glon` into one session: whichever
> loads last defines `s/span`.

> **Status: FROZEN (2026-11-06).** This is the v004 proof of concept as measured.
> The source files are pinned in `FROZEN.sha256` (`sha256sum -c FROZEN.sha256`).
> Grammar extensions and any TCO work belong to later versions, which must
> not edit these files.
>
> **Measured resource limits (shipped guards):** return stack `p/max-depth 64`
> (nesting halts beyond 71); managed heap `p/max-chain 24` pending chain steps
> (chains halted at 30–40) and `p/max-length 160` input bytes (fragmentation
> halt seen at 224). Largest inputs that parse: 25-term chains, 15 parenthesis
> levels, 10 nested products, 10 long-operand terms. Results are identical
> natively and in the CI-built WASM.

**Objective.** Translate infix text into Glon prefix source *in Glon itself*,
with a generic `p/` library, on the unchanged substrate. Use the v003
JavaScript translator (`experiments/glon-latex/web/infix.js`) as the
behavioural reference.

```
(a + b) * c   →   l/* l/+ "a" "b" "c"
```

**Result.** It works, natively and in WebAssembly, for the arithmetic subset:
names, integers, decimals, parentheses, unary minus, and `+ - * / ^`. It
matches the reference **exactly**: the same prefix text, error messages and
error positions. This holds on all 40 v003 fixtures in the subset, 400 random
differential inputs, 500 heavy random inputs, and every limit sweep. No input
halts the machine: three guards report a catchable error first. The limits are
real and small; see *Limits*.

No substrate, runtime, parser, prelude or string-vocabulary change was made.
The only `masm` is in the four string operations; the parser itself is
ordinary Glon.

## Files

| File | Purpose |
|---|---|
| `s-extra.glon` | `s/at`, `s/slice`, `s/span` (the scanner; see the naming note above), `s/from-int`: the four string operations approved for this experiment. Each is a checked Glon wrapper over an unchecked masm worker, the `strings.glon` pattern. |
| `pratt.glon` | the generic `p/` engine: lexer, Pratt core, guards, entry points |
| `math-grammar.glon` | `m/grammar`: the v003 arithmetic subset *as data* (operator bytes, binding powers, target word names) |
| `s-extra-tests.glon` | 38 native tests of the string operations |
| `pratt-tests.glon` | 24 native engine tests, including a second grammar targeting Glon's own numeric words |
| `pratt_node_test.mjs` | the differential harness (Node + `glon.wasm`); also writes `build/fixtures.glon` for a native run |
| `Makefile` | a fresh native build; `test`, `test-native-fixtures`, `test-wasm WASM=…` |

## The string operations

| Word | Meaning |
|---|---|
| `s/at s i` | the byte at `i`, or `none` out of range |
| `s/slice s start n` | a new string of `n` bytes from `start`; a bad range raises `'range` |
| `s/span s i set` | the first index ≥ `i` whose byte is not in the string `set` |
| `s/from-int n` | decimal text |

The workers loop in S1, so scanning costs no Glon recursion. `s/span` keeps
its pointers in a 16-cell scratch block, because the safe scratch cells A/B/C/F
are too few for a nested loop.

A 256-byte lookup table was tried first and dropped: one string cell per byte
made each table cost 272 heap cells. (A `s/class` helper briefly existed and
was removed; the interface is the approved four.)

## The parser

```
p/grammar infix-rows prefix-rows ops   → grammar
p/translate grammar src                → prefix text, or SIN! 'parse with [message position]
p/run grammar src                      → prints the text, or "error POS: MESSAGE"
```

- **Generic.** Operators, binding powers, associativity (right: `rbp = lbp − 1`)
  and target words are rows of data. `math-grammar.glon` holds the only `l/`
  names, and they are just strings. A test grammar targeting `+ * -` (with a
  right-associative `-`) shows the engine is independent of LaTeX.
- **Pratt shape:** `p/parse`, `p/nud` (operands, parentheses, prefix
  operators) and `p/chain` (the operator loop). Classic recursive descent with
  one function per precedence level would allow only about 9 levels of
  nesting (see *Limits*).
- **Output as data.** Prefix text is assembled as a string. The input is never
  evaluated, and neither is the output. A host may run the text as a second
  program.
- **Input transport.** The host passes text as `mk-string [b0 b1 …]`, the
  byte-block form the runtime already uses for event values. Any byte is
  allowed, and the 512-byte string-literal limit doesn't apply.
- **Reference parity.**
  - Like `infix.js`, the input is checked for unusable bytes and lone `.`
    *before* parsing, because `infix.js` tokenises everything first and its
    character errors win.
  - Messages and positions are identical.
  - Positions are byte offsets, which equal character offsets because only
    ASCII is accepted.
- **No global state.** The source, position and counters are passed and
  returned. Locals use a `pt-` prefix, because a Glon set-word rebinds a
  same-named global.

## Limits

Two different resources run out. Each has its own guard, and both raise
`SIN! 'parse 'too-deep` ("expression too deeply nested for this parser")
*before* the machine would fail-stop.

| Resource | Cause | Guard |
|---|---|---|
| **Return stack** (about 84 nested calls; Glon has no loops and no tail calls) | deep nesting | `p/max-depth 64`, counting every call on the recursive path. Nesting halts beyond 71; the margin leaves room for the caller's own frames. |
| **Managed heap** (about 7.2K cells, one cell per string byte, a non-moving collector; the libraries leave about 4.2K free) | long operator chains: each pending chain call holds a token, its right operand and a partial result | `p/max-chain 24` pending chain calls (chains halted at 30–40) |
| heap fragmentation | big strings need contiguous cells. One shape, a chain of 14-byte names, halted at 224 bytes of input. | `p/max-length 160` bytes: longer input is refused up front (`'too-long`) |

Measured with the shipped guards (identical natively and in WASM):

| Shape | Largest that parses |
|---|---|
| `a+a+…` / `a^a^…` | 25 terms |
| `a*b+a*b+…` | 23 products (91 bytes) |
| `((…a…))` | 15 levels |
| `a*(a*(…))` | 10 levels |
| `---a` | 15 |
| `-(-(…))` / `x^-(…)` | 7 / 6 levels |
| 14-byte names, 15-digit numerals, `(…)` groups | 10 terms (about 150–160 bytes) |
| one name | up to the 160-byte cap |

**Guard evidence:** none of these halted, natively or in WASM:
- 957 heap-stress chains (operand lengths 1–40, `+ * -`, every length up to
  the cap);
- every limit sweep;
- 500 heavy random inputs;
- 166 native stress chains.

These are empirical bounds, not proofs. Fragmentation in a non-moving heap is
input-dependent, which is why the caps sit well below the measured failures.

**Speed:** about 85 ms per native process, including library load.

## Chris Ross-Gill's 17 figures

**None can be parsed yet.** They need grammar this subset doesn't have:
functions (`sum`, `int`, `lim`, `sqrt`, `root`, `ln`), relations, lists,
implicit multiplication, `[ ]` and symbols.

**Resource fit: 17 of 17.** Each figure was rewritten into an arithmetic
*skeleton* of at least the same depth:
- functions become a bracketed group of their arguments joined by `+`;
- `= ≈ ,` become `+`, and implicit multiplication becomes `*`.

The real Glon parser then parsed each skeleton, under the shipped guards. All
17 parse: the largest is 65 bytes, against the 160-byte cap and 25-term chains.

This is an estimate, not a proof. The real grammar's function and list
handling will cost somewhat different frames.

## An unexpected finding: the committed `demo/shop/glon.wasm` is stale

The committed `demo/shop/glon.wasm` (2026-10-07) predates the current
runtime. Under it, this parser exhausts the managed heap after **6** terms,
where the native build and the CI-built WASM, both from current sources,
handle 25. CI rebuilds that file fresh before every Pages publish, so the
deployed pages are unaffected. But local Node tests that default to the
committed copy (as `experiments/glon-latex`'s `make web-test` does) test an
old runtime. `make test-wasm` here therefore requires an explicit
`WASM=<current build>`. The results above used the CI-built WASM published
with v003.

## Test results

| Run | Result |
|---|---|
| native `s-extra-tests.glon` | 38 / 38 |
| native `pratt-tests.glon` | 24 / 24 |
| native v003 fixtures (`build/fixtures.glon`, generated from `infix.js`) | 40 / 40 |
| WASM differential suite (current runtime) | 1,500 / 1,500 |

The WASM differential suite covers:
- the 40 subset fixtures;
- 51 fixtures outside the subset, which are errors and never halts;
- 400 random differential inputs;
- return-stack calibration;
- heap stress, limit sweeps and heavy fuzz;
- the guard and length-cap errors;
- the 17 Chris figure skeletons.

## What is needed next

1. **Grammar extensions**, as data plus a few `nud` kinds: implicit
   multiplication, relations, lists, `[ ]`, symbols, aliases (UTF-8), and
   function calls with argument counts and `_`. Comparison would continue
   against the 51 remaining v003 fixtures.
2. **Headroom.** The limits are set by the runtime's fixed resources: a
   return stack of about 84 calls and about 7.2K managed cells, of which about
   4.2K are left after loading. Raising them is a runtime configuration change
   (`GC_HEAP_LIMIT`, the return-stack area), not a substrate change. That is
   outside this experiment.
3. **Browser hosting.** Pass the text as a byte block, run `p/run`, then run
   the printed prefix as a second program, as the brief allows.
