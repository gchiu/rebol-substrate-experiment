# Extracting hyperlinks with the DOM iterator

> **Status (2026-11-07): usability example, not deployed.** It uses DOM
> iterator v001 (`experiments/glon-dom/dom.glon`, commit `369a3f0`) and
> iterator v001 unchanged. Frozen S1, the runtime and Pratt v004 are not
> touched.

The example walks a small web page, held as Glon data, and returns its
hyperlinks in document order:

```
lk/extract page
== [ [ "/docs" "documentation" ] [ "/examples" "Examples" ]
     [ "https://github.com/gchiu/rebol-substrate-experiment" "GitHub" ] ]
```

## Files

| File | What it is |
|---|---|
| `page.glon` | the example page, written by hand in `[ name attrs children ]` form |
| `links.glon` | `lk/extract`: the link extractor (plain Glon) |
| `list-block.glon` | `lb/push`, `lb/to-block`: collect results of unknown number into a block |
| `example.glon` | runs `lk/extract page` and prints the links |
| `links-tests.glon` | 21 regression tests |
| `measure.sh` | resource measurements (`make measure`) |
| `Makefile` | `make run`, `make test`, `make measure` |

## How it works

`dom/walk page` turns the tree into a stream of events, and `lk/event` looks at
one event at a time:

| event | what `lk/event` does |
|---|---|
| `open` of an `<a>` with an href | start a link: remember the element, its href and no text |
| `text`, inside a link | add the text to the link's text |
| `close` of that same `<a>` | keep `[ href text ]` |
| `empty` `<a>` with an href | keep `[ href "" ]` |
| `it/end` | stop |
| anything else | ignore it |

The text of a link is **all** the text inside it, at any depth:
`<a href="/x"><b>Git</b>Hub</a>` gives `[ "/x" "GitHub" ]`.

- **Link end.** The `close` that ends a link is recognised by identity
  (`=` on the element), so elements nested inside the link cannot end it early.
- **Skipped anchors.** An `<a>` without an href is skipped, as a browser's
  `document.links` does.

**Two things Glon makes you do by hand:**

1. **Drive the walk without a loop.** Glon has no `while`, and one recursive call
   per event exhausts the return stack after about 84 events. `lk/steps w n`
   takes n events by splitting n in halves, so its recursion depth is about
   log2 n. `lk/run` doubles n until the walker reports `it/end`.
2. **Build a block of unknown length.** `reduce` makes exactly as many values as
   its source has expressions, and core Glon has no append. Links are kept in a
   chain of pairs, newest first. `lb/to-block` copies the chain into one block,
   in document order. Copying needs one small masm worker, `lb/raw-to-block`,
   because no Glon word fills a block.

**Other notes:**
- **Constants.** The strings compared on every event (`"a"`, `"href"`, `""`) are
  global constants. A string literal makes a new 32-cell string each time it is
  evaluated.
- **Not re-entrant.** The extractor's state is in `lk/` globals, so one
  extraction must finish before another starts.

## Run

```
$ make run
3
/docs  ->  documentation
/examples  ->  Examples
https://github.com/gchiu/rebol-substrate-experiment  ->  GitHub
[ [ "/docs" "documentation" ] [ "/examples" "Examples" ] [ "https://github.com/gchiu/rebol-substrate-experiment" "GitHub" ] ]
```

## Tests

`make test` runs 21 tests on a fresh native build. All pass.

1. **The example page:**
   - exactly three links, each checked in document order;
   - a second run returns a fresh block with the same links.
2. **Nested content:**
   - `<a><b>Git</b>Hub</a>`;
   - text at several depths, with href not the first attribute;
   - an `<a>` inside an `<a>`.
3. **Edge cases:**
   - no links;
   - empty anchors (children `none` and `[]`);
   - anchors without an href;
   - a link as the root, and a text root;
   - element names compared exactly.
4. **Many links:** 40 links in 210 events. A one-call-per-event loop could not
   drive that many events.

## Measurements (64K reference machine, native, 2026-11-07)

**Bindings, loader heap and code space** (`make measure`, section 1)

| File | Bindings | Loader cells | Code cells |
|---|---|---|---|
| `list-block.glon` | 3 | 184 | 174 (one masm worker) |
| `links.glon` | 18 | 1,005 | 0 |
| **consumer total** | **21** | **1,189** | **174** |
| `page.glon` (the example data) | 1 | 795 | 0 |

The session (prelude, strings, strings-ext, iter, dom, consumer, page) uses 97
of 256 bindings and 7,908 loader cells, leaving 6,892 free. Code space ends at
14,135, out of 16,384.

**Managed heap**

| What | Cells |
|---|---|
| the consumer's constants (always live) | 96 |
| the example page's data | 1,488 |
| live **during** an extraction, per link found (a pair plus a chain cell) | 64 |
| the result for the example page (3 pairs and the block) | 128 |
| the result for 40 links | 1,344 |

**Allocation per extraction** (profile counters)

| What | Cells allocated |
|---|---|
| `lk/extract page` (26 events) | 1,558 |
| a bare walk of the same page | 1,334 |
| `lk/extract` on 40 links (210 events) | 11,862 |

The consumer adds about 224 cells per extraction of the example page; the rest
is the walker's events and frames. Almost all of it is short-lived.

**Return stack.** `lk/extract page` completes under 69 probe frames, so it uses
15. On 40 links it uses 19. The driver grows with log2 of the event count, and
nothing grows with nesting depth.

**The most links one extraction can return** is about free heap ÷ 64:

| Session | Links |
|---|---|
| libraries only | 75 |
| with `page.glon` also loaded | 52 |

Above that, the machine fail-stops with the managed heap exhausted.

## Limitations

- **Small pages only.** At 64 cells per link during extraction, about 50–75 links
  fit. The page itself also has to fit: this page (10 elements, 6 texts) costs
  1,488 managed cells and 795 loader cells. A real page of several hundred elements
  would not fit in the 64K machine as Glon data (see below).
- **One masm worker.** `lb/raw-to-block` exists because core Glon cannot build
  a block of data-dependent length. An approved block-building word (for
  example, a checked `collect`) would remove it.
- **Not re-entrant** (state in `lk/` globals). A closure-held state, like the
  walker's own, would fix this at the cost of readability.
- **No whitespace text.** `page.glon` leaves out whitespace-only text between
  tags. A browser DOM has it. It doesn't change this page's links, but whitespace
  inside a link would be part of its text, as in `textContent`.
- **WASM not tried.**

## Using the same `lk/extract` on a real browser DOM (not implemented)

> **Superseded (2026-11-07).** The serialising adapter below was not built.
> Instead, a host-backed walker keeps the document in the host and lets Glon walk
> it in batches, with `lk/event` from this file unchanged:
> - `../glon-dom-host/` (browser, `glon-live.wasm`);
> - `../glon-links-native/` (native `glon-web`: libcurl and libxml2).
>
> That design also answers the question raised below: no synchronous host call
> is needed, because it uses the existing asynchronous `GLON_LIVE`
> request/reply mechanism.

**What exists today.** A browser page runs Glon through the `standalone/glon.c`
WASM interface, used by `experiments/glon-latex/web/latex-host.js` and
`demo/shop/live-host.js`:
- `glon_init`, then `glon_load(source)` to load the libraries;
- `glon_run(source)` to run code, with `glon_result_ptr` / `glon_result_len`
  returning its printed outcome (4,096 bytes at most);
- `host_print` as an import.

JavaScript can give Glon data only as **source text** (or as event strings). No
export takes a JS value or a DOM handle.

### The smallest adapter: serialise the DOM to Glon source

`lk/extract` already accepts any tree in the `[ name attrs children ]` form, so
the adapter is a JavaScript function, about 40 lines, plus the usual loading
code. It does not change Glon, the runtime or the WASM interface.

1. **Walk the browser DOM in JavaScript and write Glon source:**

   | DOM node | Glon source |
   |---|---|
   | Element | `reduce [ "tag" reduce [ "name" "value" ... ] reduce [ children ] ]`, with `tag` = `localName`; attributes from `el.attributes`, or `none` if there are none; children `none` when there are none |
   | Text node | its text as a string |
   | Comment, processing instruction, `<script>`, `<style>`, `<template>` | skipped |

2. **Encode strings safely.** Glon string literals have no escapes, and a `"`
   ends the literal.
   - Text containing `"` must be written as bytes: `mk-string [ 34 ... ]`. That
     is exactly what the parser turns a quoted literal into.
   - Any one block or literal is limited to 512 values or bytes, so longer text
     is split and joined with `s/+`.
   - Text is written as UTF-8 bytes. Glon strings are bytes, so non-ASCII text
     arrives unchanged.
3. **Load and run.**
   - Load the libraries:
     `strings-ext.glon iter.glon dom.glon list-block.glon links.glon`.
   - Run `page: <the generated source>` with `glon_load`, then `glon_run` on a
     short program that prints each link as `href <TAB> text`.
   - JavaScript splits the printed lines. Reading the molded block instead would
     require parsing Glon output in JavaScript.
4. **Check the result.** Compare it with
   `Array.from(document.querySelectorAll('a[href]'), a => [a.getAttribute('href'), a.textContent])`.
   This should be identical if whitespace text is kept.

**What this costs and where it stops.** As data, before the walk, each element
(with its text) costs about 150 managed cells and 80 loader cells: the example
page is 10 elements and 6 texts in 1,488 and 795. The 64K machine therefore
holds pages of perhaps 25–35 elements. That covers the example page, a menu or
a fragment the host selects (for example, only `<nav>`), but not a whole real
page. Two ways past that, both larger changes:

- **A larger memory profile.** This is the separately approved scaling
  experiment in `GLON-ARCHITECTURE-MEMORY.md` §8.
- **A host-backed walker.** It would answer the same messages (0 peek, 1 next,
  2 pos, 6 close) with events produced lazily from the real DOM, so `lk/extract`
  would not change, and only the current path would be in Glon memory. It needs
  something the WASM interface does not have: a synchronous host call that
  **returns a value into Glon** (today `host_call` returns nothing). That is a
  runtime and interface change, needing its own proposal.

The serialising adapter is therefore the right next step: it reuses
`lk/extract` exactly, and it changes nothing below the example. It would be the
first time this example runs in the 32-bit WASM build.
