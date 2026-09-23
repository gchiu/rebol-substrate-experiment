# TUPLE-SPACE-V003.md — Glon implementation with REAL cooperative blocking

v0.03 ports the R3 executable specification (v0.01 matching/storage, v0.02
waiter/service laws) onto Glon and supplies the one thing the Oldes R3
laboratory could not: **a task that executes IN really stops running and
later resumes when OUT publishes a matching tuple.**

    R3 v0.02   executable semantic specification, with a STUB task adapter
               (Oldes R3 has no cooperative scheduler: INCLUDE_TASK disabled)

    Glon v0.03 same tuple laws, REAL cooperative blocked tasks and wakeup,
               using Glon's existing M1 scheduler above frozen S1

No S1 change, no new primitive, no new language feature, no host I/O.

## Files

    tuple-space-v003.glon         Glon library (scheduler wait-flag + tuple space)
    tuple-space-v003-tests.glon   executable tests (returned to the driver)
    tuple-space-v003-driver.c     native/ELF harness (reads the .glon sources)
    Makefile                      isolated build for the experiment
    TUPLE-SPACE-V003.md           this contract

## Build and run (native / ELF, no browser/JS/WASM)

    cd experiments/tuple-space
    make test

which is

    cc -std=c17 -O0 -g \
       -DM1_MAX_TASKS=5 -DM1_TASK_CELLS=2040 -DM1_RS_OFF=2040 \
       -I../.. -o tuple-space-v003 \
       tuple-space-v003-driver.c ../../r0_s1_runtime.c ../../s1.c
    ./tuple-space-v003 tuple-space-v003.glon tuple-space-v003-tests.glon

The experiment recompiles its OWN runtime object with a five-task override.
`m1_layout.h` only gained `#ifndef` guards, so the repository's default build
(and `make test`) is byte-identical in effect.

## 1. Reused machinery (not reinvented)

    M1 scheduler   task record (IP/SP/RP/RV_CUR/RV_END/RV_CTX/RV_BLK/RV_FRAME/
                   STATE), the RAW context switch, round-robin run-tasks,
                   mnew-task yield task-finish -- the exact mechanism from
                   r0_s1_m1_tests.c / examples/multitasking.r0.
    block helpers  block-len / block-at / block-set! and the managed-heap
                   `alloc` entry, as used by demo/shop/common.glon.

## 2. The one local addition: a scheduler wait flag

A blocked task must (a) not be selected by the scheduler yet (b) still be
scanned as a GC root. M1 has no blocked state. v0.03 adds a **wait flag in
task-record cell 9**:

    task-block   saves the task's machine state exactly like yield, sets the
                 wait flag, and returns to the scheduler.  STATE stays
                 RUNNABLE (0), so the collector still scans the suspended task
                 (mark/sweep roots task records with STATE == RUNNABLE).
    run-tasks    selects a task only when STATE == RUNNABLE AND wait flag == 0.
    task-deliver (called by OUT) writes the delivered tuple into the task's
                 record cells 10..15, clears the wait flag, and the task
                 becomes selectable again.

This is the smallest local mechanism that makes blocking real without moving
scheduling semantics into C or S1.

## 3. Tuple value model (v0.03, explicitly restricted)

A tuple field may be:

    INTEGER   (INT)
    NONE
    a word-family value (WORD / SET-WORD / GET-WORD / LIT-WORD)

`OUT` REJECTS strings, blocks, closures, contexts and RAW values (test s10).
Their ownership/copy semantics are not defined for v0.03; rejecting them is
preferable to inventing deep-copy semantics.  Consequently v0.01's string and
nested-block equality cases are **deferred** (see §8).

All tuple/waiter storage therefore holds only **immediate tagged cells**, so
the raw (`65200..`) and record cells never hold a managed pointer.

## 4. Tuple-data equality (`tuple-equal?` -> per-field `=`)

Glon's `=` is type-safe identity: word-family by symbol id (case-sensitive),
ints/none by tagged value, blocks by identity.  For the supported field
domain this is exactly the R3 `tuple-equal?` law:

    wildcard "*" in a PATTERN  -> matches any one field
    otherwise                  -> Glon '=' with the tuple field
    lengths must match exactly

`*` is the distinguished wildcard word.  It is only special in pattern
position; a stored `*` is ordinary data.

## 5. Storage and matching

    arena    managed block of 16 slots; slot = a tuple block or NONE
    used     high-water mark (`arena-used`) so scans recurse only over used
             slots (keeps task return-stack depth small)
    rd?      oldest matching slot -> deep copy (new-tuple) or NONE
    in?      oldest matching slot -> deep copy, clear the slot, or NONE
    out      validate; store a fresh copy; run the service pass

Linear scans only.  No hash, no first-field index, no arity index.

## 6. Waiters and the blocking operations

A waiter is implementation metadata (never a tuple), stored in a managed
`waiters` block:

    state (1 waiting / 2 done), op (0 RD / 1 IN), arity,
    pattern fields 1..5, task handle

    rd-wait / in-wait:
        register a waiter (deep-copied pattern fields)
        run the deterministic service pass (completes it NOW if a match
            already exists)
        if still waiting: task-block, then task-result on resume
        return the delivered tuple copy

`OUT` service pass (atomic: the whole pass runs before any resumed task can
re-enter):

    publish the tuple, then scan waiters oldest to newest:
        nonmatching waiter  -> leave waiting, continue
        RD match            -> deliver a copy; tuple stays
        IN match            -> deliver a copy; remove exactly one
    (a nonmatching earlier waiter does not block a later matching one)

## 7. Executed laws and results (345 checks, 0 failures)

    S1  matching        exact / mismatch / arity / wildcard (positions) /
                        int-vs-int / type mismatch / case-sensitive symbol /
                        word-family equivalence
    S2  multiset        duplicates distinct; oldest match; rd? vs in?
    S3  RD/RD/IN/RD     A=[x 1], B=[x 1], C=[x 1] consumes, D still blocked
    S4  IN/RD/RD        A consumes; B and C remain blocked
    S5  nonmatching head B completes; A remains blocked
    S6  two takers      A=[job 1], B=[job 2]
    S7  true concurrency B observes A stopped at IN (a-stage == 1), A resumes
                        to a-stage == 2, C is served [finished b]
    S8  copy ownership  mutating the caller's tuple or a returned tuple does
                        not affect stored state
    S9  forced GC       stored tuple survives collect; a BLOCKED task survives
                        collect and resumes correctly
    S10 type rejection  block / string-literal-form fields are rejected

    determinism         the three ordering scenarios + two-taker are each
                        repeated 20 times -> 300 additional checks, identical

The true-concurrency test is the evidence R3 could not produce: task A really
stops at `in-wait`, task B runs while A is blocked, `out [go a]` makes A
runnable, and A resumes **after** its IN call.

## 8. GC / safepoint discipline

    every value stored in a raw cell or task record is an immediate tagged
        value (no managed pointer)
    all managed structures (arena, waiters, tuples, checks) are reachable
        from global words, hence from the global-context GC root
    a blocked task keeps STATE == RUNNABLE, so the collector scans its
        CTX/FRAME and stacks as roots
    the forced-GC tests run the real collector while a tuple is stored and
        while a task is blocked; the full suite also passes under
        AddressSanitizer (no C-level memory error)

## 9. Divergences from R3 v0.02

    task adapter    R3 stub -> Glon REAL block/wakeup (the point of v0.03)
    field domain    INT/NONE/word-family only; strings/blocks deferred
    space           a single global space (no explicit space argument)
    word equality   Glon `=` (symbol id, case-sensitive) is exactly the R3
                    case-sensitive law for the supported domain
    arity           up to 5 fields (delivered result fits the task record)

Out of scope for v0.03: hashing/indexes, timeouts, cancellation, priorities,
networking, persistence, captures/binding, host I/O.

## 10. Environment

    frozen S1       f90496c26dc45c7a387d8fc8639cd2781507d3d2  (unchanged)
    runtime         r0_s1_runtime.c, S1 above frozen substrate
    browser/JS/WASM none participated

## 11. Correctness repairs (found while porting to the browser)

Two real defects were found and fixed minimally in this file, with native
regression tests (section S11 of `tuple-space-v003-tests.glon`):

1. **Tuple slot reuse broke oldest-first ordering.** `out` reused the first
   empty arena slot, so `OUT [job 1]; OUT [job 2]; IN [job 1]; OUT [job 3]`
   put `[job 3]` in slot 0 and `rd? [job *]` then returned `[job 3]` instead of
   the older `[job 2]`.  Fix: tuple storage is **append-only**; when the arena
   is full, `compact-arena` shifts live tuples to the front preserving order
   (dropping holes).  A reused slot can no longer make a newer tuple look
   older.

2. **Repeated immediate RD/IN exhausted the fixed waiter arena.** `rd-wait` /
   `in-wait` registered a waiter *before* checking for an immediate match, so
   every immediate operation consumed a waiter slot; after 16 the waiter arena
   returned 0.  Fix: a non-allocating `find-slot` scan is performed first; an
   immediate match returns/consumes without touching the waiter arena.  Only
   genuine waits enqueue.

The native suite grew from 345 to **354 checks, 0 failures** (ASan clean).

## 12. Browser demonstration

A public standalone page demonstrates the same law with the **stock three-task
M1 layout** (no task-count expansion) and the existing G1A runtime:

    demo/shop/demos/linda.glon     self-contained Glon (scheduler + tuple space
                                   + S7 scenario + render + event bridge)
    demo/shop/build-linda.py       bundles it into linda.html
    demo/shop/linda.html           the page
    demo/shop/linda-host.js        browser host (WASM load, Step/Run/Reset pacing)
    demo/shop/linda_node_test.js   headless test of the page wiring

`linda.glon` is self-contained (no common.glon) because the standalone page's
source buffer is a fixed 16 KiB.  Its one scheduling addition is the per-task
wait flag plus a per-call step budget (cell 25033 = M1_S9): `run-steps` resumes
at most one task slice and returns, so each browser **Step** advances one real
task slice.  JS implements no Linda semantics — it only loads WASM/source and
paces `glon_event("linda-step")`.

The scenario is the S7 shape: A `OUT [started a]`, stage-a = 1, `IN [go a]`
(blocks); B runs, `RD [started a]` (observes stage-a = 1), `OUT [go a]` (wakes
A), `IN [finished a]` (blocks); C `IN [finished b]` (blocks); then A resumes
after its IN, stage-a = 2, `OUT [finished a]`, finishes; B and C follow.  The
page renders task states, tuple space, waiters, stage markers and the event
trace, all read from the real Glon task table / tuple space.

Public URL: `https://gchiu.github.io/rebol-substrate-experiment/shop/linda.html`
(published to `docs/shop/` by `.github/workflows/deploy-pages.yml`).

