# GLON-LATEX — the `/latex/` page

A live page for the `l/` vocabulary, published to GitHub Pages at `/latex/`.
It runs on the **unchanged** shop WebAssembly runtime, with the same shared
libraries as the native CLI. It shows:
- all 17 figures of Christopher Ross-Gill's `math.test.reb`, each beside its
  Rebol original, as editable Glon;
- a numbers-versus-notation example;
- a free playground.

## How it works

Each example runs exactly as the existing Glon pages do it: the Saturnine
primer's fresh instance per run, and Patrol's fetched libraries.

```
glon.wasm (shop build, compiled once)
  └─ per Run: instantiate → glon_init
              → glon_load prelude.glon, strings.glon, latex.glon (fetched once)
              → glon_run <the example's current text>
host_print bytes → lines
  ├─ a line \[…\] or $…$  → typeset by the local KaTeX, with the LaTeX shown beneath
  ├─ any other line       → shown as text (e.g. `print` of a number)
  └─ glon_run's result    → shown when it is not `none` (a SIN! in red)
```

`latex-host.js` evaluates no Glon and builds no LaTeX. Every string comes
from `latex.glon`, and KaTeX is presentation only. A fresh instance per run
means no state is carried between examples. It also keeps each run well clear
of the loader-heap and 256-binding ceilings. The page opens by running every
example once, in order.

## Files

| File | Published as | Purpose |
|---|---|---|
| `web/latex.html` | `latex/index.html` | the page: 17 figure cards, numbers vs notation, playground, `l/` reference, credits |
| `web/latex-host.js` | `latex/latex-host.js` | the host (≈190 lines) |
| `web/latex.css` | `latex/latex.css` | layout; one column below 720px |
| `web/katex/` | `latex/katex/` | KaTeX 0.16.22, vendored unmodified with its MIT `LICENSE`; see `VENDOR.md` |
| `latex.glon` | `latex/latex.glon` | the vocabulary, linked from the footer |
| — | `latex/glon.wasm`, `prelude.glon`, `strings.glon` | the shop runtime and shared libraries, copied as for Patrol |
| `web/infix.js` | `latex/infix.js` | v003: the infix-to-Glon front end used by the Infix input section ([GLON-LATEX-V003.md](GLON-LATEX-V003.md)) |
| `web/latex_node_test.mjs` | (not published) | the headless page test, run in CI |
| `web/infix_node_test.mjs` | (not published) | the infix translator test, run in CI |

`make web` stages the page in `build/web/latex/`. The layout is identical to
what `deploy-pages.yml` publishes; this was checked with `diff -r` against a
dry run of the workflow's own shell lines. `make web-test` runs the headless
test.

**Outside this directory**, only:
- `.github/workflows/deploy-pages.yml`: one test step after the shop WASM
  build, and the `docs/latex/` copy lines plus `git add docs/latex` in the
  publish step;
- `site/check-site.py`: eleven `latex/…` entries in `REQUIRED`;
- `site/index.html`: a "Glon LaTeX" card in the landing page's "Try it" list,
  linking `latex/`.

The runtime, the WASM build and the other demos are untouched.

## Tests

**Headless, CI** (`node web/latex_node_test.mjs [glon.wasm]`): 103 checks.
- no non-inline resource comes from another origin;
- the vendored KaTeX is complete (version, LICENSE, all 20 WOFF2 fonts that
  its CSS names);
- the page's 17 figure examples print exactly the 17 figures of
  `chris-samples.glon`, in order (that file's output is pinned by the v002
  suite);
- the numbers-versus-notation example prints `17` and its two formulas;
- the playground example works;
- every printed formula parses in the vendored KaTeX with no strict-mode
  warning;
- SIN!s, parse errors and broken raw LaTeX are reported.

**Real browsers** (headless Microsoft Edge 156 and Google Chrome 156, driven
over the DevTools protocol, against a local copy of the published tree):
33 checks each, all passing.
- **Load:** all 19 examples run on load. Every request is same-origin with
  status 200, the KaTeX fonts are fetched locally, and there are no console
  errors. Every example typesets its formulas and shows their LaTeX, and all
  17 Rebol originals and the credit and licence links are present.
- **Editing:** edit and Run changes the LaTeX and the typesetting. Reset
  restores and reruns. Ctrl+Enter runs. Text and number lines appear beside
  the formulas.
- **Errors:** a SIN! (`l/+ 'x 1`), a parse error, broken raw LaTeX (KaTeX's
  message plus the LaTeX), and a halt on an unset word are all shown. The page
  recovers on the next run.
- **Mobile (390×844, touch, 2× DPR):** no horizontal page overflow, Rebol and
  Glon stack in one column, no element is wider than the viewport, buttons
  are at least 36px tall, no errors, and editing works.

**Existing demos, unaffected.** In the same simulated tree, `/`,
`/shop/`, `/shop/primer.html` and `/demos/patrol/` start with no console
errors. `check-site.py` passes on the dry-run tree (55 required files), and
fails if a `latex/` file is missing.

## Known behaviour and remaining issues

1. **A missing final argument reads as 0.** At the very end of a program, a
   missing argument evaluates to the integer 0: `l/+ 1` prints `1+0`, and
   natively `print + 1` prints `1`. This is existing runtime behaviour,
   recorded in the test as "known" and not changed. A missing argument
   mid-program is reported (`SIN! type l/term none`).
2. **Halt diagnostics.** A machine halt (e.g. an unset word) prints the
   runtime's own `[dump] …` line before the outcome, as on the primer page.
3. **Long code lines scroll inside their textarea on phones.** Glon code is
   shown unwrapped, so line breaks stay meaningful.
4. **Browser coverage.** Only Chromium-based browsers were driven, plus
   mobile emulation. Firefox, Safari and a real phone are untested.
5. **The local test used the committed `demo/shop/glon.wasm`**, which is
   older than the current runtime source. CI builds it fresh and runs the
   page test against the fresh build before publishing.
6. **Pushing deploys.** `deploy-pages.yml` publishes on every push to
   `glon-shop-g1a`, so committing *and pushing* this change deploys the page.
