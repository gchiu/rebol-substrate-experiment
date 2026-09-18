# M3D-MANAGED-BLOCK-DESIGN.md — managed runtime BLOCK! / tagged-value series

**Status:** design only. M3D adds runtime-created, GC-managed `BLOCK!` values
containing arbitrary tagged R0 values, as an immutable first slice — without any
new S1 primitive, evaluator or parser change, HOST service, or native id. It
reuses the existing `T_BLOCK = 6` tag, distinguishing permanent loader blocks
from managed blocks by pointer range, exactly as `CONTEXT` already does.

---

## 1. Current BLOCK representation (inspected)

- **Tag:** `T_BLOCK = 6` (`r0_s1.h:20`); `mk_block(p) = p + 6`.
- **Physical layout** (`r0_s1.h:197`, `BLK_COUNT=0 BLK_SITE=1 BLK_DATA=2`):
  ```
  p+0  count   raw element count
  p+1  site    return-site id (definitional RETURN)
  p+2..       tagged R0 elements
  ```
- **Allocation region:** loader heap `[40000, 47000)` only. Built by
  `make_block(cap)` → `lalloc(2+cap)` (`r0_s1_runtime.c:56`); filled by the
  parser (`parse_block`) and by `r0_s1_parse` at load time.
- **Length/count:** yes — `p+0` is the raw count.
- **Elements are tagged R0 values:** yes.
- **Mutation:** none. There is no runtime block-write primitive; blocks are
  built once at parse/load time and only read thereafter. (`r_set`/`r_append`
  mutate *contexts*, never blocks. The only runtime block write is the M1
  wrapper block at fixed `60128+slot*16`, which holds `'do`, a loader `body`,
  `'task-finish` — all permanent.)
- **Permanent-address dependence:** yes. `RV_BLK`/`RV_CUR`/`RV_END` (and their
  frame copies `FRAME_BLK/CUR/END`) are raw pointers into the *current* block
  being evaluated, and are deliberately **not** GC roots (M2-GC-DESIGN §5: they
  "point into permanent blocks"). The collector also scans the return stack for
  `[32768,40000)` cells *as closure pointers* (M2-GC-DESIGN §6); that exactness
  holds only because `RV_BLK/RV_CUR/RV_END` point into the loader heap
  (`≥ 40000`), never the managed heap.
- **"Every T_BLOCK is loader" assumption:** effectively everywhere *by
  construction*, but encoded only in two places:
  - `emit_subexpr` has **no `T_BLOCK` case** — a block self-evaluates via the
    final fall-through (`r0_s1_runtime.c` tag dispatch handles only WORD/SET/
    GET/LIT, plus RAW=10; BLOCK=6 falls through).
  - `emit_mark_value` has **no `T_BLOCK` case** — BLOCK falls through to the
    trailing `/* else: BLOCK/RAW/int/... */ asm_drop(); asm_exit()` ("no
    collected children").
  - `GC_KIND_BLOCK = 0` and `GC_KIND_RAW = 3` are **defined but never used**
    (no `r_alloc` call site, no drain dispatch references them) — legacy
    constants.

## 2. One tag or two — reuse T_BLOCK = 6

**Reuse `T_BLOCK = 6`.** It is safe because the collector can distinguish
permanent vs managed blocks by **payload pointer range**, using the exact
mechanism `mark_value` already uses for `CONTEXT` (tag 7):

- payload `p` in `[GC_HEAP_BASE, GC_HEAP_LIMIT)` = `[32768, 40000)` → **managed
  block** (validated as an allocated `GC_KIND_BLOCK` payload start, then
  `mark_push` + trace elements — §4).
- payload `p` in the loader heap `[R0S1_HEAP_BASE, R0S1_HEAP_LIMIT)` =
  `[40000, 47000)` → **permanent block** (never swept; no children, because such
  blocks never contain managed references — §4).
- any other payload → **corrupt** (fail-stop — §4). The fixed M1 wrapper region
  `[60128, 60256)` holds blocks only as raw `TREC_BLK`/`RV_BLK` pointers, never
  as tagged `T_BLOCK` values, so no valid tagged `T_BLOCK` exists there.

A second tag (`T_MANAGED_BLOCK`) would be redundant and would not even prevent
mis-evaluation (`do` is tag-agnostic; it untags and evaluates any block-shaped
value — §9). One tag + range/header check is the correct, minimal choice.

## 3. Managed physical layout (recommended)

```
managed BLOCK! value = p + T_BLOCK     (p = 16-aligned managed payload)

header (B = p - 16):
  B+0  size   : extent = 16 + align16(1 + length)
  B+1  flags  : GC_FLAG_ALLOC | GC_KIND_BLOCK<<2   (| GC_FLAG_MARK while marking)

payload (p):
  p+0         length   raw element count N
  p+1 .. p+N  values   tagged R0 values (one per cell)
  p+N+1 ..    alignment padding (unused; immutable)
```

- **GC kind:** `GC_KIND_BLOCK = 7`. The existing `#define GC_KIND_BLOCK 0` is
  dead (never used); it is repurposed/renumbered to `7` so `7` is the managed
  block kind and the drain loop's existing `else` (which skips kinds `0` and
  `3`) is untouched. Kinds `0` and `3` remain unlabeled/skipped.
- **Alignment/extent:** payload cells `= align16(1 + N)`; total extent
  `= 16 + align16(1 + N)`. Empty block: 32 cells.
- Note the managed layout deliberately has **no `site` field** (it is data, not
  code). Consequently `block-pick` (which reads `p+2+i`, the loader layout) must
  **not** be used on managed blocks; M3D supplies its own `block-at` reading
  `p+1+i` (§7). `block-len` reads `p+0` and coincidentally works on both.

## 4. GC semantics

**mark_value — T_BLOCK case** (added before the final `else`). A `T_BLOCK`
pointer is classified precisely, never merely "managed vs everything else":

```
if tag == T_BLOCK:
    p = v - T_BLOCK                                 # p is 16-aligned (tag 6 => p ≡ 0 mod 16)
    if GC_HEAP_BASE <= p < GC_HEAP_LIMIT:           # managed range [32768, 40000)
        h = p - 16
        if h >= GC_HEAP_BASE
           and alloc_bit(M[h + 1])
           and kind(M[h + 1]) == GC_KIND_BLOCK:      # allocated GC_KIND_BLOCK payload start
            mark_push(p); return                     # valid managed block -> trace
        else:
            HALT (corrupt managed pointer)            # interior / unallocated / wrong kind
    if R0S1_HEAP_BASE <= p < R0S1_HEAP_LIMIT:        # loader heap [40000, 47000)
        drop; return                                 # permanent loader block
    HALT (corrupt out-of-range T_BLOCK pointer)       # everything else
```

- **Valid managed BLOCK** — payload in `[32768, 40000)` *and* the header at
  `p-16` is an allocated `GC_KIND_BLOCK` object (alloc bit set, kind bits = 7) →
  `mark_push`, then `trace_block`. The header check rejects an *interior*
  pointer (a 16-aligned address that is not a payload start), an *unallocated*
  region, and a *wrong-kind* payload (e.g. a `T_STRING`/`T_USER` payload reached
  through a forged `T_BLOCK`).
- **Valid permanent BLOCK** — payload in the loader heap `[40000, 47000)` (the
  only region that holds permanent blocks as *tagged* `T_BLOCK` values) →
  ignored, no children. Loader blocks carry no GC header, so the range is the
  only available check; a forged pointer inside this range is merely
  over-ignored (harmless — the loader heap is never swept). The fixed M1 wrapper
  blocks live at `[60128, 60256)` but are stored only as raw `TREC_BLK`/`RV_BLK`
  pointers, never as tagged `T_BLOCK` values, so they never reach `mark_value`;
  a tagged `T_BLOCK` in that range would fall into the fail-stop case below.
- **Anything else** — `p < 32768`, `p ∈ [47000, 60128)`, `p ≥ 60256`, or a
  managed-range address that is not an allocated `GC_KIND_BLOCK` payload start →
  **fail-stop** (`HOST_DUMP; HALT`) as corruption. This is tighter than the
  existing `CONTEXT`/`CLOSURE` handling: a stray or mis-tagged `T_BLOCK` is
  rejected, not silently skipped.

**trace_block** (drain `GC_KIND_BLOCK` case; uses the `GC_T6..GC_T8` scan/drain
partition like `trace_user`/`trace_string`):

```
trace_block(p):
    size   = M[p - 16]
    length = M[p]
    if length > size - 17:  HALT (corrupt length)     # > align16(1+length)-1
    for i in 0 .. length-1:
        mark_value(M[p + 1 + i])                      # each tagged element
```

Generic, no datatype knowledge, no conservative scan, no RAW during marking.

**Permanent loader block can never contain a managed reference.** Invariant
(restated, M2-GC-DESIGN §4): loader blocks are built at parse time from source
literals — integers, words, set/get/lit words, nested blocks, `none` — and
**no managed value exists during parse**; and loader blocks are **immutable**
(no runtime block write). The M1 wrapper block is written at runtime but holds
only `'do`, a loader `body`, `'task-finish`. Therefore the collector need never
trace loader blocks, and a managed value can never become unreachable while a
loader block still points at it. **This invariant is the load-bearing fact that
makes the one-tag design sound.** Managed blocks may *contain* loader blocks
(nested literals) — safe, because the loader block is permanent and `mark_value`
on it is a no-op; managed blocks may contain managed blocks — safe, because
`trace_block` recurses.

## 5. Immutability

Managed blocks are immutable (first slice):

- `append block value` → a **new** managed block with the old elements followed
  by `value`; the original is unchanged.
- `copy block` → may return the block itself (identity), exactly as `STRING!`
  does, because immutability makes sharing safe.
- No public element writer. The low-level construction write (`blk-set`) exists
  only as trusted RAW machinery used before a block is published (mirroring
  M3C's `str-fill`).
- Existing permanent BLOCK values are untouched; no semantics of loader blocks
  change.

## 6. Construction (no parser change)

`make block! …` is dispatched inside the existing GLON `make` closure (a new
branch `= D block!` → `mk-block`), exactly like `string!`:

- `make block! []` → empty managed block.
- `make block! [e1 e2 …]` → managed block whose elements are **copied verbatim**
  from the loader source block's elements `e1 e2 …` (integers, words, nested
  blocks — **syntax values, not evaluated**; no get-word resolution).
- `append b v` → new block from `b` plus the **evaluated** value `v` (an
  ordinary function call, so `v` is reduced first).

This keeps the two things distinct: `make block! […]` copies **syntax values**
from a source block; `append`/normal evaluation collect **evaluated results**.
The acceptance graph is built with evaluated get-words:

```
alice-name: make string! [65 108 105 99 101]
alice: make person! [0 :alice-name]
people: make block! []
people: append people :alice
family: make family! [:people]
```

## 7. Generic operations (minimum first slice)

| operation | semantics |
|---|---|
| `make block! [..]` / `make block! []` | construct (copy syntax / empty) |
| `length? b` | element count (tagged) |
| `append b v` | functional append → new block |
| `block-at b i` | element at index `i` (tagged, **0-based**); out-of-range → **0 results** (existing failure convention) |
| `copy b` | identity (immutable) |
| `block=? a b` | `length?` equal, then element-wise `=` (shallow, pointer/value equality) |

RAW mechanics (trusted): `blk-alloc` (raw N → empty block), `blk-len`
(`p+0`), `blk-at` (`p+1+i`), `blk-set` (`p+1+i = v`, construction-only),
`mk-block` (copy loader source elements to a managed block). No operation added
merely for REBOL parity.

## 8. Interaction with user datatypes

`family!: make datatype! [people: block!]`. The collector traces the chain
generically with no knowledge of `family!`/`person!`/`block!`:

```
family (T_USER) → trace_user → people field (T_BLOCK, managed)
  → mark_value BLOCK → mark_push → trace_block → person (T_USER)
    → trace_user → name field (T_STRING) → mark_push → trace_string
```

Nested managed blocks and (via M3A's existing RAW `mk-cycle`) cycles remain
correct because mark/trace is a standard mark_push/mark-bit worklist that
terminates on already-marked objects. Immutable first-slice blocks cannot
self-reference without RAW, but the collector is cycle-correct regardless.

## 9. Interaction with evaluator / code blocks

M3D managed blocks are **data-only** — ordinary `BLOCK!` values, but **not
executable code**. Two concrete reasons (both documented, not silently assumed):

1. **Layout mismatch.** The evaluator's block-execution path (`r_run_block` /
   `block-eval`) reads the loader layout `[count, site, elems…]`. A managed
   block is `[length, v0, v1 …]`, so executing one would read `length` as
   `count`, `v0` as `site`, and evaluate `v1 …` as code — silent
   misinterpretation.
2. **Return-stack exactness.** `do`/`values` save `RV_BLK/RV_CUR/RV_END` on the
   return stack (`r_run_block`, `emit_values`). If those point into a managed
   block, the collector's `scan_closures([RP, RS_INIT))` — which range-scans for
   `[32768,40000)` cells and `mark_push`es them as *payload* pointers — would
   `mark_push` the **misaligned** `RV_CUR = payload+2+i`, corrupting the mark
   (header read at the wrong address). This is why the M2 invariant
   "`RV_BLK/RV_CUR/RV_END` always point into permanent blocks" must hold.

Therefore M3D **does not** make managed blocks executable, and it adds **one
central guard** at the block-execution entry so that a managed block passed to
an execution path fails cleanly instead of misbehaving. The guard is a range
check inserted at the start of the two block-*value* execution routines —
`r_run_block` (used by `do` and by `either`'s branch selection) and `r_values`
(used by `values`) — immediately after the block pointer is untagged:

```
p = untag(block)
if GC_HEAP_BASE <= p < GC_HEAP_LIMIT:   HOST_DUMP; HALT    /* managed block: not executable */
/* else: permanent loader block -> execute normally */
```

A permanent/loader block (payload ≥ 40000) executes normally; a managed block
(payload in `[32768, 40000)`) halts cleanly (`HOST_DUMP; HALT`) **before any
evaluator execution of its contents**. This single guard covers `do`, `values`,
and `either` branches — the only paths that accept a runtime block *value*.
Closure bodies and the top-level program are always loader blocks captured at
parse/definition time, never runtime-managed values (a managed block could only
become a closure body via trusted RAW, which is out of scope), so they need no
guard. This guard is the only evaluator change M3D makes (§11); it is a fail-stop
check, not a change to evaluation semantics. (Corrupt/out-of-range pointers are
the collector's concern, §4; a corrupt pointer reaching an execution path is
outside this guard's two-case contract.)

## 10. Memory architecture (ledger update)

| kind | tag | layout | region | pointer fields | lifetime | roots |
|---|---|---|---|---|---|---|
| loader BLOCK | 6 | `[count, site, elems]` | loader 40000..47000 | none (elements are literals) | permanent | n/a (untraced) |
| **managed BLOCK** | 6 | `[length, values]` | managed 32768..40000 | each value cell | collected | data stack / contexts / other managed objects |
| CONTEXT | 7 | `[parent,count,cap,…]` | managed (or loader global) | parent, values | collected/global | ctx roots |
| CLOSURE | 8 | `[spec,body,ctx,site]` | managed | spec/body/ctx | collected | ctx roots |
| STRING | 12 | `[length, bytes]` | managed | none (leaf) | collected | as above |
| USER | 11 | `[desc,count,fields]` | managed | desc, fields | collected | as above |

Headroom: code `[256, 24576)`, state `[24576, 25315)`, heap `[32768, 40000)`
(7232 cells). Managed blocks live in the existing managed heap; **no relocation
is required** for M3D.

## 11. Constraints

M3D requires:

- **no new S1 primitive**,
- **no new HOST service**,
- **no parser change**,
- **no evaluator semantic change** — `emit_subexpr` still self-evaluates
  `T_BLOCK`; no native id, no new evaluator dispatch. The **one** evaluator
  edit is the execution guard (§9): a range-check + `HOST_DUMP; HALT` at the
  entry of `r_run_block` and `r_values`. It changes no evaluation semantics — a
  managed block was previously "undefined" on these paths; it is now a clean
  fail-stop.
- **no native-id shortcut**.

Changes are confined to: `r0_s1.h` (`GC_KIND_BLOCK` → 7), `r0_s1_runtime.c`
(`mark_value` T_BLOCK case + managed-payload header validation, `trace_block`,
drain `GC_KIND_BLOCK` case, `emit_gc` hook, and the two-line execution guard in
`r_run_block`/`r_values`), and the GLON/RAW library (`blk-*` mechanics +
`make block!`/`length?`/`append`/`block-at`/`block=?`/`copy`). The single
exception to the earlier "no evaluator change" is the execution guard, argued
in §9 as the cleanest way to make managed-block execution fail safely.

## 12. Proposed acceptance tests (r0_s1_m3d_tests.c)

- **A** — `make block! []` constructs an empty managed block; `length?` = 0.
- **B** — `append` builds `[1 2 3]`; `block-at` returns 1/2/3 (0-based).
- **C** — original block unchanged after `append` (immutability).
- **D** — `length?` correct after appends.
- **E** — `block-at` out-of-range → 0 results.
- **F** — a `STRING!` element survives GC through the block.
- **G** — a `T_USER` (`person!`) element survives GC through the block.
- **H** — a managed block survives GC through a `T_USER` field
  (`family! [people: block!]`).
- **I** — a nested managed block survives GC.
- **J** — an unreachable managed block is reclaimed/reused (≥ its extent).
- **K** — corrupt length (exceeds extent) fail-stops cleanly.
- **L** — `T_BLOCK` classification: a valid managed `GC_KIND_BLOCK` payload is
  traced; a `T_BLOCK` whose payload is an interior/unallocated managed address,
  a wrong-kind managed payload (e.g. a `T_STRING` reached as `T_BLOCK`), or
  outside `[32768, 47000)` fail-stops cleanly as corruption (no `bad opcode`).
- **M** — permanent loader block behaviour unchanged (source blocks still
  evaluate as code; `block-len`/`block-pick` on loader blocks unchanged).
- **N** — **execution guard:** `do`, `values`, and `either` (managed branch)
  applied to a managed block each fail-stop cleanly (`[dump]`, no `bad opcode`)
  before any element is evaluated; the same inputs built as loader literals
  still execute normally.
- **O** — M3A A–Q, M3B A–H/R/S, M3C A–S, nested-closure, M1/M2 all unchanged.
- **P** — `check-frozen-s1.sh` unchanged.

Additional permanent-vs-managed distinction test: a managed block containing a
loader block (nested literal) survives GC while the loader block is never
collected; and a loader block never references a managed block (audit: no
block-write primitive).

## 13. Memory-map impact

None beyond adding a managed kind to the existing managed heap and one GC kind.
No relocation, no new region, no stack/host change.

## 14. Risks / open questions

1. **Executing managed blocks** is now machine-guarded (§9): the block-execution
   entry fail-stops cleanly on a managed block, closing the earlier GC-unsafe
   "undefined" gap. The guard distinguishes only managed vs permanent; a corrupt
   (out-of-range) pointer reaching an execution path is outside its two-case
   contract and is caught instead by the collector's fail-stop (§4). Future work
   (if managed blocks should ever execute): loader-compatible layout + rooting
   `RV_BLK/RV_CUR/RV_END`.
2. **`block=?` is shallow** (element-wise `=`), so `[<string A>]` vs
   `[<string B>]` with equal bytes but different objects compare unequal. Deep
   equality is deferred.
3. **Immutable blocks cannot express cycles** without RAW; if cycles through
   blocks become a requirement, immutability must be revisited (the collector is
   already cycle-correct).
4. Reusing `T_BLOCK` means `type?` returns `block!` for both loader and managed
   blocks (desirable), but no operation can cheaply tell them apart by tag — the
   distinction is the pointer range (private to the collector).

## 15. Explicit scope exclusions

No parser string/block literals; no in-place mutation; no element
replacement/insertion; no slicing/views; no deep equality; no block
serialization; no dynamic `BINARY!`; no relocation. Managed blocks remain
non-executable data: they are not given a loader-compatible layout, are not
rooted via `RV_BLK/RV_CUR/RV_END`, and cannot become a closure body — execution
is guarded to a clean fail-stop (§9), not supported. Recursive/first-class
"managed code" is explicitly out of scope.
