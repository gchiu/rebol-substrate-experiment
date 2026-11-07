# Host-backed DOM traversal: Glon walks a DOM it does not hold

> **Status (2026-11-07): experimental milestone (committed), not deployed.** Built from
> the unchanged runtime, S1 and `GLON_LIVE` layer. Iterator v001, DOM v001 and
> Pratt v004 are not changed. The native command-line version, which shares the
> Glon code, is `../glon-links-native/` (README there: fuller measurements).

## 1. Feasibility of the host interface

| Capability | Exists? | Evidence |
|---|---|---|
| Synchronous Glon → host call **returning a value** | **No** | Frozen S1's host operations are arithmetic, `ALLOC`, `PUTCHAR`, `PRINT` and `DUMP` (`s1.h`); none of them reads. `host_call` (`standalone/glon.c`, `GLON_LIVE` build only) returns `void`, and is made **after** the dispatch has finished (`r0_s1_g1a_live.c` `live_finish`). |
| Host → Glon data | Yes, between runs | `glon_load` / `glon_run` (source text; parsed into the loader heap, which is **never collected**) and `glon_event_bytes` (`GLON_LIVE`: the bytes become a managed STRING!, and the dispatcher **gives back** the loader heap its wrapper used). |
| Asynchronous request and reply | Yes (`GLON_LIVE`) | Glon returns a request fragment `0x01 op 0x00 arg`; the host performs it and replies later with `glon_event_bytes`. |
| JavaScript running during a Glon run | Only as output | `putchar` and `printf` call `host_print` synchronously. Using that to write replies into Glon's memory would be an undocumented side channel (and `M` is not exported). **Rejected.** |

**Conclusion:** host-backed traversal is possible without any runtime change, by
`GLON_LIVE`'s asynchronous conversation. Glon asks for the next **batch** of
events. The walker keeps its state between dispatches; a consumer stops when
the batch is used up and resumes when the reply arrives. A truly synchronous
`host-call` would need a new frozen-S1 host operation, which is out of bounds.

## 2. The adapter

**Ownership.**
- The browser (or libxml2, natively) owns the document and the traversal
  cursor: a stack of open elements on the host side.
- Glon holds:
  - a document handle (an integer);
  - the current batch (at most 120 bytes);
  - one view `[name attrs parent]` per currently open element, so that an
    element's open and close carry the same Glon value.
- Node pointers never reach Glon.

**Requests and replies.** These are the same in both hosts (see the native
README for the batch format):
- `web-load` url → `web-loaded` handle, or `web-error` message;
- `dom-next` "handle max close-depth attrs bytes" → `dom-events` batch;
- natively also `dom-release` handle.

Lifetime differs between the two hosts:
- **Browser:** `host.js` releases a document when it sends its end, or when a
  new page is loaded.
- **Native:** `glon-web` requires an explicit `dom-release` and refuses stale
  handles.

**The native model is the proposed one.** `app.glon` does not yet send
`dom-release`.

**Iterator semantics.**
- `it/peek`, `it/next`, `it/pos` and `it/close` keep their meaning within a
  batch.
- When a batch is used up, `it/peek` and `it/next` answer `hw/wait`, distinct
  from `it/end`.
- `it/close` skips within the batch, or sends the close depth to the host when
  the batch ends first.

**Extraction stays in Glon.** The host reports, for every element, the
attributes Glon named. It never looks for links. `lk/event`
(`../glon-dom-links/links.glon`) is loaded unchanged and decides.

## 3. Real browser demonstration (Chromium 153, headless Playwright, same origin)

`make wasm && make browser`: `run_browser.py` serves the repository root and
opens `index.html`. For each page, it compares Glon's links with a JavaScript
reference computed separately; Glon never sees the reference.

| Page | DOM nodes | Links | Events | Batches | Result | Glon time | Host encoding |
|---|---|---|---|---|---|---|---|
| `test-page.html` (nested markup, entities, Unicode, empty links, links in comments, script and CSS) | 97 | 11 | 137 | 13 | **matches** | 1.6 ms/event | 0.2 ms/batch |
| `gen-100.html` | 144 | 10 | 228 | 15 | **matches** | 1.7 ms/event | 0.1 ms/batch |
| `gen-1000.html` | 1,048 | 10 | 1,584 | 100 | **matches** | 1.5 ms/event | 0.1 ms/batch |
| `gen-10000.html` (fresh session) | 10,048 | 10 | 15,084 | 944 | **matches** | — | — |
| `no-such-page.html` | | | | | `web-error` "HTTP 404" reaches Glon | | |
| `https://example.com/` | | | | | blocked by CORS: `web-error` "Failed to fetch" | | |

`build/demo.png` is a screenshot of `index.html` after loading `test-page.html`.
After every walk, the host holds 0 documents. Chromium's `usedJSHeapSize`
stayed at 10.0 MB across these walks; the page DOM itself is the browser's.

**Open problem (WASM only).** `gen-10000.html` fails, with the managed heap
exhausted, when it runs **after** another page in the same browser session. It
passes in a fresh session, and twice in a row. Natively, the same Glon code
passes repeated 10,000-node walks in one machine (`REPEAT=3 build/hostsim -q
1250 10 20 64`), and live cells after each run stay flat (no leak). The likely
cause is fragmentation left by the previous page's retained results, but this is
not yet explained or fixed.

## 4. Measurements

The WASM build does not expose Glon's memory, so Glon-side figures come from
`hostsim`: the unchanged runtime and `GLON_LIVE` layer, with a C stand-in host
that uses the same protocol over a synthetic DOM (`make test`).

| Document | Events | Glon peak live (base 2,368) | Return stack | Loader growth | Glon time |
|---|---|---|---|---|---|
| 141 nodes | 223 | 5,184 | 1,388 cells | 0 | 1.4 ms/event |
| 1,045 nodes | 1,579 | 5,232 | 1,388 cells | 0 | 1.2 ms/event |
| 10,045 nodes | 15,079 | 5,056 | 1,388 cells | 0 | 1.3 ms/event |

**Glon's memory is bounded independently of document size.** Apart from the
results and the depth, it is at most 64 cells per open element. Its loader heap
does not grow.

Host side (JavaScript):
- the DOM (the browser's);
- one `{node, next}` frame per open element (22 at most here);
- the records pending for the next batch.

Full figures, the libxml2 equivalent, and the comparison with the materialised
Glon DOM are in `../glon-links-native/README.md`. In short: materialising fails
beyond a few dozen nodes, and the host-backed walk handles 10,000.

**Findings that shaped the design:**
- **Batch size.** A string costs one cell per byte and must be one contiguous
  block. Batches of about 8,000 bytes exhausted the heap at once. 240 bytes
  failed beyond about 30 levels of nesting, because the non-moving heap is
  fragmented by long-lived views. **120 bytes** reaches 40–50 levels. Glon sets
  the size in its request.
- **A smaller baseline.** `common.glon` (needed only for four `emit-*` words)
  left 5,344 of 7,232 cells live before any page. `emit.glon` copies those four
  words verbatim, and the baseline falls to 2,368.
- **Views carry their parent.** This makes the chain of views the walker's
  stack, at no extra cost: 64 cells per open level instead of 96.

## 5. Arbitrary external URLs

- **In the browser:** a page cannot read another origin's HTML without that
  origin's CORS consent; `fetch` fails, as the `example.com` run shows. The
  legitimate options are:
  - a same-origin proxy that the operator controls and declares, which fetches
    server-side;
  - a browser extension with host permissions for the sites it reads;
  - the native host (`../glon-links-native/`, libcurl), which is not bound by
    CORS.

  Nothing here bypasses browser security.
- **Untrusted pages:** both hosts parse without running scripts (`DOMParser`;
  libxml2 with `HTML_PARSE_NONET`). Links created by JavaScript are therefore
  not seen. Reading those would need a real rendering browser under automation.

## Files

| File | Purpose |
|---|---|
| `hostwalk.glon` | `hw/`: the host-backed walker (`it/` protocol, batches, `hw/wait`, `hw/request`, `hw/feed`) |
| `emit.glon` | four `emit-*` words copied from `common.glon` |
| `app.glon` | the browser application: the conversation, the driver, results as netstrings, a fixed-text summary |
| `host.js`, `index.html` | the browser host and page |
| `test-page.html`, `gen_pages.py` | test pages |
| `run_browser.py` | the Chromium run and checks |
| `hostsim.c` | the native measurement harness and stand-in host |
| `Makefile` | |
