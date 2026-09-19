# GLON-SHOP-G1D.md — generic event + value

**Status:** complete (G1D). Event values extend the persistent SPA model without
changing the architectural boundary.

## Progression

```text
G1A: persistent machine
G1B: composable views
G1C: generic token events
G1D: generic event + value
```

G1C proved that a browser interaction can enter the persistent machine as an
opaque **event token**. G1D proves the same for an **event token plus an
ordinary value**, which is what forms, search boxes, quantities and selections
need.

> JavaScript transports interaction; Glon owns meaning.

## The loop

```text
DOM interaction
      ↓
event token + value
      ↓
persistent Glon machine
      ↓
application semantics
      ↓
state mutation
      ↓
render-current
```

## Host bridge

One new export, mirroring `glon_event`:

- **`glon_event_value(token_ptr, token_len, value_ptr, value_len)`** — forwards
  an opaque token and an opaque value. `r0_s1_g1a_event_value` binds
  `current-event` to the token word and `current-value` to the value (as a
  loader-heap byte-list block), then runs the GLON `do-event` block.

`glon_event` (token-only) now also binds `current-value` to an empty byte-list,
so the dispatcher may always reference it safely.

## The browser convention

JS stays generic. A `[data-glon-event]` button whose token matches a
`[data-glon-input]` element forwards that element's value; otherwise it forwards
the token alone. JS never branches on what the token means:

```js
var token = el.getAttribute("data-glon-event");
var input = document.querySelector('[data-glon-input="' + token + '"]');
if (input && input.value !== undefined) glonEventValue(token, input.value);
else glonEvent(token);
```

The event token doubles as the input's name — a minimal, documented convention,
not a form framework.

## Value representation

The value is arbitrary text. It is represented as a **loader-heap byte-list
block** (GLON has no string literals and no persistent managed strings); the
host's C layer expands the bytes into a `[ b0 b1 … ]` literal embedded in the
source run for the event. This reuses the same byte-block representation the
dialect already uses for text, so no managed-string work is required. A numeric
value would need a parse step; text input is used here instead.

## Glon side

```glon
search-term: []

products-view: [
    ...
    text   [ emit-text "Search: " emit-text search-term ]
    input  "search"
    button [ emit-text "Search" ] "search"
    ...
]

search: func [] [ search-term: current-value render-current ]

do-event: [
    either = current-event 'add-one [ add-one ]
    [ either = current-event 'search [ search ] [ render-current ] ]
]
```

The token is interpreted entirely in GLON; the value is captured into
`search-term` and the current view re-renders.

## Preserved distinctions

- **Route entry vs rerender** remains separate: `go-products` performs entry
  side effects then `render-current`; an event mutates state then
  `render-current` — it never re-runs route-entry logic.
- **Host boundary** is unchanged: JS knows only `route(token)`,
  `event(token)`, `event(token, value)`.
- **Output buffer** (`G1_OUT`/`G1_OUT_DATA`/`G1_OUT_CAP`) is unchanged; no raw
  address was reintroduced into Glon code.

## Verification

- **Native:** `make test` → 351 ok, 0 fail (326 existing + 10 G1A + 4 G1B + 5
  G1C + 6 G1D); `./check-frozen-s1.sh` PASS.
- **Headless WASM:** `make wasm-g1a-test` asserts the search event with a value
  and an empty value (requires `emcc` + `node`).
- **Manual:** `make wasm-g1a` then `python3 -m http.server 8000` →
  `http://localhost:8000/demo/shop/app.html`.

## Known limitations

- Value is a byte-list; numeric input would need a parse step (deferred).
- Value length is capped (200 bytes) in the host bridge.
- Unknown events fall back to `render-current` (see the design note in
  `GLON-SHOP-G1C.md`); misspelled event names are silently tolerated.
