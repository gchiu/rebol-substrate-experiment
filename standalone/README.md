# R0 / S1 Standalone WebAssembly Demo

The same R0 application as `web/` (W1.1), but built as a **standalone**
WebAssembly module: no generated Emscripten JS runtime, no libc, no WASI, no
virtual filesystem. The public page is `docs/standalone.html`.

| file | role |
|---|---|
| `docs/standalone.html` | the public page (generated — see below) |
| `docs/glon.js`, `docs/glon.wasm` | published artifacts (copied from `standalone/`) |
| `standalone/app.glon` | the single authoritative R0/GLON source (application + RAW adapter, section-marked) |
| `standalone/glon.c` | the C/WASM portability + host layer (minimal libc shims, two host imports, the `glon_*` ABI) |
| `standalone/glon.js` | the handwritten JS bootloader (instantiates `glon.wasm`, supplies host imports, forwards clicks / DOM updates) |
| `standalone/build-docs.py` | generates `docs/standalone.html` from `app.glon`/`glon.js` and copies `glon.js`/`glon.wasm` |
| `standalone/node_test.js` | headless node verification (`make wasm-standalone-test`) |

## Build

```
emcc   # Emscripten >= 3.1 (tested 3.1.74)
make wasm-standalone       # builds glon.wasm AND regenerates docs/standalone.html
make wasm-standalone-test  # headless node run
```

`make wasm-standalone`:
1. compiles `standalone/glon.c` + `s1.c` + `r0_s1_runtime.c` to a raw
   `standalone/glon.wasm` with `-nostdlib` (22 KB raw / 6.4 KB gzip; two
   imports `env.host_print` + `env.host_set_text`, no WASI/libc imports);
2. runs `standalone/build-docs.py`, which inlines the full `app.glon` into a
   `<script type="application/glon">` block, HTML-escapes the two
   `;; @section`-marked regions into static `<pre><code>` listings, and copies
   `glon.js`/`glon.wasm` into `docs/`.

The R0/GLON source is passed to `glon_load` at runtime by `glon.js` — it is
**not** embedded in the `.wasm` and **not** fetched over the network.

## Serve and try it

```
python3 -m http.server 8000
# open http://localhost:8000/docs/standalone.html
```

Click **Increment** three times; the page shows `Counter: 3`.

## How it works

- `glon.c` provides the tiny libc surface the frozen `s1.c` / `r0_s1_runtime.c`
  need (`printf`/`fprintf`/`putchar`/`fflush`, `malloc`, `memcpy`/`memset`/
  `strcmp`/`strncmp`/`strlen`, `isspace`/`isdigit`), plus two host imports and
  a four-function ABI: `glon_alloc`, `glon_init`, `glon_load`, `glon_call`.
- On startup `glon.js` instantiates `glon.wasm`, calls `glon_init`, then
  `glon_load` with the inlined source (counter: 0, bind `increment` and the
  `web-set-int` RAW adapter).
- Each click on `[data-glon-click="increment"]` calls `glon_call("increment")`,
  which runs `[ do increment ]`: `counter: + counter 1` then
  `web-set-int 1 counter`.
- `web-set-int` emits `handle * 1000000 + value` through `PRINT`; `glon.c`'s
  `printf` special-cases a lone large integer as a `host_set_text` call, and
  `glon.js` writes the value into `[data-glon-id="1"]`.

Counter state and increment logic live entirely in R0; JavaScript only maps
handle `1` to the `#counter` element and forwards clicks.

## Boundary

- **R0:** application state, logic, and the requested UI update.
- **JS (glon.js):** instantiation, the two host imports, click forwarding,
  `data-glon-id` DOM mapping.
- **C (glon.c):** portability layer (libc shims) and the `glon_*` ABI.
- **S1:** unchanged execution substrate.
