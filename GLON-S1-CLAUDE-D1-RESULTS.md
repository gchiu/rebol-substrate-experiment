# GLON-S1-CLAUDE-D1 — Independent analysis and fix of the G3H task-mode startup halt

Date: 2026-11-26
Branch: `experiment/glon-s1-claude-d1` (from `experiment/glon-s1-d46`, `c3fcf64`)
Host: Debian 13 VPS, 967 MiB RAM, 1 GiB swap. All builds and tests run one at a time.
Expanded profile used throughout: `MEM_PROFILE=large MEM_LOADER_CELLS=500000
MEM_CODE_CAP=20480 MEM_GLOBAL_CONTEXT_CAP=512 CODE_RESERVE=128 M1_TASKS=3
M1_TASK_CELLS=3400 M1_DS_OFF=400 M1_RS_OFF=3400 M1_WRAPPER_DELTA=256`.

**Result: the root cause is found and fixed.** It was not a control-flow,
RETURN or activation-frame defect. The global context's hash index could not
encode binding slots ≥ 256. A second, independent defect in the expanded
memory profile (the return stack overlapping runtime state) was found and
fixed as well. G3H now starts in task mode.

## 1. The defect and the evidence

### 1.1 The halt is not `emit_return`

D3–D46 read `tag=3516` in the halt dump as "the `emit_return` no-matching-
activation guard fired". That reading is wrong. `g_halt_tag = 3516;` in
`emit_return` is a **C statement run once while code is generated**. It is not
emitted S1 code. So `g_halt_tag` always holds the last value assigned during
emission, whichever halt fires at run time. That is why no RETURN-entry trace
record ever appeared (D45/D46): no RETURN was executed.

To identify the halt independently, I briefly recorded each `asm_halt` call
site's C return address and resolved it with `addr2line` (temporary, now
removed). The halt that fires, at code address 12025 (dump `IP=12025`), is
**`emit_subexpr`, `r0_s1_runtime.c:3717`**: the fail-stop after `r_lookup`
returns -1, i.e. an **unbound word**. The `emit_return` halts are at 10845 and
10951 and never execute. The dump's `word=app/install` is `RV_WORD`, the last
set-word, so it is only a coincidence here. The program's last expression is
the lookup of `app/install` at the top level.

### 1.2 Why `app/install` cannot be found

The P5 context hash index (`r0_s1.h`, FIB-OPT-P5) stores each entry as
`word_id*256 + slot`. Lookup decodes `key = e/256`, `slot = e%256`. The
encoding silently assumes no context holds more than 256 bindings. The
assumption held until MEM-M2.1 made the global capacity configurable
(`GLOBAL_CTX_CAP=512`). For slot `s ≥ 256` the stored entry equals
`(word_id+1)*256 + (s-256)`, which causes two failures:

1. **The word itself misses.** Its probe sequence never meets its own key, and
   an empty bucket sends the lookup to the parent context. The global context
   has no parent, so the lookup returns -1 and SUBEXPR fail-stops (reason 0).
2. **Another word resolves to the wrong value, silently.** A lookup of the word
   with id `word_id+1` can hit the corrupted entry and read slot `s-256`.

Minimal reproducer (expanded task build, 300 top-level set-words `hx-K: K` in
three files, then one probe each):

| probe | before fix | after fix |
|---|---|---|
| `hx-5` | 5 | 5 |
| `hx-150`, `hx-200`, `hx-250`, `hx-260` | **halt** (reason 0, ip 12025) | correct |
| `hx-280` | **23** | 280 |
| `hx-298` | **41** | 298 |
| `hx-299` | **42** | 299 |

### 1.3 Why it appears only with `task.glon`

Global binding counts, measured with a temporary print after each top-level
run:

| build | before `server.glon` | during/after `server.glon` |
|---|---|---|
| non-task (`file,http,sqlite,sec`) | — | **252** (max) |
| task (`…,task`) | **267** | **270** at the halt |

`task.glon` adds about 16 globals. Without it, every slot stays below 256 and
the faulty encoding happens to work. With it, the application's own globals,
including `app/install`, land at slots 256–269. Raising the cap to 1024 (MEM-M2.6)
could not help, because capacity was never the problem. The "small programs
work" observations all had fewer than 256 globals.

## 2. Correction 1: hash slot radix (commit `1512bbb`)

- `r0_s1.h`: `#define R0S1_HASH_RADIX 65536L`, plus documentation of the encoding.
- `r0_s1_runtime.c`: all four encode/decode sites use the radix: C `bind`,
  emitted `r_lookup` (key and slot), and emitted `r_hash_insert`. Static asserts
  check that `GLOBAL_CTX_CAP` and `R0S1_CTX_CAP` ≤ radix, and that
  `SYM_CAP × radix` fits a 32-bit cell (WASM).
- No other code or library reads the encoding (checked with grep across `.c`,
  `.h` and all `.glon` masm). Emitted code size is unchanged (one literal
  replaced by another). No guard was removed or weakened.

## 3. Correction 2: expanded-profile return stack overlapped runtime state

While checking whether the expanded layout itself was sound, I found that
MEM-M2's Makefile shifted `RS_INIT` and `HEAP_BASE` up by
`MEM_CODE_CAP-16384`. The fixed runtime-state cells, however, sit at absolute
addresses in `[24576, 32768)`: `RV_*` (24595…), `GC_*`, `M1_*`, the M3 tables,
the `G1_OUT` buffer and the site table. With a code cap of 20480 the main return
stack `[20480, 28672)` therefore lay **on top of** `[24576, ~25315)`. The guards
and the stack sentry use `DS_INIT` as the return-stack floor, so they never
fired.

Evidence, from `down: func [n] [either = n 0 [0] [+ 1 down - n 1]]`:

| build | depth 36 | 40 | 42+ | 80 | 85+ |
|---|---|---|---|---|---|
| default constrained | ok | ok | ok | ok | clean RS fail-stop |
| expanded, before | ok | **hang** (from 37) | **hang** | **hang** | **hang** |
| expanded, after | ok | ok | clean RS fail-stop (ip 9918) | — | — |

The hang starts at depth 37, which is (28672−25315)/~90 cells per level.

Fix: in the large profile only the code/data-stack boundary moves.
`MEM_RS_INIT=24576` and `MEM_HEAP_BASE=32768`, as in the default map. A larger
code cap now costs return-stack cells (8192 − shift; 4096 at a cap of 20480). The
Makefile rejects `MEM_CODE_CAP` outside `[16384, 22528]`, and `r0_s1.h`
statically asserts `R0S1_RS_INIT ≤ 24576`. The old layout now fails to compile.
The default constrained build is unaffected.

## 4. Tests actually run

| test | result |
|---|---|
| Reproducer (§1.2), expanded task build | before: halt and wrong values; after: all correct |
| **New regression test** `make global-ctx-hash-test` (in `make test`) | PASS with the fix; **FAIL** (166 wrong words, 4 checks) with the radix reverted to 256 |
| G3H non-task, expanded, 9 route probes | all correct (200/201/404), before and after correction 2 |
| `make g3h-test` (G3H smoke, its own build) | G3H SMOKE PASS |
| G3H task mode, expanded, `--http-tasks` | **starts and listens**; `GET /api/users` **200**; other routes time out (§5.1) |
| `tests/h3c/acceptance.py` (task HTTP: method echo, SQLite, 3-task interleave with suspend/resume), expanded code cap | H3C ACCEPTANCE PASS |
| `tests/h2b/smoke.py` | H2B_SMOKE PASS (partial by design) |
| `make m1-large-test` at code cap 16384 and 20480 | all M1 tests passed |
| Nested closures/calls (expanded task, default) | 42 / 42 |
| spawn / yield / task-wait / task-wake / resume | `1 2 22 3 4` (correct order) |
| Default constrained `make glon` | builds; closures ok; 300 globals give a clean `context_full` |
| `./s1` (full native suite, default build, correction 1) | 30 FAIL lines, then stalls in the NaSch traffic section (900 s timeout). **Identical to baseline `c3fcf64`** (same 1503 lines, same 30 FAILs, same stall), so none come from this work. |
| `gc-worklist-overflow-test`, primer `--check` | pass |
| `./check-frozen-s1.sh` | fails, as at baseline: `s1.c`/`s1.h` were changed by D2/D42 (trace host id). Not changed here. |

`s1` was rebuilt after correction 2 (it compiles; the default layout is
unchanged), but its 15-minute run was not repeated.

Not run: `h3d-test` (a 1,000-request stress test, excluded by the resource
rules) and the WASM targets.

## 5. Remaining limitations

1. **Task-mode G3H serves only asynchronous responses.** This is pre-existing
   and independent of the fixes: a 3-line handler that responds synchronously
   also hangs under `--http-tasks`. `http/task-run` checks for a response with
   `http/take-response-for`, which **removes** it before the host's owner loop
   can collect it. Its "no response" fallback can also leave orphan entries at
   the head of `http/responses`. G3J10 §4 records the convention that task-mode
   handlers must reply from an async callback and `task-wait`. The G3H server
   predates that convention, and fixing it is an HTTP-bridge design change
   (H2B/H3), outside this assignment.
2. **`make h3c-test` as defined (code cap 16384) fails at load** with
   `masm code space exhausted` when `task.glon` loads. Baseline `c3fcf64` fails
   the same way: the D-series instrumentation in the runtime used up the code
   space. `h2b-test`/`h3d-test` use the same build line, so they presumably fail
   the same way (not run in that form). With the expanded code cap, h3c and h2b
   pass (§4).
3. **Diagnostic instrumentation is still in the runtime**: D42–D46
   `HOST_TRACE_FRAME`, `g_halt_tag`/`g_halt_pos`, and the H2Z lookup capture.
   The H2Z lookup capture writes `RV_BLK`/`RV_CUR`/the word into cells
   25040–25042, which are `GC_COLLECT_CNT`, `GC_LAST_RECLAM` and `GC_T1`, on
   every lookup, so `gc-count` readings are unreliable. `g_halt_tag` is
   misleading (§1.1) and should be removed or emitted as real code. I left all
   of this in place to preserve the diagnostic history. Removing it would give
   back code space (limitation 2).
4. With a code cap of 20480 the main return stack is 4096 cells (about 44
   closure levels) instead of 8192. This trade-off now fails cleanly instead of
   corrupting state silently.

## 6. Assessment

The substrate's control flow (CALL/RETURN, frames, closures, tasks) behaved
correctly throughout. Both defects were capacity assumptions that the
configurable expanded profile broke without anything checking them: an 8-bit
slot field and a fixed-address state block. Each is now enforced at compile
time. Larger composable applications are therefore no longer blocked by the
engine. The next limits are code space and the task-mode HTTP response
protocol.
