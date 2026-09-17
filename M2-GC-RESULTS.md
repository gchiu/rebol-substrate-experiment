# M2-GC-RESULTS.md — shared-heap garbage collection results

Exact, non-moving, stop-the-world mark/sweep over the one shared managed heap
`[32768,40000)`, emitted as S1 code above the frozen substrate. No eighth
primitive, no HOST/C GC, no new GLON syntax. See `M2-GC-DESIGN.md` for the
architecture and the resolved-bug record.

## 1. Acceptance outcome

* **Total native test assertions:** 196 (`ok:`), across the full suite
  (S1, adversarial, claims, R0, R0-S1, D1 debugger, M1, M2).
* **Failures:** 0.
* **Exit code:** 0.
* **M2 A–R result:** all 18 tests passed (A, B, C, D, E, F, G, H, I, J, K, L,
  M, N, O, P, Q, R).

## 2. Collection and reclamation metrics

* **Collection count during stress:**
  * test M (bounded-heap reuse): **50 collections**, 750 closures allocated.
  * test N (multitasking): **40 collections**, 1000 context switches.
* **Reclaimed cells/bytes (single collect):**
  * test A: **32 cells (256 bytes)** — one dead closure.
  * test M last collect: **2016 cells (16128 bytes)** (15 closures + 15 frames
    + 15 child contexts). `cell` = `intptr_t` = 8 bytes.
* **Evidence reclaimed storage was reused:**
  * test B: heap frontier unchanged after allocate→collect→drop→allocate
    (`h1 == h2`).
  * test M: 750 managed allocations completed inside a 7232-cell heap
    (high-water 39648), impossible without reusing swept blocks.
* **Peak / frontier heap state:**
  * test M high-water: **39648** (< 40000).
  * test N high-water: **38272** (< 40000).
  * test P (genuine live-set OOM): **40000**, halts cleanly (no bad opcode).
* **1000-switch multitasking result:** test N reached the shared counter
  **1000** with allocation + collection on both tasks; no diagnostics.

## 3. Architecture regression

* **frozen S1 check:** `./check-frozen-s1.sh` → **OK** (unchanged at
  `f90496c26dc45c7a387d8fc8639cd2781507d3d2`).
* No eighth primitive (S1 primitive set frozen).
* No GC graph traversal in HOST/C (collector is emitted S1 code).
* No new GLON syntax (`RV_CLOSURE`/`RV_CHILD` are ABI names for existing cells).
* RAW trapdoor semantics unchanged.
* Objects remain non-moving (mark/sweep only, no compaction).
* `REG_HP` remains global and is never restored per task.

## 4. Emitted code size

* `r0_s1_code_size()` = **6509 cells** (code region `[256, 6765)`).
* Well below the RV register area at **8192**, even with the M1/D1 RAW
  fragments assembled after it (full suite passed with no `bad opcode`).

## 5. Changed files

* `r0_s1_runtime.c` — collector + first-fit allocator, allocation-site rewrite
  (`mkctx`/`mkclosure`/`invoke_closure` use `r_alloc`), GC state seeding,
  diagnostics.
* `r0_s1.h` — GC state/heap/header constants + `r0_s1_gc_*` diagnostics.
* `r0_s1_m2_tests.c` — M2 acceptance tests (new).
* `M2-GC-DESIGN.md` — architecture + resolved-bug record.
* `Makefile`, `main.c` — build/run the M2 test module.
* `m1_layout.h` — `M1_WRAPPER_DELTA` (wrapper-block relocation).
* `r0_s1_m1_tests.c` — wrapper relocation + exported `M1_LIB` + audit wording.
* `r0_s1_debug_tests.c` — D1 state buffers moved to 8850/8870.
* `r0_s1_tests.c` — fresh `r0_s1_init()` per escape program.
* `standalone/glon.c` — seed M1 cells in `glon_init`.

## 6. Remaining limitations

* The managed heap is fixed at `[32768, 40000)` (7232 cells); genuine live-set
  exhaustion halts cleanly rather than growing.
* The mark worklist is 256 entries; the bounded heap caps the live set at
  226 objects (7232 / 32), so the worklist cannot overflow in practice.
* Collector diagnostics are test/audit-only, not GLON language features.
* No formal verification; correctness is established by the A–R acceptance
  suite and the full native suite only.
