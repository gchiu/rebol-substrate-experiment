# Glon Shop — Product Specification

## Purpose

Glon Shop is a small browser-based shop application designed to demonstrate that a tiny Glon interpreter running on S1/WASM can support a useful interactive web application with very little JavaScript.

The objective is not to build a production e-commerce system.

The objective is to prove the architectural model:

```text
browser
   ↓
persistent Glon/S1 WASM interpreter
   ↓
Glon application logic / web dialect
   ↓
small HOST browser bridge
   ↓
existing browser facilities
```

The browser continues to provide DOM rendering, networking, history, storage, cryptography and other browser services.

Glon provides the readable application model.

JavaScript should be reduced to a small bridge between Glon HOST operations and browser APIs.

---

## Core architectural principle

Do not reproduce the browser inside Glon.

Do not reproduce JavaScript APIs one-for-one unless necessary.

Instead expose a small Glon-oriented vocabulary describing application intent.

For example, rather than:

```javascript
document.querySelector("#cart-count").textContent = String(cart.length);
```

Glon might express:

```text
text "#cart-count" length? cart
```

Likewise:

```text
on ".add-cart" 'click [
    cart/add event/product-id
]
```

The JavaScript bridge performs the corresponding browser operations.

This principle is one instance of the **Substrate Principle** — see `DESIGN.md`.
Any future Glon UI dialect must preserve a defined path to raw HTML, CSS, DOM
and host functionality; concise, composable interface construction must never
become a sealed boundary that hides those facilities irreversibly.

---

## Primary demonstration

The user opens the shop.

Glon/S1 WASM is instantiated once and remains alive while the user navigates through the application.

The user can:

1. view a product catalogue;
2. open a product page;
3. add products to a cart;
4. move between pages without reloading the Glon interpreter;
5. retain cart/application state while navigating;
6. view the cart;
7. submit a simple test order.

Page changes should use partial fetches and DOM replacement rather than full browser reloads wherever possible.

Direct loading of an individual product URL should still produce a usable ordinary web page.

---

## Initial application model

### Product

```text
Product
    id
    name
    description
    price
    stock
    image-url
```

### Cart item

```text
CartItem
    product
    quantity
```

### Order

```text
Order
    id
    created
    items
    total
    status
```

For the first demonstration, the cart should remain in persistent Glon memory rather than being stored remotely.

---

## Runtime capabilities required

The demonstration requires:

* integer and ordinary numeric arithmetic;
* comparisons;
* managed UTF-8 text;
* basic text manipulation;
* managed blocks/collections;
* user-defined structured datatypes;
* closures;
* persistent mutable application state;
* JSON encoding and decoding;
* URL construction and encoding;
* asynchronous fetch;
* event handling;
* DOM query/update operations;
* browser-history operations;
* cooperative task suspension and resumption.

Existing Glon facilities should be reused wherever possible rather than introducing application-specific runtime mechanisms.

---

## UTF-8 / text requirements

Glon requires useful string operations including approximately:

```text
length?
append / concatenate
find
copy / slice
split
join
trim
compare
number -> text
text -> number
UTF-8 encode/decode where required
```

Text and arbitrary binary data should remain conceptually separate.

A future `BINARY!` datatype may be used for raw HTTP/file/binary payloads.

---

## Minimal browser HOST bridge

The browser bridge should initially expose only what the shop needs.

Conceptually:

```text
FETCH
DOM-FIND
DOM-TEXT
DOM-HTML
DOM-ATTR
DOM-ON
HISTORY-PUSH
HISTORY-REPLACE
```

These do not necessarily need to become separate S1 or evaluator operations.

They may be services reached through the existing generic HOST mechanism.

No new S1 primitive should be required.

---

## DOM philosophy

Glon should not implement:

* an HTML parser;
* a DOM;
* a virtual DOM;
* browser layout;
* CSS processing.

The browser already supplies these.

For example:

```text
html: await fetch-text "/product/17/fragment"
dom/html "#main" html
mount-product 17
```

The browser parses and inserts the HTML.

Glon manages application state and behaviour.

---

## Event model

Browser events should invoke or resume Glon closures.

Conceptually:

```text
on "#add-cart" 'click [
    add-to-cart current-product
]
```

Internally:

```text
Glon closure
      ↓
callback/task identity
      ↓
JavaScript addEventListener
      ↓
browser event
      ↓
WASM entry point
      ↓
Glon closure/task
```

The existing cooperative task mechanism should be used where appropriate rather than introducing a second callback execution model.

---

## Fetch / asynchronous model

The desired Glon surface should eventually allow code resembling:

```text
products: await get-json "/api/products"
```

or:

```text
page: await fetch-page url
```

The browser performs the network request.

A suspended Glon task resumes when the HOST operation completes.

The exact syntax is not fixed by this specification.

---

## Navigation model

Initial page load:

```text
HTML shell
    ↓
load Glon/S1 WASM
    ↓
start application
```

Subsequent navigation:

```text
user action
    ↓
Glon navigation handler
    ↓
fetch page fragment/data
    ↓
replace relevant DOM subtree
    ↓
attach page behaviour
    ↓
update browser history
```

The interpreter should not reload during ordinary navigation.

Persistent application state, including the cart, therefore survives page changes naturally.

---

## Server/API model

The first implementation should keep the server side deliberately simple.

Preferred architecture:

```text
Glon/WASM
    ↓
browser fetch
    ↓
small HTTP API
    ↓
database
```

The database is not part of the Glon language architecture.

Possible backing stores include:

* DynamoDB;
* SQLite;
* PostgreSQL;
* another HTTP-accessible datastore.

For an AWS demonstration:

```text
Glon/WASM
    ↓
fetch
    ↓
API Gateway / Lambda
    ↓
DynamoDB
```

Glon should not need DynamoDB-specific semantics.

---

## Possible Glon web dialect

The long-term application surface may resemble:

```text
page "/products" [
    products: await api/products
    show products
]

on ".add-cart" 'click [
    cart/add event/product-id
]

when cart/changed [
    text "#cart-count" cart/count
]
```

This syntax is illustrative, not yet specified.

The goal is to discover the smallest readable web vocabulary rather than prematurely design a large framework.

---

## Explicit non-goals for version 1

Do not build:

* a React-style virtual DOM;
* a complete JavaScript replacement;
* an HTML parser;
* an HTTP implementation;
* TLS;
* a SQL engine;
* a large OOP framework;
* a full flow-based framework;
* a large web framework;
* a browser networking stack;
* production payment processing.

Use browser and host facilities where those facilities already exist.

---

## Development sequence

The current optimisation work should finish first.

Then:

```text
P4 lexical resolution
        ↓
profile/regression test
        ↓
UTF-8/string operations
        ↓
collection convenience operations
        ↓
JSON
        ↓
URL handling
        ↓
minimal HOST fetch
        ↓
minimal DOM bridge
        ↓
event -> Glon closure/task bridge
        ↓
history/navigation
        ↓
Glon Shop
```

Additional runtime facilities should be introduced in response to actual requirements encountered while building the shop.

---

## Success criteria

The demonstration succeeds if:

1. S1 remains frozen.
2. Glon/S1 WASM loads only once during normal navigation.
3. Product pages can be fetched and displayed without full application reload.
4. Browser events invoke ordinary Glon behaviour.
5. The cart is maintained in Glon state across navigation.
6. At least one asynchronous fetch suspends and resumes Glon execution cleanly.
7. Product/order data can be retrieved from and submitted to a small backend API.
8. JavaScript is limited primarily to the browser HOST bridge.
9. The Glon application code is substantially simpler and more readable than the equivalent direct JavaScript plumbing.
10. No shop-specific semantics are added to S1.

The demonstration should answer one question:

**Can a tiny persistent REBOL-like interpreter in WASM provide a simpler application-facing programming model for the browser while delegating browser machinery to the browser itself?**
