# GLON-SHOP-G1E.md — repeated interactive composition

**Status:** complete (G1E).

## Progression

```text
G1A  persistent machine
G1B  composable views
G1C  generic events
G1D  event + value
G1E  repeated interactive composition
```

## The architectural question

> Can one Glon interaction abstraction be instantiated repeatedly with different
> application data without introducing application knowledge into JavaScript?

The answer is **yes**, using only the G1D `event(token, value)` bridge.

## What G1E proves

The shop now renders a repeated family of interactive controls from ordinary
Glon iteration. Each product in the catalogue becomes a row with an "Add" button
that carries that product's identity as its event value:

```html
<li>Tea  <button data-glon-event='add-product' data-glon-value='Tea'>Add</button></li>
<li>Rice <button data-glon-event='add-product' data-glon-value='Rice'>Add</button></li>
```

Every button uses the **same event semantic** (`add-product`) with a **distinct
value** (`Tea` vs `Rice`). JavaScript forwards the token+value pair opaquely;
Glon decides what each value means:

```glon
add-product: func [] [
    cart-count: + cart-count 1
    last-added: current-value
    render-current
]
```

Clicking Tea, then Rice, then Tea yields `Cart: 3` and `Last added: Tea` — the
identity of each interaction instance is preserved, and the mutation persists
across route changes.

## How the repetition is generated

No new machinery: a `value-button` view term (a button carrying an event token
and a static value) is called inside the existing `each-product` iteration.

```glon
value-button: func [content event value] [
    emit-text event-open emit-text event emit-text value-mid emit-text value emit-text btn-mid
    do content emit-text btn-close
]

products: func [items] [ emit-text ul-open each-product items emit-text ul-close ]
emit-product-at: func [items i n] [
    either < i n [
        emit-text li-open
        emit-text block-at items i
        emit-byte 32
        value-button [ emit-text "Add" ] "add-product" block-at items i
        emit-text li-close
        emit-product-at items + i 1 n
    ] [ none ]
]
```

The repetition is a plain recursive GLON function over the `catalog` block; the
product identity is ordinary event data (a byte-list), not an object, closure,
or component instance.

## Host boundary (unchanged in spirit)

The G1D bridge was sufficient: **no new host API**. The only change is a generic
convention on the JavaScript side — an event button may carry its value in a
static `data-glon-value` attribute (in addition to the G1D `data-glon-input`
read). JavaScript still knows only `route(token)`, `event(token)`,
`event(token, value)`; it never knows what `add-product` or `Tea` mean.

## The stronger conclusion

> Glon can describe dynamic interactive application structure, not merely
> individual interactive controls.

This is the framework-level pressure test: the dialect scales from one control
to a dynamically generated family of controls without growing any
infrastructure.

## Preserved properties

- Route entry vs rerender remain separate (`go-products` vs `render-current`).
- `G1_OUT` isolation unchanged; no raw address reintroduced.
- No component AST, virtual DOM, parser, widget instances, lifecycle objects,
  reactors, or dependency tracking — views remain ordinary executable Glon
  blocks lowered through the existing byte-emission renderer.
- The Substrate Principle holds (see `DESIGN.md`); HTML/CSS/DOM/RAW/HOST remain
  the trap door.

## Verification

- **Native:** `make test` → 357 ok, 0 fail (326 existing + 10 G1A + 4 G1B + 5
  G1C + 6 G1D + 6 G1E); `./check-frozen-s1.sh` PASS.
- **Headless WASM:** `make wasm-g1a-test` asserts the repeated product list and
  `add-product` with Tea/Rice/Tea (requires `emcc` + `node`).
- **Manual:** `make wasm-g1a` then `python3 -m http.server 8000` →
  `http://localhost:8000/demo/shop/app.html`.

## Known limitations

- The product value is a byte-list (the product name); a numeric id would need
  the same representation (no numeric parsing yet).
- Unknown events still fall back to `render-current` (see `GLON-SHOP-G1C.md`).
- The fixed output buffer (`G1_OUT`) remains temporary scratch storage.
