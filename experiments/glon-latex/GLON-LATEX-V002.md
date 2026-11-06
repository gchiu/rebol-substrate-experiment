# GLON-LATEX v002 — reproducing Chris Ross-Gill's `math.test.reb`

**Objective.** Reproduce every figure in Chris's sample file
(<https://gist.github.com/rgchris/d7b31eee47efc6c86d87fcc21d6b45a4>) using the
qualified `l/` vocabulary. Extend v001 only as far as the samples need. Keep
the architecture: no binding, no contexts, no `do/next`, no parser and no
second interpreter.

**Result.** All 7 `render` calls (17 figures) are reproduced in
`chris-samples.glon`. Every figure keeps its mathematical meaning, and in five
places our output keeps a grouping that the original typesetting loses. v001's
74 tests still pass, and 80 new v002 tests pass. All 121 distinct LaTeX
strings produced or expected by the suites pass a structural check (see
*Validity*).

```
make test       # v001 suite (74) and v002 suite (80), each in its own glon session
make samples    # print the seven figures
```

## What was added

These are ordinary `func`s in `latex.glon`, in the same shape as v001. Each
takes arguments that are already evaluated and returns one term.

| Word | LaTeX | Chris's api |
|---|---|---|
| `l/. a b` | `a b` (juxtaposition) | `.` |
| `l/div a b` | `a \div b` | `by` |
| `l/approx a b` | `a \approx b` | `=~` |
| `l/seq a b` | `a,\ b` | `\|` |
| `l/sum upper lower body` | `\sum_{lower}^{upper} body` | `sum` |
| `l/prod upper lower body` | `\prod…` | `product` |
| `l/int upper lower body` | `\int…` | `integrate` |
| `l/lim var target body` | `\lim_{var\to target} body` | `limit` |
| `l/from-left a`, `l/from-right a` | `a^{-}`, `a^{+}` | `/left`, `/right` |
| `l/ln a` | `\ln a` | `ln` |
| `l/root n a` | `\sqrt[n]{a}` | `root n` (`root _` is v001's `l/sqrt`) |
| `l/paren`, `l/bracket`, `l/brace` | `\left( \right)`, `\left[ \right]`, `\left\{ \right\}` | `enclose`, `/square`, `/curly` |
| `l/stack a b` | `\begin{array}{c}a\\b\end{array}` | `a . lf . b` |
| `l/pi l/sigma l/theta l/infinity l/cdots` | symbol strings | `pi sigma theta infinity ...` |

For the big operators, the argument order (upper, lower, body) follows
Chris's api, and `none` leaves a limit out. A one-sided limit is an ordinary
term passed as the target (`l/lim "x" l/from-left l/infinity …`), not a
refinement, because Glon has no refinements.

Deliberately not added, because the samples do not use them: `abs`, `+-`,
`<>`, `sup`/`sub`, `bar`, the capital-letter `uc` map (a capital is just
`"A"`), and `\times`. Glon's `l/*` prints `\cdot`. Chris's `*` prints
`\times`; both mean multiplication.

## Grouping, extended

Two levels were added: **seq** (−1, below rel) and **jux** (4, between mul
and pow; pow moved from 4 to 5). Only the order matters, and every v001 rule
keeps its meaning. One v001 slot changed: the right operand of `*` now needs
*jux* instead of *pow*. Unwrapped juxtaposition is therefore allowed there:
`a \cdot 5 x` means a·(5x), and (a·5)x prints as `\left(a \cdot 5\right) x`.

| Construct | Level | Argument rule |
|---|---|---|
| `l/.` | jux | left ≥ jux, right ≥ pow (strict): `4 x^{2}`, `\left(4 x\right)^{2}`, `a \left(b c\right)` |
| `l/div` | mul | as `l/*` |
| `l/approx` | rel | as `l/=` |
| `l/seq` | seq | left ≥ rel; nests to the right without brackets |
| `\sum \prod \int \lim` | add | body ≥ mul |
| `l/ln` | mul | argument ≥ jux: `\ln 4 x`, `\ln x^{2}`, `\ln \left(x+1\right)` |
| `\sqrt[n]{}`, delimiters, `l/stack` | atom | braces / explicit delimiters |
| `l/from-left` / `l/from-right` | pow | base must be an atom |

**Big operators.** By convention, a sum's body runs to the right over products
but stops at `+`. Giving the body slot a minimum of *mul*, and the operator
itself the level *add*, encodes that convention: `\sum a \cdot b` is
∑(a·b), `\sum x+2` is (∑x)+2, `\sum \left(3+4\right)` is ∑(3+4), and
(∑x)·y prints as `\left(\sum x\right) \cdot y`. The rule is conservative:
`2+\left(\sum x\right)` and a sum nested as a sum's body,
`\sum_{i=1}^{n} \left(\sum_{j=1}^{m} i j\right)`, get brackets they could
do without. Dropping them safely would mean tracking a second property, "open
to the right", on every term. That is beyond a minimal extension.

**Spacing.** A control word followed by a letter must be separated from it
(`\pi r`, not `\pir`). Juxtaposition joins with a space, and so do `\to`,
`\ln` and a big operator's body. TeX ignores spaces in math, so the typeset
result is unaffected.

## The figures, side by side

The "Chris" column is **hand-traced** from `latex-math.reb` (prep, squeeze and
`do/next`); no Rebol interpreter was run. Words are braced (`{x}`), and a
`[ ]` block adds `\left( \right)`. The "Glon" column is the actual output of
`make samples`. Both are wrapped in `\[ … \]`.

Rebol infix is strictly left to right, and an infix chain fills a prefix
function's argument. In several figures the tree Rebol builds therefore
differs from what the figure *shows*. The Glon form encodes what the figure
shows (marked † below).

**1.** `sum infinity (t = 1) x ^ t = (1 / (1 - x))` †
- Chris: `\sum^{\infty}_{{t}=1}{x}^{{t}}=\frac{1}{1-{x}}`
- Glon: `\sum_{t=1}^{\infty} x^{t}=\frac{1}{1-x}`
- Same figure. (In Rebol's tree, the whole equation is the sum's body.)

**2.** Six figures separated by `|`:
- Chris: `\pi\approx3.141592653589793{,\ }\left[\sum1+2\right]{,\ }\sum^{{n}}_{{i}=1}3+4{,\ }\sum^{{n}}3+4{,\ }\sum_{{i}=1}3+4{,\ }\int3+4{x}=3{x}+4{x}^{2}+{C}`
- Glon: `\pi \approx 3.141592653589793,\ \left[\sum \left(1+2\right)\right],\ \sum_{i=1}^{n} \left(3+4\right),\ \sum^{n} \left(3+4\right),\ \sum_{i=1} \left(3+4\right),\ \int \left(3+4 x\right)=3 x+4 x^{2}+C`
- **Grouping kept.** Rebol passes `1 + 2` and `3 + 4` as the sum's *whole
  body*, but `\sum1+2` typesets as (∑1)+2. Glon prints ∑(1+2). The same
  applies to the integral.
- `ans`: in `math.test.reb`, `ans: pi` holds Rebol's *number* π, while `pi`
  inside the block is the symbol. Glon has only integers, so the numeral is
  written out.
- † `integrate _ _ 3 + 4 . x = 3 x + 4 . x ^ 2 + uc/c` is two expressions to
  `do/next`, and `4 . x ^ 2` is (4x)² in Rebol. The figure shows
  ∫(3+4x) = 3x+4x²+C, and that is what Glon encodes.

**3.** `[1 / 3 * 2 by 3 * 5 . 'x] / [10 * [20 + 'a] ^ (5 . 'x)] + 100` †
- Chris: `\frac{\left(\frac{1}{3}\times2\div3\times5{x}\right)}{\left(10\times\left(20+{a}\right)^{5{x}}\right)}+100`
- Glon: `\frac{\frac{1}{3} \cdot 2 \div 3 \cdot 5 x}{10 \cdot \left(20+a\right)^{5 x}}+100`
- Same meaning. In Chris's version, the blocks that group the numerator and
  denominator in Rebol also print as visible brackets inside `\frac`. Glon
  needs no brackets there. (In Rebol's tree, `^` applies to `10 × (20+a)`;
  the figure shows 10·(20+a)^{5x}.)

**4.** `x + negate ln [3 * (4 / x) + 6 ^ (7 + 8)]` †
- Chris: `{x}+-\ln\left(3\times\frac{4}{{x}}+6^{7+8}\right)`
- Glon: `x+\left(-\ln \left(3 \cdot \frac{4}{x}+6^{7+8}\right)\right)`
- Same meaning. Glon brackets the negation after `+` (v001's rule:
  `a+\left(-b\right)`, never `a+-b`). (Rebol's tree is ((3·4/x)+6)^{7+8};
  the figure shows 3·4/x + 6^{7+8}.)

**5.** Five figures:
- Chris: `\left(\pi{\\}\pi\right){,\ }{a}=\pi{r}^{2}{,\ }\sqrt[3]{{x}+2.15}{,\ }\sqrt{{x}+5}{,\ }\frac{{dx}}{{dy}}+\frac{{dx}}{{dt}}=100`
- Glon: `\left(\begin{array}{c}\pi\\\pi\end{array}\right),\ a=\pi r^{2},\ \sqrt[3]{x+2.15},\ \sqrt{x+5},\ \frac{dx}{dy}+\frac{dx}{dt}=100`
- **Validity fixed.** Chris's `{\\}` is a line break inside `\left( \right)`,
  which LaTeX math does not support outside an alignment or array
  environment. Glon stacks the two π in a one-column `array` (base LaTeX), so
  the figure is π over π in parentheses.
- † `'a = pi . r ^ 2` is ((a=π)·r)² in Rebol; the figure shows a=πr².

**6.** `negate uc/a / (uc/a ^ 2) = negate (1 / uc/a) | limit/left x infinity (x + 1)` †
- Chris: `-\frac{{A}}{{A}^{2}}=-\frac{1}{{A}}{,\ }\lim_{{x}\to\infty^-}{x}+1`
- Glon: `-\frac{A}{A^{2}}=-\frac{1}{A},\ \lim_{x\to \infty^{-}} \left(x+1\right)`
- **Grouping kept.** `\lim…{x}+1` typesets as (lim x)+1. Glon prints
  lim (x+1). († In Rebol's tree, the outer `negate` applies to the whole
  equation.)

**7.** `(d / dx) . (root _ x) = (1 / (2 . root _ x))`
- Chris: `\frac{{d}}{{dx}}\sqrt{{x}}=\frac{1}{2\sqrt{{x}}}`
- Glon: `\frac{d}{dx} \sqrt{x}=\frac{1}{2 \sqrt{x}}`
- The same figure.

In short: 17 of 17 figures are reproduced with the same mathematical meaning.
(An earlier draft of this report said 14; the seven renders split on Chris's
`|` into 1 + 6 + 1 + 1 + 5 + 2 + 1 = 17 figures.)
In 7 of them, the original's typeset grouping differs from its intent (the
five `\sum`/`\int` bodies in figure 2, and the limit in figure 6) or uses a
non-portable construct (figure 5's `{\\}`). Glon's precedence-carrying terms
print the intended grouping. One further defect was noted in the generator's
source, though the sample doesn't use it: `enclose/curly` emits `\left{`,
which is invalid. `l/brace` emits `\left\{`.

## Validity

### Rendering with KaTeX

No TeX engine is installed, and there is no KaTeX or MathJax in the
repository or the system packages. KaTeX **0.16.21** was found bundled with
the locally installed VS Code (its built-in `markdown-math` extension). It was
loaded, unmodified, from a scratch copy, and its standard `renderToString` was
called with `displayMode: true` and `throwOnError: true`. Strict-mode
diagnostics were recorded rather than thrown. Nothing was downloaded for this
check.

| Input | Rendered | Errors | Strict warnings |
|---|---|---|---|
| Glon: the 7 renders, and each of the 17 figures on its own | 24/24 | 0 | 0 |
| Every LaTeX string produced or expected by both suites (121 distinct) | 121/121 | 0 | 0 |
| Chris's outputs (hand-traced) | 7/7 | 0 | 1 |

KaTeX confirms the two claims made above:
- **Figure 5.** For Chris's `\left(\pi{\\}\pi\right)`, KaTeX warns
  `newLineInDisplayMode: In LaTeX, \\ or \newline does nothing in display
  mode`. The original therefore typesets as "(ππ)" on one line. Glon's
  `array` stacks π over π.
- **`enclose/curly`.** Chris's `\left{x\right}` fails to parse ("Expected
  '}', got '\right'"). `l/brace` emits `\left\{x\right\}`, which renders.

The Chris and Glon versions of each render were also drawn side by side with
KaTeX's own CSS and fonts, captured with headless Microsoft Edge, and
inspected. The visible differences are the ones listed figure by figure
above:
- ∑(1+2) and ∫(3+4x) rather than (∑1)+2;
- no extra parentheses inside figure 3's fraction, and · for ×;
- `x+(−ln(…))` with tall outer parentheses (correct, but heavy);
- π stacked over π;
- lim (x+1) rather than (lim x)+1.

A thin space appears before a comma that follows a `\right)`. That is TeX's
standard spacing after an inner `\left…\right` group, and it appears in
Chris's `\right]` too.

The earlier structural checker (kept outside the repository) also passes all
121 distinct strings.

### Running in WebAssembly

The browser runtime is the same evaluator. The shop's committed
`demo/shop/glon.wasm` was driven from Node exactly as the Pages hosts drive it:
- `glon_init`;
- `glon_load` of `jupyter/prelude.glon`, `demo/shop/strings.glon` and
  `latex.glon`;
- then `glon_run`.

| Run through `glon.wasm` | Result |
|---|---|
| `chris-samples.glon` | the 7 renders, **byte-for-byte identical** to the native output |
| v001 suite (`test-helpers.glon`, `latex-tests.glon`) | 74 passed, 0 failed |
| v002 suite (`test-helpers.glon`, `chris-samples.glon`, `latex-v002-tests.glon`) | 80 passed, 0 failed |

No runtime or WASM change was needed. The vocabulary loads into the existing
browser runtime as ordinary Glon source.

## Tests

| Suite | Session | Checks |
|---|---|---|
| v001 `latex-tests.glon` | `latex.glon test-helpers.glon latex-tests.glon` | 74, unchanged in content |
| v002 `latex-v002-tests.glon` | `latex.glon test-helpers.glon chris-samples.glon latex-v002-tests.glon` | 80 |

v002 sections: juxtaposition (14), div/approx/seq (10), big operators (16),
limits (5), ln and roots (14), delimiters/stack/symbols (7), v001 rules
re-checked under the new levels (4), the seven Chris figures exactly (7), and
type errors (3).

Mutation checks: weakening the big-operator body slot, the strict right side
of juxtaposition, or the `\ln` argument slot each makes the v002 suite fail.

## Changes to v001 files

- `latex.glon`: the v002 section, the two new levels, and the `*` right-slot
  change described above; the header documentation was updated.
- `latex-tests.glon`: the check words moved to `test-helpers.glon`, so both
  suites share them. The 74 checks are unchanged.
- `Makefile`: `test` runs both suites; there is a new `samples` target.

## Limitations found

1. **Loader heap.** Literal source blocks and strings live in the permanent
   loader heap. v001's tests, the samples and v002's tests together exhaust it
   (`resource error: loader_exhausted`), so the suites run as two `glon`
   sessions. A long notation document in one session would hit the same
   ceiling.
2. **512 values per block** (from v001) still applies. Each figure and each
   test section sits in its own `do [ ]`.
3. **Conservative brackets around big operators** (see *Grouping, extended*):
   correct, but heavier than hand-written LaTeX for a sum after `+` or a
   nested sum.
4. **The checking method.** Validity was checked structurally, not by a TeX
   engine, and Chris's output was traced by hand, not executed.
