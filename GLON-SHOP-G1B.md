# GLON-SHOP-G1B.md — a minimal persistent-SPA dialect

**Status:** complete (G1B). G1A proved the runtime model; G1B moves *upward* into
the application language.

## 1. What G1A already proved

G1A (`GLON-SHOP-G1A.md`) established the persistent browser-hosted machine: one
Glon/WASM instance, source loaded once, Glon-owned routing, persistent global
state across events, browser events entering Glon through `glon_route`, HTML
returning through `host_set_html`, and JavaScript remaining a thin host layer.
G1B does **not** redo any of that; it sits on top of it.

## 2. The reframing

The earlier instinct was "VID for web pages". That was the wrong problem: it
suggested modelling HTML's structure and hiding it behind faces.

G1B explores instead:

> **VID-like composition for a persistent SPA.**

The dialect describes *application* concerns — **state, routes, events, views,
transitions** — while HTML/CSS/DOM is the rendering substrate underneath. It is
deliberately **incomplete** and **permeable**: there is no `face!`, no style
inheritance, no reactors, no virtual DOM, no layout engine, and no attempt to
model everything the browser can do. The full Substrate Principle is documented
in `DESIGN.md`; this file records only what the shop milestone actually does.

## 3. The G1B dialect

The shop is now written as ordinary GLON (no string literals — those are a
build-time convenience supplied by `demo/shop/build.py`):

```glon
;; state (a plain set-word, persistent)
visit-count: 0

;; product data (a GLON block)
catalog: [ "Tea" "Rice" ]

;; views: composable blocks of heading / text / list / link
home-view: [
    heading [ emit-text "Glon Shop" ]
    link    [ emit-text "View products" ] "products"
]
products-view: [
    heading [ emit-text "Products" ]
    list    catalog
    text    [ emit-text "Products page visits: " emit-int visit-count ]
    link    [ emit-text "Back home" ] "home"
]

;; routes: ordinary GLON functions
go-home:      func [] [ emit-clear do home-view emit-finish ]
go-products:  func [] [ visit-count: + visit-count 1 emit-clear do products-view emit-finish ]

;; router: a GLON block dispatching on the route token
route: [
    either = current-route 'products [ go-products ]
    [ either = current-route 'home [ go-home ] [ go-not-found ] ]
]
```

A view is an ordinary GLON block of function calls; **rendering a view is `do
view-block`**. Each term (`heading`, `text`, `list`, `link`) wraps its content —
a block of `emit-*` calls — in an HTML element.

### Chosen syntax, and why

- **Views as function-call blocks** (`heading [ emit-text "…" ]`) because GLON
  already evaluates blocks and calls functions; no new form or dynamic word
  binding is needed.
- **`emit-text` / `emit-int`** are the low-level emitters; they are the visible
  seam to the substrate (see §4). A view *composes* them without a closed object
  model.
- **No `app [ state […] route … ]` wrapper.** GLON has no dynamic `set 'word
  value` primitive, so a generic `app`/`state`/`route` interpreter would need
  build-time translation or raw context surgery. State is therefore a plain
  set-word, routes are plain functions, and the router is a plain block — the
  closest form that fits existing GLON naturally. This is a documented
  compromise, not a hidden one.

## 4. Lowering onto G1A machinery

```
GLON view dialect (heading / text / list / link)
        ↓  `do view-block`
byte-output RAW helpers (emit-clear / emit-byte / emit-finish)
        ↓  emit into a fixed loader-style byte-list block
r0_s1_g1a_render_fragment  (unchanged)
        ↓
host_set_html  →  DOM
```

The dialect emits bytes into a fixed output block (`M[25344..]`, an unused
region above the GC state and below the managed heap); `emit-finish` returns
`mk_block(25344)`, which the **unchanged** G1A renderer writes to the DOM. The
trap door is visible at two levels:

- `emit-text` emits arbitrary bytes — a view may drop to raw HTML/CSS at any
  point;
- the `emit-*` words are `raw` S1 fragments, the same generic trapdoor as always.

No new S1 primitive, no new native id, no evaluator/parser change, and **no C
change** was required.

## 5. What remains raw / substrate-level (deliberately)

- HTML/CSS/DOM is expressed directly as bytes; there is no DOM abstraction.
- Route tokens are byte-lists (`"home"` / `"products"`), matching the browser's
  `data-glon-route` attribute.
- String literals do not exist in GLON; `demo/shop/build.py` expands `"…"` to
  byte-lists at build time (the same tooling boundary the RAW assembler uses).
- The `<% … %>` template engine from G1A remains available as a raw-HTML escape.

## 6. Verification

- **Native:** `make test` → 340 ok, 0 fail (326 existing + 10 G1A + 4 G1B);
  `./check-frozen-s1.sh` PASS.
- **Headless WASM:** `make wasm-g1a-test` asserts home / products / home /
  products (visits 1 and 2) / unknown → not found (requires `emcc` + `node`).
- **Manual:** `make wasm-g1a` then `python3 -m http.server 8000` →
  `http://localhost:8000/demo/shop/app.html` (see `demo/shop/README.md`).

## 7. Known limitations / awkwardness

- No string literals in GLON: `emit-text` arguments are byte-lists (build.py
  hides this behind `"…"`).
- No dynamic `set`, so the `app [ … ]` wrapper is not used; state/routes are
  plain set-words/functions.
- Dynamic content is expressed with explicit `emit-int`/`emit-text` calls rather
  than `text [ "label " value ]` interpolation (no runtime word evaluation from
  a literal block).
- The raw output block is a fixed M[] region, so a view must fit in its capacity.
