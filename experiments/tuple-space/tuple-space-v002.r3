REBOL [
    Title:   "Tuple Space v0.02"
    Purpose: {
        v0.01 storage, matching, ordering and copy ownership, plus
        deterministic Linda waiting (RD, IN). No polling. No scheduler is
        available in this Oldes R3 build, so the waiter engine is explicit:
        rd-wait / in-wait park a waiter object instead of blocking a task.
    }
]

; Single source of truth for representation, equality, ordering and
; copy ownership. v0.02 adds only the waiting layer.
do to-file join system/script/path %tuple-space-v001.r3

; ---------------------------------------------------------------------
; Scheduler adapter seam
; ---------------------------------------------------------------------
; This exact Oldes R3 build has NO cooperative task scheduler:
;   - INCLUDE_TASK is disabled ("tasks are not implemented yet"), so
;     task! values are created but Do_Task is a no-op;
;   - wake-up is a port/event function, not task scheduling;
;   - do-events is a blocking GUI event loop.
; A real "suspend the current task" is therefore impossible here.
;
; Instead, rd-wait / in-wait return the waiter object. A host that has a
; cooperative scheduler would suspend the task when the waiter is still
; waiting, and resume it when the waiter completes. The two hooks below
; mark that adapter boundary. In this laboratory they are identity stubs;
; they do NOT fake blocking and they never busy-wait or poll.
suspend-task: func [
    "Adapter seam: would yield the current task until WAITER completes."
    waiter [object!]
][
    waiter
]

resume-task: func [
    "Adapter seam: would make the task suspended on WAITER runnable."
    waiter [object!]
][
    waiter
]

; ---------------------------------------------------------------------
; Waiter representation (implementation metadata, never a tuple)
; ---------------------------------------------------------------------

make-tuple-space: does [
    make object! [
        kind:    'tuple-space
        tuples:  copy []
        waiters: copy []
    ]
]

new-waiter: func [
    "A parked RD/IN request. PATTERN is deep-copied on entry."
    op
    pattern [block!]
    /local w
][
    w: make object! [
        op:      none
        pattern: none
        state:   'waiting
        result:  none
    ]
    w/op: op
    w/pattern: copy/deep pattern
    w
]

waiter-done?: func [
    "True once the waiter has been served."
    waiter [object!]
][
    waiter/state = 'done
]

waiter-result: func [
    "The delivered tuple copy, or none while still waiting."
    waiter [object!]
][
    waiter/result
]

; ---------------------------------------------------------------------
; Deterministic service pass
; ---------------------------------------------------------------------

service-waiters: func [
    {
        Serve every waiting waiter, oldest to newest. A nonmatching earlier
        waiter does not block a later matching waiter. RD leaves the tuple;
        IN removes exactly one. The pass always runs to completion before
        returning, so no resumed task can re-enter midway.
    }
    space [object!]
    /local w m
][
    foreach w space/waiters [
        if w/state = 'waiting [
            either w/op = 'rd [
                m: rd? space w/pattern
            ][
                m: in? space w/pattern
            ]
            if m [
                w/result: m
                w/state: 'done
            ]
        ]
    ]
]

; ---------------------------------------------------------------------
; Operations
; ---------------------------------------------------------------------

; OUT keeps the v0.01 storage law, then services waiters.
out: func [
    "Append a deep copy of TUPLE, then service waiting RD/IN requests."
    space [object!]
    tuple [block!]
][
    check-space space
    append/only space/tuples copy/deep tuple
    service-waiters space
    none
]

rd-wait: func [
    "RD: deep copy of oldest match. Immediate if present, else park a waiter."
    space   [object!]
    pattern [block!]
    /local w m
][
    check-space space
    w: new-waiter 'rd pattern
    m: rd? space pattern
    either m [
        w/result: m
        w/state: 'done
    ][
        append space/waiters w
        suspend-task w          ; adapter seam: would yield here
    ]
    w
]

in-wait: func [
    "IN: deep copy of oldest match and consume it. Immediate if present, else park a waiter."
    space   [object!]
    pattern [block!]
    /local w m
][
    check-space space
    w: new-waiter 'in pattern
    m: in? space pattern
    either m [
        w/result: m
        w/state: 'done
    ][
        append space/waiters w
        suspend-task w          ; adapter seam: would yield here
    ]
    w
]
