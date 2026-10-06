# Glon WASM ABI

**Status:** current / provisional. This documents what the code in this
repository does today. It is not a frozen specification and there is no ABI
version number or compatibility guarantee (see §11).

This document is derived from `standalone/glon.c` (the host/portability layer),
the G1A layers (`r0_s1_g1a.c`, `r0_s1_g1a_live.c`), the WASM build rules in the
`Makefile`, the browser hosts (`demo/shop/host.js`, `demos/patrol/patrol-host.js`,
`demos/kaka/kaka-host.js`, `standalone/glon.js`), and the WASM regression tests
listed in §12. The exported-symbol lists were read from the actual built `.wasm`
modules (`demo/shop/glon.wasm`, `demo/shop/glon-live.wasm`,
`standalone/glon.wasm`).

---

## 1. Scope

This is the **binary interface between a host program and the Glon WASM
runtime**. It describes how a host instantiates the module, moves bytes in and
out, and reads results.

It is **not**:

- the Glon/R0/S1 language specification (that is `GLON-PRIMER.md`,
  `R0-ARCHITECTURE.md`, `GLON-ALPHA-LAWS.md`);
- the native host capability protocol (`GLON-HOST-CAPABILITIES.md`,
  `GLON-DESKTOP-D6.md`) — that is the control/data plane between two native
  processes, not this WASM boundary;
- the Canvas/View rendering protocol — `host_canvas_script` and `host_set_html`
  are host imports used by the browser hosts to present Glon output, but the
  drawing command vocabulary is a presentation concern, not part of the core
  Glon ABI. It is mentioned only where a host must implement the import;
- a stable, versioned public API.

The ABI is deliberately small. JavaScript hosts in this repository contain no
application state or routing logic; they only move bytes and perform browser
operations (see `GLON-SHOP-G1A.md` §6).

---

## 2. Exported functions

### 2.1 Module variants

The source defines one host layer (`standalone/glon.c`) that is compiled into
three module shapes:

| module | build | host imports | extra exports |
|---|---|---|---|
| **G1A / shop** (canonical browser build) | `demo/shop/glon.wasm` | `host_print`, `host_set_text`, `host_set_html`, `host_canvas_script` | `glon_route`, `glon_event`, `glon_event_value` |
| **GLON_LIVE** (experimental) | `demo/shop/glon-live.wasm` (`-DGLON_LIVE`) | the four above **+ `host_call`** | the three above **+ `glon_event_bytes`** |
| **W2 standalone** | `standalone/glon.wasm` | `host_print`, `host_set_text` | none beyond the common set |

All three share the common exports: `glon_alloc`, `glon_init`, `glon_load`,
`glon_call`, `glon_run`, `glon_result_ptr`, `glon_result_len`, `memory`, plus
Emscripten CRT artifacts (`emscripten_stack_get_current`,
`_emscripten_stack_restore`, `__indirect_function_table`). The two
`emscripten_stack_*` functions are inert no-ops; no host calls them.

> **Build-artifact note.** The committed `standalone/glon.wasm` is an old binary
> from the original W2 commit and predates `glon_run`/`glon_result_*` and the
> `masm` rename. `make wasm-standalone`/`wasm-standalone-test` rebuild it from
> current sources before running. The authoritative ABI is the source
> (`standalone/glon.c`) and the G1A/GLON_LIVE modules, not that stale file.
> See the report accompanying this document.

### 2.2 Raw WebAssembly signatures

On wasm32, `int` and pointers are `i32`; every function returns `i32`. All
pointers are byte offsets into the module's exported linear memory. A pointer is
only meaningful to the instance that produced it.

| export | wasm signature | availability |
|---|---|---|
| `glon_alloc` | `(i32 len) -> i32 ptr` | all |
| `glon_init` | `() -> i32` | all |
| `glon_load` | `(i32 src, i32 len) -> i32` | all |
| `glon_call` | `(i32 name, i32 len) -> i32` | all |
| `glon_route` | `(i32 token, i32 len) -> i32` | G1A, GLON_LIVE |
| `glon_event` | `(i32 token, i32 len) -> i32` | G1A, GLON_LIVE |
| `glon_event_value` | `(i32 token, i32 tlen, i32 value, i32 vlen) -> i32` | G1A, GLON_LIVE |
| `glon_event_bytes` | `(i32 token, i32 tlen, i32 data, i32 dlen) -> i32` | GLON_LIVE only |
| `glon_run` | `(i32 src, i32 len) -> i32` | all |
| `glon_result_ptr` | `() -> i32 ptr` | all |
| `glon_result_len` | `() -> i32` | all |

### 2.3 Host imports (must be supplied or instantiation fails)

| import | signature | meaning |
|---|---|---|
| `env.host_print` | `(i32 ptr, i32 len) -> ()` | runtime plain-text/log output |
| `env.host_set_text` | `(i32 handle, i32 value) -> ()` | write a number into a `[data-glon-id]` element (W2 protocol) |
| `env.host_set_html` | `(i32 handle, i32 ptr, i32 len) -> ()` | write rendered HTML into a `[data-glon-id]` element |
| `env.host_canvas_script` | `(i32 ptr, i32 len) -> ()` | hand the host a generic visual script to draw |
| `env.host_call` | `(i32 op, i32 op_len, i32 arg, i32 arg_len) -> ()` | GLON_LIVE outbound capability request |

The host must satisfy the exact import set the module declares. The G1A build
does not import `host_call`; the GLON_LIVE build does. `host_canvas_script` is
called after every `glon_route`/`glon_event`/`glon_event_value`/
`glon_event_bytes` dispatch, possibly with `len == 0`.

### 2.4 Per-function semantics

The `handle` argument of `host_set_html` is currently hard-coded to `1`
(`standalone/glon.c`, `G1A_WRITE_HTML`). The runtime therefore writes rendered
output to the element with `data-glon-id="1"`. This is an implementation detail
of the current host layer, not a parameter the caller controls.

#### `glon_alloc(len) -> ptr`

- Allocates `len` bytes (plus one reserved byte) from the **transient ABI arena**
  (see §3) and returns an 8-byte-aligned pointer.
- Returns `0` if the arena cannot satisfy the request. `0` is the only failure
  signal.
- **Does not** clear or NUL-fill the buffer. The caller writes the bytes, then
  passes `ptr, len` to an execution entry.
- Allocation is transient: the pointer is only valid until the next execution
  entry (§3, §10).
- Multiple `glon_alloc` calls made before a single execution entry accumulate
  and must all remain live simultaneously (e.g. token + value for
  `glon_event_value`). Align each result independently.

#### `glon_init() -> 0`

- Initialises the runtime once: runs `r0_s1_init()`, seeds the M1 multitasking
  environment cells, and (GLON_LIVE) registers the outbound host-call callback.
- Idempotent: guarded by an internal `inited` flag; repeat calls are no-ops.
- Always returns `0`.
- **Not** an execution entry: it does not reset the transient ABI arena.
- Must be called before `glon_load`/`glon_call`/route/event/run.

#### `glon_load(src, len) -> 0 | -1 | -2 | -3`

- Copies `len` bytes from `src` into a 16 KiB static source buffer, strips `;;`
  line comments, parses with `r0_s1_parse`, and runs the parsed block with the
  **persistent** entry `r0_s1_run_persistent`.
- May be called repeatedly to **extend** an already-initialised machine
  (bootstrap, then libraries, then an app). It does not reset the machine.
- `-1`: `len >= 16384` (source does not fit).
- `-2`: parse error.
- `-3`: `r0_s1_run_persistent` returned a negative result (machine-level
  failure).
- `0`: success.
- **Execution entry.** The `src` pointer is consumed during the call only.

#### `glon_call(name, len) -> 0 | -1 | -2 | -3`

- Builds the source `[ do NAME ]`, parses it, and runs it with the transient
  entry `r0_s1_run` (this is the W2 counter-demo path).
- `-1`: `len == 0` or `len > 60`.
- `-2`: parse error. `-3`: run returned a negative result. `0`: success.
- **Execution entry.** `name` must be a plain word (it is interpolated directly
  into source).

#### `glon_route(token, len) -> 0 | -1 | -2`

- Binds `current-route` to the token word, runs the loaded program's `route`
  block persistently, renders the selected fragment, and emits it via
  `host_set_html(1, ...)`, then `host_canvas_script(...)`.
- `-1`: `len == 0` or `len > 63`.
- `-2`: dispatch failure — which includes a token containing a Glon delimiter
  (`[ ] ' :` space/tab/CR/LF), no fragment produced, or a parse/run error. (The
  host-layer length check returns `-1`; the delimiter check happens inside
  `r0_s1_g1a_route`, whose `-1` is mapped to `-2` by `glon_route`.)
- `0`: success. Rendered HTML is a side effect, not a return value.
- **Execution entry.**

#### `glon_event(token, len) -> 0 | -1 | -2`

- Binds `current-event` **only** (no `current-value`) and runs the program's
  `do-event` block persistently, then renders and emits as above.
- Same length/content limits and codes as `glon_route`.
- **Execution entry.**

#### `glon_event_value(token, tlen, value, vlen) -> 0 | -1 | -2`

- Binds `current-event` to the token and `current-value` to a managed `STRING!`
  built from the `value` bytes (via the loaded program's `mk-string`), then runs
  `do-event` and renders.
- `-1`: `tlen == 0`, `tlen > 63`, or `vlen > 200`.
- `-2`: dispatch failure (including a delimiter in the token). `0`: success.
- `value` is treated as a NUL-terminated byte string by the implementation (it
  is expanded byte-by-byte into source); embedded NUL is not representable on
  this path.
- **Execution entry.**

#### `glon_event_bytes(token, tlen, data, dlen) -> 0 | -1 | -2` (GLON_LIVE)

- Same as `glon_event_value`, but `data` is copied **by length**, so it may
  contain NUL and arbitrary bytes; there is no 200-byte cap.
- `-1`: `tlen == 0` or `tlen > 63`. `-2`: dispatch failure, which includes
  `dlen > 12000` (`LIVE_MAX_BYTES`); `0`: success.
- **Execution entry.**

#### `glon_run(src, len) -> 0 | -1`

- Runs one program **without** the outer brackets (source is wrapped by
  `r0_s1_show_run`), formats the outcome as text into a static 4 KiB result
  buffer, and records its length.
- `-1`: `len >= 16384`.
- `0`: the outcome text is set in **every** case (clean results, `** uncaught
  #[SIN! ...]`, `** halted...`, `** parse error`). There is no separate error
  code; inspect the text via `glon_result_ptr`/`glon_result_len`.
- **Execution entry.** It resets the ABI arena before validating `len`, so even
  a failed `glon_run` invalidates transient ABI pointers.

#### `glon_result_ptr() -> ptr`

- Returns the byte address of the static result buffer written by the last
  `glon_run`. Stable across calls; never `0`.
- The buffer is **not** in the transient ABI arena.

#### `glon_result_len() -> len`

- Returns the byte length recorded by the last `glon_run`. `0` before any
  `glon_run`.
- The length may be capped by the 4 KiB result buffer.

---

## 3. WASM memory model

- The module exports one `memory` (linear memory), built with
  `ALLOW_MEMORY_GROWTH=1` (`STANDALONE_FLAGS` in the `Makefile`). Memory may
  grow during a call. Hosts must therefore **re-create any `Uint8Array`/
  `DataView` view after a call**; a view taken before a growth is detached. The
  browser hosts do exactly this (a fresh `view()` on each access).
- The Glon machine state lives in a static `cell M[65536]` array (in linear
  memory) plus a loader heap inside it. This is **persistent** for the life of
  the instance.
- `malloc`/`free` are a trivial bump allocator over a static `heap[65536]`
  that never reclaims; it backs interned symbol spellings for the life of the
  machine. **Callers cannot call `malloc`**; it is not exported.

### 3.1 The transient ABI arena

- `glon_alloc` allocates from a dedicated static `abi_heap[65536]` bump arena
  (`standalone/glon.c`), **separate** from the persistent `heap`.
- Alignment: 8 bytes. `abi_used` is rounded up to 8; `len + 1` bytes are
  reserved (the `+1` is reserved, not necessarily zeroed).
- Allocation succeeds iff `align8(abi_used) + len + 1 <= 65536`; otherwise
  `glon_alloc` returns `0`. There is no exported way to query remaining space.
- The arena is reset to empty at the start of each execution entry
  (`abi_reset()`). Several allocations may be live at once.

### 3.2 The D11.6 lifetime rule

> **A pointer returned by `glon_alloc` for transient host→WASM ABI storage
> remains valid only until the next Glon execution entry.**

An execution entry resets the arena (its bump pointer). The entry copies the
host buffers it was passed into local storage at its start, before any further
allocation, so the caller's bytes are consumed during that call. After the call
returns, the same address may be handed out again by a later `glon_alloc`, and
its contents may be overwritten.

The functions that are **execution entries** (and therefore invalidate all
outstanding `glon_alloc` pointers) are:

- `glon_load`
- `glon_call`
- `glon_route`
- `glon_event`
- `glon_event_value`
- `glon_event_bytes` (GLON_LIVE)
- `glon_run`

`glon_init`, `glon_alloc`, `glon_result_ptr`, and `glon_result_len` are **not**
execution entries and do not reset the arena.

The reset happens at the start of the entry, before argument validation, so even
a call that ultimately returns an error invalidates outstanding ABI pointers.

Callers must not retain an ABI pointer across any of the listed calls. If bytes
must survive, copy them into host memory before the next execution entry.

### 3.3 Writing into returned memory

Callers **may and should** write directly into the memory returned by
`glon_alloc`, then pass `ptr, len` to an execution entry. No host-side
"commit"/"free" call exists.

---

## 4. Result lifetime

- `glon_result_ptr`/`glon_result_len` expose the static `resultbuf` written by
  `glon_run`. They are set **only** by `glon_run`; no other export writes them.
- The result memory is a static buffer outside the transient ABI arena, so it is
  **not** invalidated by `glon_route`/`glon_event`/`glon_load`/etc.
- It **is** replaced by the next `glon_run` (which sets `resultlen = 0`, then
  rewrites the buffer). A host that needs the text across another `glon_run`
  must copy it.
- The result buffer does not alias the ABI input arena, `htmlbuf`, or `visbuf`.
- `glon_route`/`glon_event`/`glon_event_value`/`glon_event_bytes` do **not**
  populate the result buffer; their output is delivered through host imports
  (`host_set_html`, `host_canvas_script`). Those import buffers (`htmlbuf`,
  `visbuf`) are static and are overwritten by the next dispatch; the host should
  copy during the import callback.

---

## 5. Strings and byte sequences

- **Encoding.** The ABI is byte-oriented: every input and output is a
  `(pointer, length)` pair of bytes. The runtime does **not** validate or
  guarantee UTF-8. The browser hosts decode with `TextDecoder` (UTF-8) for
  display, but that is a host choice, not an ABI guarantee.
- **Termination.** No NUL terminator is required or promised. Hosts must use the
  explicit length. Internally, source/token/value C strings are NUL-terminated
  after copying, which is why embedded NUL is significant on textual paths.
- **Embedded NUL.**
  - `glon_load`/`glon_run` source is parsed as a C string: an embedded NUL ends
    the parsed text.
  - `glon_event_value` treats `value` as a C string: an embedded NUL truncates.
  - `glon_event_bytes` (GLON_LIVE) copies `dlen` bytes: embedded NUL and
    arbitrary bytes are preserved.
- **Tokens.** A token is a single Glon word. It must be non-empty and must not
  contain `[`, `]`, `'`, `:`, space, tab, CR, or LF. Token limits:
  `glon_call` ≤ 60 bytes; `glon_route`/`glon_event`/`glon_event_value`/
  `glon_event_bytes` ≤ 63 bytes. Tokens are bound as lit-words
  (`current-route: 'token`, `current-event: 'token`).
- **Event value (`glon_event_value`).** Bound as a managed `STRING!` built via
  the loaded program's `mk-string`; max 200 bytes (a smaller practical limit
  than `glon_event_bytes`).
- **Event bytes (`glon_event_bytes`, GLON_LIVE).** Raw bytes bound as a managed
  `STRING!`; max 12000 bytes (`LIVE_MAX_BYTES`). The implementation builds the
  string in 400-byte chunks and caps the generated source at 64 KiB.
- **Output.** `host_print`, `host_set_html`, and `host_canvas_script` receive
  `(ptr, len)`; `host_set_text` receives two integers (`handle`, `value`).
  Rendered HTML is `NUL`-terminated when there is room, but hosts read by
  length. Output buffers are silently capped: rendered HTML `htmlbuf` is 16 KiB
  (byte-limited by `g1a_buf`), the canvas `visbuf` is `G1_VIS_CAP` = 2560
  bytes, and the result buffer is 4 KiB.

---

## 6. Initialisation and loading sequence

Minimal correct lifecycle, language-neutral:

```
instantiate(wasm, host_imports)      # all declared imports required
glon_init()                          # once, before anything else
ptr = glon_alloc(len(src)); if ptr == 0 -> allocation failure
write src bytes at ptr
glon_load(ptr, len(src))             # extend machine (may repeat: libs, app)
# then, per interaction:
ptr = glon_alloc(len(token)); write token
glon_route(ptr, len)  or  glon_event(ptr, len)  or
   (alloc token + value, then glon_event_value) / glon_event_bytes
# output arrives through host_set_html / host_canvas_script during the call
# for the primer path:
ptr = glon_alloc(len(primer_src)); write primer_src
glon_run(ptr, len)
text = memory[glon_result_ptr() .. glon_result_ptr()+glon_result_len()]
```

Key points:

- `glon_init` once, before anything else.
- Each `glon_alloc` result is consumed by the very next execution entry. Allocate
  and use within one interaction.
- Multiple `glon_load` calls are valid and cumulative; libraries are loaded
  before the app.

JavaScript example (the pattern used by `standalone/glon.js` and the browser
hosts):

```js
const ex = /* instantiate glon.wasm */;
const enc = new TextEncoder();
const view = () => new Uint8Array(ex.memory.buffer);   // fresh: memory can grow

function put(s) {
  const b = enc.encode(s);
  const p = ex.glon_alloc(b.length);
  if (p === 0) throw new Error("glon_alloc failed");
  view().set(b, p);
  return [p, b.length];
}

ex.glon_init();
let [p, n] = put(source);
if (ex.glon_load(p, n) !== 0) throw new Error("glon_load failed");

[p, n] = put("home");
if (ex.glon_route(p, n) !== 0) throw new Error("glon_route failed");

// primer-style result
[p, n] = put("+ 1 2");
ex.glon_run(p, n);
const text = new TextDecoder().decode(
  view().subarray(ex.glon_result_ptr(), ex.glon_result_ptr() + ex.glon_result_len()));
```

---

## 7. Error handling

| function | return | meaning |
|---|---|---|
| `glon_alloc` | `0` | allocation failure (arena exhausted); no other error |
| `glon_alloc` | `!= 0` | pointer to `len` writable bytes |
| `glon_init` | `0` | always |
| `glon_load` | `0` | success |
| | `-1` | source length ≥ 16384 |
| | `-2` | parse error |
| | `-3` | persistent run returned a negative result |
| `glon_call` | `0` / `-1` (bad name length) / `-2` (parse) / `-3` (run) | |
| `glon_route` / `glon_event` | `0` / `-1` (zero or oversized token length) / `-2` (dispatch, incl. delimiter in token) | |
| `glon_event_value` | `0` / `-1` (bad token length or value > 200) / `-2` (dispatch, incl. delimiter) | |
| `glon_event_bytes` | `0` / `-1` (bad token) / `-2` (dispatch, incl. data > 12000) | |
| `glon_run` | `0` / `-1` (source too long) | outcome text is always set on `0` |

- **Null/zero pointer:** `0` from `glon_alloc` means failure. A zero `ptr`
  argument to an execution entry is not valid input.
- **Dispatch failures:** `-2` covers a malformed token, a missing
  `route`/`do-event` result, or an internal parse/run failure. Parser and
  evaluator detail is not exposed through a numeric code.
- **Success:** `0`.
- **Error text and the result buffer:** only `glon_run` exposes text, through
  `glon_result_ptr`/`glon_result_len`. For `-2` dispatch failures, no error text
  is delivered through the result buffer; route/event output is the only
  channel.
- Severity note (from `r0_s1_show.c`): an uncaught Glon `SIN!` is a language
  error; a context-full halt is a machine-level fail-stop. `glon_run` reports
  both as text.

---

## 8. Re-entrancy / concurrency

- The runtime is a **single, global, mutable machine** (`M[]`, loader heap,
  intern tables, static C buffers). It has no locking.
- **Multiple concurrent calls are not supported.** Calls are expected to be
  serialised by the host. It is **not currently guaranteed** that calls from
  multiple threads (or workers sharing an instance) are safe; a WASM instance
  is not shareable across threads under default settings.
- **Re-entrancy is not currently guaranteed.** A `host_*` import callback runs
  synchronously on the runtime's stack. A host must not call back into a Glon
  export from inside `host_print`, `host_set_text`, `host_set_html`,
  `host_canvas_script`, or `host_call`. In particular the GLON_LIVE outbound
  `host_call` callback is invoked *during* `glon_route`/`glon_event`; re-entering
  the runtime there is undefined today.
- One WASM instance is expected to host **one active execution context**. If a
  host wants independent contexts, instantiate separate modules (each has its
  own linear memory and `M[]`); this is not exercised by the current tests.

Where the code does not establish a guarantee, this document says "not
currently guaranteed" rather than asserting one.

---

## 9. ABI invariants (implementer checklist)

Verified against `standalone/glon.c` and the tests:

- Call `glon_init()` once before `glon_load`/`glon_call`/route/event/run.
- Supply every import the module declares, or instantiation fails.
- Check `glon_alloc` for `0` and treat it as allocation failure.
- Write into the memory returned by `glon_alloc`; there is no commit/free call.
- Treat every `glon_alloc` pointer as valid only until the next execution entry
  (`glon_load`, `glon_call`, `glon_route`, `glon_event`, `glon_event_value`,
  `glon_event_bytes`, `glon_run`); copy data out first if it must survive.
- Multiple allocations before one execution entry may all be live; they do not
  alias each other.
- Read/copy `glon_run` results before the next `glon_run` (and copy them if they
  must outlive it).
- Use `(pointer, length)`; do not assume NUL termination or a specific text
  encoding.
- Re-create linear-memory views after calls, because memory can grow.
- Respect the documented size limits (source < 16384; `glon_call` name ≤ 60;
  route/event token ≤ 63; `glon_event_value` value ≤ 200;
  `glon_event_bytes` data ≤ 12000).
- Do not call Glon exports re-entrantly from a host import callback.
- Do not retain ABI pointers across calls or assume cross-instance pointer
  validity.

---

## 10. D11.6 historical note

Before D11.6, `glon_alloc` simply called the runtime's bump allocator over the
single 64 KiB `heap`:

```c
int glon_alloc(unsigned int len) { return (int)(intptr_t)malloc(len + 1); }
```

That heap never frees, because the runtime's interner stores symbol spellings
there for the life of the machine. A long-lived host such as Glon Patrol, which
forwards one `patrol-tick` event per animation frame, therefore leaked a little
of the 64 KiB on every event and eventually exhausted the heap. When the heap
was full, `glon_alloc` returned `0`; a host that wrote through the returned
pointer to address `0` could corrupt machine cell 0, and two allocations made
before one event (token + value) could collapse onto `0` instead of remaining
distinct live buffers.

D11.6 separated the two lifetimes:

- **persistent runtime allocation** stays on the never-freed `heap` (interned
  symbols);
- **transient host ABI allocation** moved to a separate `abi_heap[65536]` whose
  bump pointer is reset at each execution entry (`abi_reset()`), and
  `glon_alloc` now returns `0` when the transient arena is full.

This is why the lifetime contract in §3.2 is "one execution entry": it is what
makes a sustained, high-frequency host possible without unbounded growth. The
regression that pins this behaviour is `demo/shop/abi_arena_wasm_test.js`
(`make wasm-abi-test`).

---

## 11. ABI stability

- **Current / provisional.** There is no ABI version constant, no negotiation,
  and no stated compatibility policy in the repository. Nothing here claims the
  ABI is frozen.
- **Frozen scope.** `check-frozen-s1.sh` freezes the S1 substrate files
  (`s1.c`, `s1.h`, `tests.c`, `adversarial.c`, `claims.c`) at commit
  `f90496c2`. That is the *execution substrate*, not the WASM host ABI.
- **GLON_LIVE is experimental.** `glon_event_bytes` and the `host_call` import
  exist only in the separate `-DGLON_LIVE` build; the default G1A/shop build
  keeps the four-import ABI untouched.
- **Recommendation for host authors:** pin to a specific commit or tag. The
  source of truth for the interface is `standalone/glon.c`; the browser hosts
  are examples, not specifications.

---

## 12. Tested host behaviour

The following tests exercise the ABI. "node" tests were run with the Windows
`node.exe` v24 in this environment; the CDP tests require a debug Chrome.

| test | command | demonstrates |
|---|---|---|
| `demo/shop/abi_arena_wasm_test.js` | `make wasm-abi-test` | 10k `glon_alloc`(token)+`glon_alloc`(value)+`glon_event_value` and 10k valueless `glon_event` calls: 0 allocation failures, 0 token/value aliasing, correct dispatch throughout, and a 60000-byte single allocation still fits (D11.6 arena). |
| `demo/shop/binding_case_wasm_test.js` | `make wasm-binding-test` | 21 binding/closure-origin and `CASE` scenarios on the real WASM, matching the native `r0_s1_case_tests.c`. |
| `demo/shop/node_test.js` | `make wasm-g1a-test` | G1E launcher / lazy load / events / persistence / canvas on the real module. |
| `demo/shop/primer_node_test.js` | `make wasm-primer-test` | 30 primer examples through `glon_run`; `glon_result_ptr`/`glon_result_len`; run isolation. |
| `demo/shop/live_node_test.js` | `make wasm-live-test` | GLON_LIVE `host_call` round trip and `glon_event_bytes`. |
| `demo/shop/traffic_node_test.js` | `make wasm-traffic-test` | long-running application state over repeated route/event. |
| `demo/shop/linda_node_test.js` | `make wasm-linda-test` | cooperative multitasking across repeated events. |
| `demos/kaka/kaka_wasm_test.js` | `make wasm-kaka-test` | self-test + 1200-tick soak with held keys and valued events; asserts `allocFail == 0`, continuous drawing, and a stable restart. |
| `demos/patrol/long_session.html` + `demos/patrol/long_session_cdp.js` | `node demos/patrol/long_session_cdp.js` (debug Chrome) | real-browser long-session soak past the historical failure threshold; asserts `allocFail == 0`, no JS/WASM errors, input still live after restart. |
| `demos/kaka/{tick,refresh_rate,berry}_regression_cdp.js` | browser CDP | tick advancement, display-rate independence, and long-session berry-pool recycling (no allocation leaks). |
| `r0_s1_g1a_live_tests.c` | `make glon-live-native-test` | native focused test of the GLON_LIVE boundary (`r0_s1_g1a_live.c`). |
| `./check-frozen-s1.sh` | `make test` (first step) | frozen-S1 guard: S1 substrate files unchanged since `f90496c2`. |

Note: `standalone/node_test.js` failed in this environment against the **stale
committed** `standalone/glon.wasm` (it predates the `masm` rename and
`glon_run`); the `Makefile` target rebuilds that artifact before running. See
the report.

---

## 13. Native versus WASM distinction

**WASM-specific (this document):**

- the `glon_*` export names, their `i32` signatures, and the `env.host_*`
  imports;
- `glon_alloc` / the transient `abi_heap` arena and its one-execution-entry
  lifetime (the native session host allocates differently);
- `glon_result_ptr`/`glon_result_len` and the static result buffer;
- linear-memory view/growth handling;
- `host_set_html`/`host_canvas_script` as DOM/presentation imports.

**Reusable native concepts (not WASM-bound):**

- the `current-route` / `current-event` / `current-value` binding convention
  and the `[ do route ]` / `[ do do-event ]` dispatch that `r0_s1_g1a.c`
  implements browser-agnostically;
- parse → run → render-fragment (`r0_s1_g1a_render_fragment`);
- the GLON_LIVE request-fragment encoding (first byte `0x01`, `op` bytes,
  `0x00`, `arg` bytes) in `r0_s1_g1a_live.c`;
- `r0_s1_show_run` outcome text (`r0_s1_show.c`), shared by the native primer
  and `glon_run`.

**Not part of the core Glon ABI:**

- the Canvas/View drawing command vocabulary (`C`/`R`/`O`/`T`, `B`/`L`/`D`/`M`)
  interpreted by `demo/shop/host.js`, `demos/patrol/patrol-host.js`, and
  `demos/kaka/kaka-host.js`. That is the browser presentation protocol; a
  native or non-browser host may implement `host_canvas_script` however it
  wishes.
- the native host capability protocol (`GLON-HOST-CAPABILITIES.md`,
  `GLON-DESKTOP-D6.md`) and the desktop TCP data plane.

---

## Appendix: authoritative export sets

Read directly from the built modules:

```
demo/shop/glon.wasm  (G1A)
  imports: env.host_set_text, env.host_print, env.host_set_html, env.host_canvas_script
  exports: memory,
           glon_alloc, glon_init, glon_load, glon_call,
           glon_route, glon_event, glon_event_value,
           glon_run, glon_result_ptr, glon_result_len,
           emscripten_stack_get_current, _emscripten_stack_restore,
           __indirect_function_table

demo/shop/glon-live.wasm  (GLON_LIVE)
  imports: env.host_set_text, env.host_print, env.host_call,
           env.host_set_html, env.host_canvas_script
  exports: the G1A set plus glon_event_bytes

standalone/glon.wasm  (W2; committed binary is stale — rebuild with
                       `make wasm-standalone`)
  imports: env.host_set_text, env.host_print
  exports (current source): memory,
           glon_alloc, glon_init, glon_load, glon_call,
           glon_run, glon_result_ptr, glon_result_len,
           emscripten_stack_get_current, _emscripten_stack_restore,
           __indirect_function_table
```
