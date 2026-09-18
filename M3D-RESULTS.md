# M3D-RESULTS.md — managed runtime BLOCK!

M3D implements a managed runtime `BLOCK!` on the frozen R0/S1 substrate: the
existing `T_BLOCK = 6` tag over a managed immutable tagged-value series
(`GC_KIND_BLOCK = 7`) with payload `[length, value0..valueN-1]`. It is the
first *series-of-tagged-values* managed object; `trace_block` walks each element
through the generic `mark_value` path. Permanent loader blocks are unchanged.

## 1. Assertion count

**304 `ok:`, 0 failures**, exit 0. (M3C milestone was 278; M3D adds 26
assertions: A–N.)

## 2. Failures

0.

## 3. M3D A–N results

All pass:

- **A** — `make block! []` constructs an empty managed block; `length?` = 0.
- **B** — `append` builds `[1 2 3]`; `block-at` returns 1/2/3 (0-based).
- **C** — original block unchanged after `append` (immutability).
- **D** — `length?` correct after appends.
- **E** — `block-at` out-of-range → 0 results.
- **F** — a `STRING!` element survives GC through the block.
- **G** — a `T_USER` (`person!`) element survives GC through the block.
- **H** — a managed block survives GC through a `T_USER` field
  (`family! [people: block!]`), with nested string bytes intact.
- **I** — a nested managed block survives GC.
- **J** — an unreachable managed block is reclaimed/reused (≥ its extent).
- **K** — corrupt length (exceeds extent) fail-stops cleanly.
- **L** — `T_BLOCK` three-way classification: out-of-range (below heap),
  wrong-kind (a `T_STRING` reached as `T_BLOCK`), and unallocated (`REG_HP`)
  payloads each fail-stop cleanly (`[dump]`, no `bad opcode`).
- **M** — permanent loader block behaviour unchanged (`block-len`/`block-pick`
  on loader blocks; source blocks still evaluate as code).
- **N** — execution guard: `do`, `values`, and `either` (managed branch) applied
  to a managed block each fail-stop cleanly before any element executes.

## 4. Frozen-S1 result

`./check-frozen-s1.sh` → **OK** (`f90496c26dc45c7a387d8fc8639cd2781507d3d2`);
`s1.c`/`s1.h`/`tests.c`/`adversarial.c`/`claims.c` byte-identical.

## 5. BLOCK! object layout

```
BLOCK! value = p + T_BLOCK   (T_BLOCK = 6); p = 16-aligned payload pointer

header (B = p - 16):
  B+0  size   : extent = 16 + align16(1 + length)
  B+1  flags  : GC_FLAG_ALLOC | GC_KIND_BLOCK<<2  (| GC_FLAG_MARK while marking)

payload (p):
  p+0           length : raw element count N
  p+1..p+N      values : tagged R0 values (traced generically)
  p+N+1..       alignment padding (never traced)
```

`trace_block` validates `length <= size - 17` (fail-stop on corruption) and then
`mark_value`s each `p+1+i` element. `mark_value`'s T_BLOCK case does a three-way
classification: a managed payload is validated as an allocated `GC_KIND_BLOCK`
payload start (header alloc bit + kind) before `mark_push`; a permanent loader
payload (`[40000, 47000)`) has no children; anything else is a fail-stop.

## 6. Code-size change

Base emitted code: **7437 cells** (was 7003 at the M3C milestone; **+434 cells**
for the T_BLOCK `mark_value` case, `trace_block`, the drain `GC_KIND_BLOCK`
case, and the two execution guards in `r_values`/`r_run_block`).

## 7. Heap / bootstrap impact

- No new bootstrap descriptor (reuses the existing `block!` built-in word/type).
- `GC_KIND_BLOCK` moved `0` → `7` (the reserved value); `GC_KIND_CTX=1`,
  `GC_KIND_CLOSURE=2` restored after.
- No relocation: the M3C runtime-state block `[24576, 25315)` and the code region
  `[256, 24576)` are unchanged; the added code fits without moving anything.

## 8. Live-object-count audit

Managed blocks are ordinary collected objects. The M3D suite's reclamation
assertion (J) uses the same `>= 32 cells` convention as M3C's string test; the
M2 live-object-count assertions are non-datatype runs and are unaffected.

## 9. Bug found and fixed

A latent GC register-clobber surfaced as a full-suite hang (infinite `lookup`
loop from a self-referential context). Root cause: `mark_value` uses `GC_T1` as
its own scratch for every pointer-bearing tag (`p = v - tag` for
CONTEXT/USER/STRING/BLOCK). `trace_closure` marked its `spec`/`body`/`ctx`
through `mark_value` while holding the closure pointer in `GC_T1`, without
saving it. This was harmless while loader blocks (the only spec/body values)
fell through `mark_value` untouched, but the new T_BLOCK case began clobbering
`GC_T1`, so `trace_closure` read `body`/`ctx` from the wrong address and marked
garbage, corrupting a live context (its parent became a self-pointer) and
freeing live objects. Fix: `trace_closure` now saves `GC_T1` on the return stack
across each `mark_value` call (the same discipline `trace_frame` already used).
No other `mark_value` caller keeps `GC_T1` live across a call.

## 10. git diff --stat

```
 Makefile        |   3 +-
 main.c          |   2 ++
 r0_s1.h         |   2 +-
 r0_s1_runtime.c | 110 ++++++++++++++++++++++++++++++++++++++++++++++++++++++--
 4 files changed, 112 insertions(+), 5 deletions(-)
```

(New untracked files: `r0_s1_m3d_lib.h`, `r0_s1_m3d_tests.c`, and the design
`M3D-MANAGED-BLOCK-DESIGN.md`.)

## 11. Files belonging to M3D

Modified (tracked):

- `r0_s1.h` — `GC_KIND_BLOCK` 0 → 7.
- `r0_s1_runtime.c` — `T_BLOCK`/`GC_KIND_BLOCK` RAW ABI symbols, `trace_block`,
  `mark_value` T_BLOCK three-way case, drain `GC_KIND_BLOCK` case, `emit_gc`
  hook, execution guards in `r_values`/`r_run_block`, and the `trace_closure`
  `GC_T1` save/restore fix.
- `Makefile`, `main.c` — build/run the M3D suite.

New (untracked):

- `r0_s1_m3d_lib.h` — the managed BLOCK! library (`blk-alloc`/`blk-at`/
  `blk-set`/`mk-block` mechanics + `append`/`block-at`/`block=?` policy).
- `r0_s1_m3d_tests.c` — the M3D acceptance suite (A–N).
- `M3D-MANAGED-BLOCK-DESIGN.md` — the authoritative design.

## 12. Deviation from M3D-MANAGED-BLOCK-DESIGN.md

None in the architecture. Two defects fixed during implementation, both in the
test/RAW transcription rather than the design: `mk-badblk-kind` (test L) was
missing its `DUP` before `LIT 16 MOD SUB`; and test M's `block-pick` index
needed `div16` (the RAW fragment takes a raw index, as `block-at` does). The
`trace_closure` `GC_T1` fix (§9) is a correctness fix to a pre-existing latent
bug exposed by the new T_BLOCK case, not a design change.
