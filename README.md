# GLON

**A tiny embeddable REBOL-family runtime for machines.**

Live WebAssembly demo:

https://gchiu.github.io/rebol-substrate-experiment/

GLON began as an experiment in language architecture:

> How small can the underlying machine be if a REBOL-like high-level language
> is allowed to descend through a stable low-level "trapdoor" only when it
> needs machinery the evaluator itself does not provide?

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