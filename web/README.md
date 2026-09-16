# R0 / S1 WebAssembly Demo

A minimal browser demonstration of the R0-on-S1 system running as WebAssembly
and performing a visible DOM update. This is **not** a REPL.

The public page is `docs/index.html` (also served by GitHub Pages). It leads
with the R0 application source; the low-level RAW/S1 host adapter and the JS
glue are hidden behind `<details>` sections.

## Files

| file | role |
|---|---|
| `docs/index.html` | the public page (generated — see below) |
| `docs/demo.js`, `docs/demo.wasm` | published artifacts (copied from `web/`) |
| `web/demo.r0` | the single authoritative R0 source (application + RAW adapter, section-marked) |
| `web/demo.c` | the C/WASM entry: loads `demo.r0`, runs init, exports `r0_click` |
| `web/glue.js` | the JS half of the boundary (prepended to `demo.js` at build time) |
| `web/build-docs.py` | generates `docs/index.html` (static, HTML-escaped) from `demo.r0`/`glue.js` |
| `web/node_test.c` | headless node verification (`make wasm-test`) |

## Build

```
emcc   # Emscripten >= 3.1 (tested 3.1.74)
make wasm       # builds demo.js/wasm AND regenerates docs/index.html
make wasm-test  # headless node run
```

`make wasm`:
1. compiles `web/demo.c` + `s1.c` + `r0_s1_runtime.c` to `web/demo.js` +
   `web/demo.wasm` (with `demo.r0` embedded via `--embed-file`);
2. runs `web/build-docs.py`, which HTML-escapes the two `;; @section`-marked
   regions of `web/demo.r0` (and `web/glue.js`) into static `<pre><code>`
   listings in `docs/index.html`, and copies `demo.js`/`demo.wasm` into `docs/`.

The displayed R0 source is static HTML generated at build time — **no
JavaScript fetches or renders it** — and always matches the source the WASM
demo actually runs.

## Serve and try it

```
python3 -m http.server 8000
# open http://localhost:8000/docs/
```

Click **Increment** three times; the page shows `Counter: 3`. Reloading the
page resets R0 (and therefore the counter) to `0`.

## How it works

- `make wasm` embeds `demo.r0` into the WASM filesystem.
- On startup, `r0_init` reads `demo.r0`, runs its top level
  (`counter: 0`, bind `on-click` and `web-set-int`), and prepares the
  `[ do on-click ]` program.
- On each button click, the JS glue calls the exported `r0_click`, which runs
  the R0 handler: `counter: + counter 1` then `web-set-int 1 counter`.
- `web-set-int` is a RAW fragment that emits `handle * 1000000 + value` on the
  host's standard output. The JS glue overrides `Module.print`, decodes the
  handle/value, and writes the value into the `#counter` element.

The counter state and increment logic live entirely in R0; JavaScript only
maps numeric handle `1` to the `#counter` DOM element.

## Boundary

- **R0:** application state, application logic, the decision of what UI update
  to request.
- **JS:** startup, DOM element mapping, click forwarding, the `WEB_SET_INT`
  host service (capture stdout, decode handle/value, set `.textContent`).
- **S1:** unchanged execution substrate.

## Verify headlessly (no browser)

```
make wasm-test
```

runs the same R0 application under node and asserts three clicks emit
`1000001`, `1000002`, `1000003` (handle 1, values 1/2/3).
