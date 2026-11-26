# GLON-MEM-M2.7 — Identify the app/install Fail-Stop

Date: 2026-11-26
Branch: `experiment/glon-mem-profile-m27`
Baseline: G3J/MEM-M2.6, commit `e3c3aa0`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM.
**Diagnostic only — no capacity/eval/scheduler/auth change.**

## 1. Baseline preserved

Branch from `e3c3aa0`; G3J.6 diagnostics + `H3B-RESULTS.md` preserved.

## 2. Diagnostic (read-only, halt-only)

Added a read-only print in `r0_s1_dump_ip` (the existing D2A halt dumper). It
reads `RV_CTX`/`M[ctx+CTX_COUNT]`/`M[ctx+CTX_CAP]`, `RV_FRAME`, `REG_SP`,
`REG_RP` — **no Glon evaluation, no allocation, no machine-state change** (only
`M[]` reads and `fprintf`); it runs only on a machine halt.

## 3. Captured output (G3H task build, expanded profile)

```
[dump] IP=11971 SP=20480 RP=28670 HP=42944 top=0
glon: .../server.glon: ** halted (reason 0: no SIN!: a machine-level fail-stop)
  [ip=11972 ops 2 6 15 7 0 12245]
  [word=435 name=app/install ctx=1125447]
  [top word=0 name=func]
  [ctx=1125447 count=270 cap=512 frame=0 sp=20480 rp=28670]
  [blk=1158464 count=9 idx=-2]  [0] gw/users-db (tag 3)
```

## 4. Interpretation

- **Context occupancy 270 / 512 — NOT full.** So the fail-stop is **not** a
  context-capacity failure (and raising `GLOBAL_CTX_CAP` to 1024 earlier did not
  help, consistent).
- `frame=0` (RV_FRAME=0, the global frame) and `sp=20480` (= `DS_INIT`, the
  data-stack top → the data stack is empty). `rp=28670` (return stack).
- Halt `reason 0` = `R0S1_HALT_NONE` — a machine fail-stop with **no specific
  category**. The existing dumper shows the halt-time word/block and now the
  context/stack state, but **not the failing micro-operation** (the exact
  instruction that fail-stopped). The signature (empty data stack, no frame, at
  `app/install`) is consistent with a stack-underflow / return-with-no-frame, but
  that is a hypothesis, not observed.

## 5. Smallest demonstrated cause / unresolved boundary

- **Demonstrated:** the fail-stop is **not** context capacity (270/512) and occurs
  with an **empty data stack** (`sp=DS_INIT`) and `frame=0` at `app/install`.
- **Unresolved:** the exact failing operation/category (reason 0 gives none). The
  existing debugger cannot observe the specific stack micro-operation that
  fail-stopped; capturing it would require single-stepping the load (a new
  facility), which is out of scope.

## 6. Regression

- `./check-frozen-s1.sh` — PASS.
- Default build — PASS.
- Diagnostic is read-only and halt-only (no normal-execution effect).

## 7. Status / next

- Meaningful diagnostic captured — DONE (ctx 270/512 not full; sp empty; frame 0).
- Exact cause — **unresolved boundary** (reason 0; existing tools can't see the
  micro-operation). No capacity/app change made.
- Recommended next: a narrow single-step/guard on the stack op at `app/install`
to capture the exact failing instruction — a new diagnostic, not a redesign.

## 8. Commit

Branch `experiment/glon-mem-profile-m27`, one local commit. Not pushed/merged.