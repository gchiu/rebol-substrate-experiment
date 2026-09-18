# M3C-STRING-SERIES-DESIGN.md — managed STRING! / variable-length series

**Status:** design only. M3C introduces a managed, variable-length, non-pointer
**byte series** as an intrinsic physical representation (`STRING!`), sufficient
for names, errors, rendering labels and (later) HTTP/SQL/serialization/GUI text
— without any new S1 primitive, evaluator or parser change, and without
weakening exact GC. Submitted for review before implementation.

---

## 0. Context and constraint restatement

M3B rendered genealogy labels as `block!` values of integer character codes
(`Alice -> [65 108 105 99 101]`) because `STRING!` does not exist. That was a
proof. Real text is now the next foundational requirement.

The question is **not** "add another tag". It is: *how should GLON represent
variable-length managed data while preserving the frozen S1/evaluator
architecture?*

S1 stays frozen at seven primitives. No new primitive, no STRING-specific HOST
semantics, no string parsing/evaluation in the evaluator, no GC in C/HOST, no
hidden host string, no giant datatype switch. M3A's structured datatypes are
unchanged. `STRING!` is allowed to be an intrinsic physical representation
(M3A deliberately did *not* attempt arbitrary packed physical storage; this is
the first, and cleanest, place to add one).

---

## 1. Current value/tag layout (relevant subset)

R0 stores every value as one S1 `cell` (`intptr_t`): `(payload<<4) | tag`, or
`(16-aligned pointer) | tag`, with 4-bit tags (`r0_s1.h:19`):

| tag | type | payload |
|---|---|---|
| 0 | INT | signed `<< 4` |
| 1 | NONE | atom `0x1` |
| 2..5 | WORD/SET/GET/LIT | symbol id `<< 4` |
| 6 | BLOCK | 16-aligned loader-heap pointer |
| 7 | CONTEXT | 16-aligned managed pointer |
| 8 | CLOSURE | 16-aligned managed pointer |
| 9 | NATIVE | host id `<< 4` |
| 10 | RAW | 16-aligned loader-heap pointer |
| 11 | T_USER | 16-aligned managed pointer |
| **12..15** | **free** | — |

The evaluator's `emit_subexpr` (`r0_s1_runtime.c:1305`) special-cases only tags
2/3/4/5; every other tag falls through to "self-evaluating literal". A new tag
therefore self-evaluates with **no evaluator change** (this is exactly how
`T_USER` was added in M3A).

`type?` (RAW, `r0_s1_m3_tests.c`) is already tag-generic:

```
if tag == T_USER: return payload[0]            # user object -> its descriptor
else:             return BUILTIN_TYPE[tag]     # 0..15 -> canonical descriptor
```

So `type? <string>` returns `BUILTIN_TYPE[12]` automatically once that slot is
seeded — no change to `type?`.

`cell` is `intptr_t`: 8 bytes native, 4 bytes in the wasm32 build. This matters
only for *packed* storage (see §10); the recommended first slice stores **one
byte per cell** and is therefore independent of `sizeof(cell)`.

---

## 2. Current BLOCK representation, and why it is not reusable

A BLOCK (tag 6) is a **loader-heap, permanent** object
(`make_block`, `lalloc(2+cap)`):

```
block ptr p:  [0] count   (raw)
              [1] site    (return-site id, int)
              [2..] elems (tagged R0 values)
```

Key facts:

- Elements are **tagged values**, fixed in count at parse time.
- The loader heap (`40000..47000`) is **never swept**: blocks are permanent and
  cannot be reclaimed, resized, or appended at runtime.
- Loader blocks never contain collected pointers, so the collector does **not**
  trace them at all (M2-GC-DESIGN §4).

Why this is **not** a usable string representation:

1. **Permanence.** A runtime string (an error message, a serialized frame, an
   HTTP body) must be allocated and freed like any other value; a loader block
   cannot be reclaimed or grown.
2. **Fixed count.** Blocks are built at parse time with a fixed `count`; there is
   no append/mutation path.
3. **Tagged elements.** A byte is a raw octet `0..255`, not a tagged integer.
   Storing bytes as tagged ints (`mk_int(b)`) conflates "byte" with "integer"
   and gives the wrong `type?`/semantics.
4. **Pointer-bearing GC semantics.** A block's elements are traced; a string's
   bytes must *not* be scanned for pointers (§9).

So the M3B block-of-chars workaround is fine for *fixed, parse-time* labels and
nothing else. `STRING!` needs a managed, growable, byte-bearing object.

---

## 3. Candidate designs

### A. STRING! as `T_USER` containing a `BLOCK!` of char codes

`person!: make datatype! [id: integer!  name: block!]` — the M3B workaround.

- **Size:** 1 tagged cell per character + block header; same cell count as the
  recommended string, but with integer (not byte) semantics.
- **GC:** contents traced (they are tagged values); fine but needless.
- **Mutation/append:** none — block is loader-permanent.
- **Comparison:** byte-wise compare would need a custom walk; `=` compares
  tagged integer sequences.
- **Verdict:** rejected — cannot grow, cannot be reclaimed, wrong element
  semantics. This is what M3C replaces, not what it adopts.

### B. STRING! as a new intrinsic managed object of packed bytes

A new tag + managed object whose payload packs several bytes per cell
(8/cell native, 4/cell wasm32).

- **Size:** dense (1 byte per byte), but byte extraction/insertion requires
  `DIV/MOD` by 256 (signed-host arithmetic), and the packing factor is
  `sizeof(cell)`-dependent — a portability hazard.
- **GC/mutation/comparison:** same as the recommended design (below).
- **Verdict:** correct and denser, but over-engineered for the first slice.
  Keep it as the future `BINARY!`-style density optimization; it does not
  change the tag/kind/API.

### C. Generic managed SERIES (STRING! / BINARY! / dynamic BLOCK!)

One managed "series" layout used by all three, distinguished by whether its
elements are raw bytes (not traced) or tagged values (traced).

- **Size:** 1 cell per element (byte or tagged value) + a length word.
- **GC:** the header kind selects the trace routine — byte series are skipped;
  value series are `mark_value`'d element-by-element. Exact in both cases.
- **Mutation/append:** immutable; growth is allocate-and-copy (append returns a
  new series), so both variants share the same immutable-series discipline.
- **Verdict:** **recommended** — this is B with the packing deferred, *plus* a
  forward-compatible shape for future dynamic `BLOCK!`.

### D. Derived recommendation

Adopt C's layout, but implement **only the byte-bearing variant now**:

- **One intrinsic tag** `T_STRING = 12`.
- **One GC kind** `GC_KIND_STRING = 6` (non-pointer series).
- The layout leaves room for a later `GC_KIND_SERIES` (value series → future
  dynamic `BLOCK!`) and `BINARY!` (a second byte-series tag) with no layout
  change.

This is the smallest change that satisfies the acceptance case *and* is a
correct foundation for the next two obvious series users.

---

## 4. Recommended representation (exact)

```
STRING! value  =  mk_string(p) = p + T_STRING        (T_STRING = 12)
                p is a 16-aligned managed payload pointer

header (B = p - GC_HDR_STRIDE = p - 16):
  B+0  size     : total extent in cells, multiple of 16 (= 16 + align16(1 + len))
  B+1  flags    : GC_FLAG_ALLOC | GC_KIND_STRING<<2  (| GC_FLAG_MARK while marking)
  B+2 .. B+15   : unused

payload (p):
  p+0  length   : raw integer N (byte count)
  p+1 .. p+N    : raw integers 0..255 (bytes)
  p+N+1 ..      : alignment padding (unused)
```

- `length` at `p+0`; bytes at `p+1+i`.
- **Strings are immutable.** `length` is fixed at allocation and the bytes are
  never written after construction (the only byte writes are the initial fill of
  a freshly allocated string, which no other reference can observe). `append`
  and every other "grow" operation allocate a fresh string and copy (§8).
- The payload extent is `align16(1 + N)`; the trailing cells past `p+N` are
  alignment padding, not capacity — append never reuses them.
- A byte is a **raw integer 0..255**, never a tagged value and never a pointer.
  `0..255` is far below `GC_HEAP_BASE` (32768), so no byte can ever be mistaken
  for a managed pointer.

---

## 5. Exact tag/kind changes

- `r0_s1.h`: `T_STRING = 12` (add to the tag enum).
- `r0_s1.h`: `GC_KIND_STRING = 6` (add after `GC_KIND_USER = 5`).
- `r0_s1.h`: `BUILTIN_STRING_PAYLOAD = GC_META_PAYLOAD + 12*32 = 33168`
  (bootstrap constant for the string! descriptor).
- `r0_s1_seed_datatypes` (`r0_s1_runtime.c:1512`): bind the word `string!` to
  `mk_user(BUILTIN_STRING_PAYLOAD)`; `seed_datatype_heap` seeds the descriptor
  (one more 32-cell `T_USER` with `desc = datatype!`, `count = 0`) and sets
  `BUILTIN_TYPE[12] = mk_user(33168)`, advancing `REG_HP` 33152 → 33184. Slot 11
  (`T_USER`) stays `NONE` (its `type?` special-case reads the object's own
  descriptor). Tags 13..15 remain free.

No other tag/kind changes. `BUILTIN_TYPE` stays a fixed 16-slot table.

---

## 6. GC behaviour

`mark_value` gains a `T_STRING` case, structurally identical to the existing
`T_USER` case (`r0_s1_runtime.c:529`): require `p` in `[GC_HEAP_BASE,
GC_HEAP_LIMIT)` (fail-stop on violation), then `mark_push(p)`. The drain loop
gains a `GC_KIND_STRING` case calling `trace_string`.

`trace_string(p)`:

1. `size = M[p - 16]`; `payload = size - 16`.
2. `length = M[p]`; **if `length > payload - 1`, HALT as heap corruption**
   (mirrors `trace_user`'s fail-stop count check — never clamp).
3. **Trace no children.** The bytes are raw, non-pointer data.

A `STRING!` is therefore a **leaf object**: it has no outgoing managed
references, so `trace_string` only validates its length against the allocation
extent and marks nothing. This is exact: a string is reachable iff it is marked;
its contents are never scanned for pointers; a forged length is a clean
corruption halt, never an overrun. No conservative scanning, no weakening of
M2/M3 exactness.

---

## 7. Allocation / reclamation / reuse

- **Allocate:** `r_alloc(align16(1 + N), GC_KIND_STRING)` — exactly the M2
  first-fit + bump + collect-once + split (remainder ≥ 32) + coalesce path. No
  new allocator logic. A string is allocated once, at its final length.
- **Reclaim:** dead strings are swept and coalesced like every other object.
- **Reuse:** a freed string's extent is reused by a later string (or any object)
  via first-fit; the heap frontier is never lowered.
- **Grow:** never in place. `append` (and any future builder) allocates a new
  larger string, copies the bytes, and returns it; the old value becomes garbage
  and is collected when unreachable. Objects never move, so growth is always
  allocate-and-copy.

---

## 8. Immutability and functional append

**STRING! is immutable.** There is no byte-level mutation and no in-place
resize. Every operation that would change a string returns a *new* string:

- `append s b` → allocate `align16(1 + N + 1)` cells, copy the `N` bytes of `s`,
  write `b` at index `N`, return the new string. `s` is unchanged.
- `copy s` → returns `s` itself (immutability makes sharing safe; no duplicate
  is required). Retained for API symmetry.
- `string-byte s i` → read-only access to `p+1+i` with bounds check.
- `field-set! person 'name s2` replaces the `name` field cell with a *different*
  string value; it never writes into a string's bytes. Validation via
  `accepts? string! s2` is unchanged.

Construction mechanics (writing bytes into a *freshly allocated* string) are the
trusted RAW layer (`string-fill`), used by `make string!` and `append`; they are
never applied to a published/shared string. Policy is GLON; mechanics are RAW;
the collector sees only an immutable leaf object.

---

## 9. Text-encoding decision

**STRING! contains bytes (octets).** UTF-8 is the standard text interpretation
of those bytes.

- A `STRING!` is a `BINARY!`-compatible byte sequence; text is bytes + a
  convention, not a separate machinery.
- **Byte-oriented** (first slice): `length?` (byte count), `append` (byte),
  `string-byte` (byte at index), equality (byte-wise), `copy` (byte-wise). All
  are UTF-8-correct *because they never decode*.
- **Character-oriented** (deferred, not in M3C): code-point count, code-point
  indexing, case mapping, normalization — these require UTF-8 decode/encode and
  are a later layer.
- Consequently `length?` on a string is the **byte** length in the first slice;
  a distinct code-point-length operation is future work. This is documented, not
  accidental.

No Unicode tables, no normalization, no wide-char representation. ASCII is a
subset of UTF-8, so all M3C acceptance labels are byte- and character-identical.

---

## 10. Size / portability / WASM

- **First slice (one byte per cell):** `N`-byte string occupies
  `align16(1+N) + 16` cells. "Alice" → 32 cells (the 16-cell header granularity
  dominates tiny strings). This is cell-addressed, so it is byte-order- and
  `sizeof(cell)`-independent; identical GLON behaves the same natively and in
  wasm32.
- **Future (packed bytes, 8/cell native / 4/cell wasm32):** an order-of-
  magnitude denser for strings ≫ 16 bytes, at the cost of byte pack/unpack
  arithmetic and a packing factor that must be a known compile-time constant.
  This is a pure storage-density change behind the same byte-oriented API; it
  can land later (likely as `BINARY!`'s representation) without touching the
  tag/kind or the GLON operations.

---

## 11. STRING! / M3 user-datatype interaction

- `person!: make datatype! [id: integer! name: string!]` resolves `string!` at
  definition time to `BUILTIN_TYPE[12]` (the canonical descriptor), exactly like
  `integer!`/`block!` today — no `mk-datatype` change.
- `make person! [0 s]` validates the `name` field via `accepts? string! s` =
  `= (type? s) string!`; `type?` already reads `BUILTIN_TYPE[12]`.
- The `name` field cell holds a `T_STRING` value; `trace_user` marks it by tag;
  `mark_value` then marks the string via the new `T_STRING` case. No datatype-
  specific collector knowledge.
- `make string! [65 108 105 99 101]` is dispatched inside the existing GLON
  `make` closure (§12) — the evaluator's `make` is unchanged.

---

## 12. Operations layering (first slice)

Following M3A's split (policy in GLON, mechanics in RAW, collector generic):

| concern | layer |
|---|---|
| tag arithmetic, `MOD`/`DIV` | GLON/RAW (unchanged) |
| `type?` (16-slot table) | RAW (unchanged; slot 12 now seeded) |
| `string-alloc` (N bytes), `string-length`, `string-byte`, `string-fill` (construction write) | RAW (generic mechanics) |
| `mk-string` (block-of-bytes → string), `make string!` branch | RAW + GLON |
| `append` (functional), `copy` (identity), `string=?` (equality), `length?`, `string?` | GLON (policy) |
| `mark_value` T_STRING case, `trace_string`, drain case | emitted S1 (generic, one-time) |
| `BUILTIN_TYPE[12]` root + bootstrap seeding | emitted S1 + C loader (seed) |
| S1 primitives / HOST / evaluator / parser | **frozen / unchanged** |

Minimum useful first slice:

- `make string! [bytes]` — construct from a block of byte values (0..255).
- `type?` / `string?` — reflection (already generic).
- `length?` — byte count (immutable).
- `string-byte s i` — read-only byte access (bounds-checked).
- `append s b` — **functional**: allocate a new string of `length+1`, copy,
  return it; `s` is unchanged.
- `string=? a b` — byte-wise equality (distinct from `=`, which is pointer/
  integer equality).
- `copy s` — returns `s` (immutability makes sharing safe).

The acceptance case then replaces M3B's labels: the genealogy program builds
`alice: make person! [0 make string! [65 108 105 99 101]]`, and the rendering
`draw-text` iterates the string's bytes (`string-length`/`string-byte`) to emit
the `text x y n c1…cn` surface command. The host-side SVG decoder is unchanged.

---

## 13. Future BINARY! implications

`BINARY!` is the same physical object as `STRING!` (a byte series) with a
different tag/descriptor (e.g. `T_BINARY = 13`, same `GC_KIND_STRING`) and
byte-oriented semantics. Adding it later is: one tag constant, one more
`BUILTIN_TYPE` slot + bootstrap descriptor, one `mark_value` case — no new GC
kind, no layout change, no evaluator/parser change. (Packed-byte density, if
ever adopted, would live here.)

---

## 14. Future dynamic BLOCK! / series implications

A value series (dynamic `BLOCK!`) is the same payload layout `[length,
elems…]` with **tagged** elements, allocated as a *distinct* GC kind
(`GC_KIND_SERIES = 7`), whose `trace_series` does `mark_value` on each element.
The collector distinguishes the two kinds by the header `kind<<2`, so:

- byte series (`GC_KIND_STRING`) → `trace_string` (no children);
- value series (`GC_KIND_SERIES`) → `trace_series` (mark each tagged element);

each is exact and safe. No bitmap, no "maybe a pointer" scanning, no weakening.

---

## 15. Parser / literal implications

The parser (`parse_word`, `r0_s1_runtime.c:100`) reads a token up to
whitespace/`[`/`]`; `"Alice"` currently parses as a **word whose spelling
includes the quote characters** — it is *not* a string literal.

Adding literal syntax requires a `parse_form` branch for `"` that reads to the
closing quote and produces a string value. That is a **parser change** and —
more importantly — raises the loader-permanent vs managed-object question
(literals are loader data; `STRING!` is managed). **Recommendation: defer
string literals.** The M3C first slice is constructor-only (`make string!
[bytes]`), which keeps the evaluator/parser byte-for-byte frozen, exactly as the
task permits. Literal syntax is a small, well-understood follow-up once the
managed representation is proven.

---

## 16. What would constitute architectural cheating

1. An eighth S1 primitive (e.g. `MAKE-STRING`/`STRING`).
2. A STRING-specific HOST service (`HOST_MAKE_STRING`, `HOST_STRCAT`).
3. GC string tracing or byte storage in C/HOST.
4. A C switch of concrete string operations.
5. Evaluator special-casing of `T_STRING` (parsing, evaluation, `print`).
6. Hidden string bytes or an out-of-heap host string buffer.
7. Weakening the collector into conservative byte scanning.
8. Hardcoding string length/byte layout outside the one generic trace path.
9. Making `STRING!` a `T_USER` subtype (tagged field cells) instead of an
   intrinsic byte series.

Not cheating: one `T_STRING` tag; one `GC_KIND_STRING`; a generic `trace_string`
(fail-stop length check, no children); the `BUILTIN_TYPE[12]` bootstrap root;
a generic `string-*` RAW mechanics layer plus GLON policy — the exact pattern M3A
established for user objects.

---

## 17. Acceptance tests (proposed)

New `r0_s1_m3c_tests.c`, M3A/M3B-style harness + datatype preamble:

- **A** — `make string! [65 108 105 99 101]`; `type?` = `string!`; `string?`.
- **B** — `length?` = 5 (byte count).
- **C** — `string-byte` at each index returns 65/108/105/99/101; out-of-range
  is rejected (0 results / NONE).
- **D** — `append` is functional: `s2: append s 33` yields `length? s2 == length? s + 1`,
  `s2` has the original bytes plus the new byte, and `s` is unchanged (same
  length, same bytes) — immutability.
- **E** — `string=?` byte-wise: equal content is true; distinct objects with
  equal bytes are equal; `=` (pointer) is false for distinct equal strings.
- **F** — `copy s` returns `s` (`= s copy s`); `append` on the result never
  affects `s` (no aliasing hazard, since strings are immutable).
- **G** — GC: a reachable string survives `collect`; an unreachable string is
  reclaimed (free cells grows by its extent).
- **H** — a `person!` with `name: string!` survives `collect` and the name
  bytes are intact (STRING! nested in a `T_USER` field).
- **I** — forged string (payload out of range) and corrupt length halt cleanly
  as corruption (mirrors M3A P/Q).
- **R** — the genealogy SVG demo uses `string!` names; the emitted surface
  stream and generated SVG are byte-identical to M3B's (Alice/Bob/Charlie + 3
  lines + rects/text).
- **S** — audit: `string!`/`STRING`/string-mechanics appear in none of
  `s1.c`/`s1.h`/`r0_s1_runtime.c` evaluator-or-parser regions/`r0_s1.h`
  beyond the tag/kind/bootstrap constants; evaluator/parser `git diff` empty.

Plus: M3A A–Q, M3B A–H/R/S, the nested-closure regression, and
`check-frozen-s1.sh` all unchanged.

---

## Recommendation summary

- **Recommended architecture:** `STRING!` = intrinsic tag `T_STRING = 12` over a
  managed, **immutable** **byte series** (`GC_KIND_STRING = 6`), one raw byte
  (0..255) per cell, `[length, bytes…]` payload, non-pointer leaf-object GC.
  `append` is functional (allocate + copy + return a new string). The layout is
  the forward-compatible foundation for `BINARY!` (another byte-series tag) and
  dynamic `BLOCK!` (a distinct value-series kind).
- **New tag required:** **yes** — `T_STRING = 12`.
- **New GC kind required:** **yes** — `GC_KIND_STRING = 6`.
- **Evaluator changes required:** **no** (T_STRING self-evaluates via the
  existing fall-through; `type?`/`make` are GLON/RAW).
- **Parser changes required:** **no** (constructor-only first slice; string
  literals deferred).
- **HOST changes required:** **no** (existing `HOST_PRINT` carries the surface
  stream; `HOST_PUTCHAR` already exists for future direct byte output).
- **First implementation slice:** `T_STRING`/`GC_KIND_STRING` + `trace_string` +
  bootstrap slot 12 + `string-*` RAW mechanics + GLON `make string!`/`length?`/
  `append`/`string-byte`/`string=?`/`copy`, then swap the M3B genealogy labels
  from `block!` char codes to `string!`.
