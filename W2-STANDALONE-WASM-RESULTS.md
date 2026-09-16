# W2 — Standalone WebAssembly Runtime (no Emscripten JS runtime)

## Goal

Build a browser-embeddable WebAssembly runtime for the frozen R0-on-S1 system
that does **not** use Emscripten's generated JS runtime or virtual filesystem.
The R0/GLON source is passed from JavaScript at runtime, and the browser glue
is a small handwritten `glon.js`.

## Result

| metric | value |
|---|---|
| `standalone/glon.wasm` | 22,459 bytes raw / 6,508 bytes gzip |
| `standalone/glon.js` | 3,550 bytes (handwritten, replaces the ~133 KB Emscripten JS) |
| WASM imports | exactly 2: `env.host_print`, `env.host_set_text` |
| WASM exports | `memory`, `glon_alloc`, `glon_init`, `glon_load`, `glon_call` (+ 3 inert CRT artifacts: `emscripten_stack_get_current`, `_emscripten_stack_restore`, `__indirect_function_table`) |
| libc / WASI | none linked (`-nostdlib`) |

The 43 KB `web/demo.wasm` (W1.1, linked against libc + WASI imports) becomes a
22 KB module with no `wasi_snapshot_preview1.*` imports and no libc.

## What changed

New files only (frozen layers untouched; `git diff --exit-code` against
`s1-frozen-v1` and `r0-trapdoor-v2` is clean):

- `standalone/glon.c` — portability + host layer:
  - minimal libc surface the frozen code requires (`printf`/`fprintf`/
    `putchar`/`fflush`, `malloc`/`free`, `memcpy`/`memmove`/`memset`,
    `strcmp`/`strncmp`/`strlen`, `isspace`/`isdigit`, `stdout`/`stderr`);
  - two host imports (`host_print`, `host_set_text` via `import_module("env")`);
  - four-function ABI (`glon_alloc`/`glon_init`/`glon_load`/`glon_call`);
  - `printf` special-cases a lone integer `>= 1000000` (the `WEB_SET_INT`
    protocol `handle*1000000+value`) into a `host_set_text` call.
- `standalone/app.glon` — the authoritative R0/GLON source (application +
  RAW adapter, section-marked).
- `standalone/glon.js` — handwritten bootloader (instantiate + imports +
  `data-glon-click`/`data-glon-id` wiring).
- `standalone/build-docs.py` — generates `docs/standalone.html` and publishes
  `glon.js`/`glon.wasm`.
- `standalone/node_test.js` — headless verification.
- `Makefile` — `wasm-standalone` and `wasm-standalone-test` targets.

## Build

```
emcc -O1 -nostdlib -fno-builtin -s STANDALONE_WASM=1 -s ALLOW_MEMORY_GROWTH=1 \
  -Wl,--no-entry -Wl,--export-memory -I. \
  standalone/glon.c s1.c r0_s1_runtime.c -o standalone/glon.wasm
```

then `standalone/build-docs.py` inlines `app.glon` into `docs/standalone.html`.

## Verification

`make wasm-standalone-test` (headless node): loads `app.glon`, calls
`increment` three times, asserts `host_set_text` emits `(1,1)`, `(1,2)`,
`(1,3)` — PASS. `make test` (native) remains 156 ok / 0 fail.

## Notes / gotchas

- `-fno-builtin` is required, otherwise clang rewrites `printf`/`fprintf` to
  `iprintf`/`fiprintf` under `-nostdlib`.
- Emscripten's `STANDALONE_WASM` forces exports of
  `emscripten_stack_get_current` and `_emscripten_stack_restore` (note the
  leading underscore on the latter — the C shim must be named
  `_emscripten_stack_restore` to satisfy `wasm-ld --export=...`). Both are
  inert no-ops never called by `glon.js`.
- The `raw` keyword is a **block-context** parse keyword: `app.glon` must wrap
  its top level in an outer `[ ... ]` (as `web/demo.r0` does), otherwise
  `web-set-int: raw 2 [...]` parses `raw` as an ordinary word.
