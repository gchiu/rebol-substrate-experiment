# Glon Inflate — browser demonstration

**DEFLATE decompression using Glon's original seven-primitive execution core.**

This is a local, self-contained browser page. It runs the existing Glon DEFLATE
decoder on the standard seven-primitive runtime compiled to WebAssembly. It does
not use the experimental 12-operation machine, a JavaScript DEFLATE library,
`DecompressionStream`, zlib as the decoder, or precomputed output.

Open `index.html` through an HTTP server (not `file://`), choose or paste
compressed bytes, and press **Decompress with Glon**.

## What runs where

```
index.html / app.js / style.css
  └─ fetch glon.wasm + prelude.glon + strings.glon + strings-ext.glon
       + f2buf.glon + inflate.glon + inflate-driver.glon
  └─ instantiate glon.wasm, glon_init, glon_load each library
  └─ per decode:
       demo/browser-new
       for each input chunk:  demo/browser-feed <bytes>
                               demo/browser-read 256  (repeat until N/D/E)
       demo/browser-finish
       repeated demo/browser-read 256 (until D/E)
```

`glon.wasm` is the standard shop runtime built from `standalone/glon.c` and the
current **seven-primitive** S1/runtime sources. The Glon decoder is the
unchanged `inflate.glon`; `f2buf.glon` is F2's bounded code-length helper.
`inflate-driver.glon` only exposes the existing `dz/new`, `dz/feed`,
`dz/read`, `dz/finish` API and prints length-prefixed output frames for JS.

The browser host drives the decoder across separate `glon_run` calls. This is
true suspension/resumption: decoder state lives in Glon between calls. The
streaming sample feeds deliberately awkward fragments and drains output after
each fragment.

### Containers

The existing Glon decoder implements **raw DEFLATE**. This page also accepts
zlib-wrapped and gzip-wrapped inputs, but only by removing the container header
and trailer in JavaScript and passing the DEFLATE payload to Glon. The page
validates zlib Adler-32 and gzip CRC-32/ISIZE after decoding. The compression
codec is always Glon; no JavaScript decompressor is used.

## Differences from the LaTeX page

The LaTeX page also uses `glon.wasm` and `glon_run`, but it runs one isolated
example and renders KaTeX. DEFLATE needs binary input, repeated feed/read calls,
and a decoder that survives those calls. F2 also introduced a small masm scratch
helper. This page therefore keeps one Glon instance per decode and calls the
existing decoder API across runs; it does not use the separate GLON_LIVE byte
event ABI. The S1 core and seven primitives are unchanged.

## Assets and build

| Asset | Purpose | Size |
|---|---|---:|
| `glon.wasm` | standard seven-primitive WebAssembly runtime | 92,746 B |
| `app.js` | browser adapter and container framing | ~18.5 KB |
| `inflate.glon` | existing decoder (unmodified copy) | 28,790 B |
| `f2buf.glon` | existing F2 scratch helper | 1,875 B |
| `prelude/strings/strings-ext` | shared Glon libraries | 16,552 B |
| `inflate-driver.glon` | feed/read/output adapter | 992 B |
| `samples.json` + samples | committed genuine fixtures | ~18 KB |
| page/CSS | presentation | ~9 KB |

Total transferred page assets are about **187 KB** (the browser fetches the
unminified library copies; `app.js` strips comments before `glon_load`).

Rebuild the current runtime (requires emsdk/emcc, as documented in the root
`Makefile`):

```
make integrity/build/glon.wasm
cp integrity/build/glon.wasm experiments/glon-inflate-web/glon.wasm
```

The committed `glon.wasm` is the current-source build used by the verification (SHA-256 `4dfcb3331cdcc75ba5a4e046027c217048dd4ad9bf22048ca6ba6de60e268e78`).
The library copies are made from the repository sources; do not edit them in
place:

```
cp jupyter/prelude.glon experiments/glon-inflate-web/prelude.glon
cp demo/shop/strings.glon experiments/glon-inflate-web/strings.glon
cp demo/shop/strings-ext.glon experiments/glon-inflate-web/strings-ext.glon
cp experiments/glon-deflate/f2buf.glon experiments/glon-inflate-web/f2buf.glon
cp experiments/glon-deflate/inflate.glon experiments/glon-inflate-web/inflate.glon
```

Fixtures are generated independently with Python's zlib/gzip; the actual
decoding is always Glon:

```
python3 tools-make-samples.py
```

The samples use zlib windowBits 10, matching the decoder's 768-byte window.
Larger RFC windows are outside this 64K demonstration.

## Verification

Native baseline (repository result):

- `make -C experiments/glon-deflate test`: **480/480** conformance, **44/44**
  unit tests.
- `./check-frozen-s1.sh`: passes.

Automated local checks:

```
./test.sh
```

This compares every fixture with native `dzhost` (if built) and runs the same
`glon.wasm` + driver through the headless Node test. Recorded results:

- native vs browser: **6/6 fixtures byte-identical**;
- Node/WASM: **11/11 checks** (all samples, binary/NUL, empty, malformed,
  repeatability);
- real Chrome (headless, CDP): **31/31 checks** (all samples, binary download
  enabled, empty output, malformed, invalid Base64 and hex, empty input,
  repeatability, same-origin resources, no console errors, no horizontal
  overflow).

Real-browser test, from this directory:

```
python3 -m http.server 8765 --bind 0.0.0.0
# In another terminal, launch Chrome/Edge with a debugging port, for example:
#   chrome --headless=new --remote-debugging-address=0.0.0.0 \
#     --remote-debugging-port=9222 http://localhost:8765/index.html
node test-browser.mjs --url http://localhost:8765/index.html --port 9222
```

The browser test requires Node 22+ for global `fetch`/`WebSocket`; it does not
bundle a browser or a testing framework. Firefox/Safari and a physical phone
were not tested. The claimed browser result is Chromium (Google Chrome 157,
headless) over CDP.

## Measured example times

On the verification machine, page init/load (instantiate + six library loads)
was about **3–11 ms**; decode times were roughly **20–45 ms** for the short
binary/Hello samples, **0.9 s** for the 139-byte gzip sample, and **1.9 s** for
the 5,214-byte text and chunked streaming samples. The large range reflects
JavaScript-to-WASM dispatch overhead and Glon's evaluator; it is not a speed
comparison with zlib. No zlib speed advantage is claimed.

## Limits

- The runtime's `srcbuf` is 16 KiB, so this demonstration accepts compressed
  inputs up to **3,000 bytes**; larger inputs must be split or use a host
  with a larger source buffer.
- The decoder keeps a **768-byte** LZ77 window. A legal stream needing a
  longer distance reports a Glon `window` error.
- The output is preserved as exact bytes, including NUL; the page shows text
  when it is valid printable UTF-8, and always provides a hex preview and
  binary download.
- The page is **not deployed** and has no public URL yet.
- Browser coverage is Chromium only; real mobile was not tested.

## Source paths

- Decoder: `experiments/glon-deflate/inflate.glon`
- F2 buffer: `experiments/glon-deflate/f2buf.glon`
- Driver: `experiments/glon-inflate-web/inflate-driver.glon`
- Adapter: `experiments/glon-inflate-web/app.js`
- Runtime: `standalone/glon.c`, `s1.c`, `r0_s1_runtime.c`, `r0_s1_g1a.c`
- Public repository: <https://github.com/gchiu/rebol-substrate-experiment>
