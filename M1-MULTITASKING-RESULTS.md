# M1 — Cooperative multitasking above frozen R0/S1

**Result: success.** Multiple independent GLON/R0 computations (tasks) are
suspended, scheduled and resumed without modifying S1, without adding an eighth
primitive, without changing evaluator semantics, and without putting any task or
scheduling semantics into C, HOST, or JavaScript.

The full native suite reports **170 `ok:` checks, 0 failures, exit 0**, including
10 new M1 multitasking checks (A–J) plus a 1000-context-switch stress run with
clean stacks and zero machine diagnostics.

```
./check-frozen-s1.sh                                                        # OK
git diff --exit-code r0-trapdoor-v2 -- r0_s1_runtime.c r0_s1.h             # empty
git diff --exit-code s1-frozen-v1 -- s1.c s1.h tests.c adversarial.c claims.c  # empty
```

`r0_s1_runtime.c`, `r0_s1.h`, and all frozen S1 files are byte-for-byte
unchanged.

---

## 1. Answer to the experiment's question

**Yes** — cooperative multitasking can be manufactured *above* the frozen R0/S1
substrate.  It did not require a new primitive.  The whole scheduler is a small
amount of ordinary R0 (`spawn`, `repeat`) plus generic RAW/S1 fragments
(`mnew-task`, `yield`, `task-finish`, `run-tasks`) assembled from the *frozen*
seven-primitive set.  The evaluator has no idea that a task or a scheduler
exists; it only knows that a `raw` value is callable.

The one genuinely new piece of mechanism is a *register-level context switch* —
save `[IP,SP,RP,RV_CUR,RV_END,RV_CTX,RV_BLK,RV_FRAME]`, restore another task's
record, jump — which is exactly the D1 debugger's suspend/resume generalized
from two fixed "worlds" to N task records.

---

## 2. Task representation

A task is an **opaque first-class R0 value**: an integer handle
`mk_int(record)` pointing at a 16-cell record in a fixed task table.  No new
evaluator type was added.  The handle is assignable (`task-a: spawn [...]`) and
passable.

```
task record (16 cells, TASK_TABLE = 60000, slot i at 60000 + 16*i):
  [0] resume_ip      (continuation / main entry)
  [1] SP             (task's data-stack pointer)
  [2] RP             (task's return-stack pointer)
  [3] RV_CUR         (evaluator: current element address)
  [4] RV_END         (evaluator: one-past-last element address)
  [5] RV_CTX         (evaluator: current lexical context, tagged)
  [6] RV_BLK         (evaluator: current block base)
  [7] RV_FRAME       (evaluator: current activation-frame pointer)
  [8] state          (0 = runnable, 1 = finished, 2 = empty slot)
```

**REG_HP is deliberately NOT in the record.**  It is global allocator state.

## 3. Saved state audit

The captured set is the same live evaluator registers the hardened D1 debugger
proved sufficient, **minus HP**:

| cell | captured? | reason |
|---|---|---|
| `IP` (`resume_ip`) | yes | continuation |
| `SP`, `RP` | yes | task's private stacks |
| `RV_CUR`, `RV_END`, `RV_CTX`, `RV_BLK`, `RV_FRAME` | yes | live evaluator state |
| `REG_HP` | **no** | global, monotonic, shared (see §4) |
| `RV_T1..T6`, `RV_N`, `RV_WORD`, `RV_NAT`, etc. | no | scratch / saved on RP (per D1 audit) |

## 4. Heap ownership

**One shared, monotonically advancing heap for every task.**  `REG_HP` starts at
32768 and is never saved/restored into a task record and never moved backward.
Task A may allocate; task B may then allocate on top of that; when A resumes,
the heap pointer is at the later global value.  All live task closures,
contexts and frames coexist in the shared heap.  This is the exact rule the D1
hardening report insisted on, and it is respected here by construction: nothing
in the switch path touches `REG_HP`.

Task records live in a fixed free region (60000..60128), not in the shared heap.
The scheduler keeps only two cells of its own state outside the heap: the
round-robin cursor and the current-task pointer.

## 5. Memory layout

| region | range | owner |
|---|---|---|
| registers / assembler scratch | 0..255 | `REG_IP/SP/RP/HP`, S1 `SC_A/SC_B` |
| evaluator code | 256..4302 | frozen R0-on-S1 |
| M1 RAW fragment code | 4302..5426 | `mnew-task/yield/task-finish/run-tasks/read-stress/spin` |
| D1 debugger buffers | 6000..6028 | D1 (not used by M1) |
| RV register file | 8192..8261 | frozen |
| M1 scheduler cells | 8262..8273 | main-entry, CUR_TASK, SCHED_REC (8), cursor, stress counter |
| M1 RAW scratch | 8276..8285 | S0..S9 |
| standard DS | 16384 down | scheduler world |
| standard RS | 24576 down | scheduler world |
| **shared S1 heap** | **32768..40000** | **all tasks (monotonic, never freed)** |
| loader heap | 40000..47000 | toolchain (parse time only) |
| **M1 task arena** | **47000..59800** | **per-task DS/RS** |
| **M1 task table** | **60000..60128** | task records |
| free | 60128..65535 | unused |

No region overlaps.  Fixed maximum task count for M1: **8**.

## 6. Stack allocation

Each task owns a private 1600-cell region of the arena: an 800-cell data stack
(top = `47000 + i*1600 + 800`, grows down) and an 800-cell return stack
(top = `47000 + i*1600 + 1600`, grows down).  The scheduler world uses the
standard stacks (16384 / 24576).  Task stacks never overlap each other or the
scheduler.

**Memory overhead per task:** 16 cells (record) + 1600 cells (stacks) = 1616
cells, all carved from free regions — no heap cost.

## 7. Context-switch mechanism

`yield` / `task-finish` (task → scheduler) and the switch tail of `run-tasks`
(scheduler → task) are plain RAW fragments.  `yield` pushes its own result
`[NONE,1]` onto the task stack, saves the task's nine live registers into
`CUR_TASK`'s record, restores the scheduler world from `SCHED_REC`, and jumps to
the scheduler's resume point.  The scheduler's `run-tasks` loop saves its own
state once (resume_ip = the loop point), then round-robins: pick the next
runnable task (a `MOD`-wrapped cursor scan over the fixed table), restore its
record, jump to its `resume_ip`.

A task's `resume_ip` is the evaluator's main entry on first run (so it evaluates
the wrapper block `[ do body task-finish ]`), and the return address of the
suspended `yield` call on subsequent runs.  `task-finish` marks the record
`finished` and returns to the scheduler; when no runnable task remains,
`run-tasks` returns `[NONE,1]` to the caller.

`spawn` and `repeat` are ordinary `func`s.

## 8. Tests (all automated, in `r0_s1_m1_tests.c`)

- **A** two tasks alternate across explicit `yield` → `121212`.
- **B** three tasks round-robin, all terminate → `123456`.
- **C** `yield` from inside an unaware call chain (`h→g→f`) → `11`.
- **D** recursive calls survive `yield`/resume (`fact 3`) → `6`.
- **E** lexical closures survive `yield`/resume → `15`.
- **F** independent locals/activation frames per task → `[1 2]`.
- **G** tasks intentionally share ordinary heap state → `303`.
- **H** closures allocated by alternating tasks are not overwritten → `[111 222]`,
  with 100 switches.
- **I** stress: **1000 context switches**, shared counter == 1000, scheduler
  RP/SP back to baseline, no `bad opcode`, no unbound-word `[dump]`, no hang.
- **J** mixed R0 closures + 400 raw switches → `[111 222]`, 400 switches.

Plus a mechanical audit asserting the frozen runtime/host files contain no
`spawn/yield/task/scheduler/round-robin/context-switch/multitask` semantics.

## 9. Stress results

`spin 500` × 2 tasks = 1000 context switches, single continuous run:

```
[stress] sp 16384->16382  rp 24576->24576 (N=1)
```

Scheduler RP returns exactly to baseline; SP holds exactly the result set;
shared counter reaches exactly 1000; zero `bad opcode`, zero `[dump]`, no hang.
The scheduler loop and the `spin` stress driver are allocation-free (their
counters live on the per-task data stack), so 1000 switches consume zero heap.

## 10. Frozen-layer integrity

- `./check-frozen-s1.sh` passes (S1 frozen at `s1-frozen-v1`).
- `git diff --exit-code r0-trapdoor-v2 -- r0_s1_runtime.c r0_s1.h` is empty.
- `git diff --exit-code s1-frozen-v1 -- s1.c s1.h tests.c adversarial.c claims.c`
  is empty.
- No HOST service was added; no eighth primitive was added.

## 11. WASM status

- **Native M1 cooperative multitasking is verified** (see §§8–9): all 170 native
  checks pass, including the 1000-switch stress run.
- **The WASM integration source has been prepared**, not completed: a shared
  layout header (`m1_layout.h`), a `glon_init()` environment-seeding change, a
  standalone demo (`standalone/app-m1.glon`) and a headless test
  (`standalone/node_test-m1.js`) exist.
- **The actual `glon.wasm` rebuild and execution have NOT yet been performed.**
  The Emscripten toolchain (`emcc`) was unavailable in this environment, so the
  WASM host change is unbuilt and untested.  It is not claimed as verified.

The architecture of the deployment is unchanged: the scheduler runs entirely
inside GLON/R0/S1, and the browser host (`glon.js`) remains a mundane boundary
that only provides `host_print`/`host_set_text` and the generic
`glon_alloc/glon_init/glon_load/glon_call` ABI.  JavaScript implements no
scheduling.

## 12. Prepared (unverified) WASM demo

The following exists but has **not** been rebuilt/executed under WASM (no
`emcc`); it is recorded here as prepared wiring, not a completed result:

- `glon_init()` seeds the same environment cells the native driver seeds after
  `r0_s1_init()`: `M[8262] = main_entry`, `M[8272] = 0` (cursor),
  `M[8273] = 0` (stress counter), and marks all 8 task slots empty, via the
  shared `m1_layout.h`.
- `standalone/app-m1.glon` is the M1 library + a two-counter demo intended to
  emit `host_set_text(1/2, 1..3)` interleaved `(1,1),(2,1),(1,2),(2,2),(1,3),(2,3)`.
- `standalone/node_test-m1.js` asserts that interleaving.

The demo source was run against the **native** runtime as a sanity check only
(emitting `1000001,2000001,1000002,2000002,1000003,2000003`), which proves the
GLON source is well-formed and the interleaving logic is correct — it does **not**
constitute a WASM verification.

## 13. Was an eighth primitive required?

No.  Everything is `LIT/DUP/DROP/@/!/0BRANCH/HOST` plus the derived
`CALL/EXIT/>R/R>`; a context switch is a memory copy of the nine-cell record
into the memory-mapped registers and `RV_*` cells, followed by a jump.

## 14. Bugs encountered (and fixed)

1. **Macro expansion in RAW source.**  Layout constants were first defined as
   compound expressions (`(M1_SCRATCH+0)`), which stringified to non-integer
   tokens.  Fixed by making each constant a plain integer literal.
2. **`@` after a base address.**  `LIT SCHED_REC @` read the *content* of the
   scheduler-record cell (0) instead of the record's address, so the scheduler's
   `resume_ip` landed at IP 0.  Fixed by removing the spurious `@` (the constant
   is a base address, not a pointer cell).
3. **Resume-point offset.**  Removing those `@`s shortened the `run-tasks`
   prologue, so the self-referential `LIT REG_IP @ LIT 12 ADD` offset changed to
   `11`.  The stress test (which fails loudly on a wrong resume point) verified
   the corrected value.
4. **RAW block size.**  The scheduler loop initially exceeded `parse_block`'s
   256-element cap; tightened with `MOD`-based wrapping and an inverted
   (`NE`) runnable test to 255 elements.
