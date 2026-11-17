# Glon Host ABI

**Status:** current/provisional. This document describes the boundary between
Glon programs and host-provided functionality as implemented in this
repository. It does not create a frozen ABI and does not add host operations
to the language.

## Provenance and relationship to the original ABI document

A repository-wide search (working trees, reachable history, unreachable Git
objects, and the main worktree) found **no artifact that names Rebolek**. The
only non-specification local mention of Rebolek is an unrelated project note
outside this repository (`glon-memory-handoff-2026-10-10.md`). Therefore no
claim is made here that a particular file was "prepared for Rebolek".

The original ABI document in this repository is:

- `GLON-WASM-ABI.md`, added in commit `e2edb0086664a7ad44c1487299933cda365d80a2`
  ("Document the Glon WASM ABI", 2026-10-06). Its host-allocation lifetime
  contract was established immediately before it by commit
  `3f5780037a09069c24413b3c41015819049be22e` ("Fix transient Glon WASM ABI
  allocation lifetime", 2026-10-05), tagged `glon-wasm-abi-d11.6`.

This `GLON-HOST-ABI.md` is a **derivative summary plus native/C boundary
guide**, not a replacement. For the WASM binary interface, `GLON-WASM-ABI.md`
remains the detailed reference; where the two differ, that document and
`standalone/glon.c` win. The comparison is recorded in
`GLON-SPEC-VERIFICATION.md` §2.

---

Authoritative sources:

- `GLON-WASM-ABI.md` — the detailed WASM boundary.
- `r0_s1.h` — C API declarations and outcome constants.
- `r0_s1_session.c`, `r0_s1_show.c` — native session facade and outcome text.
- `standalone/glon.c` — WASM exports/imports.
- `r0_s1_g1a.h`, `r0_s1_g1a.c`, `r0_s1_g1a_live.c` — route/event and live
  request dispatch.
- `glon.c` — native CLI.
- `desktop/glon_host.h` — desktop OS boundary.
- `GLON-HOST-CAPABILITIES.md`, `GLON-APP-MODEL.md` — host/application policy.

The runtime is one global machine (`M[65536]`) with no locking. Calls must be
serialised.

---

## 1. Portable language vs host-specific capability

| Area | Portable Glon? | Where |
|---|---|---|
| Values, words, blocks, contexts, closures, SIN! | yes | language laws |
| Arithmetic, comparison, `print` | yes, ordinary R0 natives over frozen S1 `HOST` | `s1.c:134-160` |
| Allocation, raw diagnostics | only through `masm`/`raw` or host setup, not ordinary vocabulary | `s1.c` `HOST_ALLOC`/`HOST_DUMP` |
| String literals `"..."` | syntax is portable; the `mk-string` word is library | prelude/common |
| `case`, `parse`, `s/` strings, tasks | shipped Glon libraries, optional per session | `demo/shop/*.glon` |
| Source loading and comments | host convention | native session, WASM loader, CLI |
| Browser route/event dispatch | host convention | G1A / browser host |
| DOM/HTML/canvas presentation | host-specific | browser host |
| Files, network, processes, time, randomness | host-specific capability | `GLON-HOST-CAPABILITIES.md`, desktop host |
| WASM exports `glon_*` | host-specific binary interface | `standalone/glon.c` |
| OS boundary `glon_host.h` | host-specific C interface | `desktop/` |

The rule is: **Glon decides; the host moves bytes; the browser presents.** A
host may provide a semantic capability (open a file, make a request), but it
must not contain application routing or domain rules.

---

## 2. Native C embedding API

Declarations are in `r0_s1.h`. These functions are the loader/toolchain and
session boundary, not a second evaluator.

### 2.1 Core lifecycle

```c
cell r0_s1_init(void);
cell r0_s1_parse(const char *src, int *err);
int  r0_s1_run(cell block);              /* fresh transient stacks */
int  r0_s1_run_persistent(cell block);   /* preserve managed heap */
int  r0_s1_parse_error_kind(void);
```

- `r0_s1_init` resets loader/global state, emits the evaluator and returns the
  main entry. Call once per machine.
- `r0_s1_parse` parses source into a loader block. On failure it sets `*err`,
  records a kind retrievable with `r0_s1_parse_error_kind`, and rolls back
  loader heap, symbols and sites. The returned value is then NONE and must
  not be run.
- `r0_s1_run` resets transient stacks/registers and the managed heap frontier.
  `r0_s1_run_persistent` preserves the managed heap frontier so definitions,
  strings and closures survive across calls. Both reset the current context to
  the global context and clear the frame chain.
- `r0_s1_run` returns the result count derived from the top of the data stack.
  Do not use the return value alone to decide success: use
  `r0_s1_ran_cleanly`, `r0_s1_uncaught_error` or the session outcome. Read
  results with `r0_s1_result`.

### 2.2 Result, error and resource introspection

```c
cell r0_s1_result(int i, int N);          /* 0-based i */
int  r0_s1_ran_cleanly(void);
int  r0_s1_uncaught_error(cell *type, cell *id, cell *arg);
int  r0_s1_stack_sentry_fired(void);
int  r0_s1_halt_reason(void);
void r0_s1_global_usage(cell *count, cell *cap);
const char *r0_s1_sym_name(cell id);
```

- `r0_s1_ran_cleanly` is true iff the run reached the normal halt.
- `r0_s1_uncaught_error` returns 1 and fills the SIN! fields only when an
  uncaught SIN! ended the run.
- `r0_s1_halt_reason` returns a `R0S1_HALT_*` value:
  `NONE`, `CONTEXT_FULL`, `GC_WORKLIST_FULL`, `DS_EXHAUSTED`, `DS_UNDERFLOW`.
- `r0_s1_stack_sentry_fired` reports a detected stack-bound violation.
- `r0_s1_global_usage` reports global binding count and capacity.
- `r0_s1_sym_name` maps a symbol id to its spelling (or null).

Instrumentation-only accessors include `r0_s1_sp_start`, `r0_s1_sp_end`,
`r0_s1_rp_start`, `r0_s1_rp_end`, `r0_s1_sp_min`, `r0_s1_rp_min`,
`r0_s1_host_calls`, `r0_s1_code_size`, and the GC diagnostics.

### 2.3 Session facade

```c
enum {
    R0S1_OUT_OK = 0,
    R0S1_OUT_PARSE_ERROR = 1,
    R0S1_OUT_RESOURCE_ERROR = 2,
    R0S1_OUT_UNCAUGHT_SIN = 3,
    R0S1_OUT_HALT = 4
};

typedef struct {
    int status, detail;
    int count;                    /* OK: number of results */
    cell sin_type, sin_id, sin_arg;
} r0_s1_outcome;
```

```c
int r0_s1_session_run(const char *src, unsigned int len, r0_s1_outcome *out);
int r0_s1_session_run_block(cell prog, r0_s1_outcome *out);
void r0_s1_session_parse_error(r0_s1_outcome *out);
const char *r0_s1_outcome_status_name(int status);
const char *r0_s1_outcome_detail_name(int detail);
```

`r0_s1_session_run` is the canonical native cell runner:

- It strips `;;` line comments.
- It wraps the cell as `[ src ]`.
- It parses and runs persistently.
- It classifies the outcome from structured runtime state, never by inspecting
  rendered text.
- It does not mutate earlier definitions; a failed parse consumes nothing.

Detail values: `none`, `syntax`, `too_large`, `symbol_table_full`,
`loader_exhausted`, `site_table_full`, `context_full`, `stack_sentry`,
`machine`, `gc_worklist_full`.

### 2.4 Text outcome

```c
int r0_s1_show_run(const char *src, unsigned int len, char *out, int cap);
int r0_s1_mold(cell v, char *out, int cap);
```

`r0_s1_show_run` strips comments, wraps `[ src ]`, runs persistently, and
formats the real results. It emits exactly the text the browser primer shows:

- clean run: molded results separated by one space; no results: `none`;
- uncaught SIN!: `** uncaught #[SIN! type id arg]`;
- context full: `** halted: context full (no SIN!: a machine-level fail-stop)`;
- other halt: `** halted (no SIN!: a machine-level fail-stop)`;
- parse error: `** parse error`.

`r0_s1_mold` is deliberately minimal: integers, NONE, word forms, nested
blocks, strings, SIN!, and opaque `#[...]` for closures/natives/RAW/contexts.

### 2.5 G1A route/event bridge

Declared in `r0_s1_g1a.h` and implemented in `r0_s1_g1a.c`:

```c
int r0_s1_g1a_render_fragment(cell fragment, char *out, int cap, int *out_len);
int r0_s1_g1a_route(const char *token, char *out, int cap, int *out_len);
int r0_s1_g1a_event(const char *token, char *out, int cap, int *out_len);
int r0_s1_g1a_event_value(const char *token, const char *value,
                          char *out, int cap, int *out_len);
```

- `render-fragment` evaluates embedded Glon in a bundled byte-list fragment.
- `route` binds `current-route` to the token word, runs the application's
  `route` block, then renders the selected fragment.
- `event` binds `current-event` only.
- `event_value` also binds `current-value` to a managed STRING! built with the
  loaded program's `mk-string`.
- The application's routing and event meanings remain in Glon; C only supplies
  the token/value and invokes the renderer.
- Tokens are length-checked by the WASM host layer; the G1A route/event layer
  performs the delimiter check and reports a failure as `-2`.

The GLON_LIVE variant (`r0_s1_g1a_live_*`) adds request-fragment extraction and
`glon_event_bytes`; it is experimental.

### 2.6 C integration invariants

- One machine per process/instance. Calls are serialised; there is no locking.
- Do not call Glon exports re-entrantly from a host callback.
- Do not mutate `M[]` except for documented driver seed cells (M1 main entry,
  scheduler, task table, datatype seeding). Those are host/driver setup, not
  application semantics.
- Do not retain a `glon_alloc` pointer across an execution entry.
- A parse failure is recoverable; a machine fail-stop ends the run but leaves
  the session process alive in the native hosts.

---

## 3. Native command-line runner

Built from `glon.c`:

```text
glon FILE...
```

- Initialises one runtime, loads `glon-lib/prelude.glon` and
  `glon-lib/strings.glon` from beside the executable, then runs each FILE in
  the same persistent session, so definitions carry across files.
- Accepts ordinary Glon source, optionally wrapped in one outer `[ ... ]`.
  `;;` line comments are stripped.
- Prints the final results (not NONE-only) to stdout.
- Exit status: 0 all files clean; 1 a parse/SIN!/resource/machine error; 2
  usage, IO or library failure.
- It has no host extension point: every argument is a file. Host-backed
  experiments (`glon-web`) are separate executables.

The Jupyter kernel host (`jupyter/host/glon_kernel_host.c`) is another native
session host. It uses the same session facade and adds notebook persistence,
not language semantics.

---

## 4. WebAssembly ABI

**Current/provisional; no version constant or compatibility guarantee.**
`GLON-WASM-ABI.md` is the detailed reference. The source of truth is
`standalone/glon.c`; the browser hosts are examples.

### 4.1 Module variants

| Build | Artifact | Host imports | Extra exports |
|---|---|---|---|
| W2 standalone | `standalone/glon.wasm` | `host_print`, `host_set_text` | common set |
| G1A shop | `demo/shop/glon.wasm` | four: `host_print`, `host_set_text`, `host_set_html`, `host_canvas_script` | `glon_route`, `glon_event`, `glon_event_value` |
| GLON_LIVE | `demo/shop/glon-live.wasm` (`-DGLON_LIVE`) | five: the four plus `host_call` | the three plus `glon_event_bytes` |

Common exports: `glon_alloc`, `glon_init`, `glon_load`, `glon_call`,
`glon_run`, `glon_result_ptr`, `glon_result_len`, `memory`.

The committed `standalone/glon.wasm` binary predates `glon_run`/`glon_result_*`
and the `masm` rename. Rebuild it with `make wasm-standalone` before testing;
the source and the G1A/GLON_LIVE modules are authoritative.

### 4.2 Exports

On wasm32, all signatures use `i32`; pointers are byte offsets into the
exported linear memory.

| Export | Signature | Purpose |
|---|---|---|
| `glon_alloc` | `(len) -> ptr` | allocate transient host→WASM bytes; 0 on failure |
| `glon_init` | `() -> 0` | initialise once; idempotent; seeds M1 scheduler cells |
| `glon_load` | `(src, len) -> 0/-1/-2/-3` | parse and extend the persistent machine |
| `glon_call` | `(name, len) -> 0/-1/-2/-3` | run `[ do NAME ]` in a fresh transient run |
| `glon_route` | `(token, len) -> 0/-1/-2` | bind `current-route`, run `route`, render |
| `glon_event` | `(token, len) -> 0/-1/-2` | bind `current-event`, run `do-event`, render |
| `glon_event_value` | `(token, tlen, value, vlen) -> 0/-1/-2` | also bind `current-value` as STRING! |
| `glon_event_bytes` | `(token, tlen, data, dlen) -> 0/-1/-2` | GLON_LIVE raw bytes; NUL allowed |
| `glon_run` | `(src, len) -> 0/-1` | run one program and format outcome text |
| `glon_result_ptr` | `() -> ptr` | pointer to static result text |
| `glon_result_len` | `() -> len` | length of result text |

Error codes:

- `glon_alloc`: 0 = arena exhausted; otherwise a pointer.
- `glon_load`: -1 source too long (>= 16 KiB), -2 parse error, -3 run failure.
- `glon_call`: -1 bad name length (0 or > 60), -2 parse error, -3 run failure.
- `glon_route`/`glon_event`: -1 zero/oversized token (max 63), -2 dispatch
  failure.
- `glon_event_value`: -1 bad token or value > 200 bytes, -2 dispatch failure.
- `glon_event_bytes`: -1 bad token, -2 dispatch failure (including data >
  12000 bytes).
- `glon_run`: -1 source too long; on 0 the text is set in every case,
  including parse errors, uncaught SIN! and halts.

### 4.3 Host imports

| Import | Signature | Meaning |
|---|---|---|
| `env.host_print` | `(ptr, len) -> ()` | plain text/log output |
| `env.host_set_text` | `(handle, value) -> ()` | write a number into a host element |
| `env.host_set_html` | `(handle, ptr, len) -> ()` | write rendered HTML (handle is currently 1) |
| `env.host_canvas_script` | `(ptr, len) -> ()` | hand the host a generic visual script |
| `env.host_call` | `(op, op_len, arg, arg_len) -> ()` | GLON_LIVE outbound capability request |

The host must supply exactly the imports the module declares. The
`host_canvas_script` import is called after every route/event dispatch,
possibly with `len == 0`. Rendered HTML is bounded by a 16 KiB `htmlbuf`; the
canvas buffer is `G1_VIS_CAP` = 2560 bytes. The canvas command vocabulary is a
presentation protocol, not core ABI.

### 4.4 Memory and lifetime

- The module exports one linear `memory`, built with memory growth. Recreate
  every JS `Uint8Array`/`DataView` view after a call.
- The Glon machine lives in `M[65536]` plus the loader heap inside linear
  memory; it is persistent for the instance.
- `glon_alloc` uses a separate transient bump arena (`abi_heap[65536]`),
  8-byte aligned, `len + 1` bytes reserved, reset at each execution entry.
- **D11.6 lifetime rule:** a `glon_alloc` pointer is valid only until the next
  execution entry. The execution entries are `glon_load`, `glon_call`,
  `glon_route`, `glon_event`, `glon_event_value`, `glon_event_bytes` and
  `glon_run`. `glon_init`, `glon_alloc`, `glon_result_ptr` and
  `glon_result_len` are not entries.
- Multiple allocations before one entry are all live and do not alias.
- `glon_run`'s result buffer is static (4 KiB) and not in the arena, but it is
  replaced by the next `glon_run`. Copy it if it must survive.
- Route/event output goes through imports, not the result buffer.

### 4.5 Source, tokens and values

- Source is byte-oriented; UTF-8 is a host choice, not an ABI guarantee.
- `glon_load`/`glon_run` copy source into a static 16 KiB buffer and strip
  `;;` line comments before parsing. Embedded NUL ends the C string.
- Tokens must be non-empty and must not contain `[`, `]`, `'`, `:`, space,
  tab, CR or LF. The host layer enforces the length; the G1A route/event layer
  enforces the delimiter rule and reports it as `-2`. Limits: `glon_call` name
  <= 60 bytes; route/event token <= 63.
- `glon_event_value` binds a managed STRING! built from the value bytes; max
  200 bytes; embedded NUL is truncated by the C-string path.
- `glon_event_bytes` copies by length (NUL and arbitrary bytes preserved) and
  allows up to 12000 bytes (`LIVE_MAX_BYTES`).
- Route/event dispatch requires the loaded application to define `route` and
  `do-event` as appropriate. The host does not interpret the token.

### 4.6 Loading sequence

```text
instantiate(wasm, all declared imports)
glon_init()
ptr = glon_alloc(len(source)); write source; glon_load(ptr, len)  ;; repeat for libraries
ptr = glon_alloc(len(token));  write token;  glon_route or glon_event
;; output arrives synchronously through host_set_html / host_canvas_script
ptr = glon_alloc(len(program)); write program; glon_run(ptr, len)
text = memory[glon_result_ptr() .. glon_result_ptr()+glon_result_len()]
```

---

## 5. Host capabilities and policy boundary

`GLON-HOST-CAPABILITIES.md` and `GLON-APP-MODEL.md` define the intended host
model. These are **not part of the Glon language**:

- A **capability** is a semantic operation a host can implement (time, files,
  network, view, streams).
- A **permission** is what an application may be granted.
- A **module** is code an application loads.
- An **entitlement** is licensed functionality.

The proposed universal core and optional capabilities (time, secure random,
app-private files, networking, streams, view, process spawn, clipboard, etc.)
are host features. Their exact primitive names are not frozen. An application
must not assume an optional capability exists.

The desktop proof uses `desktop/glon_host.h` for TCP, open, spawn, SHA-256 and
filesystem read. Those primitives are supplied by platform-specific C files;
the host never contains application names such as `greeting` or
`desktop/big.txt`. Glon decides; C moves bytes.

Browser-owned facilities (DOM, HTML, CSS, canvas, input events) are presented
by the browser host and are not reimplemented in Glon.

---

## 6. Minimal C embedding example

```c
#include "r0_s1.h"
#include <stdio.h>

int main(void) {
    r0_s1_init();

    int err = 0;
    cell prog = r0_s1_parse(
        "[ square: func [x] [ * x x ]  square 9 ]", &err);
    if (err) {
        r0_s1_outcome out;
        r0_s1_session_parse_error(&out);
        fprintf(stderr, "parse error: %s\n",
                r0_s1_outcome_detail_name(out.detail));
        return 1;
    }

    int n = r0_s1_run(prog);
    if (r0_s1_ran_cleanly() && n == 1)
        printf("%ld\n", (long)int_val(r0_s1_result(0, n)));
    else
        fprintf(stderr, "run did not complete cleanly\n");
    return 0;
}
```

The example is illustrative; production embedders should use
`r0_s1_session_run_block` and its structured `r0_s1_outcome`, which correctly
classifies parse errors, uncaught SIN! and machine halts. Do not inspect
rendered text to decide an outcome.

Session-style use:

```c
r0_s1_outcome out;
r0_s1_init();
r0_s1_session_run("x: 10", 5, &out);          /* definitions persist */
r0_s1_session_run("+ x 5", 5, &out);          /* => 15 */
```

`r0_s1_session_run` strips comments and wraps cells. `r0_s1_run_persistent`
preserves the managed heap frontier; `r0_s1_run` does not.

---

## 7. Failure mapping at the boundary

| Runtime event | C/WASM observation |
|---|---|
| clean run | `r0_s1_ran_cleanly()` true / outcome `OK` |
| uncaught SIN! | `r0_s1_uncaught_error` fills fields / outcome `UNCAUGHT_SIN` |
| parse failure | `r0_s1_parse_error_kind` / `PARSE_ERROR` or `RESOURCE_ERROR` |
| `CONTEXT_FULL` | `HALT`/`RESOURCE_ERROR` detail `context_full`; no SIN! |
| GC worklist full | `RESOURCE_ERROR` detail `gc_worklist_full` |
| stack sentry | `HALT` detail `stack_sentry` |
| other machine halt | `HALT` detail `machine` |
| WASM route/event dispatch failure | export returns -2; no result text |
| WASM `glon_run` parse/SIN!/halt | export returns 0; inspect result text |

A host must never treat `** halted` as a catchable language error. It is a
machine fail-stop and cannot be judged.

---

## 8. Tested boundary

The host/wasm boundary is exercised by:

- `demo/shop/abi_arena_wasm_test.js` — 10k valued and 10k value-less events,
  allocation lifetime and aliasing;
- `demo/shop/binding_case_wasm_test.js` — binding and CASE on real WASM;
- `demo/shop/node_test.js` — G1E launcher, events, persistence, canvas;
- `demo/shop/primer_node_test.js` — primer examples through `glon_run`;
- `demo/shop/live_node_test.js` — GLON_LIVE `host_call` and
  `glon_event_bytes`;
- `demo/shop/traffic_node_test.js`, `demo/shop/linda_node_test.js` — long
  sessions;
- `demos/kaka/*_test.js` and CDP soaks — sustained host interaction;
- `r0_s1_g1a_live_tests.c` — native GLON_LIVE boundary;
- `r0_s1_session_tests.c` — structured native session outcomes;
- `r0_s1_primer_tests.c` — the same text the primer displays;
- `./check-frozen-s1.sh` — frozen substrate guard.

The WASM ABI is not frozen. Pin a commit or tag; the source, not a committed
binary, is authoritative.
