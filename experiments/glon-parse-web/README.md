# Glon Parse Playground

Interactive browser demonstration of the existing Glon Rebol 2-style PARSE
engine, running in WebAssembly on the frozen seven-primitive S1-7 core.

The page accepts a small, explicit **textual** PARSE rule syntax and lowers it
to Glon's existing encoded rule data in the browser. The lowering layer only
tokenises/translates; the actual parser, matching, repetition, searching,
charset matching and captures all execute in Glon.

## Run locally

```
cd experiments/glon-parse-web
python3 -m http.server 8766 --bind 0.0.0.0
# open http://localhost:8766/index.html
```

Use HTTP, not `file://`, so the WASM and Glon libraries can be fetched.

## Execution path

```
index.html + app.js + rules.js
  fetch glon.wasm, prelude.glon, strings.glon, strings-ext.glon,
        parse.glon, parse-driver.glon
  instantiate the standard seven-primitive shop WASM runtime
  glon_init + glon_load each library
  per Run:
    lower textual definitions/top rule -> encoded Glon data expressions
    fresh runtime -> web/reset
    b: mk-string [input bytes]
    web-r: judge [
      web-rules: <lowered registry>
      web-rule:  <lowered top rule>
      p/parse-mode web-rules b web-rule <all> <case>
    ]
    web/report web-r
```

`parse.glon` is the actual Glon parser, and `parse-driver.glon` only calls
`p/parse-mode` and prints length-prefixed result/capture frames. JavaScript does
not implement matching, searching, repetition, character-set membership or
capture semantics.

`glon.wasm` is the current-source seven-primitive runtime used by `/inflate/`.
S1-12 is not used.

## Textual rule grammar

Supported forms:

| Syntax | Meaning |
|---|---|
| `"literal"` | literal string; case-insensitive by default |
| `[ rule … ]` | sequence; `\|` separates ordered alternatives |
| `some rule`, `any rule`, `opt rule` | repetition |
| `n rule`, `min max rule` | exact / bounded numeric repetition |
| `skip` | consume one byte |
| `to "target"` | seek without consuming target |
| `thru "target"` | seek and consume target |
| `end` | match end of input |
| `none` | zero-length success, no consumption |
| `charset "a-z0-9_"` | case-sensitive member/range set |
| `copy name rule` | capture the matched source slice |
| `name` | named rule |

String escapes: `\n`, `\r`, `\t`, `\0`, `\xHH`, `\"`, `\\`. Other escapes and
unexpected characters are syntax errors.

Named rules are defined one per line in the definitions box:

```
parens: [ "(" parens ")" | "x" ]
```

The top rule can be a block, string or named rule, e.g. `[ parens end ]`.

### Lowered representation examples

The browser keeps this internal representation available in the page's
**Diagnostic: lowered Glon data** panel:

| Textual | Lowered Glon data |
|---|---|
| `[none end]` | `reduce [ 'none reduce [ 7 ] ]` |
| `["hello" end]` | `reduce [ mk-string [...] reduce [ 7 ] ]` |
| `[some "a" "b" end]` | `reduce [ 1 mk-string [...] mk-string [...] reduce [ 7 ] ]` |
| `[copy captured some "a" "b" end]` | `reduce [ reduce [ 9 mk-string [...] 1 mk-string [...] ] mk-string [...] reduce [ 7 ] ]` |

The numeric code table is not part of the user-facing syntax; it is retained
only for diagnostics and regression tests.

## Tests

Headless WASM/textual-lowering test (Node 22+):

```
node test-node.mjs
```

Real-browser test (Chrome/Edge + CDP):

```
python3 -m http.server 8766 --bind 0.0.0.0
# launch Chrome with --remote-debugging-port=9222 and the URL above
node test-browser.mjs --url http://localhost:8766/index.html --port 9222
```

Recorded local results:

- Native PARSE suite: **151/151**
- Rebol 2 oracle: **127 probes**
- Node/WASM + textual lowering/errors: **27/27**
- Chrome: **19/19**
- Frozen-S1: passes

The textual reader tests nested rules, alternatives, escapes, numeric
repetition, named recursive rules, charsets, captures, empty input, malformed
syntax and unsupported operators. Browser tests also verify fresh capture state
across consecutive runs.

## Examples

The page includes literal/failure, sequence, alternatives, nested rules,
repetition, numeric repetition, recursive named rules, `skip`, `to`, `thru`,
charset, `copy`, and `none`/`end` examples. Input and rules remain editable.

## Limits

- Input: **2,000 UTF-8 bytes**.
- Generated Glon source: **16 KiB** runtime limit.
- No textual Rebol evaluator, block parsing or arbitrary actions.
- No `set`, `ahead` or `not`; these are not R2 string-parse capabilities in the
  oracle.
- `to`/`thru` targets are literal strings.
- Capture names are strings so the browser can display them.
- This is a tested subset, not full Rebol 2 PARSE compatibility.
- This local version has not yet been deployed; the public `/parse/` page still
  serves the previous encoded-rule version until a separately approved
  deployment.

## Assets

| Asset | Bytes |
|---|---:|
| `glon.wasm` | 92,746 |
| `parse.glon` | 14,347 |
| `parse-driver.glon` | 856 |
| `prelude.glon` + `strings.glon` + `strings-ext.glon` | 16,552 |
| `rules.js` | ~9.0 KB |
| `app.js` | ~11.0 KB |
| `examples.json` | ~3.2 KB |

## Deployment (not performed)

When authorised, follow the existing Pages integration: stage these assets as
`docs/parse/`, add the required files to `site/check-site.py`, and update the
landing page. No push or deployment was done in this task.
