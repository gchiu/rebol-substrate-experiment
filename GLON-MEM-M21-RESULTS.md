# GLON-MEM-M2.1 — Configurable Global Context Capacity

Date: 2026-11-26
Branch: `experiment/glon-mem-profile-m21`
Baseline: G3J/MEM-M2, commit `469d76e`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM.

## 1. Audit

- The 256-entry capacity is at `r0_s1_runtime.c:3872`:
  `global_ctx = make_context(R0_NONE, 256)`.
- `make_context(parent, cap)` allocates `CTX_DATA + 3*cap` cells via `lalloc`
  (**loader arena**), stores `cap` in the context header (`CTX_CAP`), and
  initialises a `cap`-entry hash index. The capacity is **not** a fixed-size
  C array — it is a runtime value in the context header.
- No hash/slot/GC/width assumption on 256 elsewhere: the hash index is sized by
  `cap`; GC traces the context via `TREC_CTX`/`r_mark_value` (cap-agnostic);
  the context grows in the **loader** (separate from managed heap / M1 arena), so
  no overlap with other regions.
- Static cost of 512 vs 256: +`3*256` ≈ 768 loader cells (data 2*cap + hash cap).
  Runtime: a larger hash index (cap). Bounded by `MEM_LOADER_CELLS` (500000).

## 2. Change (default preserved)

- `r0_s1_runtime.c`: `#ifndef GLOBAL_CTX_CAP / #define 256 / #endif` and
  `make_context(R0_NONE, GLOBAL_CTX_CAP)`.
- `Makefile`: `MEM_GLOBAL_CONTEXT_CAP ?= 256` appended to
  `GLON_MEMORY_CFLAGS` as `-DGLOBAL_CTX_CAP=$(MEM_GLOBAL_CONTEXT_CAP)`.
- Default (256) unchanged; frozen `s1.c`/`s1.h` untouched.

## 3. Result (server profile: MEM_CODE_CAP=20480, GLOBAL_CTX_CAP=512)

- `context_full` is **resolved** — the app loads `web.glon` + `json.glon` +
  `min_auth_task.glon` (no `too_large`, no `context_full`).
- The app does **not** reach `http/listen`: it **halts in `start`** with a
  machine fail-stop (reason 0) before listening:
  `[word=start] [blk=… count=27 idx=14] … gw-err (tag 3)`.
- One setup request: **not run** (server does not listen).

## 4. New independent defect (stopped)

A machine fail-stop inside the `start` function (before `http/listen`) is a new,
independent defect. Per the task, I stop and report it rather than debug it in
this milestone. It is not a context-capacity, code-space or frozen-S1 issue.

## 5. Regression

- `./check-frozen-s1.sh` — PASS (frozen S1 unchanged).
- Default build (`make glon MODULES= MEM_PROFILE=large`) — PASS.
- Debugger/M1/H3C/G3H — not re-run (build/config-only change; default 256
  preserved) — not claimed as passed.

## 6. Status

- Configurable global-context capacity — DONE (default 256, server 512).
- `context_full` resolved — DONE.
- App reaches `http/listen` / setup — **BLOCKED** by a new `start` machine halt.
- Frozen S1 / default / auth logic — unchanged.

## 7. Next

Investigate the `start` machine halt (the earliest incomplete operation) in a
separate milestone; it is independent of the capacity/code-space work.

## 8. Commit

Branch `experiment/glon-mem-profile-m21`, one local commit. Not pushed/merged.