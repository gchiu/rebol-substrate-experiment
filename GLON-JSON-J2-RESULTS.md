# GLON-JSON-J2 — Bounded-stack JSON decoding

Date: 2026-11-27
Branch: `experiment/glon-json-j2` (from `experiment/glon-json-j1`, `daa444a`; checkpoint `ac4d805`)
Host: Debian 13 VPS, 967 MiB RAM. Builds and tests run one at a time.
Server profile: `MEM_PROFILE=large MEM_LOADER_CELLS=500000 MEM_CODE_CAP=20480
MEM_GLOBAL_CONTEXT_CAP=512 CODE_RESERVE=128 M1_TASKS=3 M1_TASK_CELLS=3400
M1_DS_OFF=400 M1_RS_OFF=3400 M1_WRAPPER_DELTA=256`. **Task stacks unchanged.**

**Result:** `json/decode`'s return-stack use no longer grows linearly with
escapes, elements or nesting depth. All three now grow logarithmically, a few
dozen cells per doubling. The interface, results, error ids, error positions
and duplicate-key reporting are unchanged, checked against a frozen copy of the
original decoder.

Two demonstrated decoder fail-stops of the HTTPS server are corrected:

- a body with many escapes, for example 125 escapes in 386 bytes;
- a **17-byte** valid body nested 8 deep, `[[[[[[[[1]]]]]]]]`.

All nine G3H routes work in task mode.

## 1. Original behaviour (measured)

Measurements are of the main return stack, using the min-RP register
`RV_RPMIN`, in `tests/json-j2/depth.glon` and the nesting probe.

| input | depth (cells) |
|---|---|
| 1 / 4 / 16 / 64 escapes in one string | 620 / 956 / 2,300 / 7,676; 1,024 exhausts even 8,192 |
| array of 1 / 8 / 32 items | 887 / 1,671 / 4,359 (≈ 112 per item) |
| object of 1 / 8 / 32 pairs | 871 / 1,767 / 4,839 (≈ 128 per pair) |
| 1 / 4 / 8 / 16 nested arrays | 887 / 1,607 / 2,567 / 4,487 (≈ 240 per level) |

The decoder recursed in three places:

1. **`j/parse-string-loop`** called itself once per backslash. This is the
   unbounded case the brief targets.
2. **`j/array-rest` / `j/object-rest` / `j/pair-has-key?`** recursed once per
   element. That is bounded by `json/max-items` (32), but 32 elements cost more
   than a task's whole ~3,000-cell stack.
3. **`j/parse-value` → `array-rest`/`object-rest`** recursed once per nesting
   level, at two closure activations per level. That is structural and bounded
   by `json/max-depth` (16), but a task fits only about 6–7 levels.

**Demonstrated over HTTPS** (G3H, `--http-tasks`, 3 × 3,400), before J2:
- `POST /api/messages` with nested-array bodies: depth 1–6 gave 400; **depth 8
  (17 bytes) gave no response; at depth 12 the server halted** (`run-tasks:
  machine halt`).
- `POST /api/users` with a 386-byte body containing 125 escapes: the server
  halted on that first request.

## 2. Implementation

The fix uses existing Glon only: no language change, no native primitive, and
no capacity change. `j/parse-escape`, `j/parse-unicode`, `j/hex4`, `j/utf8`,
`j/parse-number` and `j/literal` are unchanged.

- **The logarithmic `do` driver.** A loop body is run by a *run* block that,
  at level k, enters level k−1 twice, so it executes the body up to 2^k times
  at nesting depth k. A *drive* block runs levels 0, 1, 2, … until a done flag
  is set. A loop of n iterations therefore nests about 2·log2(n) `do` levels.
  At ~9–24 cells per `do` level against ~95 per closure activation, that is a
  few dozen cells per doubling. J1's per-element `do`-walk would still be
  linear, so it isn't enough for unbounded escapes.
- **The string scan** (`j/parse-string-loop`). The body is the old loop body
  verbatim. Output is gathered in a ≤512-byte chunk, so long escape-heavy
  strings aren't re-copied in full for every escape.
- **Containers: a state machine with an explicit container stack**, a cons list
  in the managed heap. Phases 0–4 are the five branches of the old
  `parse-value` / `array-rest` / `object-rest`, with the same checks in the same
  order and the same error ids and positions. Opening a container pushes the
  current container; closing pops it and delivers the value to the parent. The
  whole machine runs under the logarithmic driver.
- **Object completion** (`j/obj-build`) rebuilds the pairs in source order from
  last to first. It checks each key against the pairs after it, which is the
  old unwinding order, so the same pair raises `'duplicate-key` with the same
  key and parse errors still take precedence. `j/pair-has-key?` and
  `j/reverse` use the same driver.
- **Global-context economy.** A first version added about 60 globals for loop
  state and failed with `context_full` under the default cap of 256. Loop state
  is now kept in locals of each call. The loops' blocks are locals as well,
  except the state machine's 14 blocks, which live in one global table, `j/sm`,
  indexed by phase.

  Measured bindings added by loading `json.glon`:

  | version | bindings |
  |---|---|
  | original | 74 |
  | J1 | 72 |
  | **J2** | **73** |

## 3. Measured stack usage (after; default large build)

| input | before | after |
|---|---|---|
| 1 / 4 / 16 / 64 / 1,024 escapes | 620 / 956 / 2,300 / 7,676 / exhausted | **700 / 748 / 796 / 844 / 940** |
| 512 / 2,048 escapes (J2 suite) | exhausted / exhausted | 908 / 956 |
| 10,240 plain bytes | 505 | 583 |
| array 1 / 8 / 32 items | 887 / 1,671 / 4,359 | **711 / 791 / 839** |
| object 1 / 8 / 32 pairs | 871 / 1,767 / 4,839 | **711 / 845 / 1,007** |
| 1 / 4 / 8 / 16 nested arrays | 887 / 1,607 / 2,567 / 4,487 | **711 / 759 / 791 / 807** |
| 32 / 64 nested arrays (limit raised for the probe) | — | 839 / 855 |
| G3H POST body `{"text":"hello"}` | 729 | 615 |

## 4. JSON correctness and error-handling tests

**`make json-j2-test`: `JSON_J2_TESTS PASS (96 checks)`** (about 3 minutes).
It also passes on the expanded server build. The reference,
`tests/json-j2/reference.glon`, is the **original decoder frozen under `jold2/`
names**.

| group | coverage |
|---|---|
| A (15) | results equal the original: empty, ASCII, `\"` `\\` `\/` `\b\f\n\r\t`, `A`, `é`, upper-case `€`, surrogate pair, `\u0000`, raw UTF-8, mixed, 16 consecutive escapes |
| B (10) | exact decoded bytes, independent of the reference |
| C (18) | malformed input gets the same error id and position as the original: unknown escape, short `\u`, bad hex, lone high and low surrogates, high + text, high + non-low, unterminated, backslash at end, raw control byte, bad UTF-8, trailing data, late bad escape; three exact positions |
| D (6) | 512 consecutive `\"`; 512 `\n`; 256 `é`; 128 surrogate pairs; 512 mixed escapes |
| E (6) | 1,000 / 4,000 / 10,000 plain characters; 4,000 characters with 256 escapes; 10,240 with 1,024 |
| F (5) | depth: 2,048 escapes < 1 escape + 400 cells; 4× the escapes adds < 100; 32-element containers < 40 cells per element; 32 escape-heavy strings cost one string's extra depth |
| G (31) | containers equal the original (empty, 16 elements, whitespace, escaped keys and values, nested mixed); duplicate keys (simple, two duplicates, duplicate then a later parse error, nested); trailing commas, missing colon, non-string key, unterminated; too-deep at the default limit (exact) and at `max-depth` 4 (vs the original); 16-level nesting decodes; `too-many-items` at the default limit (exact) and at `max-items` 8 (vs the original); 32-element order |
| H (5) | encode → decode → encode round trips; decode(encode s) = s for 512 bytes that all need escaping |

- **The suite halts the machine on the original decoder** in section D
  (512 escapes), so it guards the defect.
- `make json-test`: PASS (also on the expanded build, where it used to halt).
  `make json-j1-test`: PASS (73 checks).

## 5. G3H task-mode results (final source, 3 × 3,400)

| test | result |
|---|---|
| **`make json-j2-g3h-test`** (new, `tests/json-j2/g3h_task.py`) | **JSON_J2_G3H PASS (18 checks)** |
| … `POST /api/users`, 386-byte body, 125 escapes (30 `\"`, 30 `\\`, `\n` `\t` `\/` `é`, 5 surrogate pairs) | 201; stored `display_name` decoded exactly; round-trips through `GET /api/users` |
| … `POST /api/messages` with 6 `\"` | 201, decoded correctly |
| … 10 malformed bodies (bad escape, lone surrogates, short `\u`, unterminated, raw control, 17 levels, 33 items, duplicate key, trailing data) | all **400**, server alive, no halt in the log |
| … the same test with the J1 decoder | the server dies on the first request |
| Nested-array bodies, depth 1 / 4 / 8 / 10 / 12 / 14 / 16 / 17 / 16 / 8 | all controlled **400**, server alive (before: depth 8 no response, depth 12 halt) |
| All nine G3H routes (groups, session, users, POST users, users, messages, POST message, messages with a stored message, 404) | **9/9 correct**, server alive |
| `make h3e-test` | H3E ACCEPTANCE PASS |
| `tests/h3c/acceptance.py` | H3C ACCEPTANCE PASS |
| `make g3h-test` (direct mode, global cap 256) | G3H SMOKE PASS |
| Default constrained `make glon` | builds; closures 42/42; `json.glon` loads; `global-ctx-hash-test` PASS |

Not run: H3D (stress, excluded) and WASM targets. `tests/json/tests.glon` does
not fit the constrained build's 14,800-cell loader with **any** version of
`json.glon` (`loader_exhausted`, pre-existing), which is why `make json-test`
uses the large profile.

## 6. Fail-stop safety

**Can malformed JSON or decoder stack exhaustion stop the HTTPS server?** For
the decoder, not any more within the current limits. Every malformed body
tested gives a judgeable SIN! (a 400 in G3H). Decoding depth is now
logarithmic in escapes, elements and nesting, well under a task's stack, so
neither escape-heavy, wide nor deep input (up to `max-depth`) can exhaust it.

**Request isolation is still incomplete outside the decoder.** These
limitations are separate from J2 and were not changed:

1. **`modules/web.glon` HTML escaping recurses per character.**
   `wp/find-html-special` recurses once per character scanned, and
   `wp/escape-html-loop` once per HTML-special character. G3H escapes every
   message text with them, so in task mode **`POST /api/messages` with a text
   of more than a few dozen characters halts the server**, with or without
   escapes (a ~180-character text did). G3H allows 1,024. This is the most
   serious remaining task-mode defect. The J1/J2 technique applies directly:
   a masm scanner, as in `j/raw-special-at`, plus a logarithmic driver.
2. Task-mode request bodies over ~500 bytes still stop the server, because the
   HTTP bridge embeds the body in parsed source (see J1 §6.3).
3. Any machine fail-stop inside a task (return-stack or heap exhaustion) stops
   the whole server: fail-stops are not judgeable and the bridge does not
   isolate them.

## 7. Remaining limitations

- **Throughput**, the same order as the original decoder: about 4 ms per escape
  in this `-O0` interpreter (512 escapes: 2.2 s against 1.9 s originally);
  10,000 escapes take about 100 s. An object of 32 pairs takes about 1.4 s,
  mostly in the O(n²) duplicate-key check (as before). Bounded stack does not
  mean bounded time.
- **Pre-existing asymmetry:** the decoder accepts 16 nesting levels, but
  `json/encode` advances its depth counter by 2 per level and rejects more
  than 8, so a legal deep document cannot always be re-encoded (left unchanged:
  J1 froze encoder semantics).
- The decoder is still not re-entrant. Its state is global, as before.
- `json-j2-test` takes about 3 minutes because of the interpreter's per-escape
  cost.
