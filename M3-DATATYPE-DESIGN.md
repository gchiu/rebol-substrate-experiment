# M3-DATATYPE-DESIGN.md — extensible datatypes above the frozen R0/S1 architecture

**Status:** design + implemented (M3A). Revision 4 records the two hardening
changes applied after M3A review (explicit T_USER heap-range check with
fail-stop; fail-stop COUNT validation, no clamping), the final relocated memory
layout, and the deferred recursive-datatype limitation. See §10, §19 and
§"Recursive datatypes".

---

## 0. Executive summary

M3 provides extensible **structured datatypes** whose content cells are tagged
R0 values. The unified model is:

- **Every value has a datatype descriptor.** Immediates map (by frozen tag) to
  canonical built-in descriptors (`integer!`, `word!`, …); user-defined values
  carry their descriptor in `payload[0]`.
- **`datatype!` is the meta-datatype descriptor value** — an ordinary descriptor
  whose own type is `datatype!` — **not** a constructor.
- **`make` is the constructor.** `make datatype! [spec]` builds a descriptor;
  `make D [values]` builds a value of type `D`.
- **`type? v` always returns a descriptor**; **`accepts? type value`** is
  descriptor identity.
- A datatype descriptor's content is an interleaved `(field-name, expected-type)`
  list; construction and mutation validate with `accepts?`.
- Validation failure is an **existing result convention** (NONE), not a typed
  exception.

The collector gains exactly one generic trace path (`T_USER` + `GC_KIND_USER` +
`trace_user`) plus explicit permanent roots for the bootstrap descriptor set.
S1 stays frozen; the evaluator and parser are not modified; no native id or
HOST service is added.

---

## 1. Current R0 value representation

R0 stores every value as a single S1 cell (`intptr_t`) with a **4-bit low tag**:

```
value = (payload << 4) | tag          for small immediates (INT, WORD, NATIVE)
        (16-aligned pointer) | tag     for heap kinds (BLOCK, CONTEXT, CLOSURE, RAW)
```

Tagging uses only `HOST` arithmetic already in S1 (`MOD 16`, `DIV 16`, `MUL 16`,
`SUB (v mod 16)`). The tag is the *only* distinction; the evaluator has no
runtime type table.

Live tags (`r0_s1.h:19`):

| tag | type | payload |
|---|---|---|
| 0 | INT | signed `<< 4` |
| 1 | NONE | the atom `0x1` |
| 2 | WORD | symbol id `<< 4` |
| 3 | SET-WORD | symbol id `<< 4` |
| 4 | GET-WORD | symbol id `<< 4` |
| 5 | LIT-WORD | symbol id `<< 4` |
| 6 | BLOCK | 16-aligned heap pointer |
| 7 | CONTEXT | 16-aligned heap pointer |
| 8 | CLOSURE | 16-aligned heap pointer |
| 9 | NATIVE | host-function id `<< 4` |
| 10 | RAW | 16-aligned heap pointer (assembled S1 fragment) |

The evaluator's `emit_subexpr` (`r0_s1_runtime.c:1231`) special-cases only tags
2/3/4/5; every other tag falls through to "self-evaluating literal"
(`r0_s1_runtime.c:1328`), returning `[elem, 1]`. A new tag therefore
self-evaluates with no evaluator change.

---

## 2. Current tag-space constraints

Tags 0..10 are occupied; 11..15 are free. M3 consumes **exactly one** free tag
for *all* user-defined values and datatype descriptors:

| tag | meaning |
|---|---|
| **11** | `T_USER` — a generic managed user value (instance) or a datatype descriptor |

Tags 12..15 remain reserved for **future intrinsic representations** (e.g. a
`string!` tag). The `BUILTIN_TYPE` table is therefore sized at **16 slots now**
(§5, §11), so that adding a descriptor for tag 12..15 never changes the `type?`
algorithm or the table shape — only a previously-empty slot is filled.

---

## 3. Current GC object/header constraints

Non-moving, stop-the-world, exact, mark/sweep over `[32768, 40000)`. Managed
objects have a 16-cell-aligned header before a 16-aligned payload:

```
B+0  size    : total extent (multiple of 16; = 16 + payload cells)
B+1  flags   : bit0 allocated, bit1 marked, bits2.. kind<<2
B+2..B+15    : unused
payload = B + GC_HDR_STRIDE (= B + 16)
```

Kinds (`r0_s1.h:117`): `BLOCK=0 CTX=1 CLOSURE=2 RAW=3 FRAME=4`.

Two binding facts: (1) the collected heap currently holds only CLOSURE, child
CONTEXT, and FRAME; loader BLOCKs never contain a collected pointer and are not
traced; (2) mark is tag-driven (`emit_mark_value`, `r0_s1_runtime.c:461`) — it
knows only CONTEXT (7) and CLOSURE (8), dropping all other tags. The collector
must learn **one** new tag → one kind → one trace routine, and nothing
datatype-specific.

---

## 4. Candidate architectures

(Unchanged from revision 1; retained for the record.)

- **A — pointer bitmap in the descriptor.** Exact, safe, but only needed if
  fields can be untagged raw data. In this design every field is a tagged value,
  so a bitmap is unnecessary.
- **B — field/type metadata interpreted by the GC.** Unnecessary complexity:
  the GC must not interpret the type layer; `mark_value` already disambiguates
  via tags.
- **C — descriptor-supplied RAW trace routine.** Rejected: running RAW code
  during marking breaks collector reentrancy/safety.
- **D (recommended) — "all fields tagged; trace by tag".** One `T_USER` tag,
  one `GC_KIND_USER`, one `trace_user` that marks the descriptor and then
  `mark_value`s each content cell. No bitmap, no type interpretation, no RAW
  during marking.

---

## 5. Built-in datatype descriptors (the unified foundation)

To make the *same* checking operation work for `integer!`/`word!` (tag-based)
and `person!`/`vector!` (descriptor-defined), built-in types must *also* be
descriptors.

**Built-in datatype descriptors are required.** They are ordinary `T_USER`
objects — one **canonical** descriptor per frozen tag — created at bootstrap.
They do **not** rewrite the value system: the tag scheme and immediate layouts
(`n<<4`, `id<<4`) are untouched; the descriptors are additional objects that
*denote* the existing tags.

Built-in descriptor layout (identical in shape to any user object):

```
payload:  [0] desc  = datatype! (the meta-descriptor)
          [1] count = 0                 (no field spec; it denotes a tag)
```

### The `BUILTIN_TYPE` table (16 slots, permanent root)

The association *tag → built-in descriptor* is a **fixed 16-slot table**
`BUILTIN_TYPE[0..15]`, seeded by the loader and **scanned by the collector as a
permanent root** (§10). It is bounded by the frozen tag set, so it is *not* a
switch that grows per user datatype.

| slot | tag | word | contents |
|---|---|---|---|
| 0 | INT | `integer!` | built-in descriptor |
| 1 | NONE | `none!` | built-in descriptor |
| 2 | WORD | `word!` | built-in descriptor |
| 3 | SET | `set-word!` | built-in descriptor |
| 4 | GET | `get-word!` | built-in descriptor |
| 5 | LIT | `lit-word!` | built-in descriptor |
| 6 | BLOCK | `block!` | built-in descriptor |
| 7 | CONTEXT | `context!` | built-in descriptor |
| 8 | CLOSURE | `closure!` | built-in descriptor |
| 9 | NATIVE | `native!` | built-in descriptor |
| 10 | RAW | `raw!` | built-in descriptor |
| 11 | T_USER | — | **empty (NONE)**; `type?` special-cases tag 11 |
| 12..15 | reserved | — | **empty (NONE)**; filled only by future intrinsic tags |

`datatype!` (the meta-descriptor) is **not** a slot in this table; it is the
`desc` of every descriptor and is rooted separately (§10).

---

## 6. Recommended minimal architecture

1. One tag `T_USER = 11` for user values **and** descriptors.
2. A value is `[desc, count, fields…]`; a descriptor is `[meta, count,
   (name,type)…]`. Every content cell is a tagged value.
3. Built-in types are canonical descriptors in the fixed 16-slot
   `BUILTIN_TYPE` table (§5), rooted permanently (§10).
4. `datatype!` is the meta-descriptor value; `make` is the constructor (§13).
5. `type?` maps every value to a descriptor; `accepts?` is descriptor identity
   (§11).
6. The collector gains `GC_KIND_USER = 5` and a generic `trace_user` (§10); no
   concrete datatype knowledge.
7. `make`, `type?`, `accepts?`, `field`, `field-set!`, and predicates are
   ordinary GLON over generic RAW primitives (§16).

Capability → mechanism:

1. create a datatype → `make datatype! [x: integer! …]` builds a descriptor.
2. construct values → `make D [ … ]` builds + validates a value.
3. check type → `type? v` (uniform, §11).
4. field access → `field v 'x` (§17).
5. nesting → a field is a tagged pointer; GC traces it generically.
6. exact GC → `trace_user` (generic, §10).
7. reclamation → `GC_KIND_USER` header; sweep frees it.
8. no datatype-specific C/evaluator changes.

### Scope boundary

M3 provides extensible **structured datatypes** whose content cells are tagged
R0 values. It does **not** provide arbitrary user-defined *physical* storage
representations (e.g. packed bytes, unboxed numeric arrays, foreign memory).
That is a future layer; M3's tagged-content model is sufficient for VECTOR,
PERSON, RELATIONSHIP, SQL-like rows, and future GOB values.

---

## 7. Exact value representation

`T_USER = 11`; `mk_user(p) = p + 11` for a 16-aligned payload pointer.

```
payload (16-aligned):
  [0] desc   : tagged T_USER — the datatype descriptor
  [1] count  : raw integer N — number of content (field) cells
  [2..2+N-1] : N tagged R0 field values
```

Payload cell count `n = align16(2 + N)`. `count` is stored raw (untagged), as
BLOCK/CONTEXT store their counts.

---

## 8. Exact datatype-descriptor representation

A **user datatype descriptor** is a `T_USER` object whose content is an
interleaved list of `(field-name, expected-type)`:

```
payload (16-aligned):
  [0] desc   : tagged T_USER = datatype! (the meta-descriptor)
  [1] count  : raw integer 2N — number of content cells
  [2..2+2N-1]: content =  name0, type0, name1, type1, …, name(N-1), type(N-1)
               name_i is a tagged WORD
               type_i is a tagged datatype descriptor (built-in or user)
```

- `N = count / 2` is the number of fields.
- Each `type_i` is a **resolved descriptor value** (built-in or user), captured
  at definition time by ordinary context lookup — so identity is fixed at
  creation and is not re-resolved later (§12).
- The meta-descriptor `datatype!` is a managed `T_USER` with `desc = itself` and
  `count = 0`. It is **an ordinary descriptor value**, not a constructor; its
  type is `datatype!` (self-loop).

`count` is defined uniformly as "number of tagged content cells" for both
roles: a value has `N` content cells; a descriptor has `2N`. The collector
scans exactly `count` cells and needs no role knowledge; the GLON layer derives
field count as `N` (value) or `count/2` (descriptor).

---

## 9. Exact user-value object layout

See §7. Header precedes the payload:

```
B+0  size  = align16(2+N) + 16
B+1  flags = GC_FLAG_ALLOC | GC_KIND_USER<<2
payload:
  B+16+0  desc   (tagged T_USER)
  B+16+1  count  (raw N)
  B+16+2 .. B+16+2+N-1   N tagged field values
```

No per-object type tag, hash, or vtable; the descriptor pointer *is* the type.

---

## 10. Exact GC tracing mechanism and bootstrap roots

### Three generic additions

1. `mark_value` gains a `T_USER` case that **requires** the payload pointer to
   lie inside the managed heap before `mark_push`. An out-of-range payload is
   fail-stop heap corruption, not silently skipped and not blindly pushed:

```
if tag == T_USER:
    p = v - T_USER
    if p < GC_HEAP_BASE  or  p >= GC_HEAP_LIMIT:  HALT (corrupt T_USER)
    mark_push(p)
```

   There are no loader/out-of-heap user objects, so every legitimate `T_USER`
   payload is in `[GC_HEAP_BASE, GC_HEAP_LIMIT)`; anything else is corruption.

2. `trace_user(p)` — leaf mark/trace routine reached **only** from the drain
   loop, so it uses the `GC_T6..GC_T8` scan/drain partition for its locals (no
   `>R`/`R>` saves needed). **It verifies `count` against the allocated block
   extent and fail-stops on corruption — it never clamps:**

```
trace_user(p):
    size  = M[p - GC_HDR_STRIDE + GC_HDR_SIZE]   # header size = p-16
    count = M[p+1]
    if count > size - (GC_HDR_STRIDE + 2): HALT (corrupt count)   # count > size-18
    mark_value(M[p])              # descriptor (tagged T_USER)
    for i in 0..count-1:
        mark_value(M[p+2+i])      # each content cell (tagged)
```

3. The drain loop (`r0_s1_runtime.c`) gains a `GC_KIND_USER` case calling
   `trace_user`.

The invariant these two checks preserve: **any object that survives GC has a
structurally valid USER layout** (payload in range, `count` within capacity).

### Explicit permanent roots for the bootstrap descriptor set

The 11 built-in descriptors and `datatype!` are managed objects; global word
bindings are mutable and therefore **not** sufficient roots. One explicit,
permanent root is added to `emit_collect`'s root phase:

- **`BUILTIN_TYPE` table** — the collector scans `[BUILTIN_BASE,
  BUILTIN_BASE+16)` as tagged values (the same `scan_values` used for the data
  stack). Slots hold the canonical descriptor values; empty slots hold NONE
  (harmlessly skipped).

Consequences:

- **Rebinding `integer!` (or any built-in type word) in the global context
  cannot collect its canonical descriptor**, because `type?` reads
  `BUILTIN_TYPE[tag]`, and that table is a permanent root independent of word
  bindings.
- `datatype!` (the meta-descriptor) is additionally reachable transitively:
  every rooted built-in descriptor's `desc` field points at it, and every user
  descriptor's `desc` points at it too. No separate meta root is needed; the
  `GC_META` *cell* still holds the meta value for the library to read, but the
  collector roots the meta through `BUILTIN_TYPE`.

### Bootstrap memory layout (deterministic)

`r0_s1_run` seeds, in order, after `s1_reset` (which sets `HP = 32768`):

| address | object |
|---|---|
| 32768..32799 | `datatype!` meta-descriptor (payload 32784) |
| 32800..32831 | built-in descriptor tag 0 (`integer!`, payload 32816) |
| … | one 32-cell object per tag 1..10, payload `32816 + 32*i` |
| 33152 | `REG_HP` after seeding (32768 + 12×32) |

`BUILTIN_TYPE[i] = mk_user(32816 + 32*i)` for `i in 0..10`; slots 11..15 are
NONE. `GC_META = mk_user(32784)`.

The table itself lives in a fixed, disjoint region outside the managed heap:
`GC_META = 8567` and `BUILTIN_BASE = 8568` (16 cells: 8568..8583), with the
datatype-library RAW scratch `SCRATCH_C = 8584`, `SCRATCH_D = 8585`. These sit
just below the relocated RV register file.

### Relocated runtime-state layout (M3A final)

The emitted evaluator+collector code grew past the original RV boundary (8192)
once `trace_user`, the datatype-library RAW fragments, and the D1/M1 fragments
were assembled, so the runtime-state regions were relocated upward:

| region | final address |
|---|---|
| emitted code | `256 .. ~8409` (grows to ≤ 8567) |
| `GC_META` | 8567 |
| `BUILTIN_BASE` (16 slots) | 8568..8583 |
| `SCRATCH_C` / `SCRATCH_D` | 8584 / 8585 |
| RV register file (`RV_BASE`) | 8586..8641 |
| `SCRATCH_A/B/E/F` | 8642..8645 |
| D1 debugger state records | 8850..8878 (unchanged) |
| M1 cells | 9001..9024 |
| GC state (incl. 256-entry worklist) | 9025..9305 |

All existing tests use symbolic ABI names (`RV_CUR`, `SCRATCH_A`, …), so the
relocation is transparent to them.

---

## 11. Unified TYPE? / ACCEPTS?

`type?` is a **generic RAW primitive** (type reflection, like `untag`), not a
per-datatype operation. Its algorithm is fixed over the **16-slot** table:

```
type? ( v -- descriptor ):
    t = v mod 16
    if t == T_USER (11):  return M[v - T_USER]        # user value -> payload[0] (its descriptor)
    else:                 return M[BUILTIN_BASE + t]  # t in 0..15, t != 11
```

Adding a future intrinsic representation in tag 12..15 fills one empty
`BUILTIN_TYPE` slot and requires **no change** to this algorithm or table shape.

`accepts?` is ordinary GLON:

```
accepts?: func [type value] [ = (type? value) type ]
```

Consequences:

- `type? 42`            → `integer!` (BUILTIN_TYPE[0]).
- `type? <a word>`      → `word!`   (BUILTIN_TYPE[2]).
- `type? <a vector>`    → `vector!` (the vector's `payload[0]`).
- `type? vector!`       → `datatype!` (a descriptor's `payload[0]` is the meta).
- `type? datatype!`     → `datatype!` (self-loop).
- `accepts? integer! 42`      → true.
- `accepts? integer! [2]`     → false (`block! ≠ integer!`).
- `accepts? person! someone`  → `= (type? someone) person!`.

The *same* operation handles built-in and user types because both reduce to
descriptor identity; the tag→descriptor table is the only place built-in
knowledge lives, and it is closed (frozen tag set, fixed 16 slots).

---

## 12. Datatype identity

Identity is **descriptor identity (pointer equality)**, not name, not structure.
Two `make datatype! [x: integer!]` evaluations allocate two distinct descriptor
objects, hence two distinct types even though their specs and names are
identical. `accepts?` and `type?` use `=` (tagged-cell equality → pointer
equality for `T_USER`). Field `type_i` cells hold the descriptor *pointer*
captured at definition time, so a later redefinition of `person!` (a new
descriptor) does not silently rebind an existing `relationship!` — the old
relationship keeps pointing at the old person descriptor.

---

## 13. Construction

`make` is the constructor. It is an ordinary GLON closure that dispatches on
whether its first argument is the `datatype!` meta-descriptor:

```
make ( D args -- result ):
    if = D datatype!:   return make-datatype args      # build a descriptor
    if not datatype? D: return NONE                    # D is not a datatype
    return make-value D args                           # build a value of type D
```

**`make datatype! [spec]` builds a descriptor** (`make-datatype`):

- Walk `spec` (a block of alternating `set-word name`, `type-word`).
- Resolve each type word to its current binding (a descriptor value) via
  ordinary context lookup (`word-value`).
- `user-alloc` a descriptor (`desc = datatype!`, `count = 2N`) and fill the
  interleaved `(name, type)` content.

**`make D [values]` builds a value** (`make-value`):

```
make-value ( D vals -- value | NONE ):
    N = D.count / 2                          # number of fields
    if block-len(vals) != N:  return NONE    # arity mismatch
    for i in 0..N-1:
        type_i = D.content[2i+1]
        v_i    = vals[i]
        if not accepts? type_i v_i:  return NONE   # type mismatch
    p = user-alloc D N                        # allocate [desc, count=N, fields]
    for i in 0..N-1: user-set! value i v_i
    return value
```

- `make vector! [1 2 3]` **succeeds** → the vector value.
- `make vector! [1 "x" 3]` **fails** → NONE (field 2 expects `integer!` but
  `type? "x" = string!`; in current R0, which has no `string!`, the equivalent
  is `make vector! [1 [2] 3]` — a `block!` is rejected identically).
- `make vector! [1 2]` **fails** → NONE (arity 2 ≠ 3).
- `make 42 [1 2 3]` **fails** → NONE (`42` is not a datatype).

---

## 14. Construction-time vs mutation-time validation, and failure semantics

M3 enforces validation at **both** construction and generic field mutation:

```
field-set! ( value name newvalue -- value | NONE ):
    i = field-index (value.desc) name       # from the descriptor's name list
    type_i = value.desc.content[2i+1]
    if not accepts? type_i newvalue:  return NONE
    user-set! value i newvalue
    return value
```

### Failure semantics (precise)

Typed exceptions do not exist yet, and M3 does **not** introduce them. M3
reuses the existing GLON result/failure convention:

- **Success** → the ordinary result protocol `[result, 1]` (the constructed
  value, or the mutated value for `field-set!`).
- **Validation failure** (arity mismatch, type mismatch, or a non-datatype
  argument) → a single **`NONE`** result `[NONE, 1]`.

`NONE` is unambiguous: `make` and `field-set!` never legitimately produce NONE
as a *successful* result (a constructed/mutated object is always a `T_USER`
value), so NONE marks failure precisely. The caller tests the result with the
existing `=`/`either` machinery. This reuses the existing result protocol and
the existing NONE value; a future typed exception system may supersede this
convention without changing the value model.

The low-level RAW `user-set!` (the raw `!` to `payload+2+i`) is trusted/unsafe
and does **not** validate — that is the documented trapdoor boundary, and GC
correctness does not rely on type validity.

---

## 15. Protecting DESC and COUNT

`desc` lives at `payload[0]` and `count` at `payload[1]`. The ordinary field
API indexes only content cells `payload[2+i]` with `0 ≤ i < N` (bounds-checked
in the GLON `field`/`field-set!` wrappers). Therefore desc and count are
unreachable through any field write; they are written exactly once by
`user-alloc`. (The trusted RAW `user-set!` could in principle overwrite them;
that is the same trusted/unsafe class as the rest of the trapdoor, not a
language-visible field operation.)

---

## 16. What belongs in GLON vs RAW vs C

| concern | layer |
|---|---|
| tag arithmetic, `MOD`/`DIV` | GLON/RAW (unchanged) |
| `r_alloc` entry | emitted S1; exposed to RAW by address |
| `type?` (16-slot tag→descriptor table) | RAW (generic, closed over frozen tags) |
| context lookup (`word-value`) | RAW (generic) |
| field read/write, untag, block len/pick, user-alloc/count/desc | RAW (generic mechanics) |
| `make`, `accepts?`, `field`, `field-set!`, predicates | GLON (policy) |
| `trace_user`, `T_USER` case, `GC_KIND_USER` | emitted S1 (generic, one-time) |
| `BUILTIN_TYPE`/`GC_META` roots + bootstrap seeding + `r0_s1_alloc_addr()` | emitted S1 (roots) + C loader (seed) |
| S1 primitives / HOST services / evaluator / parser | **frozen / unchanged** |

`datatype!` is the meta-descriptor **value** (a `T_USER`, bound in the global
context); it is not code. Policy is GLON; memory/reflection mechanics are RAW;
the collector learns only the generic "trace a user object" rule plus the two
bootstrap roots; C only seeds the fixed built-in/meta descriptors and exposes
one allocator address.

---

## 17. How nested datatypes work

```
person!:       make datatype! [ id: integer!  name: word! ]
relationship!: make datatype! [ kind: word!  from: person!  to: person! ]
```

`make datatype!` resolves `integer!`/`word!` to built-in descriptors and
`person!` to the person descriptor, storing each `type_i` as a tagged `T_USER`
cell in `relationship!`'s content. A relationship value's `from`/`to` fields
hold `T_USER` pointers to person values. `make relationship! [ ... ]` validates
`from`/`to` via `accepts? person! …`. The collector traces `from`/`to` by tag
and recurses into the persons generically — it never learns that
`relationship!`, `person!`, `from`, or `to` exist.

`string!` in `person!` is identical in mechanism; it is deferred only because
`string!` itself is a future intrinsic tag (slot 12..15), not because the model
lacks support.

---

## 18. How cycles work

Cycles are tagged `T_USER` pointers pointing back. `trace_user` → `mark_value`
→ `mark_push` sets the mark bit and returns early when already marked, so a
self- or mutually-referential value terminates; sweep reclaims the whole cycle
when unreachable. This is the identical mechanism M2 tests F/G prove for
closures, requiring no datatype-specific collector logic.

## 18b. Recursive datatypes (deferred to M3B)

Ordinary GLON cannot yet define a *recursive* datatype such as:

```
person!: make datatype! [ friend: person! ]
```

because field-type words are resolved **eagerly** during descriptor
construction (`mk-datatype` looks up each type word immediately), and `person!`
is not yet bound while its own spec is being built. Forward/mutually-recursive
references have the same limitation.

This is **not** a GC gap: the collector is already cycle-correct. M3A proves
that with a RAW-manufactured self-referential value (test E). Recursive and
forward datatype references are deferred to M3B and are out of scope here.

---

## 19. Is any evaluator modification truly necessary?

**No.** Identical reasoning to revision 1:

- `T_USER` self-evaluates via the existing fall-through in `emit_subexpr`.
- Words bound to descriptors are ordinary values (the WORD path already returns
  non-NATIVE/CLOSURE/RAW bindings as `[v, 1]`).
- `make`/`type?`/`field` are ordinary closures + RAW fragments, both invoked
  through existing `T_CLOSURE`/`T_RAW` paths; no native id.
- `make datatype! [x: integer! …]` parses with the existing reader.

The evaluator and parser are byte-for-byte unchanged.

---

## 20. Risks / invariants

1. **Content cells hold tagged values** — enforced by `make`/`field-set!`;
   violable only by RAW (documented).
2. **`count` ≤ allocated extent** — enforced in `trace_user` (§10); a corrupt
   `count` is fail-stop heap corruption (never clamped), so a surviving object
   always has a structurally valid USER layout.
3. **Descriptor stays reachable while values of its type are reachable** —
   `trace_user` marks `desc` first.
4. **No RAW executes during marking** — the tag-driven trace preserves
   reentrancy.
5. **Bootstrap descriptors are permanent GC roots** — `BUILTIN_TYPE` (16 slots)
   is scanned by `collect`, independent of mutable word bindings (§10); the
   meta-descriptor is rooted transitively through every descriptor's `desc`.
6. **`BUILTIN_TYPE` is fixed at 16 slots** — tags 12..15 fill empty slots; no
   `type?`/table-shape change (§5, §11).
7. **Identity is definition-time, by pointer** — type cells capture descriptor
   values at `make datatype!` time (§12).
8. **Validation failure is NONE, not a typed exception** (§14).

Risks: a trusted RAW fragment can forge a user object (wrong `count`/`desc`/
untagged field) — but the two fail-stop checks (§10) turn a forged payload or
`count` into a clean corruption halt rather than silent overretention or
overrun. Field *subtyping/inheritance* and *unboxed physical storage* are
deliberately absent. `string!` requires a future tag + a filled `BUILTIN_TYPE`
slot, not new datatype machinery.

---

## 21. Migration impact on existing M2 objects

No existing object layout changes. Additive only:

- `r0_s1.h`: `T_USER=11`, `GC_KIND_USER=5`, `BUILTIN_BASE`, `GC_META`,
  `GC_META_PAYLOAD`; RV/GC-state relocation (RV_BASE 8586, GC state 9025).
- `r0_s1_runtime.c`: `trace_user` (+ fail-stop extent check), `mark_value`
  `T_USER` case (+ fail-stop range check), drain case; `BUILTIN_TYPE` root scan
  in `emit_collect`; bootstrap seeding; `r0_s1_alloc_addr()` /
  `r0_s1_lookup_addr()`.
- Global context: `integer!`…`raw!` (11) + `datatype!` (1) new bindings
  (capacity 48 is sufficient).
- Managed heap bottom: 12 bootstrap descriptors × 32 cells = 384 cells;
  `REG_HP` seeded past them after `s1_reset` (§10).
- Fixed region: `GC_META` (8567), `BUILTIN_BASE` (8568..8583), `SCRATCH_C/D`
  (8584/8585); RV at 8586..8641; `SCRATCH_A/B/E/F` 8642..8645.

Existing M2 A–R and the full native suite pass unchanged. The base emitted code
is **6784 cells** (was 6509; +275 for the generic user-object GC support), and
the datatype-library preamble adds ~1625 more cells at parse time; the code
region `[256, 8567)` now has headroom after the RV/M1/GC relocation.

---

## 22. Proposed M3A acceptance tests

(`r0_s1_m3_tests.c`, M2-style harness + datatype-library preamble.)

- **A** — VECTOR create/construct/type-check: `vector!: make datatype! [x:
  integer! y: integer! z: integer!]`; `v: make vector! [1 2 3]`; `type? v` =
  `vector!`; `vector? v`.
- **B** — field access: `field v 'x` → 1, `field v 'y` → 2, `field v 'z` → 3.
- **C** — first-class: pass `v` through an unaware function; round-trips.
- **D** — nesting: person/relationship; after `collect`, `field (field r 'from)
  'id` still correct.
- **E** — cyclic user value survives `collect`.
- **F** — unreachable user value reclaimed.
- **G** — unreachable nested graph (relationship + persons) reclaimed.
- **H** — descriptor liveness follows its values.
- **I** — user value captured in a closure survives GC.
- **J** — `datatype?` closed loop: `datatype? vector!`, `datatype? datatype!`.
- **K** — immediates are not pointers (exactness; mirrors M2 Q).
- **L** — **validation failure yields NONE**: `make vector! [1 [2] 3]` → NONE;
  `field-set! v 'y [2]` → NONE; `make vector! [1 2]` → NONE (arity); `make 42
  [1 2 3]` → NONE.
- **M** — built-in vs user unified: `accepts? integer! 42` true, `accepts?
  word! 42` false, `accepts? vector! 42` false, `accepts? vector! v` true.
- **N** — **bootstrap-root regression**: rebind `integer!` to some other value,
  `collect`, then `type? 42` still yields the canonical integer descriptor
  (BUILTIN_TYPE[0] was not collected).
- **O** — clean OOM (mirrors M2 P); frozen-S1 check; evaluator/parser
  `git diff --exit-code` empty; prior suites pass.
- **P** — **forged/out-of-range T_USER → corruption**: a RAW-built `T_USER`
  whose payload is outside the managed heap, then `collect`, must halt cleanly
  as corruption (no `bad opcode`).
- **Q** — **USER count beyond extent → corruption**: a RAW-built user object
  whose `count` exceeds its payload capacity, then `collect`, must halt cleanly
  as corruption (no `bad opcode`).

---

## "Could this be done with the current RAW trapdoor alone?"

**Current RAW can encode datatype-like structures using existing traced
representations, but cannot introduce this new independent managed object
layout while preserving exact GC without one generic collector extension.**

In more detail: RAW already has the machinery to build *datatype-like*
structures out of existing, already-traced representations (contexts, blocks,
closures) and to read/write any cell, untag/tag, reflect a tag, and — once
`r_alloc`'s address is exposed — allocate managed objects. What it cannot do is
introduce a **new, independent managed object kind** (the `T_USER` payload
`[desc, count, fields…]`) and have the already-emitted collector trace it
exactly. The collector's `mark_value` knows only CONTEXT/CLOSURE and its drain
loop only CTX/CLOSURE/FRAME; without the generic `T_USER`/`GC_KIND_USER`/
`trace_user` addition, user objects would be swept dead or their children
collected out from under them. That one generic collector extension — plus the
two bootstrap roots — is the only non-RAW, non-GLON change, and it names no
concrete datatype.

---

## "What would constitute architectural cheating?"

1. An eighth S1 primitive (e.g. `DATATYPE`/`MAKE`/`TYPE?`/`FIELD`).
2. A datatype-specific HOST service (`HOST_MAKE_VECTOR`, `HOST_GET_FIELD`).
3. GC/datatype tracing in C or HOST.
4. A giant C switch of concrete datatypes (VECTOR/PERSON/RELATIONSHIP/GOB/…).
5. Special evaluator knowledge (a `T_USER` case reading a built-in type table,
   or `make`/`type?`/field as hardcoded natives).
6. Hidden datatype semantics in HOST.
7. Weakening M2 GC into conservative scanning.
8. Hardcoding concrete datatype layouts in the trace dispatch.
9. Reifying datatype as control flow.

Not cheating: one generic `T_USER`/`GC_KIND_USER`/`trace_user`; the fixed
`BUILTIN_TYPE`/`GC_META` bootstrap roots; exposing `r_alloc` to RAW; a datatype
library in GLON + generic RAW. Note that the built-in descriptor table is
cheating **if it grows per user datatype**; it is legitimate only because it is
bounded by the frozen tag set (16 slots) and user datatypes never enter it.

---

## "Extensible record shapes" vs "extensible datatypes with value constraints"

- **Record/object shapes** = named fields only; a field is a slot with a name
  and no expectation about its value.
- **Datatypes with value constraints** = named fields **plus** an expected type
  per field, checked by the constructor/setter.

**M3 implements the second.** The distinction is carried by the descriptor's
interleaved `(name, type)` content: without `type_i` it is a shape; with
`type_i` enforced by `accepts?` it is a datatype.

A second, orthogonal boundary applies: M3's datatypes are **structured**
(tagged content cells) rather than **arbitrary physical representations**
(packed bytes). M3 does **not** add subtyping, inheritance, methods, exceptions,
SQL, genealogy, or GUI machinery — those are future users of this one checking
abstraction.

---

## Proposed M3A implementation slice (first slice)

1. `r0_s1.h`/`r0_s1_runtime.c`: `T_USER`, `GC_KIND_USER`, `trace_user` (+
   extent check), `mark_value`/drain cases, `BUILTIN_TYPE`/`GC_META` roots,
   bootstrap seeding, `r0_s1_alloc_addr()`.
2. RAW generic primitives: `type?`, `word-value`, `untag`, `user-alloc`,
   `user-set!`, `user-get`, `user-desc`, `user-count`, `block-len`,
   `block-pick`.
3. GLON library: `accepts?`, `make` (both modes), `field`, `field-set!`,
   `datatype?`, `vector?`.
4. `r0_s1_m3_tests.c`: VECTOR, then nested PERSON/RELATIONSHIP with GC, then
   validation-failure (NONE) and cycle/reclamation/root cases (A–O).
