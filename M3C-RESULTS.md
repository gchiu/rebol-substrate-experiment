# M3C-RESULTS.md — managed immutable STRING! (first slice)

M3C implements the first STRING! slice on the frozen R0/S1 substrate:
intrinsic tag `T_STRING = 12` over a managed, immutable byte series
(`GC_KIND_STRING = 6`). It replaces M3B's `block!`-of-char-codes labels with
real `string!` names in the genealogy demo, without any new S1 primitive,
evaluator/parser special case, HOST service, or native id.

## 1. Assertion count

**278 `ok:`, 0 failures**, exit 0. (M3A/M3B milestone was 249; M3C adds 29
assertions: A–S.)

## 2. Failures

0.

## 3. M3C A–P results

All pass:

- **A** — `make string! [65 108 105 99 101]` constructs "Alice" (length 5).
- **B** — `type? s` = `string!`; `string?` predicate correct.
- **C** — `length?` returns byte length (5; empty string 0).
- **D** — `string-byte` returns 65/108/101 at indices 0/1/4.
- **E** — `append` returns a new string; the original is unchanged; bytes copied
  and appended byte correct.
- **F** — `string=?` compares content, not identity; distinct equal objects are
  not `=`.
- **G** — `copy` returns the string itself (immutability).
- **H** — byte <0 (`-1`) and >255 (`256`) rejected (0 results).
- **I** — wrong input datatype (`block!` element) rejected (0 results).
- **J** — reachable string survives GC.
- **K** — unreachable string reclaimed (≥ 32 cells).
- **L** — forged out-of-range `T_STRING` halts cleanly as corruption.
- **M** — corrupt length exceeding extent halts cleanly as corruption.
- **N** — `STRING!` nested in a `person!` survives GC with bytes intact.
- **O** — M3B genealogy demo now uses `string!` names and still renders
  Alice/Bob/Charlie (see §9).
- **P** — all prior suites (M3A A–Q, M3B A–H/R/S, nested-closure, M2, M1, …)
  unchanged.
- **S** — `STRING!` semantics absent from frozen substrate/evaluator/parser/HOST.

## 4. Frozen-S1 result

`./check-frozen-s1.sh` → **OK** (`f90496c26dc45c7a387d8fc8639cd2781507d3d2`);
`s1.c`/`s1.h`/`tests.c`/`adversarial.c`/`claims.c` byte-identical.

## 5. Exact STRING! object layout

```
STRING! value = p + T_STRING   (T_STRING = 12); p = 16-aligned payload pointer

header (B = p - 16):
  B+0  size   : extent = 16 + align16(1 + length)
  B+1  flags  : GC_FLAG_ALLOC | GC_KIND_STRING<<2  (| GC_FLAG_MARK while marking)

payload (p):
  p+0       length : raw byte count N
  p+1..p+N  bytes  : raw integers 0..255, one byte per cell
  p+N+1..   alignment padding (unused; strings are immutable)
```

`trace_string` validates `length <= size - 17` (fail-stop on corruption) and
traces **no children** (leaf object). `mark_value` range-checks the payload in
`[GC_HEAP_BASE, GC_HEAP_LIMIT)` and `mark_push`es it (identical to the `T_USER`
case). Immutable: the only byte writes are construction-time (`str-fill`, used
by `mk-string`/`append` before the value is published).

## 6. Code-size change

Base emitted code: **7003 cells** (was 6832 at the M3B milestone; **+171 cells**
for the `T_STRING` `mark_value` case, `trace_string`, and the drain
`GC_KIND_STRING` case).

## 7. Heap / bootstrap impact

- One more permanent bootstrap descriptor: `string!` seeded at heap header
  33152 / payload **33168** (`BUILTIN_STRING_PAYLOAD`), `BUILTIN_TYPE[12]`
  rooted by the existing 16-slot scan; `REG_HP` advances 33152 → 33184.
- **Runtime-state relocation (implementation necessity).** The M3C collector
  code plus the string-library RAW fragments filled the old code region
  `[256, 8567)`, so the runtime-state block (`GC_META`, `BUILTIN_BASE`, `RV`
  file, `RV_SCRATCH_*`, D1 buffers, M1 cells, GC state) was relocated from
  `[8567, 9306)` up to `[24576, 25315)` — the free gap between the return-stack
  top `RS_INIT=24576` and the managed heap `GC_HEAP_BASE=32768`. The code region
  is now `[256, 24576)`. This is the same kind of upward relocation M3A did, and
  is transparent to all symbolic ABI users.

## 8. Live-object-count audit

No existing test asserts a live-object count in a datatype run, so **no existing
expectation changes**. Specifically:

- The new `string!` descriptor is a **+1 permanent live object** in datatype runs
  (12 descriptors → 13), but the only `r0_s1_gc_live_objs()` assertions are in
  the M2 suite (`== 0`, `== 1`, `== 2`), which are **non-datatype** runs
  (`r0_s1_seed_datatypes()` is not called there, so no descriptors are seeded and
  `BUILTIN_TYPE` is all `NONE`).
- The M3A/M3B assertions that touch the heap use `r0_s1_gc_free_cells() >= N`,
  which counts *dead* objects swept; the `string!` descriptor is a permanent
  root and is never freed, so it does not change `free_cells`.

Therefore: no test's expected count changed; no `+1` adjustment was required.

## 9. M3B SVG verification

`examples/m3b-genealogy.svg` (regenerated) contains `Alice`, `Bob`, `Charlie`,
three `<line>` elements, and the node `<rect>`/`<text>` elements; the
59-integer surface-command stream is byte-identical to the golden sequence.
The genealogy `person!` now has `name: string!`, built via
`name: make string! [...]` and referenced by get-word in `make person! [0 :name]`
(the `make-value` argument block resolves get-words, not nested calls).

## 10. git diff --stat

```
 Makefile            |   5 +--
 m1_layout.h         |  40 +++++++++---------
 main.c              |   2 ++
 r0_s1.h             |  94 ++++++++++++++++++-------------------------
 r0_s1_debug_tests.c |   6 +--
 r0_s1_m3b_tests.c   |  75 +++++++++++++++++-----------------
 r0_s1_runtime.c     |  65 ++++++++++++++++++++++++++--
 7 files changed, 177 insertions(+), 108 deletions(-)
```

(New untracked files: `r0_s1_m3c_lib.h`, `r0_s1_m3c_tests.c`.)

## 11. Files belonging to M3C

Modified (tracked):

- `r0_s1.h` — `T_STRING=12`, `GC_KIND_STRING=6`, `mk_string`, `BUILTIN_STRING_PAYLOAD`, relocated runtime-state addresses.
- `r0_s1_runtime.c` — `T_STRING`/`GC_KIND_STRING` RAW ABI symbols, `trace_string`, `mark_value` T_STRING case, drain case, `emit_gc` hook, `string!` bootstrap (word + slot-12 descriptor + `REG_HP`).
- `m1_layout.h`, `r0_s1_debug_tests.c` — address relocation only (M1 cells, D1 buffers).
- `r0_s1_m3b_tests.c` — genealogy `person!` uses `string!` names; `draw-text` uses `length?`/`string-byte`.
- `Makefile`, `main.c` — build/run the M3C suite.

New (untracked):

- `r0_s1_m3c_lib.h` — the STRING! library (RAW mechanics + GLON policy), shared by M3C and M3B.
- `r0_s1_m3c_tests.c` — the M3C acceptance suite (A–S).

(Plus the previously-submitted design `M3C-STRING-SERIES-DESIGN.md`.)

## 12. Deviation from M3C-STRING-SERIES-DESIGN.md

None in the architecture. One implementation necessity the design did not
anticipate: the runtime-state cell block had to be **relocated upward** (to
`[24576, 25315)`) because the M3C GC code and string-library RAW fragments
filled the code region `[256, 8567)`. This is the same upward relocation M3A
performed; it changes no semantics and no design decision. Two implementation
bugs found and fixed during the slice (inverted `mk-string` loop conditions;
`str-fill` store operand order) were defects in the RAW transcription, not
design changes.
