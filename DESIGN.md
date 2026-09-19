# DESIGN.md — GLON design principles

This is the permanent design document for the GLON project. Where the milestone
reports (`R0-ARCHITECTURE.md`, `M*-*.md`, `FIB-OPT-*.md`, `GLON-SHOP-G1A.md`)
are *descriptive* — they record what was built and measured — this file is
*normative*: it records the principles that hold across the language, the
runtime, and the intended GUI layer. The frozen substrate (`s1-frozen-v1`) and
the frozen evaluator baseline (`r0-trapdoor-v2`) remain the fixed points
everything here builds above.

---

## The Substrate Principle

> **Glon treats abstraction as a convenience, not as a boundary.**

GLON provides compact, composable, REBOL-like abstractions for the common case.
Words, blocks, contexts, functions, closures, recursion, multiple results and
structured datatypes make ordinary programs short and readable.

Those abstractions must never become a sealed world. However good they are for
the common case, there will always be machinery the language did not anticipate,
and the programmer must retain a **deliberate path to the underlying substrate**
when the abstraction is insufficient.

The principle applies at every level.

**At the runtime level**, GLON provides REBOL semantics *over* host facilities.
The language layer is a convenience stacked above a stable substrate:

```text
GLON / R0
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
new primitive and without leaving the GLON programming model.

**JavaScript is not intended to become the application language.** It is the
browser *host / instruction layer* through which GLON reaches DOM operations,
networking, timers, storage and the other browser APIs. It supplies host and
GUI services; it does not supply the application model. That is why the host
bridge is deliberately small: it forwards events, writes rendered HTML into a
DOM container, and performs requested browser operations, while routing, state
and application logic stay in GLON.

**At the GUI level the same rule applies.** A future VID-like UI dialect should
provide concise, composable interface construction for the common case, but it
must contain a **defined trap door** to the lower levels — HTML, CSS, DOM and
host facilities — so that a program can descend through the abstraction without
abandoning the surrounding GLON programming model.

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

This is an explicit response to a weakness of historical VID. VID gained
exceptional simplicity by *hiding* detail, but its simplified model could become
a boundary when an application needed facilities outside the abstractions VID
had anticipated. There was no sanctioned way down; the programmer was forced to
step entirely outside the model.

GLON follows a different rule:

> **Simplify aggressively, but never make the simplification irreversible.**

A high-level dialect should remove *unnecessary* detail without removing access
to *necessary* detail. The path down must always exist, be documented, and be
part of the design rather than an escape from it.

The same idea already governs the runtime (see `README.md` — "stay in GLON
unless you genuinely need to descend" — and `R0-ARCHITECTURE.md` §14, "The
staircase into the basement"). The Substrate Principle extends that single
rule so it also covers the GUI dialect and the browser-host boundary.

---

## The persistent interpreter / application model

GLON in the browser is a **persistent REBOL machine**, not a per-request
function. The interpreter is instantiated once and kept alive:

```text
page load      → instantiate Glon once
               → glon_init
               → glon_load(application source)

browser event  → glon_route(...)
               → existing Glon machine executes
               → host operation updates the GUI

next event     → same Glon machine
               → same global context
               → persistent application state
```

The application source is **not reloaded per event**, and the global context is
**not reconstructed on each event**. State persists because the cells that make
up the global context remain reachable for the lifetime of the GLON instance:
a mutation performed by one execution (e.g. `visit-count: + visit-count 1`) is
visible to every later execution, exactly as a variable in a long-lived process
would be.

The useful conceptual formulation is:

> **The browser hosts a persistent Rebol machine; JavaScript supplies the GUI
> and host services.**

This is the model `GLON-SHOP-G1A.md` and `docs/glon-shop-product-spec.md`
instantiate: instantiate once, load once, then serve events from the same
machine and the same global context.
