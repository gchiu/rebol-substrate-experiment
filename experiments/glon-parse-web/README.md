# Glon Parse Playground

Interactive browser demonstration of the existing Glon Rebol 2-style PARSE
engine, running in WebAssembly on the frozen seven-primitive S1-7 core.

## Run locally

```
cd experiments/glon-parse-web
python3 -m http.server 8766 --bind 0.0.0.0
# open http://localhost:8766/index.html
```

The page must be served over HTTP (not `file://`) so that `glon.wasm` and the
Glon libraries can be fetched.

## Execution path

```
index.html + app.js
  fetch glon.wasm, prelude.glon, strings.glon, strings-ext.glon,
        parse.glon, parse-driver.glon
  instantiate the standard seven-primitive shop WASM runtime
  glon_init + glon_load each library
  per Run: fresh instance, generated driver:
      web/reset
      b: mk-string [input bytes]
      web-r: judge [
        web-rules: <registry expression>
        web-rule:  <top-rule expression>
        p/parse-mode web-rules b web-rule <all> <case>
      ]
      web/report web-r
```

`parse.glon` and `parse-driver.glon` are the actual Glon parser/driver; the
driver only calls `p/parse-mode` and prints length-prefixed result/capture
frames. JavaScript only loads files, builds the source text, and displays
frames. It does not match, search, repeat, test character membership or
implement captures.

The `glon.wasm` asset is the current-source seven-primitive runtime used by the
existing `/inflate/` page. S1-12 is not used.

## Rule representation

The editor accepts the existing encoded Glon data representation, not textual
Rebol syntax. Rules are built with `reduce` (literal Glon blocks are code).

| Data | Meaning |
|---|---|
| `"literal"` | literal string (case-insensitive by default) |
| `[ rule … ]` | sequence; integer `0` separates alternatives |
| `[ 1 rule ]`, `[ 2 rule ]`, `[ 3 rule ]` | `some`, `any`, `opt` |
| `[ 4 ]`, `[ 5 "target" ]`, `[ 6 "target" ]`, `[ 7 ]` | `skip`, `to`, `thru`, `end` |
| `[ 8 descriptor ]` | charset; descriptor is a byte string of `lo,hi` pairs |
| `[ 20 n rule ]`, `[ 21 min max rule ]` | exact / bounded repetition |
| `[ 9 "name" rule ]` | `copy` capture; use string names in this page |
| `'none` | zero-length success, no consumption |
| `'name` | named rule from the flat registry |

The **named rules registry** textarea is a Glon expression producing the flat
registry `[ name rule … ]`; the **top rule** textarea produces the rule value.
For example, the recursive example uses:

```
rules: reduce [ 'parens reduce [ "(" 'parens ")" 0 "x" ] ]
rule:  'parens
```

Capture names should be strings in this UI so the host can display them. The
parser's native tests also support symbol names, but Glon has no dynamic
symbol-to-string operation; the browser driver deliberately prints string names.

`set`, `ahead` and `not` are not R2 string-parse capabilities and are not
offered. There is no textual Rebol rule reader, block parsing or arbitrary
action execution.

## Examples

The page includes literal, failure, sequence, alternatives, repetition,
recursive named rules, `skip`, `to`, `thru`, charset, `copy`, and `none`/`end`
examples. Every run is fresh, resetting captures and parser state.

## Tests

Headless WASM test (Node 22+):

```
node test-node.mjs
```

It checks all examples, the `copy` value, malformed rules and empty input.

Real-browser test (Chrome/Edge + CDP):

```
python3 -m http.server 8766 --bind 0.0.0.0
# launch Chrome with --remote-debugging-port=9222 and the URL above
node test-browser.mjs --url http://localhost:8766/index.html --port 9222
```

Recorded results: **16/16 Node/WASM checks** and **17/17 Chrome checks**
(all examples, captures, malformed rule, fresh state). Native `make -C
experiments/glon-parse-r2 test` remains **151/151**; native example outputs match
the WASM outputs.

## Limits

- Input: **2,000 UTF-8 bytes**; generated source: **16 KiB** runtime limit.
- Rules use the encoded Glon representation; no textual R2 rule reader.
- `set`, `ahead`, `not`, block parsing, arbitrary actions and general R2
  datatypes are outside this demo.
- `copy` names are shown as strings; captures are source slices.
- `to`/`thru` targets are literal strings.
- This is a local page, not deployed.

## Assets

| Asset | Bytes |
|---|---:|
| `glon.wasm` | 92,746 |
| `parse.glon` | 14,347 |
| `parse-driver.glon` | 856 |
| `prelude.glon` + `strings.glon` + `strings-ext.glon` | 16,552 |
| `app.js` | ~10.6 KB |
| `examples.json` | ~3.6 KB |

Runtime startup is roughly 3–15 ms and representative parses 2–8 ms in the
recorded Chrome runs; these are local smoke measurements, not benchmarks.

## Deployment (not performed)

When separately authorised, follow the existing Pages conventions: add
`experiments/glon-parse-web/` to the site staging step, stage it as
`docs/parse/`, include its required assets in `site/check-site.py`, and add a
landing-page link. Do not deploy from this task.
