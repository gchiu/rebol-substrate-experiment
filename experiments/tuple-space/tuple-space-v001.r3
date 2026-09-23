REBOL [
    Title:   "Tuple Space v0.01"
    Purpose: {
        Smallest deterministic Linda-like tuple space.
        Storage, matching, ordering and copy ownership only.
        No blocking, no binding, no captures, no indexing, no evaluation.
    }
]

; ---------------------------------------------------------------------
; Wildcard sentinel
; ---------------------------------------------------------------------
; The usual token "_" cannot be used in Oldes R3: the loader turns "_"
; into none!, and the literal '_ is rejected as an invalid word-lit.
; "*" is an ordinary word, so wildcard recognition stays an explicit
; structural rule and none remains an ordinary comparable value.
wildcard-token: to-word "*"

; ---------------------------------------------------------------------
; Representation: an insertion-ordered multiset of tuples
; ---------------------------------------------------------------------

make-tuple-space: does [
    make object! [
        kind:   'tuple-space
        tuples: copy []
    ]
]

check-space: func [
    "Fail fast unless SPACE is an object marked as a tuple-space."
    space [object!]
][
    unless true? attempt [space/kind = 'tuple-space] [
        cause-error 'script 'invalid-arg "argument is not a tuple-space"
    ]
]

; ---------------------------------------------------------------------
; Matching: patterns are data; no binding, no evaluation
; ---------------------------------------------------------------------
; tuple-equal? is the tuple-field comparison law chosen from the R3
; equality audit (see TUPLE-SPACE-V001.md section 4). It is deliberately
; NOT R3's = (too loose: coerces 1 to 1.0, ignores string/word case) and
; NOT R3's == (compares word *binding* for bare words, and is
; inconsistent for words nested in blocks).
;
; Word domain: word!/lit-word!/get-word!/set-word! only (not issue!).
; They compare by case-preserved spelling, binding-independently, so
;     job = 'job = :job = job:      but      JOB <> job
; to-string yields the case-preserved bare spelling for every word
; family and does not depend on binding.
;
;   - binding-independent at every nesting level
;   - type-safe: different datatypes never match, so 1 does not match 1.0
;   - strings compare case-sensitively
;   - blocks compare structurally, recursively under this same law

tuple-word?: func [
    "True for word!/lit-word!/get-word!/set-word! only (excludes issue!)."
    v
    /local t
][
    t: type? v
    any [t = word!  t = lit-word!  t = get-word!  t = set-word!]
]

tuple-equal?: func [
    "Binding-independent, type-safe data equality for tuple fields."
    a
    b
    /local i
][
    either all [tuple-word? a tuple-word? b] [
        strict-equal? to-string a to-string b
    ][
        unless equal? type? a type? b [return false]
        either block? a [
            unless equal? length? a length? b [return false]
            repeat i length? a [
                unless tuple-equal? pick a i pick b i [return false]
            ]
            true
        ][
            strict-equal? a b
        ]
    ]
]

tuple-match?: func [
    "True when TUPLE matches PATTERN: equal length; wildcard matches one value; others tuple-equal?."
    tuple   [block!]
    pattern [block!]
    /local i p
][
    unless equal? length? tuple length? pattern [return false]
    repeat i length? tuple [
        p: pick pattern i
        unless tuple-equal? p wildcard-token [
            unless tuple-equal? p pick tuple i [return false]
        ]
    ]
    true
]

; ---------------------------------------------------------------------
; Operations
; ---------------------------------------------------------------------

out: func [
    "Append a deep copy of TUPLE to SPACE. The space owns its copy."
    space [object!]
    tuple [block!]
][
    check-space space
    append/only space/tuples copy/deep tuple
    none
]

rd?: func [
    "Return a deep copy of the oldest tuple matching PATTERN, or none. Non-destructive."
    space   [object!]
    pattern [block!]
    /local t
][
    check-space space
    foreach t space/tuples [
        if tuple-match? t pattern [return copy/deep t]
    ]
    none
]

in?: func [
    "Remove and return a deep copy of the oldest tuple matching PATTERN, or none."
    space   [object!]
    pattern [block!]
    /local i t result
][
    check-space space
    repeat i length? space/tuples [
        t: pick space/tuples i
        if tuple-match? t pattern [
            result: copy/deep t
            remove at space/tuples i
            return result
        ]
    ]
    none
]
