# AGENTS.md — instructions for coding agents

Before changing the Glon runtime (`r0_s1_*.c`, `r0_s1.h`, `m1_layout.h`), the
frozen S1 substrate, the memory layout, resource limits or language semantics,
read **`GLON-ARCHITECTURE-MEMORY.md`** and the authoritative documents it
cites for the area you are changing.

- **Decisions are not defects.** Entries marked **[DECISION]** there are
  deliberate. To change one, write an explicit proposal that cites the entry,
  and wait for approval.
- **The frozen S1 files never change:** `s1.c`, `s1.h`, `tests.c`,
  `adversarial.c`, `claims.c`. `./check-frozen-s1.sh` must pass.
- **`GLON-ALPHA-LAWS.md` is normative.** The implementation is the ground
  truth for addresses and sizes.
- **Raising a capacity or changing the 65,536-cell memory map** is a separate,
  explicitly approved scaling experiment, never an incidental fix.
- **Record new measurements** (dated, with their configuration) and decisions
  in `GLON-ARCHITECTURE-MEMORY.md`; record milestones in `MEMORY.md`.

---

# Glon Developer Preview 1 — guide for AI coding agents

This section is self-contained. It does not assume access to the Glon
development history or private engineering memory. The repository is the
authority; where this summary and the source disagree, follow the source and
`GLON-ALPHA-LAWS.md`.

## What Glon is

Glon is an experimental, compact, embeddable execution substrate for
AI-generated applications. Programs are ordinary Glon source; the optional
native host provides capabilities such as files, HTTPS and screenshots.

Stable execution semantics live in the frozen S1 substrate and the R0
evaluator:

- seven S1 operations: `LIT DUP DROP @ ! 0BRANCH HOST`;
- flat cell memory with memory-mapped registers;
- a REBOL-family evaluator above S1;
- a non-moving mark/sweep collector;
- cooperative M1 tasks;
- first-class `SIN!` errors with `raise` and `judge`.

The language is frozen at Alpha. Do not invent syntax that is not in the
implementation.

## Evaluation

Evaluation is left-to-right and prefix. A function call consumes a fixed
number of argument expressions from the stream:

```glon
double: func [x] [ * x 2 ]
double 21
;; => 42
```

There are no parentheses for calls. `[ ... ]` is a block value; it is evaluated
only when something explicitly evaluates it (`do`, a function body, a control
form, etc.). A block passed as an argument is data unless the callee evaluates
it.

`func [spec] [body]` creates a closure. The body is a block. A function's
arguments are local bindings in its activation context. `return` is
definitional and non-local. Functions can call functions, be stored in words,
and be passed as arguments.

`do block` evaluates a block in the current context. `either cond [a] [b]`
evaluates one branch. `reduce` evaluates each expression in a block and returns
a block of the results. `values` returns multiple results.

## Words, contexts and binding

A word looks up dynamically through the lexical parent chain. `T_BOUND` words
are pre-resolved lexical references valid only under their origin activation.
A block that depends on a live activation must not escape it; the runtime raises
`SIN! 'escape` with an id such as `'return`, `'argument`, `'store`, `'capture`
or `'transport` when it detects a violation. Use a closure, not a raw dependent
block, when code must escape.

A set-word updates the nearest existing binding, otherwise it creates a local
of the current call. Choose local names that do not collide with globals in the
libraries you load.

## Values, strings and collections

- Integers are tagged cells. Native range is about ±2^59; WASM about ±2^27.
- `none` is the no-value value. `0` and `none` are falsey; everything else is
  truthy. JSON `null` is not `none` (see the JSON mapping).
- Strings are immutable byte sequences (`STRING!`), one byte per managed cell.
  A quoted source string is expanded to `mk-string [byte ...]`. There are no
  escape sequences in source string literals; a `"` ends the string. Use the
  byte-list form or existing string vocabularies for control bytes.
- Blocks hold values and are mutable only through the low-level `block-set!`
  style helpers used by applications. Ordinary code should prefer immutable
  composition.
- `s/+` concatenates strings; `s/length` gives bytes; the optional
  `strings-ext` adds `s/at`, `s/slice`, `s/span`, `s/scan-set`, `s/from-int`.
  `str-eq` is content equality for strings; `=` is identity for strings.
- There is no general `type?`, no dynamic block append, and no loop syntax.
  Recursion and the M1 task scheduler are the available iteration mechanisms.
  Keep recursion within the return-stack limit.

## Errors: SIN!

`create-sin type id arg` builds an inert `SIN!` value. `raise err` unwinds to the
nearest `judge` in the same task; `judge [ ... ]` returns the SIN! instead of
propagating. `sin?`, `sin-type`, `sin-id`, `sin-arg` inspect it. A non-SIN!
raise becomes `'type 'raise`. Machine fail-stops (heap/stack exhaustion,
`CONTEXT_FULL`, GC worklist full) are not SIN!s and cannot be judged.

## M1 multitasking

M1 provides cooperative tasks through a library of RAW/S1 mechanism plus
ordinary `spawn`, `yield`, `run-tasks`, `task-finish` policy. Tasks share one
managed heap; each has private data/return stacks. Scheduling is deterministic
and cooperative. Do not assume pre-emption or parallel Glon evaluation. The
HTTPS host serialises request handlers; it does not run concurrent Glon tasks.

## Memory profiles

Memory capacity is a deployment characteristic, not a language restriction.
Every capacity is declared together in `profiles/<name>.profile`; the build
generates `build/profile/<name>/` with `scripts/glon-profile.sh`. One-at-a-time
`MEM_MANAGED_CELLS`/`MEM_LOADER_CELLS`/`MEM_GLOBAL_CAP`-style overrides are
rejected. `make profile-list` lists the declared profiles.

- **constrained** (default): frozen 64K map, 7,232-cell managed heap,
  14,800-cell loader, 3×3,400 M1 arenas.
- **constrained-notask**: the same 64K map without M1 tasks; loader
  `[40000,65536)`. Used by Kaka and by HTTP builds that create no tasks.
- **large**: generated S1 object, 1,048,576 managed cells, 40,000 GC worklist
  entries, 200,000 loader cells, relocated M1 arenas.
- **server**: 1,048,576 managed cells, 40,000 worklist, 500,000 loader
  (the G3A HTTPS configuration).
- **desktop-256k**, **desktop-1m**, **desktop-4m**, **server-https**:
  provisional profiles carried from the View/MS-1 and HTTPS records.

Build with a named profile:

```sh
make glon MODULES=file,http MEM_PROFILE=server
```

`r0_s1_layout.h` rejects overlapping or unsafe layouts at compile time. The
worklist must be at least managed/32. Memory is build-time, not
runtime-selectable. The frozen 64K profile remains the conformance baseline.
See `GLON-CORE-C2-RESULTS.md`.

## Optional capabilities and modules

`MODULES=` builds the minimal CLI. `MODULES=file` adds host-mediated
asynchronous file I/O and `file.glon`/`file-stream.glon`.
`MODULES=http` adds CivetWeb HTTPS plus `http.glon`.
`MODULES=file,http` adds both. The memory profile is selected separately.

File access is denied unless the host grants it:

```sh
./glon --io-root . --allow-file-read --allow-file-write ...
```

The file protocol is one outstanding asynchronous request. A file operation is
issued now and its success/failure closure runs in a later host pump. The HTTP
host pumps the file queue after each request and at shutdown.

## HTTPS vocabulary (built-in with MODULES=http)

`http/listen port handler` installs a handler closure. The handler receives an
opaque request block `[id method path query body headers]`.
`http/method`, `http/path`, `http/query`, `http/body`, `http/header`,
`http/content-type` inspect it. `http/respond request status content-type body`
sends a response; the body can be a string or a nested block of string/block
parts. `http/respond-with` adds application headers (for example `Allow`).
`http/stop` requests shutdown. Stale handles raise
`SIN! 'http 'stale-request`.

Minimal application (actual, six lines / 125 bytes):

```glon
[
hello: func [request] [
    http/respond request 200 "text/plain; charset=utf-8" "Hello World"
]
http/listen 8443 :hello
]
```

Run:

```sh
make glon MODULES=http
tests/https-g1/make-cert.sh
./glon --http-cert tests/https-g1/.cert/server.pem hello.glon
curl -k https://127.0.0.1:8443/
```

## JSON vocabulary (`modules/json.glon`)

Load `modules/json.glon` after `strings-ext`. Mapping:

| JSON | Glon |
|---|---|
| string | `STRING!` |
| integer | `INTEGER!` when exact |
| fraction/exponent/out-of-range | `[ 'json-number "<exact token>" ]` |
| true/false | `[ 'json-bool 1 ]` / `[ 'json-bool 0 ]` |
| null | `[ 'json-null ]` (not `none`) |
| array | `[ 'json-array <cons list> ]`, `none` when empty |
| object | `[ 'json-object <pair cons list> ]`, `none` when empty |

Use `json/decode`, `json/encode`, `json/object?`, `json/object-get`,
`json/array?`, `json/string?`, `json/null?`, `json/number-text`. Duplicate keys
are rejected. Limits: depth 16 and 32 items per container by default; the G3A
app raises `json/max-items` to 128. Larger collections must be paginated or
line-oriented. Malformed input raises `SIN! 'json ...`.

## Web helpers (`modules/web.glon`)

`wp/form-scan body key 0` returns the first URL-decoded form value or `none`.
`wp/form-check-loop body 0` rejects malformed percent escapes.
`wp/escape-html s` escapes HTML. `wp/ci-prefix? s prefix` is a case-insensitive
content-type prefix test. It deliberately contains no JSON parser.

## Running and diagnosing

A command-line application is one or more Glon source files:

```sh
./glon app.glon
```

Libraries are loaded from `glon-lib/` beside the executable. The minimal CLI
loads `prelude.glon` and `strings.glon`; optional modules load their libraries.
A parse error is reported before execution; an uncaught SIN! is reported with
its type/id/arg; a machine fail-stop is reported as a halt. Exit status is 0 for
clean, 1 for a Glon failure, 2 for usage/I/O/library failure.

Diagnose by:
1. running the smallest source that reproduces the issue;
2. checking that a required vocabulary is actually loaded;
3. using `judge` and `sin-id`/`sin-arg` around the failing operation;
4. checking block/string types with `s/raw-string?`/`s/raw-int?` or the
   vocabulary's predicates;
5. keeping recursion below the return-stack limit;
6. running the focused tests rather than guessing.

## Tests

```sh
./check-frozen-s1.sh
make test
make json-test
make io-file-c-test
make https-g1-test
make https-g2-test
make https-g3-test
make https-g3a-test
```

The HTTPS tests use loopback port 8443 and a local self-signed certificate. Do
not expose the experimental server to the public internet.

## Instructions for AI agents

- Inspect existing vocabulary and examples before generating new functionality.
- Reuse composable words; do not create a parallel parser, framework or error
  system.
- Do not invent syntax or assume REBOL/Red semantics where Glon differs
  (`str-eq` vs `=`, no dynamic block append, no general `type?`, no loops).
- Do not add C functionality merely for convenience. The host provides
  capabilities, not application routes or membership logic.
- Distinguish implemented capabilities from proposals in the documentation.
- Build and execute generated applications, test them, and report actual
  results. Do not claim success you did not observe.
- Preserve frozen files and the constrained regression target.

Known limits of this preview: HTTPS handlers are serialised; keep-alive
connection starvation is known; the membership register passed a 200-record
restart but not 10,000; 100,000-request endurance is uncompleted; JSON nesting
and collection sizes are bounded; persistence has no fsync/power-failure
guarantee; authentication/authorisation/CSRF and production hardening are
absent.
