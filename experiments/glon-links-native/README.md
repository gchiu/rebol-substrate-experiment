# glon-web: a Glon program prints the hyperlinks of a real web page

> **Status (2026-11-07): experimental milestone (committed), not deployed.** Built from
> the unchanged runtime, S1 and `GLON_LIVE` dispatcher. Iterator v001, DOM
> v001, Pratt v004 and the 65,536-cell memory map are not changed.

```
$ build/glon-web links.glon https://en.wikipedia.org/wiki/Rebol
#bodyContent	Jump to content
/wiki/Main_Page	Main page
/wiki/Wikipedia:Contents	Contents
...
# 449 links, 5946 events
# 880 batches
```

**How the work is divided:**
- **The native host** (`glon-web.c`) fetches the URL with libcurl, parses it with
  libxml2's HTML parser, and keeps the document.
- **Glon** (`links.glon`) walks the document event by event through the iterator
  protocol, decides which elements are links, joins each link's text, and prints
  the results.

The host never decides what a link is. Glon never holds the document: only the
current batch of events and the chain of open elements.

## Run it

```
make            # build/glon-web (needs libxml2 and libcurl; see "Building")
make test       # 15 offline tests (file:// pages)
make measure    # the measurements below
build/glon-web links.glon URL
```

| Option | Effect |
|---|---|
| `--raw` | print each link exactly, as two netstrings `len:href` `len:text` |
| `--check` | `--raw`, then compare every link, byte for byte, with libxml2's own list of `[href, text content]` for each `<a href>`; print PASS or FAIL |
| `--collect` | Glon keeps the links in a block (as `lk/extract` does) and prints them at the end, instead of printing each as it is found |
| `--measure` | report Glon's heap, return stack, loader, bindings, time and host memory |
| `--materialise` | for comparison: copy the whole document into Glon as `[name attrs children]` blocks, then run `lk/extract` over `dom/walk` |

Several PROGRAM files may be given before the URL; later files may redefine
earlier words, as the tests do. The readable output is `href<TAB>text`, with the
text's whitespace runs collapsed and the text cut at 80 bytes. `--raw` is exact.

## Sample output

```
$ build/glon-web links.glon file://$PWD/tests/test-page.html
/	Home
/docs	Docs
/docs/primer	Glon primer
/docs/laws	alpha laws
/examples	Examples
https://github.com/gchiu/rebol-substrate-experiment	GitHub
/search?q=a&b="c"	A query with & and "quotes"
/unicode	Ünïcödé — 漢字 ✓
/empty
/image
mailto:someone@example.com	mail link
# 11 links, 137 events
# 13 batches
```

- `Glon primer` is `<a>Glon <em>primer</em></a>`, and `alpha laws` is
  `<a><strong>alpha <span>laws</span></strong></a>`: nested elements give the
  combined text.
- The page also has an `<a>` without href, links inside a comment, inside
  `<script>` and inside CSS, and none of them is reported.

Real pages, each checked against libxml2's own list (`--check`, 2026-11-07):

| URL | Links | Events | Result |
|---|---|---|---|
| https://www.iana.org/help/example-domains | 31 | 393 | PASS |
| https://www.rebol.com/ | 20 | 375 | PASS |
| https://en.wikipedia.org/wiki/Rebol | 449 | 5,946 | PASS |
| https://example.com/ | 0 | 22 | PASS (the page currently has no links) |

## What runs where

| Native host (`glon-web.c`, C) | Glon (`links.glon` and its libraries) |
|---|---|
| HTTP/HTTPS/`file:` fetch (libcurl: redirects only to http/https, 16 MB cap, timeouts, TLS verified) | asks for the page, and for each next batch |
| HTML parsing (libxml2, `HTML_PARSE_NONET`, recovering) | decodes events from batches (`hw/`, `../glon-dom-host/hostwalk.glon`) |
| a cursor over the parsed tree, with its stack of open elements | the `it/` protocol: `it/peek` `it/next` `it/pos` `it/close`, and `it/end` |
| encodes the next events into one batch; reports, for **every** element, the attributes Glon named (here `href`) | decides which elements are links, and collects their text across nested elements (`lk/event`, `../glon-dom-links/links.glon`, unchanged) |
| frees the document when Glon releases it | builds and prints the results; tidies the text |
| the `--check` reference list (Glon never sees it) | |

### The host interface (new, built on the existing `GLON_LIVE` mechanism)

Glon cannot call the host and wait for an answer. Frozen S1's host operations
are arithmetic, `ALLOC`, `PUTCHAR`, `PRINT` and `DUMP`, and none of them reads
anything. The existing `GLON_LIVE` layer (`r0_s1_g1a_live.c`) provides an
**asynchronous** conversation instead:
1. Glon ends a dispatch by returning a request fragment, `0x01 op 0x00 arg`.
2. The host performs the request.
3. The host answers with a new event, `r0_s1_g1a_live_event_bytes`. The reply's
   bytes become `current-value`, a managed STRING!, and `do-event` runs.

The dispatcher gives back the loader heap its wrapper used, so a reply leaves
nothing on the loader heap. `glon-web` drives this loop natively. The same Glon
code runs in the browser, through `glon-live.wasm`
(`../glon-dom-host/REPORT.md`).

| Direction | Event or request | Data |
|---|---|---|
| host → Glon | `start` | the URL from the command line |
| Glon → host | `web-load` | URL |
| host → Glon | `web-loaded` / `web-error` | a document handle / a message |
| Glon → host | `dom-next` | `"handle max close-depth attrs bytes"`, e.g. `"257 64 -1 href 120"` |
| host → Glon | `dom-events` | one batch of encoded events |
| Glon → host | `dom-release` | handle: the host frees the document |
| host → Glon | `dom-released` | the handle; a program that answers with no request ends the conversation |

**Batch format.** Each event is one record (u16 means two bytes, high byte
first):

| Event | Bytes |
|---|---|
| open (1), empty (3) | kind, depth u16, name length u8, name, then for each requested attribute: value length u16 (65535 = absent), value |
| close (2) | kind, depth u16 |
| text (4) | kind, depth u16, length u16, bytes |
| end (5) | kind, depth u16 (0) |

Glon sets the batch limits:
- **at most `max` events and `bytes` bytes per batch.** The host always sends at
  least one event, and splits long text without cutting a UTF-8 sequence.
- **`close-depth`** carries an `it/close` that the walker could not finish within
  its batch. The host pops to that depth and finishes the element, without
  sending its remaining children.

**Ownership and lifetime.**
- **Handles only.** Glon holds only a document handle, `slot*256 + generation`.
  Node pointers never leave the host.
- **Release is explicit.** The handle is valid from `web-loaded` until Glon sends
  `dom-release`. The host then frees the libxml2 document and bumps the slot's
  generation.
- **Stale handles are refused.** A stale, forged or out-of-range handle is
  answered with `web-error`, never dereferenced (test: `tests/forged-handle.glon`).
- **No leaks.** At exit the host frees any document Glon did not release, and
  reports it as leaked. Every test run reports `leaked 0`.

### How the Glon side works
- **The walker** (`hw/walk`) is an `it/` iterator over the current batch.
  - When the batch is used up it answers **`hw/wait`**, a third sentinel next to
    `it/end`, meaning "ask the host for more". It also drops the batch at once,
    because it is the largest object Glon holds.
  - Its node for an element is a view, `[name attrs parent]`. An element's open
    and close events carry the same view, so `lk/event`'s identity test
    (`= node anchor`) works unchanged.
  - The chain of parent views is the walker's stack. It also gives the
    ancestors, like Chris's `path`.
- **The driver** (`hl/steps`) is `lk/steps`' balanced driver, stopping at
  `hw/wait`. The dispatch then ends with a `dom-next` request, and the next
  `dom-events` resumes the same walker and the same extraction state.
- **Streaming.** `links.glon` replaces `lk/keep` so a link is printed when found,
  not kept. `--collect` restores keeping.

**Changes against the Glon-data DOM (`dom/walk`), for opaque host nodes:**
1. A host view's third slot is the **parent**, not the children: children are
   never in Glon.
2. The attributes are only those Glon named in the request.
3. `it/peek` and `it/next` can answer `hw/wait`. A consumer driven across host
   replies must stop there and resume after the reply. Within a batch,
   `it/peek`, `it/next`, `it/pos` and `it/close` behave exactly as with
   `dom/walk`.

## Measurements (native, `-O1`, the 65,536-cell machine, 2026-11-07, `make measure`)

### Loaded code: bindings, loader cells, code cells

| File | Bindings | Loader cells | Code cells | |
|---|---|---|---|---|
| prelude, strings, strings-ext | 28 | 1,969 | 1,224 | existing libraries |
| `emit.glon` | 4 | 78 | 193 | 4 words copied from `common.glon` |
| `iter.glon` (iterator v001) | 19 | 2,153 | 27 | |
| `dom.glon` (DOM v001, for `it/close`) | 6 | 1,033 | 0 | |
| `list-block.glon`, `links.glon` (glon-dom-links) | 21 | 1,189 | 174 | |
| `hostwalk.glon` | 14 | 1,963 | 0 | the host-backed walker |
| `links.glon` (this program) | 34 | 1,931 | 0 | |
| **session** | **149 / 256** | **11,119 used, 3,681 free** | **code ends at 14,371** (of 16,384) | managed heap 2,992 of 7,232 live before any page |

### Managed heap and other costs, by page size

"Peak" is the highest live heap after any dispatch (forced collection), above
the 2,992-cell baseline. "After" is what remains once the walk has ended.

| Page | Nodes | Events | Glon peak, streaming | Glon peak, collecting | After, collecting | Return stack | Loader growth | Glon time | libxml2 peak |
|---|---|---|---|---|---|---|---|---|---|
| small-page (3 links) | 25 | 36 | +640 | — | — | 1,345 cells | 0 | 1.3 ms/event | 20 KB |
| gen-100 (10 links) | 144 | 228 | +1,728 | +2,960 | +1,136 | 1,413 | 0 | 1.2 ms/event | 37 KB |
| gen-1000 (10 links) | 1,048 | 1,584 | +1,344 | +2,576 | +1,120 | 1,526 | 0 | 1.2 ms/event | 158 KB |
| gen-10000 (10 links) | 10,048 | 15,084 | **+1,424** | +2,624 | +1,056 | 1,526 | 0 | 1.1 ms/event | 1.40 MB |
| Wikipedia "Rebol" (449 links) | — | 5,946 | +1,792 | (does not fit) | — | 2,469 | 0 | 3.6 ms/event | 1.92 MB |

Nodes are elements plus text nodes (for the gen pages, Chromium's count of the
same files; for small-page, derived from its events).

- **Traversal does not grow with document size.** From 1,000 to 10,000 nodes,
  Glon's peak stays near +1,400 cells, its return stack stays at 1,526 cells,
  and the loader heap does not grow at all. The traversal cost is:
  - the current batch (120 bytes, at most 136 cells);
  - one view and name string per open element (64 cells per level of nesting;
    these pages are 22 deep);
  - short-lived decoding garbage.
- **Storing links is the cost that grows.**
  - While collecting, each kept link costs about 120 cells (a pair, a chain
    cell, the href and the text). After the walk, about 100 cells remain per
    link in the result block.
  - Streaming keeps nothing: the Wikipedia page's 449 links print within the
    same +1,792 cells.
  - Collecting fails on `tests/close-page.html` (104 links) after about 50
    links: the managed heap is full (fail-stop).
- **Host memory is separate from Glon's.**
  - libxml2 holds the document: about 140 bytes per node, 1.4 MB for 10,000
    nodes and 1.9 MB for the Wikipedia page.
  - The cursor holds 16 bytes per open element (22 frames: 352 bytes).
  - Each batch is at most 120 bytes.
  - Glon's 7,232-cell heap never holds the document.
- **Time.** Glon takes about 1.1–1.5 ms per event; the Wikipedia page's
  whitespace tidying makes its 3.6 ms. The host's own work is negligible:
  fetch 2–3 ms from `file:` (about 1 s from the network), parse 3 ms for
  10,000 nodes, and encoding 2 ms for 944 batches. Glon is the bottleneck:
  roughly one collection per dispatch, because only about 4,000 heap cells are
  free.

### The fully materialised Glon DOM, for comparison

| Page | Result |
|---|---|
| small-page (10 elements, 15 text nodes, whitespace included) | works: page data costs **918 loader cells and 1,776 managed cells**; links found |
| test-page (97 nodes) | **fails**: loader or managed heap exhausted while loading the generated source |
| gen-100 | **fails** the same way (6,743 bytes of source) |
| gen-1000 | **fails** (45,505 bytes) |
| gen-10000 | **refused**: `<body>` has 1,250 children, more than one Glon block can hold (512 values) |

Copying the document costs about 70 managed and 37 loader cells per node, and
the loader heap is never collected. On the 64K machine the materialised approach
stops at a few dozen nodes. The host-backed walk handled 10,000 nodes (and
Wikipedia) with the same Glon memory as 100.

## Tests (`make test`: 15 tests, all pass)

1. **Exact match with libxml2's own list** of `[href, text content]`, byte for
   byte: the test page (11 links), gen-100, gen-1000 and gen-10000 (10 each),
   and a page with two navs (104).
2. **Nested text:** `Glon primer` and `alpha laws` in the readable output;
   `--collect` gives the same links.
3. **`it/close`** (`tests/skip-nav.glon` skips every `<nav>`):
   - inside a batch: the test page gives 9 links;
   - across batches, done by the host: a 100-link nav is skipped, giving 3 links
     in only 24 events.
4. **Handles:**
   - a forged or stale handle is refused;
   - the document is released, `leaked 0`, after a refusal and after a normal
     walk.
5. **Errors** reach Glon as `web-error`: a missing file, and a refused scheme
   (`ftp:`).

The existing suites still pass:
- DOM v001: 67 + 8 + 7 + 5.
- glon-dom-links: 21.
- iterator v001: 77 + 28 for both variants.

Frozen S1 checks OK, and all 8 Pratt v004 checksums are OK.

## Limitations and findings

- **Not the existing `glon` executable.**
  - `glon.c` treats every argument as a source file and has no host extension
    point, so `glon links.glon URL` needs a CLI change.
  - The smallest one: pass arguments after the program to Glon, and run the
    `GLON_LIVE` conversation loop with a registered host. That would make `glon`
    depend on libcurl and libxml2, so a separate `glon-web` (this experiment), or
    an optional build of `glon`, is better than changing the default executable.
    Not done; the change needs approval.
- **No synchronous host calls**, by architecture. A synchronous `host-call`
  returning a value would need a new frozen-S1 host operation. The only
  synchronous path into the host during a run is output (`PUTCHAR`/`PRINT`), and
  using that to write replies into Glon's memory would be an undocumented side
  channel. It was rejected.

  The asynchronous conversation is the clean mechanism. Its cost:
  - the third sentinel, `hw/wait`;
  - a consumer must be resumable across replies;
  - one dispatch per batch: 944 dispatches for 10,000 nodes.
- **Fragmentation decides the batch size.**
  - A string costs one cell per byte and needs one contiguous block.
  - With views spread through a non-moving heap, a 240-byte batch could not be
    placed beyond about 30 levels of nesting; 120 bytes (the default) reaches
    40–50.
  - Glon chooses the size in its request.
- **Depth is still in Glon**: 64 cells per open element. A host could pair
  open and close events by depth instead, so Glon would keep nothing per level,
  but `lk/event` would then have to compare depths, not identity.
- **Results are bounded by the heap**: about 40–50 kept links. Streaming
  removes the limit for printing.
- **libxml2 is not a browser.**
  - Its HTML parser recovers differently from the HTML5 algorithm (for example
    around implied elements), and scripts do not run, so links added by
    JavaScript are not seen.
  - The `--check` comparison is against libxml2's own tree.
- **Speed**: about 1 ms per event; 23 s for a 10,000-node page.
- **Whitespace tidying recurses once per word**, so displayed text is cut at 80
  bytes. `--raw` is exact.
- **The walker trusts the host's encoding.** A malformed batch (a host bug) can
  raise or fail-stop. The only check is a `close` with nothing open, which
  raises.

## Building

```
make headers   # only if the -dev packages are not installed: unpacks
               # libxml2-dev, libcurl4-openssl-dev and libicu-dev (matching the
               # installed libraries) into build/sysroot with apt-get download
make
```

`glon-web` links the installed `libxml2.so.2` and `libcurl.so.4`.

## Files

| File | Purpose |
|---|---|
| `glon-web.c` | the native host: fetch, parse, handles, cursor, batches, measurements, `--check` |
| `links.glon` | the Glon program |
| `tests/skip-nav.glon`, `tests/forged-handle.glon` | test programs (`it/close`, stale handles) |
| `tests/*.html` | offline test pages |
| `gen_pages.py` | the 100 / 1,000 / 10,000-node pages (a copy of `../glon-dom-host/gen_pages.py`) |
| `test.sh`, `measure.sh`, `Makefile` | |

The program also uses three uncommitted files from sibling experiments:
- `../glon-dom-host/hostwalk.glon` and `../glon-dom-host/emit.glon` (the
  host-backed walker; unreviewed);
- `../glon-dom-links/links.glon` and `list-block.glon` (unreviewed).
