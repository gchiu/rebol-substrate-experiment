# DEBUGGER D1 — Results

A small cooperative debugger whose control logic is ordinary R0, descending
through the generic RAW/S1 trapdoor only for genuinely low-level work
(capturing / inspecting / restoring execution state). The evaluator has no
knowledge that debugging exists.

**Result: success.** Full suite **149 `ok:` checks, 0 failures, exit 0** (23 of
them are D1 debugger checks A–I + stack instrumentation + runtime audit). The
frozen runtime/trapdoor and S1 are byte-for-byte unchanged.

---

## 1. Architecture

The debugger and the debuggee are two "worlds" swapped **in one S1 run**. Two
9-cell state records (fixed free-region buffers, addresses installed by the C
driver into `SCRATCH_A` / `SCRATCH_B`) hold:

```
[ resume_ip, SP, RP, HP, RV_CUR, RV_END, RV_CTX, RV_BLK, RV_FRAME ]
```

- `debug-break` (RAW, arity 0): the cooperative breakpoint. Pushes its own
  result `[NONE,1]` onto the debuggee's data stack, captures the debuggee
  record (A), restores the debugger record (B), pushes `[handle,1]` as the
  value the suspended `continue` will return, and jumps to the debugger's
  resume IP.
- `continue` (RAW, arity 0): save the debugger record (B), copy the live HP
  into record A's HP slot (monotonic heap — the two worlds never allocate over
  each other), restore the debuggee record (A), and jump to its resume IP.

The C harness is only the run driver: it emits nothing, parses source, seeds
record A with the debuggee's *initial* machine state (exactly what
`r0_s1_run` would install), and runs the debugger block. No debugger logic
lives in C.

## 2. Complete HLL debugger source

See `examples/debugger-basic.r0` and `examples/debugger-frames.r0` (canonical
listings), and the `DBG_LIB` string in `r0_s1_debug_tests.c`. The essential
definitions:

```
debug-break: raw [ ... capture A / restore B / push [handle,1] / jump ... ]
continue:    raw [ ... copy HP / capture B / restore A / jump ... ]

state-get:   raw 2 [ LIT 16 DIV >R LIT 16 DIV R> ADD @ ARITY 1 EXIT ]
state-get-i: raw 2 [ LIT 16 DIV >R LIT 16 DIV R> ADD @ LIT 16 MUL ARITY 1 EXIT ]
frame-get:   raw 2 [ LIT 16 DIV ADD @ ARITY 1 EXIT ]
ctx-count:   raw 1 [ DUP LIT 16 MOD SUB LIT 1 ADD @ LIT 16 MUL ARITY 1 EXIT ]
ctx-word:    raw 2 [ LIT 16 DIV >R DUP LIT 16 MOD SUB LIT 3 ADD R> LIT 2 MUL ADD @ ARITY 1 EXIT ]
ctx-val:     raw 2 [ LIT 16 DIV >R DUP LIT 16 MOD SUB LIT 4 ADD R> LIT 2 MUL ADD @ ARITY 1 EXIT ]
ctx-lookup:  raw 2 [ ... lexical chain-walk ... ]
frame-depth: raw 1 [ ... frame-chain counter ... ]
```

The debugger *session* (the POLICY layer) is ordinary R0:

```
st:  continue              ; start debuggee; returns the suspended state
ctx: state-get st 6        ; current lexical context
dbg-a: ctx-lookup ctx 'a   ; inspect a == 5
dbg-b: ctx-lookup ctx 'b   ; inspect b == 6
continue                   ; resume; debuggee finishes with 16
```

## 3. Complete RAW fragments

All ten fragments are in the `DBG_LIB` of `r0_s1_debug_tests.c` and in the two
example files. They use **only** the symbolic RAW ABI (`REG_IP/SP/RP/HP`,
`RV_CUR/END/CTX/BLK/FRAME`, `FRAME_*`, `SCRATCH_A/B`) — no hard-wired machine
addresses. The assembler resolves those names to their frozen layout constants.

## 4. HLL vs RAW responsibility split

| Concern | Layer |
|---|---|
| suspend/resume state machine (capture/restore/jump) | RAW (`debug-break`, `continue`) |
| single-cell reads of captured state | RAW (`state-get`, `frame-get`, `ctx-*`) |
| lexical chain-walk, frame-chain count | RAW (`ctx-lookup`, `frame-depth`) |
| **which** fields to show, **in what order**, when to continue | ordinary R0 (session body) |

Rough split: ~95% of the *policy* (what to present, when to resume) is R0; the
RAW layer is a thin set of generic mechanism primitives. The debugger's
presentation decisions (`dbg-a: ctx-lookup ctx 'a`, `dbg-depth: frame-depth fr`,
the choice to `continue`) are all ordinary R0 source.

## 5. Suspended-state representation

A **first-class opaque handle** (a tagged R0 integer): `handle = mk_int(base)`
where `base` is the address of the debuggee's 9-cell record. It is assignable
(`st: continue`) and passable (`inspect-state st`, `resume-it st`). The exact
layout it points to:

```
state[0] = resume_ip   (continuation to resume at)
state[1] = SP          (data-stack pointer at the breakpoint)
state[2] = RP          (return-stack pointer at the breakpoint)
state[3] = HP          (heap pointer at the breakpoint)
state[4] = RV_CUR      (current element address)
state[5] = RV_END      (one-past-last element address)
state[6] = RV_CTX      (current lexical context, tagged)
state[7] = RV_BLK      (current block base)
state[8] = RV_FRAME    (current activation-frame pointer, linked list)
```

Only these nine cells are captured — not the whole S1 image. The captured
`RV_CTX`/`RV_FRAME`/`RV_BLK` pointers reach the debuggee's live contexts,
frames and code; the transient evaluator scratch cells (`RV_T*`, `RV_NAT`,
`RV_WORD`, …) are already saved on the return stack by the enclosing
operations and are preserved by the `RP` capture.

## 6. Breakpoint mechanism

`debug-break` is an ordinary word bound to a zero-arity `raw` callable, so it is
invoked through the normal RAW dispatch (`r_subexpr` → `r_invoke_raw` → `CALL
entry`). On entry it:

1. pushes `[NONE, 1]` — its own result — onto the debuggee data stack;
2. captures the nine cells into record A;
3. restores the debugger's nine cells from record B;
4. pushes `[handle, 1]` as the value returned by the suspended `continue`;
5. jumps to the debugger's resume IP (`M[record_B + 0]`).

The suspend is a register jump, not a value. No `DEBUG` status, no
caller-by-caller propagation, no HOST service.

## 7. Resume mechanism

`continue` saves the debugger's nine cells into record B, copies the live HP
into record A's HP slot (so the debugger's inspection allocations never
overwrite the debuggee's live data — a monotonic shared heap), restores the
debuggee's nine cells from record A, and jumps to `M[record_A + 0]`.

Because `debug-break` left `[NONE,1]` on the debuggee stack and captured the
resume IP at the top of the return stack, the resume continues exactly after
the breakpoint, exactly once, with clean SP/RP/frame state.

## 8. Frame inspection

`state-get st 8` yields the innermost activation-frame pointer; `frame-get
frame off` reads `M[frame + off]` for the `FRAME_*` offsets; `frame-depth`
counts the `FRAME_PREV` chain. Test C (f→g→h) reports depth 3, plus the
innermost frame's site-id, saved IP, saved RP and saved context.

## 9. Locals / context inspection

`ctx-count`, `ctx-word`, `ctx-val` read a single context's bindings;
`ctx-lookup ctx word` walks the parent chain to the matching binding. Test B
reports `a == 5`, `b == 6`; test G reports the captured `n == 15` through the
closure's parent context. Values are read from the real captured context — not
faked.

## 10. Tests

`r0_s1_debug_tests.c` adds tests A–I + stack instrumentation + a runtime audit:

- **A** basic pause/continue: `x == 10` at break, result `15`.
- **B** locals: `a == 5`, `b == 6`; result `16`.
- **C** frames: depth 3, saved IP/RP/ctx; result `99`.
- **D** unaware callers: f/g/h have no debugger code; result `7`.
- **E** side-effect boundary: `counter == 1` at break, `11` after.
- **F** two breakpoints: `1`, then `11`, final `111`; no leakage.
- **G** closure state: captured `n == 15`.
- **H** debugged == normal: `15 == 15`.
- **I** first-class state: `resume-it st` (ordinary R0 fn) resumes; `x == 10`.
- **stack**: breakpoint SP/RP reported; final RP == debuggee baseline.

## 11. Stack measurements

For a nested breakpoint (f→g→h):

```
debugger baselines      SP=16384  RP=24576
debuggee baselines      SP=12000  RP=20000
SP/RP at breakpoint     11998 / 19987
deepest RP              19991
final SP/RP             11998 / 20000  (N=1)
```

The debuggee's final RP equals its own baseline (20000); its final SP holds
exactly the result set. No leakage.

## 12. Bugs encountered

1. **Missing result on resume.** The first cut of `debug-break` did not leave
   `[NONE,1]` on the debuggee stack, so `block-eval`'s discard read a garbage
   arity and corrupted control. Fixed by pushing `NONE LIT 16` first.
2. **Shared stack regions clobbering the debugger's return stack.** The
   debuggee initially used the standard `DS_INIT`/`RS_INIT`, overwriting the
   debugger's live return stack. Fixed by giving the debuggee a reserved lower
   region (`SP=12000`, `RP=20000`).
3. **Heap clobbering across worlds.** The debugger's inspection allocations
   could overwrite the debuggee's live data. Fixed by copying the live HP into
   the debuggee record on every `continue` (monotonic shared heap).
4. **R0 closure calls during inspection** (recursive/nested `lookup`/`find-in`,
   and a closure wrapping `continue`) triggered an address-dependent evaluator
   corruption (unbound word / bad opcode / infinite loop). Worked around by
   moving the chain-walk into generic RAW fragments (`ctx-lookup`,
   `frame-depth`); `resume-it` still works as an ordinary R0 function when the
   HP-copy fix is in place.
5. **Tag collisions on raw values.** Reading `SP`/`RP` returned cells whose low
   4 bits happened to be `SET`/`GET`/`LIT` tags, misinterpreting them on
   retrieval. Fixed with `state-get-i` (a tagged read) for numeric fields.
6. **`r0_s1_result` read after a retrieval reset SP.** The stack test read the
   result after `get_int` retrievals had reset the machine. Fixed by capturing
   the result before the retrievals.

## 13. Limitations

- This is a **machine/evaluator-state debugger**, not a source-level debugger.
  It knows only the cooperative breakpoint location and machine state; there is
  no source metadata, no `STEP`/`NEXT`/`FINISH`, no arbitrary source
  breakpoints, no watchpoints, no REPL/terminal UI. (D2 territory.)
- The debuggee runs on a reserved lower stack region; deep recursion is not
  bounded (documented, not checked).
- R0 closure calls *during* inspection are fragile (bug #4); the debugger's own
  inspection is therefore expressed as top-level R0 over generic RAW
  primitives.
- A debuggee breakpoint at the top level (no enclosing function frame) is not
  supported in D1.

## 14. Frozen-runtime verification

```
./check-frozen-s1.sh                                                  # OK
git diff --exit-code r0-trapdoor-v2 -- r0_s1_runtime.c r0_s1.h       # empty
```

Both pass; `r0_s1_runtime.c` and `r0_s1.h` are byte-for-byte unchanged. The
mechanical audit in `audit_runtime_clean` additionally asserts the runtime
contains none of `debug-break / breakpoint / suspend / debugger / resume /
DEBUG / HOST_DEBUG / HOST_BREAKPOINT / HOST_CAPTURE / HOST_RESUME`.

## 15. Was a new HOST operation required?

No. The fragments use only the frozen HOST arithmetic/comparison/allocation
(`ADD SUB MUL DIV MOD EQ GE` inside fragments, `ALLOC`/`PRINT` for the rest of
the existing set). No HOST op captures, unwinds or restores execution state.

## 16. Was an eighth S1 primitive required?

No. Everything is `LIT`/`DUP`/`DROP`/`@`/`!`/`0BRANCH`/`HOST` plus the derived
`CALL`/`EXIT`/`>R`/`R>`. The suspend/resume is a memory copy of the nine-cell
record into the memory-mapped registers and `RV_*` cells, followed by a jump.
