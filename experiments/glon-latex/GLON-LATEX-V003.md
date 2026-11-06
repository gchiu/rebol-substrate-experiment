# GLON-LATEX v003 — an infix input front end

**Objective.** Let a person type conventional infix mathematics, translate it
into Glon prefix source over the existing `l/` vocabulary, and run that source
on the unchanged Glon runtime:

```
(a + b) * c   →   l/* l/+ "a" "b" "c"   →   \left(a+b\right) \cdot c
  infix             Glon (generated)          LaTeX (from the l/ words)
```

This keeps front-end notation separate from substrate evaluation. The
translator (`web/infix.js`, pure JavaScript) only rewrites notation. Glon
evaluates the result, and the existing `l/` words alone decide the LaTeX and
its brackets. Glon calls take a fixed number of arguments, so the prefix
output needs no brackets at all. Input parentheses steer the parse only.

**Result.** All seven of Chris Ross-Gill's `render` calls (17 figures),
written as infix, produce LaTeX **byte-identical** to the existing
`chris-samples.glon` output. This was verified by running both through the
same `glon.wasm` in the same test, not by hand-tracing. 163 translator checks
pass, the 154 existing regression tests pass, and the `/latex/` page gains an
**Infix input** section (Edge and Chrome, 45 browser checks each).

Unchanged: `latex.glon`, `chris-samples.glon`, every test suite, the 17 figure
cards, the vendored KaTeX, the prelude, the string vocabulary and the
runtime.

## The translator

A recursive-descent parser, one function per precedence level, emits Glon by
walking the parse tree operator first (`infix.js`, about 300 lines including
comments).

| Precedence (loosest first) | Input | Associativity | Glon |
|---|---|---|---|
| list | `a, b` | right | `l/seq` |
| relation | `=`, `≈` (`~=`) | none: `a = b = c` is an error | `l/=`, `l/approx` |
| additive | `+ -` | left | `l/+`, `l/-` |
| multiplicative | `*` (`×` `·`), `/` (a fraction), `÷` | left | `l/*`, `l/frac`, `l/div` |
| unary minus | `-a` (`−`) | prefix | `l/neg` |
| implicit multiplication | `4x`, `a b`, `2 sqrt(x)`, `(a+b)(a-b)` | left | `l/.` |
| power | `^`; the exponent may begin with `-` | right | `l/^` |

**Unary minus** binds more tightly than multiplication and less tightly than
exponentiation, which is the conventional rule:

| Input | Meaning | Glon | LaTeX |
|---|---|---|---|
| `-a*b` | (−a)·b | `l/* l/neg "a" "b"` | `\left(-a\right) \cdot b` |
| `-(a*b)` | −(a·b) | `l/neg l/* "a" "b"` | `-a \cdot b` |
| `-a^2` | −(a²) | `l/neg l/^ "a" 2` | `-a^{2}` |
| `(-a)^2` | (−a)² | `l/^ l/neg "a" 2` | `\left(-a\right)^{2}` |
| `x^-1` | x⁻¹ | `l/^ "x" l/neg 1` | `x^{-1}` |

Implicit multiplication binds more tightly than unary minus, so `-2x` is
−(2x).

**Operands:**
- integers stay Glon integers, up to 15 digits;
- decimals, and longer numerals, become string atoms (`2.15` → `"2.15"`),
  because Glon has only integers;
- names `[A-Za-z][A-Za-z0-9]*` become string atoms. A multi-letter name is
  **one** variable (`dx`), so write `a b` for a product;
- `pi π`, `inf infinity ∞`, `sigma` and `theta` become `l/pi`, `l/infinity`,
  `l/sigma` and `l/theta`.

**Functions:**
- `sqrt(x)`, `root(n, x)` (`root(_, x)` is `sqrt`), `ln(x)`;
- `sum(lower, upper, body)`, `prod(…)` and `int(…)`, with `_` for an omitted
  limit. These are reordered to the `l/` (upper, lower, body) order;
- `lim(var, target, body)`, `lim_left(…)`, `lim_right(…)`;
- `paren(x)`, `brace(x)`, `stack(a, b)`, `bracket(x)`, and `[ … ]` for
  square brackets.

**Safety by construction.** Only known words, digits and validated names reach
the generated text, so it is always well-formed Glon, and input text is never
pasted into a Glon string. Errors are reported before Glon runs, with a
message and a position: unexpected characters, unbalanced brackets, missing
operands, unknown functions, wrong argument counts, chained relations, and
input too long for one Glon block (512 values).

### Implicit multiplication after division: `1/2x`

Implicit multiplication binds more tightly than `/`, so `1/2x` is read as
1/(2x). Readers disagree about this, so the page documents it and parentheses
settle it:

| Input | Meaning | LaTeX |
|---|---|---|
| `1/2x` | 1/(2x) | `\frac{1}{2 x}` |
| `1/(2x)` | 1/(2x), explicitly | `\frac{1}{2 x}` |
| `(1/2)x` | (1/2)·x | `\frac{1}{2} x` |

The same applies to `a/b c` (a/(bc)) and `(a/b) c`. A name directly followed
by `(` is a function call, so `x(y + 1)` is an error ("unknown function
'x'"). Write `x (y + 1)`, with a space, or `x * (y + 1)`.

## Chris Ross-Gill's figures, from infix

Each line below was translated, run through `glon.wasm`, and compared byte for
byte with the matching render printed by `chris-samples.glon` in the same
runtime. **All seven renders (17 figures) are identical.**

| Render | Infix |
|---|---|
| 1 | `sum(t = 1, inf, x^t) = 1/(1 - x)` |
| 2 | `pi ≈ 3.141592653589793, [sum(_, _, 1 + 2)], sum(i = 1, n, 3 + 4), sum(_, n, 3 + 4), sum(i = 1, _, 3 + 4), int(_, _, 3 + 4x) = 3x + 4x^2 + C` |
| 3 | `(1/3 * 2 ÷ 3 * 5x) / (10 * (20 + a)^(5x)) + 100` |
| 4 | `x + -ln(3*(4/x) + 6^(7 + 8))` |
| 5 | `paren(stack(pi, pi)), a = pi r^2, root(3, x + 2.15), root(_, x + 5), dx/dy + dx/dt = 100` |
| 6 | `-(A/A^2) = -(1/A), lim_left(x, inf, x + 1)` |
| 7 | `(d/dx) sqrt(x) = 1/(2 sqrt(x))` |

**One spelling matters: figure 6.** Under the conventional unary-minus rule,
the natural spelling `-A/A^2 = -1/A` means (−A)/A² = (−1)/A. It produces
`\frac{-A}{A^{2}}=\frac{-1}{A}`, which differs from the figure: equal in
value, different in tree and typesetting. The figure's −(A/A²) needs the
parentheses shown. The test checks both: the parenthesised form is identical,
and the natural form gives `\frac{-A}{A^{2}}`, by design. No other render
needed a non-obvious spelling.

## Tests

| Suite | Checks | Result |
|---|---|---|
| native v001 + v002 (`make test`) | 74 + 80 | all pass, unchanged |
| page test (`web/latex_node_test.mjs`; now also checks `infix.js` is local) | 105 | all pass |
| infix test (`web/infix_node_test.mjs`) | 163 | all pass |
| real browsers, Edge 156 and Chrome 156 | 45 each (33 earlier + 12 infix) | all pass |
| `check-site.py` on a dry run of the edited workflow | 56 required files | OK |

The infix test covers:
- 70 exact translations: the brief's examples; associativity of `-` `/` `÷`
  `^`; unary minus; implicit multiplication; operands, symbols and Unicode
  aliases; relations and lists; every function and `_`;
- 22 error cases, each with its message and position;
- 27 end-to-end infix → Glon → WASM → LaTeX cases, each also parsed by the
  vendored KaTeX in strict mode;
- the seven Chris renders, byte-identical and KaTeX-parsed, plus the
  natural-spelling figure 6 check.

**Mutation checks.** These temporary changes, each restored afterwards, all
make the infix test fail:
- making unary minus looser than `*` (2 failures);
- making `^` left-associative (3 failures);
- spelling figure 6 without its parentheses ("6 DIFFERENT").

**Browser checks (infix section):**
- the default `(a + b) * c` is translated, run and typeset on load;
- Enter and the button both run;
- `-a*b`, `-a^2` and `1/2x` give the expected LaTeX;
- Chris figure 1 from infix equals the figure 1 card;
- an example chip loads and runs;
- an unknown function and unbalanced brackets are reported with a caret, and
  nothing runs;
- the caret lands on the right line of multi-line input;
- the section recovers after errors, and Reset works;
- on mobile, the layout passes the page's existing overflow and touch-target
  checks.

## Files

| File | Change |
|---|---|
| `web/infix.js` | new: the translator (browser global `GlonInfix`, also a Node module) |
| `web/infix_node_test.mjs` | new: the infix test |
| `web/latex.html` | an **Infix input** section (input, generated Glon, rendered maths and LaTeX, example chips, a syntax reference covering `1/2x`) and the `infix.js` script tag; the 17 figure cards are untouched |
| `web/latex-host.js` | `wireInfix`: translate, show the generated Glon (or the error with a caret), then run it with the existing `runProgram`/`show` |
| `web/latex.css` | styles for the new section |
| `Makefile` | `web` stages `infix.js`; `web-test` also runs the infix test |
| `.github/workflows/deploy-pages.yml` | the CI step also runs the infix test, and `infix.js` is published |
| `site/check-site.py` | `latex/infix.js` is required |

## Limitations

1. **The input's redundant parentheses are not kept.** `(a*b) + c` renders as
   `a \cdot b+c`, because the `l/` words decide the brackets. In the other
   direction, the `l/` words' strict rules can add brackets nobody typed:
   `a + (b + c)` keeps its brackets, and `x + -y` becomes
   `x+\left(-y\right)`. Use `paren(…)` to force brackets.
2. **A fixed function table.** `sin`, `cos`, `log` and the rest are errors,
   because the `l/` vocabulary has no words for them. There are no
   subscripts, primes or factorials.
3. **Decimals are text, not numbers.** `/` always means a fraction (`÷` is
   the division sign). The output targets only the `l/` vocabulary; emitting
   the numeric words as well would be a separate step.
4. **Big operators take their limits positionally:**
   `sum(lower, upper, body)`.
5. **Large inputs are rejected** if their Glon would exceed one block, about
   250 tokens.
6. **The translator is JavaScript, in the browser host.** A Glon-side
   translator would need string indexing, which the string vocabulary
   (`s/+ s/= s/length s/print`) does not provide.
