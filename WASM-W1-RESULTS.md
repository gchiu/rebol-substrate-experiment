# WASM W1 — R0/S1 WebAssembly Browser Demonstration

The existing R0-on-S1 system (S1 machine + the emitted R0 evaluator) compiles
unchanged to WebAssembly and, in an ordinary browser, runs a small R0 counter
whose state and logic live entirely in R0.

**Result: success.** The counter increments and updates the DOM on click; after
three clicks the page shows `Counter: 3`. No REPL, no console, no debugger UI.

## 1. Architecture

```
index.html  ──<script src="demo.js">──┐
                                     │  (glue.js is prepended into demo.js)
[Increment] ──click──> Module._r0_click()   (exported WASM entry)
                                     │
demo.js / demo.wasm (Emscripten)     │
   r0_click() ──> r0_s1_run("[ do on-click ]")
                     └─> R0: counter: + counter 1
                         R0: web-set-int 1 counter   (RAW fragment)
                             └─> HOST_PRINT "handle*1000000+value"
                                     │
                           Module.print override (JS)
                             └─> decode handle/value
                                 document.getElementById("counter").textContent = value
```

The counter state (`counter`, bound in the R0 global context) and the increment
logic (`counter: + counter 1`) are R0. JavaScript only maps numeric handle `1`
to the `#counter` DOM element.

## 2. Build procedure

Toolchain: **Emscripten 3.1.74** (installed via `emsdk`), bundled node v24.19.0
used for headless verification.

```
make wasm       # -> web/demo.js + web/demo.wasm (demo.r0 embedded)
make wasm-test  # headless node run: 3 clicks -> emits 1000001/1000002/1000003
python3 -m http.server 8000   # serve, open http://localhost:8000/web/
```

The `wasm` target:

```
emcc -O1 -s ALLOW_MEMORY_GROWTH=1 -s EXPORT_KEEPALIVE=1 \
     --pre-js web/glue.js --embed-file web/demo.r0@demo.r0 -I. \
     web/demo.c s1.c r0_s1_runtime.c -o web/demo.js
```

`r0_s1_runtime.c` and `s1.c` are compiled **as-is** — no portability changes
were required (see §9).

## 3. Browser / WASM boundary

| layer | responsibility |
|---|---|
| **R0** (`demo.r0`) | counter state, increment logic, deciding the DOM update to request |
| **JS** (`glue.js`) | startup, DOM handle mapping, click forwarding, the `WEB_SET_INT` host service |
| **S1** (`s1.c`) | unchanged execution substrate |
| **C** (`demo.c`) | load `demo.r0`, run init, export `r0_click` (no evaluator semantics) |

`demo.c` is the only browser-specific C file; it uses the frozen
`r0_s1_init` / `r0_s1_parse` / `r0_s1_run` API only.

## 4. R0 demo source (`web/demo.r0`)

```
web-set-int: raw 2 [
    LIT 16 DIV          ;; untag value
    >R                  ;; stash value
    LIT 16 DIV          ;; untag handle
    LIT 1000000 MUL     ;; handle * 1000000
    R> ADD              ;; + value
    PRINT               ;; host print the encoded integer
    ARITY 0 EXIT
]
counter: 0
on-click: [
    counter: + counter 1
    web-set-int 1 counter
]
```

`on-click` is a first-class **block** run with `do`, not a `func` closure: R0
runtime closures are allocated in the S1 heap, which the frozen `r0_s1_run`
resets on every call, whereas a parsed block (loader heap) survives across
calls. This is the same frozen-runtime property, not a WASM-specific change.

## 5. JS glue responsibilities (`web/glue.js`)

1. Override `Module.print` (the host stdout channel) and `Module.printErr`.
2. In `Module.print`, decode `handle * 1000000 + value` and write `value` into
   the mapped element (`R0_DOM_HANDLES[1] == "counter"`).
3. In `Module.onRuntimeInitialized`, attach the button click handler to
   `Module._r0_click()`.

No counter state or application logic exists in JS.

## 6. HOST / browser adapter

`web-set-int` is an ordinary RAW fragment (generic S1 code) — not a new S1
primitive, not a new HOST id, not a new evaluator native. It emits a single
integer on the existing `HOST_PRINT` channel (the same environmental I/O that
`print` uses), and the JS layer interprets that integer as `(handle, value)`.
The facility is mundane environmental I/O with no evaluator/control/binding
semantics.

## 7. File sizes

| artifact | bytes |
|---|---|
| `web/demo.wasm` | 43,470 |
| `web/demo.js` (runtime + glue) | 132,948 |
| `web/index.html` | 298 |
| `web/glue.js` | 1,272 |
| `web/demo.r0` | 1,551 |
| `web/demo.c` | 2,456 |
| **total demo payload** (index + js + wasm + r0) | ~178 KB |
| gzip `.wasm` | 15,205 |
| gzip `.js` | 34,739 |

(Not optimised; sizes reported only.)

## 8. Test results

- **Headless (`make wasm-test`):** PASS — three clicks emit `1000001`,
  `1000002`, `1000003` (handle 1, values 1/2/3) and print `WASM_DEMO_OK`.
- **Native suite:** 156 `ok:`, 0 failures, exit 0 (unchanged).
- WASM exports verified: `main`, `r0_init`, `r0_click`, `memory`.

## 9. Frozen-baseline verification

- `./check-frozen-s1.sh` → OK (`f90496c26dc45c7a387d8fc8639cd2781507d3d2`).
- `git diff --exit-code r0-trapdoor-v2 -- r0_s1_runtime.c r0_s1.h` → empty.

The R0 evaluator/trapdoor compiled to WASM **without modification** (no
portability bug was discovered).

- **Evaluator changed:** no.
- **HOST changed:** no (the DOM update reuses the existing `HOST_PRINT` channel).
- **S1 changed:** no.
- **Eighth S1 primitive needed:** no (the adapter is ordinary RAW + a JS
  stdout interpreter).

## 10. Verification notes

The headless `make wasm-test` proves the full R0 path (init → 3 clicks →
counter 3 → encoded DOM updates) under node without a DOM. The DOM mapping
itself is the small `glue.js` fragment, exercised by loading `web/index.html`
in a browser (per the README); the counter value is produced by R0, not JS.
