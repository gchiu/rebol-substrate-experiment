# GLON-S1-D2 — Trace and Diagnose OP_HALT (checkpoint)

Date: 2026-11-26
Branch: `experiment/glon-s1-d2`
Baseline: G3J/S1-D1, commit `d80cd8c`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM.
**Diagnostic checkpoint — S1 modified (permitted); root cause narrowed, not yet fixed.**

## 1. Baseline

- Branch `experiment/glon-s1-d2` from `d80cd8c`. G3J.6 diagnostics + `H3B-RESULTS.md` preserved.
- Original frozen S1 checksum `f90496c…` is a historical reference (Git history).

## 2. Instrumentation (read-only, no behaviour change)

- `s1.c`: `asm_halt()` now records its code position into `g_halt_pos[256]`/
  `g_halt_n` before emitting `OP_HALT` (recording only — no execution change).
- `r0_s1_runtime.c`: `r0_s1_dump_ip` prints the emitter list and the executed IP.
- No Glon evaluation/allocation/state change.

## 3. Captured (G3H task build, expanded profile)

```
[ip=11972 ops 2 6 15 7 0 12245]
[word=435 name=app/install ctx=1125447]
[ctx=1125447 count=270 cap=512 frame=0 sp=20480 rp=28670]
[halt-emitters=32 executed-ip=11972: 422 952 … 11729 11971 12446]
[origin blk=1158464 idx=8 word=435 name=app/install]
```

- Executed IP **11972**; the matching `OP_HALT` emitter is at **11971**
  (IP advances past the OP_HALT). 12446 is the final (normal) halt.
- Context **270/512 (not full)**; `frame=0`; `sp=20480` (empty data stack).
- Halt `reason 0` (`R0S1_HALT_NONE`) — the guard at 11971 does **not** set
  `RV_HALTWHY` (so it is not CONTEXT_FULL / worklist / stack-sentry).

## 4. Root cause (narrowed)

- The fail-stop is a specific guard emitted at code position **11971** (the 31st of
  32 halt emitters), executed at `app/install` with an empty data stack and no
  frame, and **not** a capacity failure (ctx 270/512).
- The **exact source check** that emitted 11971 is not yet mapped to a name — that
  is the next diagnostic step (per-site tagging of the ~32 `asm_halt` callers).

## 5. Correction

**None yet** — the specific violated invariant is not identified (the emitter
position is known, the source check is not). No speculative S1 change made.

## 6. Verification

- G3H non-task (expanded) — PASS (200, earlier).
- G3H task (expanded) — FAILS at 11971 (this checkpoint).
- Frozen-S1 check — now **fails** because `s1.c` was modified (permitted for this
  milestone; original recoverable at `f90496c…`).
- Default build — PASS.

## 7. Next diagnostic step

Tag each `asm_halt` caller with a small id (or map code position 11971 to its
emit function) to name the guard, then read the condition it checks. Bounded; no
redesign.

## 8. Commit

Branch `experiment/glon-s1-d2`, one local commit (checkpoint). Not pushed/merged.