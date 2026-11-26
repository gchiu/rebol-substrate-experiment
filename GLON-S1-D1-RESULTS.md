# GLON-S1-D1 — Diagnose the Execution-Engine Fail-Stop (investigation checkpoint)

Date: 2026-11-26
Branch: `experiment/glon-s1-d1`
Baseline: G3J/MEM-M2.7, commit `7b07554`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM.
**Investigation only — no S1/evaluator change (cause uncertain).**

## 1. Baseline

- Branch `experiment/glon-s1-d1` from `7b07554`.
- Original S1 checksum (historical reference): `f90496c26dc45c7a387d8fc8639cd2781507d3d2`
  (frozen files unchanged at this commit).
- G3J.6 diagnostics + `H3B-RESULTS.md` preserved.

## 2. Reproduction (expanded profile)

- G3H **non-task** (`MODULES=file,http,sqlite,sec`) → **HTTP 200, listening**.
- G3H **task** (`MODULES=…,task`, `--http-tasks`) → **machine halt** at
  `app/install` (reason 0).

## 3. Diagnostic evidence

- The read-only halt dumper (MEM-M2.7) reports at the fail-stop:
  `ctx count=270 cap=512` (**not full**), `frame=0`, `sp=20480` (=`DS_INIT`,
  empty data stack), `rp=28670`.
- The **stack sentry did NOT fire** (no `stack sentry: …` message) → the SP/RP
  bounds were **not** violated.
- Halt `reason 0` = `R0S1_HALT_NONE` — an `OP_HALT` executed without setting a
  specific `RV_HALTWHY`. So it is **not** `CONTEXT_FULL`, **not** GC worklist
  full, and **not** a stack-sentry violation.

## 4. Earliest divergence

- The only build difference is `task.glon`. It changes the interned symbol set
and the loaded vocabulary, but **not** the code path before `http/listen`
(the app installer runs at load, before any task). The fail-stop is at
`app/install` with an empty data stack and no frame.
- This is **not** a capacity failure (ctx 270/512) and **not** a stack-bounds
failure (sentry silent). It is an `OP_HALT` from a fail-stop path that does not
set `RV_HALTWHY`.

## 5. Correction

**None applied.** The exact `asm_halt` emitter (which fail-stop path fires) was
not identified with the existing tools: the halt dumper shows the state but not
the originating check, and reason 0 gives no category. Per the task, I stop with
this evidence rather than guess or redesign the evaluator. No S1/evaluator change
was made; frozen S1 remains recoverable at `f90496c…`.

## 6. Status vs criteria

- Reproduce non-task 200 / task halt — DONE.
- Instrument the boundary (read-only) — DONE (ctx 270/512, empty SP, frame 0).
- Earliest divergence — isolated to the task build at `app/install`; **not**
capacity, **not** stack bounds.
- Root cause / correction — **UNRESOLVED** (the specific fail-stop check is not
observable with existing tools; reason 0).
- Frozen S1 / default build — PASS (unchanged).

## 7. Recommended next action

A bounded, compile-time trace of the masm `asm_halt` sites (ctx-full guard,
worklist-full, sentry, and any other `asm_halt` emitter) that records **which
check fires first** with the IP — a narrow diagnostic, not a redesign. That will
identify the exact fail-stop path; only then apply a minimal correction.

## 8. Commit

Branch `experiment/glon-s1-d1`, one local commit (this report). Not pushed/merged.