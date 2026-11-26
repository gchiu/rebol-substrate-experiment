# GLON-JSON-J1 — Bounded-stack JSON encoding

Date: 2026-11-26
Branch: `experiment/glon-json-j1` (from `experiment/glon-http-h3e`, `518bb8c`)
Host: Debian 13 VPS, 967 MiB RAM. Builds and tests run one at a time.
Server profile: `MEM_PROFILE=large MEM_LOADER_CELLS=500000 MEM_CODE_CAP=20480
MEM_GLOBAL_CONTEXT_CAP=512 CODE_RESERVE=128 M1_TASKS=3 M1_TASK_CELLS=3400
M1_DS_OFF=400 M1_RS_OFF=3400 M1_WRAPPER_DELTA=256`. **Task stacks unchanged.**

**Result: all nine G3H routes work in task mode with 3 tasks × 3,400 cells.**
String encoding no longer recurses: its stack depth is constant for any length
or content. The `json/encode` API is unchanged and its output is byte-identical
to the old encoder's. Only the encoder changed.

## 1. Original behaviour, measured

I measured return-stack depth in the main world with the evaluator's min-RP
register (`RV_RPMIN`, updated on every closure activation;
`tests/json-j1/depth.glon`).

| input | depth (cells) |
|---|---|
| plain string, 10 / 1,280 / 10,240 bytes | 500 / 500 / 500 |
| 4 / 16 quotes | 943 / 2,287 (**≈ 112 per escaped byte**) |
| 64 quotes | **exhausts the 4,096-cell main stack** |
| object with 1 / 4 / 8 pairs | 868 / 1,252 / 1,764 (**≈ 128 per pair**) |
| G3H message object (11 pairs, no escapes) | 2,148 |
| `{"messages":[1 message]}` | **2,868** |
| `{"messages":[2 messages]}` | 2,996 (≈ 128 per array item) |

The H3E report said the encoder recursed "once per character". That was wrong.
`j/encode-string-loop` jumps between escapable bytes with the masm scanner
`j/raw-special-at`, so it recursed **once per escaped byte** (`"`, `\`, bytes
below 32), at one closure activation (~112 cells) each.

The G3H failure, however, came from **containers**. `j/encode-pairs` and
`j/encode-items` recursed once per pair or item, at ~128 cells each. A task's
return stack is about 3,000 cells, and the request path above `json/encode`
uses several hundred, so a single stored message was enough to exhaust it.
Within `json/max-items` (32) the array alone could cost about 4,000 more.
Reaching the acceptance criterion therefore needed bounded per-element cost in
containers as well as in strings.

## 2. New implementation

Only existing facilities are used: no language change, no new C primitive, no
loop construct.

- **`j/raw-escape`** is a masm worker (`( string! -> string! )`). Pass 1 sizes
  the output; pass 2 allocates it with `CALL alloc` and fills it. The input
  stays on the data stack as a GC root across the allocation, and the result is
  rooted before filling, exactly as in `strings.glon`'s `s/raw-cat`. Only the
  safe scratch cells A/B/C/F are used. Escapes are the same as before: `"` →
  `\"`, `\` → `\\`, byte < 32 → `\u00xx` (lower-case hex), everything else
  copied, including UTF-8. `j/encode-string` keeps the `j/raw-utf8?`
  validation and wraps the result in quotes. **This is the existing
  library-level pattern**: `strings.glon` already states that its masm workers
  "loop in masm, so long strings cost no recursion depth".
- **`j/encode-items` / `j/encode-pairs`** iterate by re-entering a `do` block
  over the activation's own locals (`j/ei-*`, `j/ep-*`; no such globals
  exist). Measured cost: ~9 cells per `do` level against ~95 per closure
  activation. Each nested container still has its own activation, so the walk
  is re-entrant. Checks, error ids and arguments, the 0-based count passed to
  `too-many-items`, depth arithmetic, and evaluation order (an element is
  encoded before the next list cell is examined) are all unchanged.
- One closure activation per **nesting level** remains, bounded by
  `json/max-depth`. That is inherent to a recursive value structure.
- `j/hex-char`, `j/hex2` and `j/encode-string-loop` were removed (encoder-only,
  no other users). `j/backslash` is kept but no longer used internally.

## 3. Measured stack usage (after)

| input | before | after |
|---|---|---|
| any string, 10 – 10,240 bytes | 500 | **391** |
| 16 / 64 / 1,024 quotes | 2,287 / exhausted / exhausted | **391 / 391 / 391** |
| 10,240 mixed escapes (25,602 output bytes) | exhausted | **391** |
| object 1 / 8 / 32 pairs | 868 / 1,764 / est. ~4,800 (not measurable: exceeds the stack) | 775 / 983 / **1,703** |
| array 1 / 32 items | — | 759 / **1,367** |
| G3H message object | 2,148 | **1,063** |
| `{"messages":[1 message]}` | 2,868 | **1,815** |
| `{"messages":[2 messages]}` | 2,996 | **1,831** (+16 per item) |

The regression suite asserts these properties: depth for 10,000 plain, quote or
control bytes equals depth for `""`; a 32-element container costs less than 40
cells per element over a 1-element one; and 32 strings of 1,000 quotes cost the
same as 32 short strings.

## 4. JSON correctness tests

**`make json-j1-test`: `JSON_J1_TESTS PASS (73 checks)`**
(`tests/json-j1/tests.glon`). The reference `tests/json-j1/reference.glon` is
the **original encoder, frozen under `jold/` names** before any change.

| group | coverage |
|---|---|
| A (13) | byte-for-byte agreement with the original: empty, ASCII, `"`, `\`, mixed, all 32 control bytes, DEL, UTF-8 2/3/4-byte, `/` not escaped |
| B (6) | exact expected escapes, independent of the reference |
| C (6) | malformed UTF-8 still raises `'json 'bad-utf8` (continuation byte, truncation, 0xFF, overlong, encoded surrogate, bad key) |
| D (11) | 1,000 / 4,000 / 10,000-byte plain, quote and control strings (outputs up to 60,002 bytes); mixed long string equals the repeated unit escape |
| E (6) | stack depth independent of length and content; per-element container cost bounded |
| F (13) | containers equal the original: empty, strings, escaped keys, 16 elements (32 composed from the original's halves), nested mixed document, G3H message and 3-message response, 8-level nesting |
| G (13) | errors identical to the original (id and argument identity): too-many-items (default limit and lowered limit), malformed list cell, non-string key, malformed pair, `none` element, late bad element, too deep, bad UTF-8 |
| H (5) | encode → decode → encode round trips, including the G3H response and 10,000 bytes |

- The suite **halts on the original `json.glon`** (stack exhaustion at the
  long-escape cases), so it guards the defect.
- It passes on both the default large build and the expanded server build. The
  latter's 4,096-cell main stack is too small for the *original* encoder on 32
  elements, which is why those comparisons are composed.
- **`make json-test` (existing suite): `JSON_TESTS PASS`**, unchanged.

## 5. G3H and system results (final source)

| test | result |
|---|---|
| G3H task mode, 3 × 3,400: groups, session, users, POST users, users, messages (empty), POST message, **messages with a stored message**, 404 | **9/9 correct**, server alive |
| G3H task mode, store full: 3 messages containing `"`, `\` and HTML, then `GET /api/messages` | 200, 887 bytes, valid JSON (checked with Python's `json`), escapes correct |
| `make g3h-test` (G3H smoke, direct mode) | G3H SMOKE PASS |
| `make h3e-test` | H3E ACCEPTANCE PASS |
| `tests/h3c/acceptance.py` (task acceptance) | H3C ACCEPTANCE PASS |
| Default constrained `make glon` | builds; closures 42/42; `global-ctx-hash-test` PASS |

Not run: H3D (stress, excluded) and WASM targets.

## 6. Remaining limitations

1. **The decoder still recurses once per escape**, in `j/parse-string-loop`.
   Measured: 604 / 940 / 2,284 cells for 1 / 4 / 16 escapes, and the 4,096-cell
   main stack is exhausted before 32. In a 3,000-cell task, a request body with
   roughly 20 escaped characters would hit a machine fail-stop and stop the
   server. This is outside J1's encoder scope and is the obvious next fix; the
   same masm technique applies, but `\uXXXX` and surrogate validation make it
   larger.
2. **The existing `tests/json/tests.glon` halts on the expanded server build**
   (main stack 4,096 cells): IP 9918, the closure return-stack guard. It halts
   identically with the original `json.glon`, so this is pre-existing (decoder
   depth on the deep test documents). It passes on the build `make json-test`
   uses.
3. **Task-mode request bodies over ~500 bytes stop the server.** This is an
   HTTP-bridge defect, not JSON. The host embeds the body in the spawn source
   as `mk-string [ … ]`, which exceeds the parser's 512-element block limit
   (`bridge spawn: parse error (too_large)`), and spawn failure is fatal. H3C
   already runs with `--http-max-body 500`.
4. Nesting depth still costs one closure activation per level (bounded by
   `json/max-depth` 16), and container elements still cost ~16–30 cells each
   (bounded by `json/max-items` 32).
5. Output is built by repeated `s/+`, so total copying is quadratic in the
   number of elements (as before). This is a heap and time cost, not a stack
   cost.
6. `j/backslash` is no longer used internally.

## 7. Assessment: separate encode and decode modules (not implemented)

I resolved the definition dependencies of `modules/json.glon` mechanically
(72 top-level definitions):

| part | definitions |
|---|---|
| shared by encode **and** decode | 13: `j/block? j/string? j/raw-utf8? j/error j/valid-number?` and its number grammar (`j/digit? j/digit19? j/digits j/exp? j/num-exp j/num-tail`), `json/max-depth`, `json/max-items` |
| encoder only | 9: `json/encode`, `j/encode*`, `j/raw-escape`, `j/int?`, `j/quote` |
| decoder only | ~32: `json/decode` and its parser |
| public API used by neither path | 18: constructors (`json/object` …), predicates (`json/object?` …), `json/object-get`, `json/array-values`, `json/number-text`, `j/tagged?` |

**Feasible without duplication:** `json-core.glon` (the shared 13 plus the API
18), `json-encode.glon` (9) and `json-decode.glon` (~32). No definition would
appear twice.

**Load order is not fragile between the parts.** Glon resolves words at *call*
time, and none of these files runs core code at load time: their top-level
code only defines names, and the masm blocks reference runtime symbols, not
Glon words. Encode and decode can therefore load in either order, and even
before core, as long as everything is loaded before the first call. The real
hazard is a **missing** part. An unbound word is a machine fail-stop, not a
judgeable SIN!, so an application that loads only `json-encode.glon` would halt
on its first call. A guard would need a non-halting "is this word bound?"
test. I have not verified that `get` provides one, so that question comes
first.

**Compatibility:** Glon has no source-include primitive, so existing apps that
load `modules/json.glon` would keep working only if `json.glon` stays a single
file. The duplication-free way to do that is to **generate** `json.glon` at
build time by concatenating the three parts. The Makefile already copies
`modules/` files into `glon-lib/`.

**Benefit:** an encode-only server would save about 32 global bindings and the
decoder's masm code. Code space and the global context are the scarce
resources in the task build. A decode-only consumer would save the 9 encoder
definitions.

**Recommendation:** worthwhile, but it should be its own small milestone
(generated `json.glon`, plus a test that each part loads alone and with core).
Fix the decoder's per-escape recursion (§6.1) first, since it can stop a
task-mode server today.
