# GLON-LATEX v001 — a LaTeX vocabulary beside the numeric one

**Question.** Can Glon carry a second *semantic* vocabulary for mathematics —
where `+` means "build the notation for a sum" rather than "add" — using only
the substrate's existing extension mechanisms, with no new parser, evaluator
or interpreter?

**Answer: yes.** `latex.glon` is an ordinary loadable library of qualified
words (`l/+ l/- l/* l/frac l/^ l/neg l/sqrt l/=`), in the same shape as the
string vocabulary (`s/+ s/= …`). In one session:

```
+ 1 * 2 3           ;; => 7
l/+ 1 l/* 2 3       ;; => term, l/text gives  1+2 \cdot 3
```

The two vocabularies compose. Numeric words compute first, and their results
become LaTeX atoms: `l/^ "x" + 1 2` gives `x^{3}`. Loading the LaTeX words
changes nothing about the numeric ones. 74 of 74 regression checks pass on a
freshly built `glon`.

Inspired by Christopher Ross-Gill's Rebol *LaTeX Equation Notation Generator*
(<https://gist.github.com/rgchris/d7b31eee47efc6c86d87fcc21d6b45a4>).

## Running it

```
make test       # builds ./build/glon fresh from the repo sources, runs latex-tests.glon
make examples   # prints examples.glon
```

The Makefile compiles its own copy of the CLI (the root Makefile's
`GLON_SRCS`) into `build/`, and copies the canonical `jupyter/prelude.glon`
and `demo/shop/strings.glon` into `build/glon-lib/`. Nothing outside this
directory is written.

## Files

| File | Purpose |
|---|---|
| `latex.glon` | the vocabulary (pure Glon plus two one-line `masm` type tests) |
| `latex-tests.glon` | 74 exact-output regression checks; raises a SIN! on any failure (exit 1) |
| `examples.glon` | numeric and LaTeX results side by side |
| `Makefile` | fresh build + `test` / `examples` |
| `.gitignore` | ignores `build/` |

## Vocabulary

Operands: an **integer** (an atom; a negative integer counts as a negation),
a **string** (raw LaTeX taken as an atom: `"x"`, `"\pi"`, `"2.15"`), or a
**term** (the result of any `l/` word). Anything else raises
`SIN! type l/term <value>`.

| Word | Output |
|---|---|
| `l/+ a b` | `a+b` |
| `l/- a b` | `a-b` |
| `l/* a b` | `a \cdot b` |
| `l/frac a b` | `\frac{a}{b}` |
| `l/^ a b` | `a^{b}` |
| `l/neg a` | `-a` |
| `l/sqrt a` | `\sqrt{a}` |
| `l/= a b` | `a=b` |
| `l/text a` | the bare LaTeX string |
| `l/inline a` | `$…$` |
| `l/render a` | `\[…\]` |

## How it works: existing mechanisms only

- **Qualified words.** GLON-ALPHA-LAWS §2 reserves interior `/` for
  `domain/operation` names. `l/+` is a flat word in the global context, as
  `s/+` is. Nothing is shadowed, rebound or bound.
- **The one evaluator does the walking.** Each `l/` word is a `func` that
  receives arguments that have already been evaluated and returns a new term at
  once. Glon's prefix, fixed-arity evaluation visits the expression tree, so
  there is no tree walker, no PARSE dialect and no deferred re-interpretation.
- **Terms are plain values.** A term is a managed block `[precedence latex]`
  made with `reduce`. LaTeX text is built with the immutable `s/+`.
- **Integer → text** is written in Glon (`l/raw-digits`, using
  `mk-string reduce [ + 48 d ]`), because Glon has no FORM.
- **Type dispatch** uses the prelude's `s/raw-string?` plus two local masm
  tests, `l/raw-int?` and `l/raw-block?`. They have the same one-line shape
  as `s/raw-string?`: mechanism in masm, meaning in Glon, as in `strings.glon`.

## Grouping and precedence

Prefix Glon has no precedence and no parentheses. Fixed arity decides grouping,
so **the input is never ambiguous**: the call tree *is* the expression tree.
The vocabulary's only job is to put the *minimum* brackets into the *output*,
so that the printed formula keeps that tree.

Each term carries a level: `0 rel (=)`, `1 add (+ -)`, `2 neg`, `3 mul`,
`4 pow`, `8 frac`, `9 atom`. Each argument slot has a minimum level, and an
argument below that minimum is wrapped in `\left( … \right)`:

| Slot | Minimum | Effect |
|---|---|---|
| `+` / `-` left | add | `a-b+c`, `-a+b` print as written |
| `+` / `-` right | mul | `a-\left(b-c\right)`, `a+\left(-b\right)`, `a+\left(b+c\right)` |
| `*` left | mul | `\left(a+b\right) \cdot c`, `\left(-a\right) \cdot b` |
| `*` right | pow | `a \cdot \left(b \cdot c\right)`, `2 \cdot \left(-3\right)` |
| `neg` operand | mul | `-\left(a+b\right)`, `-\left(-a\right)`; `-a \cdot b`, `-a^{2}` |
| `^` base | atom | `\left(1+x\right)^{2}`, `\left(a^{b}\right)^{c}`, `\left(-3\right)^{2}`, `\left(\frac{1}{2}\right)^{2}` |
| `=` both sides | add | `\left(a=b\right)=c` |
| `^` exponent, `\frac` parts, `\sqrt` | — | braces group; never wrapped: `e^{1+x}`, `\frac{a+b}{c-d}` |

Right operands are **strict**: for a right-nested tree, `a+(b+c)` keeps its
brackets even though `+` is associative. Dropping them would be an algebraic
judgement, which v001 deliberately does not make. As a result, two different
trees never print the same text. The tests check this for each tricky pair:
`-(a·b)` prints `-a \cdot b`, while `(-a)·b` prints `\left(-a\right) \cdot b`.

### Compared with the Rebol original

Chris's generator rebinds infix `+ - * / ^` and runs the block with
`do/next`. Rebol infix has no precedence (strict left-to-right), so grouping
is up to the author: `( )` groups silently and `[ ]` adds `\left( \right)`.
A paren group can therefore lose its grouping in the output: `(2 + 3) * 4`
gives `2+3\times4`. In Glon the same shape, `l/* l/+ 2 3 4`, *always* prints
`\left(2+3\right) \cdot 4`. This is because the grouping lives in the call
tree, and the precedence table decides only how to print it.

Not taken from the original, deliberately: the `bind`-based rebinding of
`+ - * /` (Glon has no `bind`; the dynamic `do`-capture described in the laws
(§3) is discouraged; and the point here is coexistence, not replacement).
Also left out: `sum`, `integrate`, `limit`, `enclose`, Greek tables and
juxtaposition. Each follows the `l/frac` pattern and is v002 material.

## Test results

```
$ make test
cc -std=c17 -Wall -Wextra -O0 -g -I../.. -o build/glon ...   (fresh build, no warnings)
build/glon latex.glon latex-tests.glon
#[func]
74          <- passed
0           <- failed
glon-latex v001: all tests passed
```

There are ten sections: coexistence (numeric and LaTeX in one session, 9),
atoms (6), addition and subtraction (12), multiplication (11), exponentiation
(12), negation (5), fractions and roots (5), relations (3), rendering and full
formulas (8), and type errors raised as SIN! (3).

The harness was mutation-checked. A wrong expectation, or weakening the `l/-`
right-slot minimum from `mul` to `add`, makes the run fail with exit 1 and
`FAIL` lines that show the result produced and the result expected.

## Limitations found

1. **`masm` in an unwrapped source file fail-stops.** A top-level
   `k: masm 1 [ … ]` in a file *not* wrapped in an outer `[ … ]` halts the
   machine (`** halted (no SIN!: a machine-level fail-stop)`) when the
   definition runs. This happens even with a fresh build. The same definition
   in a wrapped file works, which is why `latex.glon` is wrapped (as
   `strings.glon` is). This looks like a loader/CLI defect worth a separate
   look; it is not fixed here.
2. **512 values per block, and a string literal costs two.** A quoted literal
   loads as `mk-string [bytes]`. The flat test file exceeded the limit
   (`parse error: too_large`), so each test section sits in its own `do [ … ]`
   (the technique `common.glon` already uses). For the same reason, a lookup
   table of strings needs `reduce`.
3. **No word → text.** Symbol names exist only in the C runtime
   (`r0_s1_sym_name`). Variables are therefore strings (`"x"`), not words
   (`'x`) as in the original. A word operand is a type error.
4. **No FORM and no public type predicates.** Integer → text was written in
   Glon. Integer and block tests needed the two local masm helpers.
5. **Integers only.** Decimals are written as strings (`"2.15"`).
6. **Cost.** Every `s/+` allocates a new string, so building the output costs
   quadratic time in its length. That is fine at formula sizes: a 60-deep
   right-nested sum (888 bytes) renders without trouble.
7. **The repo-root `./glon` is stale.** It is dated 2026-09-26, before the
   current runtime. Use `make test` here, which always builds fresh.
8. **Cosmetic.** The CLI echoes each file's last value, so `#[func]` (the
   value of `latex.glon`) appears before the test output.

## Not done (by design)

No symbolic algebra (no simplification, no reassociation, no evaluation of
terms), no second interpreter or tree walker, no rebinding of the numeric
words, and no changes to the substrate, parser, evaluator, prelude or string
vocabulary.
