# FIB-OPT-P10D — Single top-of-data-stack caching

## 1. Hypothesis

P10C showed HOST dispatch was not the bottleneck. The remaining compiled-S1
cost is dominated by data-stack cell traffic through `M[sp]`. P10D tests:

> Does caching the top element of the S1 data stack in a C local materially
> reduce execution time?

One TOS cache only — no second-item caching, no dispatch/return-stack change.

## 2. TOS invariant and stack-pointer convention

Data stack grows down. In P10B/P10C, `sp` points to the top cell (`M[sp]` is the
top). P10D holds the top in a C local `tos` and shifts `sp` down one:

- `tos` = the logical top value (cached, not in memory).
- `sp` = the address of the **second-from-top** value (`M[sp]` is the second).
- The top slot in memory, if flushed, is `M[sp - 1]`.

Initial state (empty stack): `sp = M[1]` (DS_INIT), `tos = 0` (dummy; the first
push spills this dummy to `M[sp-1]`, and the S1 program's balanced stack
discipline never reads that phantom cell as a live value).

| operation | P10C (memory) | P10D (TOS) |
|---|---|---|
| push v | `M[--sp] = v` | `M[--sp] = tos; tos = v;` |
| DROP | `sp++` | `tos = M[sp++]` |
| DUP | `{v=M[sp]; M[--sp]=v;}` | `M[--sp] = tos;` |
| `@` | `{a=M[sp++]; M[--sp]=M[a];}` | `tos = M[tos];` |
| `!` | `{a=M[sp++]; v=M[sp++]; M[a]=v;}` | `{a=tos; v=M[sp]; tos=M[sp+1]; sp+=2; M[a]=v;}` |
| 0BRANCH | `{f=M[sp++]; ip=…}` | `{f=tos; tos=M[sp++]; ip=…}` |
| ADD etc. | `{b=M[sp++]; a=M[sp++]; M[--sp]=a+b;}` | `{b=tos; a=M[sp]; sp+=1; tos=a+b;}` |
| NEG | `{v=M[sp++]; M[--sp]=-v;}` | `tos = -tos;` |

## 3. Register-access idiom (the virtualised SP)

`IP` (`M[0]`) and `RP` (`M[2]`) remain direct locals, so `LIT 0/2 @`/`!` lower
as in P10B. `SP` (`M[1]`) is now **virtualised** (`tos` + `sp`), so the three SP
idioms need care:

- `LIT 1 @ @` (e_peek, = DUP) → `M[--sp] = tos;` (push the cached top).
- `LIT 1 @` (read SP, used to save/compare SP) → `{_s = sp - 1; M[--sp]=tos; tos=_s;}`
  (push the logical SP / top slot).
- `LIT 1 !` (write SP) → `{_v = tos; tos = M[_v]; sp = _v + 1;}` (restore SP =
  value; refill tos from the restored stack, set sp to value+1).

## 4. Flush / reload rules

- **Flush** = `M[--sp] = tos;` (spill tos to the top slot; `sp` now points to the
  top, the canonical memory-backed position). **Reload** = `tos = M[sp++];`.
- Flush/reload at genuine boundaries only: generic `HOST` (`ALLOC`/`PUTCHAR`/
  `PRINT`, which expect a memory-backed stack), `HOST_DUMP`, `HALT`, `L_bad`.
- Pure inline HOST and the register idioms need no boundary — they operate on
  `tos`/`sp`/`ip`/`rp` directly.

## 5. Correctness safeguards

Every lowering is a direct transcription of the P10C operation with the top
moved into `tos`; the SP idioms were derived from the memory-model semantics
(`sp - 1` is the top slot, `tos` is `M[top slot]`). `cell` arithmetic
(signedness, overflow, division truncation) is unchanged. No S1/Glon/RAW/frame/
task/debugger/GC semantics change; the frozen stream is untouched.
