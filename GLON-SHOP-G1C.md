# GLON-SHOP-G1C.md — generic SPA application events

**Status:** complete (G1C). G1C completes the minimal persistent SPA loop.

## Progression so far

```text
G1A: persistent Glon machine + routes + state + HTML fragments
G1B: composable Glon views (a view is a block of ordinary Glon calls)
G1C: generic application events (browser interaction enters Glon as an event)
```

G1A proved the machine; G1B proved the view dialect; G1C proves that arbitrary
browser interaction can enter the persistent machine as an **application
event** while JavaScript stays ignorant of the event's meaning.

> We did not need VID for web pages. We needed a VID-like language for a
> persistent application.

The browser already supplies the GUI substrate; the dialect supplies the
application model on top of it.

## The loop G1C closes

```text
state
  ↓
view
  ↓
event
  ↓
state
  ↓
(view again)
```

A click on a `[data-glon-event]` button forwards a bare token into Glon; Glon's
`do-event` dispatcher interprets it, mutates persistent state, and re-renders
the current view. Navigating away and back shows the mutation persisted.

## The generic event bridge

The host bridge gained exactly one export, mirroring `glon_route`:

- **`glon_event(token)`** — forwards an application-event token into Glon
  (`r0_s1_g1a_event` binds `current-event` and runs the GLON `do-event` block).

JavaScript only detects a DOM element carrying `data-glon-event`, extracts the
token, and forwards it. It never branches on the token's meaning — no
`if (event === "add-one") …` appears in `host.js`. That logic lives in GLON:

```glon
;; events (mutate state, rerender current view)
add-one: func [] [ cart-count: + cart-count 1 render-current ]

;; unknown events fall back to re-rendering the current view (no state change)
do-event: [
    either = current-event 'add-one [ add-one ] [ render-current ]
]
```

## The shop now

```glon
cart-count: 0

products-view: [
    heading [ emit-text "Products" ]
    list    catalog
    text    [ emit-text "Products page visits: " emit-int visit-count ]
    text    [ emit-text "Cart: " emit-int cart-count ]
    button  [ emit-text "Add" ] "add-one"
    link    [ emit-text "Back home" ] "home"
]

add-one: func [] [ cart-count: + cart-count 1 render-current ]
```

The `button` term emits `<button data-glon-event='add-one'>Add</button>`; a
click enters Glon as the `add-one` event, `cart-count` increments, and the
current view re-renders with the new count.

## Routing refactor

G1C separates *route entry* (which may have entry side effects such as the
visit counter) from *render current view*:

```glon
render-current: func [] [
    either = current-route 'products [ render-products ]
    [ either = current-route 'home [ render-home ] [ render-not-found ] ]
]

go-products: func [] [ visit-count: + visit-count 1 render-current ]

add-one: func [] [ cart-count: + cart-count 1 render-current ]
```

Both a route and an event converge on `render-current`, so an event re-renders
the view it interrupted without re-running route-entry logic.

## Output-buffer isolation

The fixed scratch region G1B introduced is now named and isolated: `G1_OUT`,
`G1_OUT_DATA`, `G1_OUT_CAP` in `r0_s1.h`, exposed to RAW via `G1_OUT` /
`G1_OUT_DATA` symbols. The dialect refers to `G1_OUT` symbolically and never
hardcodes the address. This remains **temporary scratch/output storage** — a
trap-door convenience, not part of the permanent GUI architecture (see
`r0_s1.h`).

## Deliberately out of scope

No component lifecycle, reactive graph, virtual DOM, DOM diffing, widget
hierarchy, event-bubbling abstraction, message bus, promises, store framework,
or actors/reactors. A single generic token (plus an optional future value) is
sufficient. The `app [ state […] ]` wrapper is still deferred until the
language has dynamic `set`/`get`; ordinary persistent variables and functions
are already enough.

## Unknown-event fallback (design note)

The defined fallback for an unknown event is `render-current` — re-render the
current view with no state change. This is intentionally simple for G1C/G1D.

> Silent fallback can conceal misspelled event names as the application grows.

A later development/debug mode may want an observable `unknown-event` diagnostic
or failure, while production retains the benign fallback. This is not
implemented here; it is recorded as a known future pressure point.

## Verification

- **Native:** `make test` → 345 ok, 0 fail (326 existing + 10 G1A + 4 G1B + 5
  G1C); `./check-frozen-s1.sh` PASS.
- **Headless WASM:** `make wasm-g1a-test` asserts home / products / add-one ×2
  / persist / unknown (requires `emcc` + `node`).
- **Manual:** `make wasm-g1a` then `python3 -m http.server 8000` →
  `http://localhost:8000/demo/shop/app.html`.

## Known limitations

- Event value is token-only; an optional event value is future work.
- Unknown events re-render the current view (a defined fallback), rather than a
  dedicated error view.
- The output buffer is a fixed M[] region (temporary; see above).
