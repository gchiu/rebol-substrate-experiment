# TUPLE-SPACE-V001.md — executable-semantics contract

A tiny deterministic Linda-like tuple space for **Oldes' Rebol3**.
v0.01 establishes storage, matching, ordering, destructive vs
non-destructive retrieval, and copy ownership **before** any concurrency.

    Rebol/Bulk 3.22.1 (2026-05-27 19:58:00 UTC)

## Laboratory relationship

The executable specification lives in **rebol-substrate-experiment** (the Glon
repository). It is currently executed using Oldes/Rebol3 from:

    D:\repos\tja-membership\dist\runtime-test\r3.exe

That binary is merely the **laboratory interpreter**. It is not part of the
Glon project and not part of the tuple-space design. The semantics below do not
depend on that path; only a local test invocation does. The helper executable
lives in another repository purely because that is where it was installed.

Files (in `experiments/tuple-space/`):

    tuple-space-v001.r3         implementation
    tuple-space-v001-tests.r3   executable law tests
    TUPLE-SPACE-V001.md         this contract

Run the tests (external interpreter, Glon-repository sources):

    D:\repos\tja-membership\dist\runtime-test\r3.exe ^
        D:\repos\rebol-substrate-experiment\experiments\tuple-space\tuple-space-v001-tests.r3

## 1. Representation

A tuple space is an object:

    kind:   'tuple-space
    tuples: []          an insertion-ordered block; each element is one tuple

`tuples` is an **ordered multiset**. Tuples are ordinary Rebol blocks.
Duplicate tuples are distinct entries. Retrieval uses a linear scan with no
index of any kind.

## 2. API

    make-tuple-space            -> a new empty space
    out   space tuple           -> store a deep copy; append at the end; returns none
    rd?   space pattern         -> copy of oldest match, or none; non-destructive
    in?   space pattern         -> copy of oldest match and remove it, or none

`out` uses `append/only`, so a tuple is stored as **one** element rather than
being spliced into the collection.

## 3. Wildcard token

The token is the word:

    *

`_` cannot be used. Oldes R3 lexes `_` as `none!`, and `'_` is rejected as an
invalid word-literal. Using `*` keeps the wildcard an ordinary word and leaves
`none` as ordinary comparable data.

Law: in a **pattern**, `*` always means "match any one value". A literal `*`
cannot be exact-matched in v0.01; an escaping mechanism is deferred.

## 4. Matching law

Patterns and tuples are **data**; nothing is evaluated.

    same tuple/pattern length is required

    for each position i:
        if pattern[i] is the wildcard word  -> match (any single value)
        otherwise                           -> tuple-equal? pattern[i] tuple[i]

### 4.1 `tuple-equal?` — the field equality law

Field equality is a small explicit predicate, chosen after auditing equality
in this exact build (see "Equality audit" below). It is deliberately **not**
REBOL `=` and **not** REBOL `==`.

The word domain is exactly `word!`, `lit-word!`, `get-word!`, `set-word!`.
`issue!` is **not** included, even though R3's `any-word?` accepts it. Values in
the word domain compare by case-preserved spelling, independently of binding:

    job = 'job = :job = job:          but          JOB <> job

The spelling is obtained with `to-string`, which returns the case-preserved
bare spelling for every word family and does not depend on binding.

    tuple-equal? a b:
        if both a and b are in the word domain
                                       -> strict-equal? to-string a to-string b
        if type? a is not type? b      -> false
        if a is a block                -> same length, then
                                          tuple-equal? on each element
        otherwise                      -> strict-equal? a b

Properties:

    binding-independent at every nesting level
    type-safe: different datatypes never match (1 does not match 1.0)
    strings compare case-sensitively
    word-family compares by case-sensitive spelling, binding ignored
    blocks compare structurally, recursively under this same law
    none/logic compare exactly
    no evaluation, no coercion, no captures

### 4.2 Why not R3 `=` or `==`

Measured in `Rebol/Bulk 3.22.1` (see the audit matrix):

    expression                 =        ==
    1 vs 1.0                   true     false
    "ABC" vs "abc"             true     false
    'ABC vs 'abc               true     false
    [1 "A"] vs [1 "a"]         true     false
    word bound in ctx A vs
      same spelling in ctx B   true     false

`=` is binding-independent but too loose: it coerces numeric types and ignores
case. `==` is type/case strict but compares the **binding** of bare words, so
it is not binding-independent. Worse, `==` is inconsistent: for words nested
inside a block it ignores binding, but for bare words it compares binding.

### 4.3 Equality audit (this build)

    Rebol/Bulk 3.22.1, Oldes/Rebol3

    -- top level --
    1 = 1.0                true     1 == 1.0                false
    "ABC" = "abc"          true     "ABC" == "abc"          false
    'ABC = 'abc            true     'ABC == 'abc            false
    none = none            true     none == none            true
    true = true            true     true == true            true
    [1 2] = [1 2]          true     [1 2] == [1 2]          true
    [1 "A"] = [1 "a"]      true     [1 "A"] == [1 "a"]      false

    -- words bound to different contexts, same spelling --
    wa = wb                true
    wa == wb               false
    same? wa wb            false
    wa == wa2 (same ctx)   true

    -- word-family (word/lit/get/set) --
    word = lit-word        true     word == lit-word        false
    word = get-word        true     word == get-word        false
    word = set-word        true     word == set-word        false

    -- binding inside blocks is ignored by == --
    [wa] == [wb]           true     (wa, wb differently bound)
    [[wa]] == [[wb]]       true

    -- blocks are structural, not identity --
    [1 2] == [1 2]         true     same? [1 2] [1 2]        false

    -- word spelling is case-preserved and extractable binding-free --
    mold 'ABC                      "ABC"
    mold [Job job JOB]             "[Job job JOB]"   (each spelling kept)
    to-string to-word "Job"        "Job"
    to-string to-lit-word "Job"    "Job"
    to-string to-get-word "Job"    "Job"    (leading ":" stripped)
    to-string to-set-word "Job"    "Job"    (trailing ":" stripped)
    strict-equal? (to-string to-word "Job") (to-string to-word "job")   false

    -- any-word? covers word!/lit-word!/get-word!/set-word!/issue! --
    -- so a separate word-domain predicate excludes issue! --

## 5. Copy / ownership law

The space **owns** its stored tuples.

    out            stores copy/deep of the caller's tuple
    rd?  / in?     return copy/deep of the stored tuple

Consequences (proved by tests):

    t: [job 17 pending]
    out space t
    t/3: 'cancelled           ; stored tuple is unchanged

    r: rd? space [job * pending]
    r/2: 999                  ; stored tuple is unchanged

## 6. Ordering law

Retrieval scans **oldest to newest**; the oldest matching tuple wins.

    out space [job 1 pending]
    out space [job 2 pending]
    out space [job 3 pending]
    rd? space [job * pending]   -> [job 1 pending]
    in? space [job * pending]   -> [job 1 pending], removed
    rd? space [job * pending]   -> [job 2 pending]

## 7. Multiset law

Duplicates are distinct entries and are never merged.

    out space [x 1]
    out space [x 1]
    in? space [x 1]        ; removes exactly one
    rd? space [x 1]        ; still finds the remaining one

## 8. No captures, no binding

`rd?`/`in?` return only the whole matching tuple. No binding map, no captured
values, no caller assignment. The caller inspects fields itself.

No binding is used anywhere in the implementation, and the test comparator is
binding-insensitive.

## 9. Input / error law

Fail fast; no coercion.

    out/rd?/in? with a non-block tuple/pattern  -> script error (type spec)
    space that is not an object                  -> script error (type spec)
    object not marked kind: 'tuple-space         -> cause-error 'invalid-arg

`cause-error 'script 'invalid-arg` is the only explicit error raised.

## 10. Deliberately omitted

    blocking rd / in
    wait queues, scheduler, promises, callbacks, tasks
    networking
    persistence
    captures / binding
    hashing, first-field index, arity index, trie, tree, cache
    evaluation of tuple or pattern contents

## 11. Natural first future index (not implemented)

`*` is wildcard; the first element of a tuple is normally a constant "kind"
word (`job`, `state`, `claim`, `result`). So the natural first index is a map
from first field / tuple kind to the ordered list of tuple positions. It is
deliberately **not** implemented in v0.01.

## 12. Oldes R3 laboratory quirks encountered

These shaped the implementation. They are properties of the laboratory
interpreter, not of the tuple-space design.

    `_`            lexes as none!; '_ is an invalid word-lit -> wildcard is *
    append         splices a block's contents -> tuples need append/only
    strict-equal?  compares the binding of bare words (and is inconsistent
                   for words nested inside blocks) -> custom tuple-equal?
    func           does NOT auto-localise body set-words in this build;
                   every body variable must be declared with /local or it
                   leaks to the global context and can clobber other
                   functions mid-call. All prototypes use explicit /local.
    to-string      returns the case-preserved bare spelling of any word
                   family, independently of binding (enables case-sensitive,
                   binding-free word comparison)
