# Glon Shop — Programmer's Guide

This is the field guide for extending the Glon Shop demo. It explains the code
from the *application* downward, which is the direction an ordinary programmer
will approach it. After ten minutes you should be able to answer the only four
questions that matter:

- Where do I add a **screen**?
- Where do I add an **event** (an action)?
- Where do I add **state**?
- Where do I add a **browser capability**?

## The mental model

The browser page mounts a single Glon application here:

```html
<div id="app" data-glon-id="1"></div>
```

The Glon program is loaded once and remains alive. It owns the application
state and decides what HTML to render. JavaScript does **not** know what a
product, a basket, a search, the Home page, or the Products page means.

The JavaScript host does only three interesting things:

1. load the WASM and the Glon source;
2. put rendered HTML into the browser;
3. forward generic route / event / value messages.

The host's own header comment states the boundary explicitly: *"No routing
decision, application state, or page logic lives here. Glon is the application
controller; this file is only the host bridge."*

## The contract: three (and a half) paths from browser to Glon

```text
data-glon-route="products"
        ↓
route("products")
        ↓
current-route
        ↓
Glon route logic


data-glon-event="search"
        ↓
event("search")
        ↓
current-event
        ↓
Glon event logic


data-glon-event="add-product"
data-glon-value="Tea"
        ↓
event("add-product", "Tea")
        ↓
current-event + current-value
        ↓
Glon application logic
```

There is a fourth, form-related case. When an event button has **no**
`data-glon-value`, the host looks for an element whose `data-glon-input`
matches the event token, and forwards *that element's current value*. The
Search button is the canonical example: the button carries
`data-glon-event="search"`, and the host finds `<input data-glon-input="search">`
and sends its value. The host forwards the value but never interprets it.

That is the entire central contract.

## Reading the Glon code here, not at the top

The `raw [...]` routines at the beginning of `g1s.glon` and `g1b.glon` are
implementation machinery (byte output, managed string allocation, string
comparison). Someone extending the shop should skip past them initially.

The first interesting piece is this:

```glon
wrap: func [open close] [
    func [content] [
        emit-text open
        do content
        emit-text close
    ]
]

heading: wrap h1-open h1-close
subhead: wrap h2-open h2-close
text:    wrap p-open p-close
tagline: wrap tagline-open tagline-close
meta:    wrap meta-open meta-close
```

This is the beginning of the GUI vocabulary. `wrap` is a **function factory**.
Calling:

```glon
heading: wrap h1-open h1-close
```

returns a function that has *captured* the two strings representing `<h1>` and
`</h1>`. Consequently:

```glon
heading [
    emit-text "Glon Shop"
]
```

means approximately:

```text
emit <h1>
evaluate the content block
emit </h1>
```

This matters because it is more than syntactic sugar: the little view system is
made out of ordinary Glon functions and closures, not a special GUI
implementation.

## The browser-aware controls

The next layer introduces the few controls that communicate with the generic
host:

```glon
link: func [content route] [
    emit-text nav-open
    emit-text btn-open
    emit-text route
    emit-text btn-mid
    do content
    emit-text btn-close
    emit-text nav-close
]

button: func [content event] [
    emit-text event-open
    emit-text event
    emit-text btn-mid
    do content
    emit-text btn-close
]

value-button: func [content event value] [
    emit-text event-open
    emit-text event
    emit-text value-mid
    emit-text value
    emit-text btn-mid
    do content
    emit-text btn-close
]
```

These controls do not navigate and do not fire events themselves. They generate
ordinary HTML carrying attributes that `host.js` understands.

For example:

```glon
link [emit-text "View products"] "products"
```

eventually produces roughly:

```html
<button data-glon-route="products">View products</button>
```

while:

```glon
value-button [emit-text "Add"] "add-product" "Tea"
```

produces the equivalent of:

```html
<button data-glon-event="add-product" data-glon-value="Tea">Add</button>
```

JavaScript sees only `"add-product"` and `"Tea"` and hands them back to Glon.
There is no `if (Tea)` and no basket logic in the host. The host uses event
delegation for exactly these attributes. This is where an extender should begin
inventing additional higher-level controls.

## Application state is just Glon state

The shop currently starts with:

```glon
visit-count: 0
tea-count: 0
rice-count: 0
search-term: []

tea: "Tea"
rice: "Rice"

catalog: [tea rice]
```

Nothing in the browser owns those counters. When an Add event arrives:

```glon
add-product: func [] [
    either str-eq current-value "Tea" [
        tea-count: + tea-count 1
    ][
        either str-eq current-value "Rice" [
            rice-count: + rice-count 1
        ][
            none
        ]
    ]
    render-current
]
```

Glon mutates its persistent state and then renders again.

This is one of the most important properties of the demo: it is **not**
regenerating an isolated page from scratch on each interaction. The Glon
machine remains alive across route and event calls.

## Why `get block-at` appears in the product renderer

The catalogue is:

```glon
catalog: [tea rice]
```

Those are **words** inside a block, not the strings themselves. The product
renderer therefore contains:

```glon
emit-text get block-at items i
```

`block-at items i` retrieves the word, e.g. `tea`; `get` obtains the value of
that word, which is `"Tea"`. Likewise:

```glon
value-button [emit-text "Add"] "add-product" get block-at items i
```

uses that same value as the event payload.

So one generic Add control can be instantiated repeatedly from application
data. The event token remains `"add-product"` while the value distinguishes the
particular product. That is the important G1E idea in very little code.

## Views are executable blocks

There is no separate template language:

```glon
home-view: [
    heading [emit-text "Glon Shop"]
    tagline [emit-text "A tiny GLON storefront"]
    link [emit-text "View products"] "products"
]
```

and:

```glon
products-view: [
    heading [emit-text "Products"]
    products catalog

    emit-text panel-open
    emit-text search-open

    input "search"
    button [emit-text "Search"] "search"

    emit-text search-close

    meta [emit-text "Search: " emit-text search-term]
    meta [emit-text "Products page visits: " emit-int visit-count]

    emit-text panel-close

    basket

    link [emit-text "Back home"] "home"
]
```

are ordinary blocks of Glon code. Rendering executes the appropriate block.
That distinction matters: there is not a second language sitting inside the
first one.

## Rendering

The three page renderers are deliberately boring:

```glon
render-home:      func [] [emit-clear do home-view emit-finish]
render-products:  func [] [emit-clear do products-view emit-finish]
render-not-found: func [] [emit-clear do not-found-view emit-finish]
```

The pattern is:

```text
clear output  →  execute view  →  finish output
```

The finished HTML reaches the host's `host_set_html`, which assigns it to the
element identified by `data-glon-id`. For most application work you should not
have to touch `emit-clear`, `emit-text` or `emit-finish` — they are beneath the
useful abstraction line.

## Routing

The router is entirely in Glon:

```glon
render-current: func [] [
    either = current-route 'products [ render-products ]
    [ either = current-route 'home [ render-home ] [ render-not-found ] ]
]
```

and:

```glon
route: [
    either = current-route 'products [ go-products ]
    [ either = current-route 'home [ go-home ] [ go-not-found ] ]
]
```

The distinction between `"products"` in generated HTML and `'products` in
application logic is worth noticing. The browser transports **text**; at the
Glon application boundary, routing identity is represented symbolically as a
**word**.

## Events

Events follow exactly the same pattern:

```glon
do-event: [
    either = current-event 'add-product [ add-product ]
    [ either = current-event 'search [ search ] [ render-current ] ]
]
```

and:

```glon
search: func [] [
    search-term: current-value
    render-current
]
```

So adding an application action consists of two pieces:

1. generate a control carrying an event token; and
2. dispatch that token in `do-event`.

No JavaScript edit is required.

## Strings

The source can now say:

```glon
"Tea"
"Rice"
"Glon Shop"
"× "
```

These are real managed Glon `STRING!` values, not byte-list notation. The RAW
`mk-string`, `str-eq`, and `emit-text` machinery near the top of the library
files is implementation-level support and should normally be ignored by an
application author. The source also demonstrates `str-eq` operating directly
against readable string literals.

> **If you are writing an application and find yourself writing
> `[84 101 97]`, something has gone wrong.**

## Worked extension: adding a page

To add an `about` route today you would touch only Glon:

```glon
;; a new view
about-view: [
    heading [emit-text "About"]
    text [emit-text "This is the Glon Shop."]
    link [emit-text "Back home"] "home"
]

;; a renderer
render-about: func [] [emit-clear do about-view emit-finish]

;; extend render-current
render-current: func [] [
    either = current-route 'products [ render-products ]
    [ either = current-route 'home [ render-home ]
      [ either = current-route 'about [ render-about ] [ render-not-found ] ] ]
]

;; an entry handler (the go-* layer holds per-route side effects)
go-about: func [] [ render-current ]

;; extend the route dispatcher
route: [
    either = current-route 'products [ go-products ]
    [ either = current-route 'home [ go-home ]
      [ either = current-route 'about [ go-about ] [ go-not-found ] ] ]
]

;; and place a link somewhere that carries the token
link [emit-text "About"] "about"
```

## Extension points

| Goal                 | What to do                                                                                    |
|----------------------|-----------------------------------------------------------------------------------------------|
| New **page**         | Define a view, a renderer, a `route`/`render-current` branch, and generate a link with that token. |
| New **action**       | Generate a `button` or `value-button`, then add a branch to `do-event`.                        |
| New **state**        | Create a Glon value, mutate it in the event handler, and call `render-current`.               |
| New **repeated UI**  | Put the data in a block and write a Glon function that renders each value. Do not add application-specific JavaScript. |
| New **browser capability** | Only now extend the generic host boundary. Browser APIs belong below Glon; application meaning stays above it. |
| New **appearance**   | Edit `style.css`, not the application logic.                                                   |

## Files

| file | role |
|---|---|
| `app.html` | the bundled page (generated by `build.py`); links `style.css` + `host.js`, inlines the Glon source |
| `bundle.glon` | the combined Glon source (generated by `build.py`; used by `node_test.js` and the native G1E test) |
| `shop.glon` | the authoritative application (state, product data, views, routes, events) |
| `g1b.glon` | the view-dialect library (emitters + view terms + the `wrap` factory) |
| `g1s.glon` | the STRING! primitives (`mk-string`, `str-eq`, `get`) |
| `style.css` | the extracted shop stylesheet |
| `host.js` | the handwritten JS host bridge (DOM writes + route/event/value forwarding) |
| `build.py` | bundles the Glon source (no string rewriting); writes `app.html` + `bundle.glon` |
| `node_test.js` | headless WASM verification (browser-equivalent) |
| `fragments/*.html` | G1A's HTML fragments (superseded by the view dialect; retained for reference) |
| `glon.wasm` | the WASM runtime (built by `make wasm-g1a` / CI; not committed here) |

## Build, run, and test

```
emcc               # Emscripten (WASM build)
node               # headless test
make wasm-g1a      # build glon.wasm + regenerate app.html
make wasm-g1a-test # headless browser-equivalent test
make test          # native tests (no emcc/node needed)
```

To rebuild the generated sources by hand:

```
python3 demo/shop/build.py   # writes app.html + bundle.glon
```

To serve the page locally:

```
python3 -m http.server 8000
# open http://localhost:8000/demo/shop/app.html
```

## What this demo does not yet prove

Be candid before building on this for real use:

- Adding `"Coffee"` to `catalog` would make it *appear*, but it would not
  magically acquire persistent basket state. `tea-count`, `rice-count`, the
  basket rows, and the `add-product` branches are still written out explicitly.
  The next application-level refactoring should make products and their mutable
  state **data-driven**, rather than adding a third parallel `coffee-count`.

- The search example demonstrates browser input → Glon value → persistent
  state → rerender. It does **not** yet filter the catalogue.

- Routes are internal SPA routes. `host.js` does not use `history.pushState`,
  and it starts every fresh session at `"home"`.

- State lives in the running Glon/WASM machine. Reloading the page is not
  persistence to disk or to a server.

- **HTML escaping** is the one point to fix before calling this
  production-ready. `host_set_html` installs rendered output with `innerHTML`,
  and Glon can emit values such as `search-term` directly into that HTML. A real
  application must distinguish text content from trusted markup and escape
  user- and data-derived text and attribute values correctly. This is not a
  reason to change the architecture; it is a missing production-level GUI
  primitive the demo has not needed to solve yet.
