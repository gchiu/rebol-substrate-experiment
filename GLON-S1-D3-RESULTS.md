# GLON-S1-D3 — Identify and Fix the Failing Guard (checkpoint)

Date: 2026-11-26
Branch: `experiment/glon-s1-d3`
Baseline: G3J/S1-D2, commit `3685eb4`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM.
**Diagnostic checkpoint — guard identified; fix not yet applied.**

## 1. Baseline

- Branch `experiment/glon-s1-d3` from `3685eb4`. G3J.6 diagnostics + `H3B-RESULTS.md` preserved.
- Original frozen S1 checksum `f90496c…` is a historical reference.

## 2. Guard identified (emission site + condition)

- Tagged the `asm_halt` sites; the executed halt (IP 11972, emitter 11971) reports
  `tag=3516`.
- **Site:** `emit_return` in `r0_s1_runtime.c` — the **"no matching activation"**
  guard:
  ```c
    e_cell(RV_FRAME); e_setc(RV_T2);
    e_cell(RV_T2); asm_lit(0); asm_host(HOST_EQ);
    cell j_nonzero = asm_zbranch_fwd();
    g_halt_tag = 3516; asm_host(HOST_DUMP); asm_halt();   /* no matching activation */
  ```
- **Condition:** `RV_FRAME == 0` — a **RETURN** is executed while the activation
  frame chain is empty. The return walks `RV_FRAME` to find the activation with the
  return site-id; `RV_FRAME==0` means there is no live activation to return to.
- **Invariant:** a RETURN must have a live activation frame; the top-level/global
  frame (id 0) is not a returnable activation.

## 4. Why it triggers (task vs non-task)

- Non-task G3H (expanded) → 200 (the same `app/install` returns normally).
- Task G3H (expanded) → this RETURN with `RV_FRAME==0` at `app/install`.
- The difference is the `task.glon` build. The failure is a RETURN reaching the
  global frame (id 0) — the activation chain is empty at that point in the task
  build. The exact reason the frame is empty (call setup vs an earlier unwind) is
  the next step; it is **not** assumed to be an empty-data-stack error (the data
  stack is separately at DS_INIT, which is normal).

## 5. Correction

**None applied yet.** The guard is correctly firing on an invalid state
(RETURN with no activation); the defect is what leaves `RV_FRAME==0` at that RETURN
in the task build. Disabling/bypassing the guard would be wrong. The next step is
to trace the frame setup for the `app/install` call in the task build.

## 6. Verification

- G3H non-task (expanded): PASS (200, earlier).
- G3H task (expanded): FAILS at the 3516 guard (this checkpoint).
- Default build: PASS.
- Frozen-S1 check: fails (s1.c/r0_s1_runtime.c modified — permitted; original in Git history).

## 7. Status / next

- Guard located (3516, `emit_return`, `RV_FRAME==0`) — DONE.
- Root cause (why the frame is empty) — **UNRESOLVED**; next: trace the
  `app/install` call's frame setup in the task build (bounded).
- No speculative fix; guard not bypassed.

## 8. Commit

Branch `experiment/glon-s1-d3`, one local commit (checkpoint). Not pushed/merged.