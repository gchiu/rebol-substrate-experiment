# M2-GC-DESIGN.md — shared-heap garbage collection above frozen S1

**Status:** complete + verified. The collector is implemented and all M2
acceptance tests (A–R) plus the full native suite pass. The audit below is the
authoritative record of the architecture. §10 records the bugs found and fixed
during the debugging phase (all resolved).

---

## 1. Memory map (audited, final)

S1's flat cell memory `M[0..65535]` is laid out as follows. Every region is
disjoint.

| range | owner |
|---|---|
| 0..255 | registers + assembler scratch (`REG_IP`=0, `REG_SP`=1, `REG_RP`=2, `REG_HP`=3, `SC_A`=8, `SC_B`=9) |
| 256..~6597 | emitted code: **GC first, then the R0 evaluator** (`r0_s1_init` emits `emit_gc()` then the evaluator) |
| ~6597..~7750 | RAW-fragment code assembled at parse time (varies per test) |
| 8192..8247 | **RV register file** (`RV_CUR`=8192, `RV_END`=8193, `RV_CTX`=8194, `RV_WORD`=8195, `RV_NAT`=8196, `RV_HOSTCALLS`=8197, `RV_N`=8198, `RV_T1..T6`=8199..8204, `RV_CLOSURE`=8205, `RV_ARITY`=8206, `RV_CHILD`=8207, `RV_BODY`=8208, `RV_NVALS`=8209, `RV_RPMIN`=8212, `RV_SPMIN`=8213, `RV_BLK`=8214, `RV_FRAME`=8215, `RV_SITE`=8216, `RV_SIP`=8217, `RV_SRP`=8218, `RV_FNEW`=8219, `RV_RES_BUF`=8232..8247) |
| 8260..8261 | `RV_SCRATCH_A/B` (RAW ABI `SCRATCH_A/B`) |
| 8262..8285 | M1 cells (`M1_MAIN_ENTRY_CELL`=8262, `M1_CUR_TASK`=8263, `M1_SCHED_REC`=8264, `M1_CURSOR`=8272, `M1_STRESS_CNT`=8273, `M1_SCRATCH`=8276..8285) |
| **8286..8568** | **GC state** (see §8) |
| 8850..8878 | D1 debugger state records (`DBGEE_BUF`=8850, `DBGER_BUF`=8870; moved here from 6000 to stay above the grown code region) |
| 9000 | scratch used by one `r0_s1_tests` RAW test (keep clear) |
| 12000 / 20000 | D1 debuggee DS / RS tops (grow down) |
| 16384 / 24576 | standard DS / RS tops (`R0S1_DS_INIT`/`R0S1_RS_INIT`) |
| **32768..40000** | **managed (collected) heap** `GC_HEAP_BASE..GC_HEAP_LIMIT` |
| 40000..47000 | loader heap (`R0S1_HEAP_BASE..R0S1_HEAP_LIMIT`), permanent |
| 47000..59800 | M1 per-task DS/RS arena (`M1_ARENA_BASE`, 1600 cells/task) |
| 60000..60128 | M1 task table (8 records × 16 cells) |
| 60128..60256 | M1 wrapper blocks (fixed region; `M1_WRAPPER_DELTA`=128) |
| 60256..65535 | free |

The collected heap is bounded below by the RS region and above by the loader
heap; `REG_HP` is the shared, global, never-restored high-water frontier.

---

## 2. Managed allocation sites (every one)

**Loader (C `lalloc`, permanent, never collected/swept):**
* `make_block` — BLOCK, `lalloc(2+cap)`.
* `make_context` — global CONTEXT, `lalloc(CTX_DATA + 2*cap)` (only the **global**
  context is loader-allocated).
* RAW callables — `parse_block`'s `raw` handling, `lalloc(2)` → `[entry, arity]`.

**Runtime collected (S1 `r_alloc`, in the managed heap):**
* `emit_mkctx` — child CONTEXT, payload 48 cells (`(CTX_DATA+2*R0S1_CTX_CAP+15)&~15`).
* `emit_mkclosure` — CLOSURE, payload 16 cells.
* `emit_invoke_closure` — activation FRAME, payload 16 cells.

**Runtime fixed (not collected, no header):**
* M1 wrapper block — `mnew-task` RAW writes it at `60128 + slot*16`
  (`[count=3, site=0, 'do, body, 'task-finish]`). It is a BLOCK but lives outside
  the managed heap so that `RV_BLK`/`RV_CUR`/`RV_END` (and their return-stack
  copies) never point into the collected range.

---

## 3. Object layouts and their managed-reference fields

Tagged values are `payload | tag` with 4-bit tags: INT=0, NONE=1, WORD=2, SET=3,
GET=4, LIT=5, BLOCK=6, CONTEXT=7, CLOSURE=8, NATIVE=9, RAW=10.

| kind | tag | layout | managed-reference fields |
|---|---|---|---|
| BLOCK | 6 | `[count, site, elems…]` (`BLK_COUNT`=0, `BLK_SITE`=1, `BLK_DATA`=2) | `elems[i]`, `i<count` (tagged values) |
| CONTEXT | 7 | `[parent, count, cap, (word,value)…]` (`CTX_PARENT`=0, `CTX_COUNT`=1, `CTX_CAP`=2, `CTX_DATA`=3) | `parent` (tagged), `value` cells at `CTX_DATA+1+2i`, `i<count` |
| CLOSURE | 8 | `[spec, body, ctx, site]` (`CLOSURE_SPEC/BODY/CTX/SITE`=0/1/2/3) | `spec` (BLOCK), `body` (BLOCK), `ctx` (CONTEXT) |
| RAW | 10 | `[entry, arity]` (`RAW_ENTRY`=0, `RAW_ARITY`=1) | none |
| FRAME | (untagged) | `[prev, site, SP, RP, IP, CTX, CUR, END, BLK]` (`FRAME_PREV`=0 … `FRAME_BLK`=8) | `prev` (frame ptr), `CTX` (tagged), `BLK` (block ptr). SP/RP/IP/CUR/END are addresses, `site` int |

Word cells in contexts are immediate word-ids (never pointers). Block `site` and
closure `site` are integers.

---

## 4. What the collected heap actually contains

Audited invariant: the **managed heap contains only CLOSURE, child CONTEXT and
FRAME objects**. BLOCK, the global CONTEXT, and RAW callables are loader objects
(permanent) and are *traced but never swept*. Two consequences:

* Loader **BLOCK**s never contain a collected pointer (they hold parse-time
  values only), so the collector does **not** trace blocks at all.
* Loader **CONTEXT**s (the global context) **can** hold collected pointers
  (`c: make-counter 10`), so they are traced.

---

## 5. GC root set (exact)

The collector never treats an arbitrary integer as a pointer; it understands the
R0 tags and object layouts above.

1. `global_ctx` — mirrored into `M[GC_GLOBAL_CTX]` by `r0_s1_init`.
2. `RV_CTX`, `RV_CHILD` (tagged values), `RV_CLOSURE` (untagged closure ptr),
   `RV_FRAME` (frame chain). (`RV_BLK`/`RV_CUR`/`RV_END` point into permanent
   blocks and are not rooted.)
3. **Active data stack** — scanned as tagged values:
   * `REG_SP <= DS_INIT` (main world): `[REG_SP .. DS_INIT)`.
   * else (a task runs): `[REG_SP .. task_DS_top)` with task index
     `(REG_SP - M1_ARENA_BASE)/M1_TASK_CELLS`.
4. **Active return stack** — scanned for `[32768,40000)` cells (the only
   collected pointers ever saved on the return stack are `RV_CLOSURE` values; see
   §6).
5. **Scheduler/main-world DS/RS** — `[M1_SCHED_REC+1 .. DS_INIT)` and
   `[M1_SCHED_REC+2 .. RS_INIT)` (seeded to empty ranges in `r0_s1_init`).
6. **Every M1 task record** (state ≠ `TASK_EMPTY`): `TREC_CTX` (value),
   `TREC_FRAME` (frame chain), plus the task DS `[TREC_SP..task_DS_top)` (values)
   and task RS `[TREC_RP..task_RS_top)` (closure scan). `TREC_BLK`/`TREC_CUR`/
   `TREC_END` point into permanent blocks.
7. **Frame chains** (from `RV_FRAME` and each `TREC_FRAME`): trace `FRAME_CTX`,
   `FRAME_PREV` (recursively). `FRAME_BLK` is a permanent block (not traced).
8. **Closure fields** and **context value cells** (reached transitively).

`RV_CHILD`/`RV_CLOSURE` are kept 0-or-live: cleared to 0 at `emit_invoke_closure`
entry (`RV_CHILD`), at its normal-return epilogue, and at `emit_return` (both).
They are rooted unconditionally (a stale-but-allocated value is only
over-retained, never dereferenced after free, because rooting keeps it alive).

---

## 6. Return-stack exactness (RAW contract)

The return stack is **not** scanned as values. The exact invariant is: the only
values that ever land in `[32768,40000)` on the return stack are saved
`RV_CLOSURE` (untagged closure) copies. Return addresses are code (< ~6597),
saved `RV_BLK/RV_CUR/RV_END` point into the permanent loader heap (≥40000, after
the wrapper-block relocation of §2), and saved `RV_WORD/RV_NAT/RV_T4/RV_T5/RV_T6/
RV_ARITY` are small immediates. Therefore range-scanning the return stack for
`[32768,40000)` cells is **exact** for this machine.

RAW memory is trusted systems-level memory. The collector **never** scans
arbitrary RAW scratch looking for heap-looking values. Managed references inside
known RAW machinery (M1 task records, scheduler record, RV cells) are included in
the root set explicitly and by name. A managed object referenced only by
unregistered RAW scratch (e.g. a value stashed at cell 9000 by a test) is **not**
rooted and may be collected.

---

## 7. Allocator / GC metadata (implemented)

Every collected object has a **16-cell-aligned header immediately before its
16-aligned payload** (16-cell granularity is forced by the 4-bit tag scheme).

```
block base B (16-aligned):
  B+0  size    : total block extent in cells (multiple of 16; = 16 + payload)
  B+1  flags   : bit0 allocated, bit1 marked, bits2.. kind<<2
  B+2..B+15    : unused padding
payload = B + GC_HDR_STRIDE (= B+16)
```

```
GC_HDR_SIZE=0  GC_HDR_FLAGS=1  GC_HDR_STRIDE=16
GC_FLAG_ALLOC=1  GC_FLAG_MARK=2
GC_KIND_BLOCK=0  GC_KIND_CTX=1  GC_KIND_CLOSURE=2  GC_KIND_RAW=3  GC_KIND_FRAME=4
```

`r_alloc(n, kind)`: first-fit scan of free blocks below `REG_HP` (split if the
remainder ≥ 32), else bump `REG_HP`; on failure run `r_collect` once and retry;
on the second failure HALT with a dump (clean OOM). `REG_HP` is never lowered.
The tooling escape: if `REG_HP >= GC_HEAP_LIMIT` (the D1 debugger's `set-hp`
50000), `r_alloc` does a plain bump (no header, never collected) — this is the
historical tooling heap and is not part of the managed heap.

Mark phase is **tag-driven** (headers only needed for sweep), with an explicit
256-entry worklist. Sweep walks `[GC_HEAP_BASE, REG_HP)` by `size`, clears the
mark bit on live blocks, frees dead blocks, and **coalesces adjacent free runs**.

---

## 8. GC state cell layout (8286..8568)

```
GC_GLOBAL_CTX 8286   GC_LOADER_HP 8287
GC_LIVE_CELLS 8288   GC_LIVE_OBJS 8289   GC_FREE_BLOCKS 8290   GC_FREE_CELLS 8291
GC_COLLECT_CNT 8292  GC_LAST_RECLAM 8293
GC_T1..GC_T5 8294..8298   (mark/trace leaf functions)
GC_T6..GC_T8 8299..8301   (scan/drain/sweep transient)
GC_C1..GC_C6 8302..8307   (collect() persistent: SP0, RP0, task index, rec, top, prev_free)
GC_WL_SP 8308
GC_WORKLIST 8309..8564    (256 entries)
GC_A1 8565  GC_A2 8566    (alloc() persistent: extent, kind)
```

Scratch discipline: the collector calls itself recursively, so **nested calls must
not clobber the caller's live cells**. `GC_T1..T5` are used only by the leaf
mark/trace functions (mark_value, mark_push, trace_ctx, trace_closure,
trace_frame, mark_frame, mark_closure_inline); `GC_T6..T8` only by
scan_values/scan_closures and the drain/sweep; `GC_C1..C6` only by collect(); and
`GC_A1/A2` only by alloc() for the values it needs across its call to collect().
(`GC_LOADER_HP` is the loader-heap bump cell shared with `lalloc`.)

Diagnostics (`r0_s1_gc_*` in `r0_s1.h`) expose collection count, live/free
cells, live objects, free blocks, reclaimed cells, the collector entry address,
and the heap frontier — for tests/audit only, not GLON features.

---

## 9. What M2 does NOT do

No eighth primitive; no new S1 opcode; no GC in HOST (`HOST_ALLOC` in frozen
`s1.c` is no longer called by the evaluator); no new GLON syntax or user-visible
type; no copying/compacting (objects never move, so RAW/GLON addresses stay
valid); no conservative scanning of arbitrary integers or RAW scratch; no
WASM/JS/server/UPARSE work.

---

## 10. Resolved bugs (debugging record)

Every bug found during the debugging phase was a wrong write / wrong state
transition inside the emitted collector, never an architectural change. The
architecture described in §1–§9 is unchanged and now verified end-to-end.

1. **Scratch-cell clobbering across nested `mark_value` calls (resolved).**
   `trace_ctx` and `trace_frame` held live state (`p`, `count`, `i`) in
   `GC_T1/GC_T4/GC_T5` across recursive `mark_value` calls, which overwrote
   them (skipping context bindings beyond the first closure, and mis-tracing
   frame `prev` vs `ctx`). Fixed by saving/restoring that state on the return
   stack (`>R`/`R>`) around each recursive call, making `trace_ctx`/`trace_frame`
   reentrant without relying on the cell partition.

2. **Closure `spec` field becoming 8309 (`GC_WORKLIST`) (resolved).** Two
   independent defects:
   * `mark_push` wrote the worklist with the value and address in the wrong
     stack order (`M[p] = 8309` instead of `M[8309+sp] = p`), landing the
     worklist base address on the object's payload.
   * the allocator encoded `flags = kind*2 + ALLOC` while the drain/sweep decode
     `kind = (flags/4)%8` (i.e. `kind<<2`), so every object's kind was halved.
   Fixed to the documented `kind<<2` encoding.

3. **Active-world branch inverted (resolved).** The `SP0 > DS_INIT` test
   dispatched to the opposite world (main vs running task), scanning the wrong
   stack ranges. Fixed by swapping the two branches.

4. **Task-record root condition inverted (resolved).** The collector scanned
   records with `state != TASK_EMPTY`, which includes `TASK_FINISHED`; finished
   tasks kept their stale `TREC_CTX`/`TREC_FRAME` alive. Fixed to scan only
   `state == TASK_RUNNABLE`, so terminated tasks stop retaining objects.

5. **Scheduler DS/RS double-fetch (resolved).** `e_cell(M1_SCHED_REC+1)` already
   fetches the saved SP/RP; a second `asm_fetch()` re-dereferenced it, scanning
   the whole memory map. Removed the extra fetch.

6. **Tooling escape boundary (resolved).** The `REG_HP >= GC_HEAP_LIMIT` plain
   bump fired when the managed heap was exactly full (`REG_HP == 40000`), bumping
   into the loader heap instead of OOM-halting. Fixed to `REG_HP > GC_HEAP_LIMIT`.

7. **Stress-test parameters (resolved).** `repeat 500 [ mk 50 … ]` nests one R0
   frame+context per level, so its live-set peak exceeded the 7232-cell managed
   heap; the stress now uses a bounded depth with a RAW `spin` switch loop
   (M: 750 allocations/50 collections; N: 1000 switches/40 collections). The
   reclamation itself was correct (reuse verified).

No new primitive, no HOST/C GC, no new GLON syntax, non-moving, and a single
global never-restored `REG_HP` — all unchanged and re-verified (§9, Phase 6).

---

## 11. Source locations still to inspect if re-auditing

* `r0_s1_runtime.c`: `emit_gc`/`emit_mark_*`/`emit_trace_*`/`emit_scan_*`/
  `emit_collect`/`emit_alloc` (collector); `emit_mkctx`/`emit_mkclosure`/
  `emit_invoke_closure`/`emit_return` (allocation sites + RV clears);
  `lalloc`/`make_block`/`make_context`/`parse_block` (loader + raw callables);
  `r0_s1_init` (GC state seeding).
* `r0_s1.h`: GC cell constants, header constants, kind/flag constants.
* `r0_s1_m1_tests.c`: `M1_LIB` (mnew-task/yield/task-finish/run-tasks — the
  wrapper relocation and scheduler/task records that the GC roots).
* `m1_layout.h`: `M1_WRAPPER_DELTA`, task table/arena constants.
* `r0_s1_m2_tests.c`: the acceptance tests.
* `r0_s1_debug_tests.c`: D1 buffer relocation (8850/8870).

---

## 12. Feasibility conclusion

An exact, non-moving collector is feasible **without changing R0 value semantics
or adding a primitive**, because: (a) the collected heap is exactly three kinds
(closures/contexts/frames) whose layouts are known and whose fields are tagged or
known pointers; (b) the only return-stack pointer into the collected range is
`RV_CLOSURE`, making the return-stack scan exact; (c) the wrapper-block relocation
keeps `RV_BLK`/`RV_CUR`/`RV_END` out of the collected range; (d) the RV
0-or-live discipline makes the two transient roots safe to trace unconditionally.
The open items are implementation bugs (cell clobbering, one address-corruption
case), not architectural blockers.
