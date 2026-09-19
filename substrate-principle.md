# The Substrate Principle

> **Glon treats abstraction as a convenience, not as a boundary.**

Glon is a tiny, embeddable REBOL-family runtime. It provides a compact,
composable, REBOL-like language — words, blocks, contexts, functions, lexical
closures, recursion, multiple results, structured datatypes — that makes
ordinary programs short and readable.

The Substrate Principle is the rule that keeps those abstractions honest:

> Glon provides REBOL-like abstractions for the common case, but those
> abstractions must never become a sealed world. A programmer must retain a
> deliberate, documented path to the underlying substrate whenever the
> abstraction is insufficient.

However good an abstraction is for the common case, there is always machinery
the language did not anticipate. When that happens, the programmer must be able
to *descend* — without adding a new language feature, and without abandoning the
surrounding programming model.

The principle applies at every level of the system.

---

## At the runtime level

Glon provides REBOL semantics *over* host facilities. The language layer is a
convenience stacked above a small, stable substrate:

```text
Glon / R0
    |
   RAW
    |
   S1
    |
 host / WASM / native
```

`RAW` is the generic low-level trapdoor into the S1 machine; `HOST` is the
generic escape to the outside world. A programmer who needs machinery the
evaluator does not provide descends through `RAW`/`S1`/`HOST` without adding a
new primitive and without leaving the Glon programming model. The runtime
shorthand is: *stay in Glon unless you genuinely need to descend.*

---

## JavaScript is not the application language

At the browser boundary, **JavaScript is the host / instruction layer**, not the
application language. It is the layer through which Glon reaches DOM operations,
networking, timers, storage and the other browser APIs.

JavaScript supplies host and GUI services; it does not supply the application
model. The host bridge is deliberately small: it forwards events, writes
rendered HTML into a DOM container, and performs requested browser operations,
while routing, state and application logic stay in Glon.

---

## At the GUI level

The same rule applies to a future VID-like UI dialect. Such a dialect should
provide concise, composable interface construction for the common case — but it
must contain a **defined trap door** to the lower levels: HTML, CSS, DOM and host
facilities.

```text
Glon application
    ↓
high-level Rebol abstractions
    ↓
UI dialect / language facilities
    ↓
defined trap door
    ↓
JavaScript / DOM / CSS / browser APIs
```

A program must be able to descend through the interface abstraction without
abandoning the surrounding Glon programming model.

---

## A response to VID

This is an explicit response to a weakness of historical VID. VID gained
exceptional simplicity by *hiding* detail, but its simplified model could become
a boundary when an application needed facilities outside the abstractions VID
had anticipated. There was no sanctioned way down; the programmer was forced to
step entirely outside the model.

Glon follows a different rule:

> **Simplify aggressively, but never make the simplification irreversible.**

A high-level dialect should remove *unnecessary* detail without removing access
to *necessary* detail. The path down must always exist, be documented, and be
part of the design rather than an escape from it.
