# GLON-SHOP-G1A.md — browser-hosted Glon/WASM application skeleton

**Status:** complete (G1A). First real Glon *application* architecture above the
frozen R0/S1 substrate.

## 1. Goal

Prove that a **persistent** Glon runtime inside WebAssembly can act as the
application engine of a browser-hosted web application — no external web server
(after page load), no RDBMS, no backend API, no frontend framework, no
server-side Cheyenne. The browser is the host; the Glon/WASM runtime performs
the application/server role inside the page.

This milestone is an **architectural proof**, not a product. It deliberately
stops at two routes and a visit counter.

## 2. Why the browser/WASM runtime is the application server

In a classic server-side template system (Cheyenne + RSP), the page's embedded
executable sections are evaluated by a persistent interpreter *on a server*,
and each request re-enters that interpreter while application state lives in
that process. G1A relocates the same shape into the browser: the WASM module
contains the Glon/R0-on-S1 interpreter, instantiated **once** at page load and
retained for the life of the page. The interpreter, not JavaScript, owns
application state and routing.

```
Browser
  └─ app.html
      ├─ Glon/S1 WASM runtime   (persistent, owns state + routing)
      ├─ application state      (a global context in the runtime)
      ├─ page fragments         (bundled byte-lists; embedded Glon)
      ├─ CSS
      └─ host.js                (thin bridge: DOM writes + event forwarding)
```

### Relationship to Cheyenne/RSP

- **Cheyenne** evaluated embedded REBOL (`<% … %>`) on a server, producing HTML
  per request; the REBOL process held application state between requests.
- **G1A** evaluates embedded Glon (`<% … %>`) inside browser-hosted WASM,
  producing HTML into a DOM container; the WASM module holds application state
  between route changes.

The conceptual difference is *where* the persistent interpreter lives, not the
template model. G1A does **not** attempt to reproduce Cheyenne; it uses only
enough syntax to demonstrate the model.

## 3. Persistent interpreter model

The runtime is instantiated once and kept alive:

```
page load  → instantiate Glon once → glon_init → glon_load(shop source)
click      → glon_route("products") → Glon handles it → host_set_html
click      → glon_route("home")     → Glon handles it → host_set_html
…
```

There is no per-event re-instantiation, no reload of the source, no
reconstruction of state. State is a **global context** of `cell`s in the
runtime's flat memory. Integers bound at load time (`visit-count: 0`) persist
across every `r0_s1_run` because the global context lives in the loader heap,
which is permanent and never reset or swept.

**Why integers and byte-blocks, not `STRING!`:** the managed heap (and therefore
managed `STRING!` objects) is reset to its seeded frontier on every `r0_s1_run`
(`s1_reset` sets `REG_HP`, then the M3 datatype bootstrap re-seeds the
frontier). Managed objects therefore do **not** survive a run boundary. G1A
sidesteps this without any runtime change:

- persistent **state** → integers in the global context (loader heap);
- persistent **fragments** → loader-heap `BLOCK!`s of integer byte codes
  (0..255), the same permanent representation M3B used before `STRING!` existed.

This is a documented compromise, not a hidden one. When the runtime grows a
persistent managed heap (or a loader-heap byte series), fragments and richer
state can migrate to `STRING!` with no change to the renderer's byte-stream
model.

## 4. Fragment lifecycle

The intended pipeline (compression deferred, but the shape is reserved):

```
compressed fragment  →  decompress  →  scan/evaluate embedded Glon  →  render
```

- **Bundled.** Fragments are not fetched over HTTP. `demo/shop/build.py` reads
  `fragments/*.html`, encodes each as a GLON byte-list (`home-fragment: [ … ]`),
  and inlines the result into the `<script type="application/glon">` block of
  `app.html`. The network panel shows **no** fragment fetches during navigation.
- **Decompress = identity today.** The fragment is stored uncompressed; the
  byte-list representation is exactly what a compressed fragment would
  decompress into, so a compression step can be inserted in `build.py` (or a
  future GLON/C decompressor) without touching the renderer.
- **Scan/evaluate/render** is `r0_s1_g1a_render_fragment` (§6). Each `<% … %>`
  block is extracted, parsed with the ordinary reader, run with the ordinary
  evaluator (`r0_s1_parse` / `r0_s1_run`), and its non-`none` result is
  stringified into the output.

## 5. Embedded Glon syntax

Cheyenne/RSP-inspired delimiters, adjusted to fit the frozen parser:

```html
<h1>Products</h1>
<% visit-count: + visit-count 1 none %>
<p>Products page visits: <% visit-count %></p>
```

- `<% code %>` evaluates `code`; the result is stringified into the HTML.
- A result of `none` (or zero results) contributes nothing, so a
  side-effect-only block ends in `none`.
- **`print`** is *not* used for inline output: in the frozen runtime `print`
  maps to `HOST_PRINT` → the host console, not the rendered HTML. Instead a
  block **returns** the value to splice (e.g. `<% visit-count %>`). This is the
  one deliberate syntax compromise and is documented here.

## 6. Architecture split

| concern | layer |
|---|---|
| presentation, markup | HTML fragments |
| routing decision, state, page logic, fragment selection | **Glon** (`demo/shop/shop.glon`) |
| template mechanics (scan `<% … %>`, evaluate, stringify) | `r0_s1_g1a.c` (browser-agnostic; uses the ordinary evaluator) |
| DOM writes, DOM events, WASM loading/interop | **host.js** (thin bridge) |
| WASM host imports/exports + libc shims | `standalone/glon.c` |
| frozen substrate | `s1.c` / `s1.h` — **unchanged** |

JavaScript is **not** the application controller. It receives clicks, forwards a
single route token, and writes rendered HTML into `[data-glon-id="1"]`. It
contains no routing table, no state, no page logic.

### Host interface (smallest useful surface)

Two additions, following the existing `host_*` / `glon_*` convention:

- **Glon → JS:** `host_set_html(handle, ptr, len)` — write rendered HTML into
  `[data-glon-id="<handle>"]` (the "DOM_SET_HTML" equivalent).
- **JS → Glon:** `glon_route(token_ptr, token_len)` — forward a route token
  (the "EVENT/ROUTE input into Glon" equivalent). It binds `current-route`,
  runs the GLON `route` block, renders the selected fragment, and emits via
  `host_set_html`.

`host_print` / `host_set_text` are retained for the W2/M1 demos. No direct JS
execution from arbitrary Glon strings; the surface is narrow and explicit.

### Routing

`glon_route("products")` builds `[ current-route: 'products do route ]` and runs
it. The GLON `route` block maps the token to a page block, which returns the
fragment; the renderer then assembles the HTML. The **selection** lives in
Glon. URL↔token mapping (`/` → `home`, `/products` → `products`) is a one-line
history-integration concern in `host.js`, not a routing decision; `pushState`
is deliberately out of scope.

## 7. Security / trust model

Only **trusted, bundled** fragments may contain executable Glon. `host_set_html`
accepts HTML, and the template engine evaluates `<% … %>`, but the only source
of fragments is the inlined, build-time `bundle.glon`/`shop.glon` — there is no
path by which external, user, or API-supplied content reaches the evaluator. A
future network-capable Glon would need an explicit, distinct "eval" boundary;
none exists today.

## 8. Out of scope (deliberately)

Shopping cart, checkout, payment, database, remote API, authentication, product
search, CSS sophistication, component framework, virtual DOM, tuple-space
agents, production compression, `history.pushState`, and any new runtime
optimisation. G1A is architectural proof only.

## 9. Verification

- **Native:** `make test` → 336 ok, 0 fail (326 existing + 10 G1A), and
  `./check-frozen-s1.sh` PASS (S1 substrate byte-identical).
- **Headless WASM:** `make wasm-g1a-test` (requires `emcc` + `node`) asserts
  home → products → home → products renders visit counts 1 and 2 and the
  unknown route renders "Not found".
- **Manual:** `make wasm-g1a` then `python3 -m http.server 8000` and open
  `http://localhost:8000/demo/shop/app.html` (see `demo/shop/README.md`).

## 10. Known limitations

- No `emcc`/`node` in the current build environment, so the `.wasm` module and
  the headless node test were authored but not executed here; the template
  engine and router are fully covered by the native tests, and `build.py`
  round-trips fragments byte-for-byte.
- Fragments are byte-list blocks (not `STRING!`) because managed strings do not
  persist across `r0_s1_run` boundaries (§3).
- `print` is console-only; inline output uses returned values (§5).
- No `history.pushState`; back/forward are browser history only.
