# TUPLE-SPACE-V002.md — waiting semantics contract

v0.02 adds Linda-style **RD** and **IN** waiting on top of the v0.01
tuple-space laws. Storage, matching, ordering, copy ownership and the
nonblocking `rd?`/`in?` operations are unchanged and defined in
`TUPLE-SPACE-V001.md`.

Files (in `experiments/tuple-space/`):

    tuple-space-v002.r3         v0.01 + deterministic waiter engine
    tuple-space-v002-tests.r3   waiting law tests
    TUPLE-SPACE-V002.md         this contract

`tuple-space-v002.r3` loads `tuple-space-v001.r3` for the v0.01 laws, so
there is a single source of truth for representation and equality.

## 1. Operations

    rd?  space pattern     nonblocking read; oldest match copy or none
    in?  space pattern     nonblocking take; oldest match copy or none
    rd-wait space pattern  RD: copy of oldest match, waiting if absent
    in-wait space pattern  IN: copy of oldest match and consume, waiting if absent
    out  space tuple       store a copy, then service waiters

`rd-wait`/`in-wait` retain the conceptual Linda names RD and IN; the R3
spelling is a prototype detail and does not fix eventual Glon vocabulary.

## 2. No cooperative scheduler in this R3 build

This exact build has **no** cooperative task scheduler:

    INCLUDE_TASK     disabled ("tasks are not implemented yet")
    task!            created, but Do_Task is a no-op
    wake-up          port/event function, not task scheduling
    do-events        blocking GUI event loop
    yield/resume     absent

Therefore a real "suspend the current task" cannot be performed, and this
prototype does **not** fake blocking, busy-loop, poll, or sleep-loop.
Instead:

    rd-wait / in-wait return a WAITER object;
    if the waiter is still `waiting`, a real host would suspend the task;
    when the waiter is served, the host would resume it.

The adapter boundary is marked by two identity stubs, `suspend-task` and
`resume-task`. In Glon these become real task operations; here they do
nothing. This is the **one missing adapter** between the R3 executable
specification and true blocking Linda.

## 3. Waiter representation

A waiter is implementation metadata, never a tuple:

    op:      'rd or 'in
    pattern: a deep copy of the request pattern
    state:   'waiting or 'done
    result:  the delivered tuple copy, or none while waiting

Waiters are held in `space/waiters`, an insertion-ordered block. Arrival
order is the order of `rd-wait`/`in-wait` calls. Completed waiters remain
in the list but are skipped by the service pass (so tests can inspect
results).

    waiter-done?   waiter   -> logic
    waiter-result  waiter   -> tuple copy or none

## 4. RD / IN

    rd-wait space pattern:
        try rd? immediately
        if match: complete the waiter now with the copy; return it
        else:     deep-copy pattern, enqueue a 'waiting waiter, return it

    in-wait space pattern:
        try in? immediately
        if match: complete the waiter now with the copy; return it
        else:     deep-copy pattern, enqueue a 'waiting waiter, return it

RD is non-destructive; IN consumes exactly one matching tuple.

## 5. OUT service algorithm

    out space tuple:
        append/only a deep copy of tuple to space/tuples
        service waiters, oldest to newest:
            for each waiter still 'waiting:
                if it cannot match the current space: leave it waiting
                if op is RD and it matches: deliver a deep copy; tuple stays
                if op is IN and it matches: deliver a deep copy; remove one
                (a nonmatching earlier waiter does not block a later one)
        return

The whole service pass runs to completion before `out` returns, so no
resumed task can re-enter tuple-space operations midway.

## 6. Defining ordering cases

    A: RD [x 1]
    B: RD [x 1]
    C: IN [x 1]
    D: RD [x 1]
    OUT [x 1]
    =>
    A = [x 1], B = [x 1], C = [x 1] (consumed), D remains waiting

    A: IN [x 1]
    B: RD [x 1]
    C: RD [x 1]
    OUT [x 1]
    =>
    A consumes [x 1]; B and C remain waiting

    A: IN [x 2]
    B: IN [x 1]
    OUT [x 1]
    =>
    B completes; A remains waiting   (head-of-line does not block)

    A: IN [job *]
    B: IN [job *]
    OUT [job 1]
    OUT [job 2]
    =>
    A = [job 1], B = [job 2]        (oldest waiter + oldest tuple)

## 7. Copy ownership

    the queued pattern is a deep copy, so later mutation of the caller's
        pattern cannot change what a waiter matches
    RD delivers a deep copy; the stored tuple is untouched
    IN delivers a deep copy; the stored tuple is removed
    no caller or waiter receives a direct reference to internal storage

## 8. Still no binding, no evaluation, no captures

Patterns and tuples remain pure structural data. The wildcard is `*`,
valid only in pattern position. The matcher never binds, assigns, captures
or evaluates. Field equality is v0.01's `tuple-equal?`.

## 9. Natural first future index (still not implemented)

Linear scans remain. The likely first index is still first-field / tuple
kind (`job`, `state`, `claim`, `result`), with arity as a possible
secondary key. Not implemented in v0.02.
