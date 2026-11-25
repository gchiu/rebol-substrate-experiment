# GLON

**A tiny embeddable REBOL-family runtime for machines.**

Live WebAssembly demo:

https://gchiu.github.io/rebol-substrate-experiment/

GLON also runs as a persistent Jupyter kernel (definitions carry over from
cell to cell), through the standard Jupyter kernelspec machinery, so any
Jupyter frontend can use it. Linux/WSL is the proven native target; you
install it from a repository checkout (there is no pip package yet), and
macOS and native Windows are not yet supported. See `jupyter/README.md`.

GLON began as an experiment in language architecture:

> How small can the underlying machine be if a REBOL-like high-level language
> is allowed to descend through a stable low-level "trapdoor" only when it
> needs machinery the evaluator itself does not provide?

## Alpha

The language is frozen at Alpha: `glon-alpha-v1`. The authoritative contract
(what each value is, how equality/truth/words/blocks/closures/reduce/parse
behave, and what is explicitly deferred) is in:

    GLON-ALPHA-LAWS.md

Read that first. Native tests (`make test`) and `./check-frozen-s1.sh` are the
correctness gates. The storefront demo lives under `demo/shop/`.

The result is a small stack of layers:

    GLON / R0
        |
       RAW
        |
       S1
        |
    host / WASM / native

Ordinary programs are written at the REBOL-like level.

S1 is not intended as an application language. It is a tiny Forth-like
substrate used only when the high-level language cannot express some required
mechanism directly.

A useful shorthand is:

> stay in GLON unless you genuinely need to descend.

The same principle extends to the intended GUI layer and to the browser host:
GLON is a persistent REBOL-style machine hosted by the browser, JavaScript
provides host and GUI services rather than the application model, and GLON
abstractions are deliberately permeable — high-level facilities retain a
defined path down to the underlying substrate. See `DESIGN.md` ("The Substrate
Principle").

## S1

S1 is a frozen cell-addressed machine with only seven primitive operations:

    LIT
    DUP
    DROP
    @
    !
    0BRANCH
    HOST

IP, SP and RP are memory-mapped.

Calls, returns, jumps and other control mechanisms are therefore constructed
from the substrate rather than added as special VM primitives.

The frozen S1 baseline is tagged:

    s1-frozen-v1

## R0 / GLON

R0 is the current small REBOL-like evaluator running above S1.

It provides the experimental high-level language used by GLON, including:

- words and blocks
- contexts
- functions
- lexical closures
- recursion
- multiple results
- definitional RETURN
- first-class RAW values

RAW is the low-level trapdoor:

    raw [ ... ]

It exposes the S1 substrate without requiring another evaluator feature.

The frozen evaluator/trapdoor baseline is tagged:

    r0-trapdoor-v2

## What has been demonstrated

The project has so far constructed several facilities above the same frozen
substrate without adding another S1 primitive.

These include:

- a genuine R0 evaluator running on S1
- functions, closures and recursion using S1 state
- non-local RETURN crossing unaware code
- a user-defined first-class escape mechanism
- a cooperative debugger
- suspension, inspection and resumption of R0 execution
- a standalone WebAssembly build
- runtime loading of GLON source in the browser

The hardened debugger survives repeated suspend/inspect/resume cycles while
ordinary recursive R0 closures execute during inspection.

No debugger-specific S1 primitive was added.

## Standalone WebAssembly

The current standalone browser runtime is:

    glon.wasm     22,459 bytes raw
                  6,508 bytes gzip

    glon.js        3,550 bytes handwritten

The deployed WASM has:

- no Emscripten JavaScript runtime
- no libc
- no WASI
- no virtual filesystem

It imports exactly two host services:

    env.host_print
    env.host_set_text

and exports:

    glon_alloc
    glon_init
    glon_load
    glon_call
    memory

The GLON source is supplied at runtime rather than compiled into the WASM.

The JavaScript host is deliberately small. It forwards browser events and
performs requested browser operations; language semantics remain inside
GLON/R0/S1.

## Why the trivial browser counter?

The counter demo is intentionally uninteresting as an application.

One line of JavaScript would obviously be simpler if the goal were merely to
increment a number.

The purpose of the demo is to establish that a REBOL-like language runtime can
live independently inside WebAssembly, maintain its own state, load source at
runtime, and ask the browser for only mundane host services.

## Embedding

The intended model is not primarily:

    launch GLON

but:

    embed GLON inside something else

For example:

    browser
        |
      GLON
        |
       S1

or:

    server
        |
      GLON
        |
       S1

or:

    native application
        |
      GLON
        |
       S1

Only the host boundary should change.

This is the idea behind the phrase:

> **A sort of REBOL for ROBOTs.**

## Build

Native tests:

    make test

Frozen S1 verification:

    ./check-frozen-s1.sh

Standalone WebAssembly:

    make wasm-standalone

Standalone WASM test:

    make wasm-standalone-test

HTTPS + SQLite message application (G3E), large profile:

    make glon MODULES=file,http,sqlite MEM_PROFILE=large MEM_LOADER_CELLS=500000
    tests/https-g1/make-cert.sh
    python3 tests/https-g3e/acceptance.py --glon ./glon

or, equivalently:

    make https-g3e-test

HTTPS concurrency (G3F) uses a bounded CivetWeb worker pool; one thread at a
time is inside the Glon session:

    make https-g3f-test

Cooperative multitasking (M2) is an ordinary Glon vocabulary (`modules/task.glon`)
with a host pump; ten tasks can run, yield and suspend on async I/O:

    make m2-test

Multiple outstanding async I/O (M3) lets many tasks each have a request pending
at once (bounded FIFO, id-correlated replies), over the same file/SQLite modules:

    make m3-test

Task-slot recycling (M4) lets finished/failed/cancelled tasks release their slots
with generation-tagged handles, so a process can create unlimited tasks with a
bounded number simultaneously active:

    make m4-test

HTTPS requests as cooperative tasks (H1) is an investigation: CivetWeb's
synchronous `begin_request` prevents safe request suspension, so this runs the
scheduler-level interleave demonstration and documents the obstacle (see
`H1-RESULTS.md`). G1–G3F remain the supported HTTPS path:

    make h1-test

Bounded asynchronous HTTP bridge (H2A) decouples receiving a request from
executing its Glon logic: an opt-in `--http-bridge` mode has CivetWeb workers
enqueue into a bounded table and wait while one interpreter-owner loop runs the
Glon application (default mode is unchanged G3F):

    make h2a-test

Cooperative HTTP tasks (H2B) are an opt-in `--http-tasks` experiment. The
host-side task path is in place, but the 10-task goal is blocked by the per-task
return-stack vs. M1-arena limit (see `H2B-RESULTS.md`); G3F/H2A remain the
supported HTTPS paths:

    make h2b-test   # partial smoke only; not a passing 10-task demo

H2C measured the requirement: the HTTP response path needs ≈2,500 return-stack
cells/task, so ten tasks need a ≈32,000-cell M1 arena (option B in
`H2C-RESULTS.md`); the arena increase is an approved scaling change, not applied.

H2D makes the arena configurable (`MEM_M1_CELLS`, default 10,200; 32k verified
for ten non-HTTP tasks) and fixes the H2B request encoding; the HTTP+SQLite task
path now suspends/resumes but response delivery still halts (see `H2D-RESULTS.md`).

H2E diagnosed that halt: it is a collector task-sentry fail-stop on a resumed
task (IP 11953, reason 0; SP at the arena base, RP in bounds, HP low), not a
Glon error or stack/heap exhaustion. A minimal proposed correction is recorded
in `H2E-RESULTS.md`; it is not applied (runtime-contract change).

H2F confirmed the diagnosis and implemented the fix (use the live `REG_SP/RP`
for the running task in the GC sentry): the first request then succeeds, but
the added code overflows the fixed 16,128-cell code region, so the fix is
reverted pending an approved code-limit increase (see `M2F-RESULTS.md`).

H2G enlarged the code region (+256 cells, fits) but the halt persisted: tagging
every GC fail-stop showed the real cause is an **unbound word** in the resumed
task's context, not the GC sentry. The H2E/H2F diagnosis is disproven;
see `H2G-RESULTS.md`.

H2H resolved the word: id 218 = `result` (a `http/task-run` local), then `a`
after removing it. The task's restored context does not chain to the
intermediate activation contexts created across the async suspension, so live
locals are treated as unbound (see `H2H-RESULTS.md`).

H2I showed the context model is sound for ordinary nested tasks (locals survive
`yield` and async file I/O); the failure is specific to the HTTP `task-run`/
handler/SQLite-callback path (see `H2I-RESULTS.md`).

H2J isolated it: the mock `task-run`+handler+respond (spawn+run in one session)
passes, but H2B spawns in the dispatch turn and first runs in a later pump turn
(see `H2J-RESULTS.md`).

H2K made spawn+first-run happen in one session; the first run then succeeds, but
the task still halts on an unbound helper local (`a`) after the async SQLite
resume, so the activation chain across the async suspension is the remaining
defect (see `H2K-RESULTS.md`).

H2L isolated it: a standalone SQLite mock (task-run+handler+respond, one
session) PASSES, so SQLite/7-args/respond are not the cause; the host-spawned
HTTP task resumed in a later pump session loses the helper activation (see
`H2L-RESULTS.md`).

H2M ruled out the session boundary: spawning in one CLI session and running in
another preserves nested locals (`S=1`), so persistent ownership is not the fix.

H2N ruled out the loader-heap lifetime (a non-restoring spawn expression changes
nothing); the defect is the activation/context chain across the async SQLite
resume in the C-spawned HTTP task path (see `H2N-RESULTS.md`).

To serve the generated browser demo locally:

    python3 -m http.server 8000

then open:

    http://localhost:8000/docs/standalone.html

## Current experiment status

The project is deliberately exploratory.

It is not:

- a complete REBOL implementation
- a Red replacement
- a production language
- a claim that S1 is pleasant application code
- a claim that the individual underlying ideas are novel

The interesting question is whether useful high-level machinery can repeatedly
be manufactured above a very small stable substrate rather than requiring the
evaluator or VM to grow a new fundamental mechanism.

The next experiment is cooperative multitasking.