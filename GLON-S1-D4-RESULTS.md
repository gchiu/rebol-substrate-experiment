# GLON-S1-D4 — Trace and Repair Activation-Frame Integrity (checkpoint)

Date: 2026-11-26
Branch: `experiment/glon-s1-d4`
Baseline: G3J/S1-D3, commit `ed4294f`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM.
**Diagnostic checkpoint — guard/condition known; first bad transition not yet isolated.**

## 1. Baseline

- Branch `experiment/glon-s1-d4` from `ed4294f`. G3J.6 diagnostics + `H3B-RESULTS.md` preserved.
- Original frozen S1 checksum `f90496c…` historical.

## 2. Established (from D2/D3)

- Failing halt: `emit_return` "no matching activation" (tag 3516), condition
  `RV_FRAME == 0` — a RETURN with an empty activation-frame chain, at
  `app/install`, task build only.
- Non-task G3H (expanded) → 200; task G3H (expanded) → halt at 3516.

## 3. Frame-transition tracing (planned/partial)

A full transition trace requires emitting trace records at **call** (frame
creation/linking, site-id assignment) and **return** (lookup/restore) in the
masm — i.e., inserting diagnostic HOST calls around the `RV_FRAME` writes in
`emit_call`/`emit_return`. That is a deeper instrumentation than the read-only
halt dumper and was not completed here. The existing read-only dumper confirms the
state at the halt (`RV_FRAME=0`) but not how it became 0.

## 4. First incorrect transition

**Not yet isolated.** The candidates (no activation created / removed / site
mismatch / RETURN without call) are not distinguished without the transition
trace. The empty frame at the halt is the *symptom*, not the demonstrated cause.

## 5. Correction

**None applied.** The guard is correct (RETURN with no activation is invalid); the
defect is the transition that leaves `RV_FRAME=0`. No speculative change; guard not
bypassed.

## 6. Verification

- G3H non-task (expanded): PASS (200, earlier).
- G3H task (expanded): FAILS at 3516 (checkpoint).
- Default build: PASS.
- Frozen-S1 check: fails (s1.c/r0_s1_runtime.c modified — permitted).

## 7. Next

Emit bounded call/return frame-transition records (ring buffer) in
`emit_call`/`emit_return` and compare task vs non-task around `app/install` to
identify the first transition that leaves `RV_FRAME=0`. This is the direct
continuation; no capacity change.

## 8. Commit

Branch `experiment/glon-s1-d4`, one local commit (checkpoint). Not pushed/merged.