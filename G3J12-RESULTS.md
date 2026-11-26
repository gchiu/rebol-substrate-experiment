# G3J.12 — Server Code-Space Scaling Experiment (blocked: json.glon needs 521 cells)

Date: 2026-11-26
Branch: `experiment/glon-http-g3j12`
Baseline: G3J.11, commit `ed9773f`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM.

## 1. Memory map (before)

- Code grows up from `CODE_BASE` to `R0S1_CODE_LIMIT`; data stack grows down
  from `R0S1_DS_INIT = 16384`; `R0S1_CODE_LIMIT = DS_INIT - DS_RESERVE`.
- `R0S1_DS_RESERVE = 256` (default) → `CODE_LIMIT = 16128`. Deepest measured
  data-stack use = 87 cells.
- M1 arena (54800), loader (to 54800), managed heap (1M) are **above** the code
  region — unaffected by the code limit.
- No overlaps: code [CODE_BASE, CODE_LIMIT), data stack [CODE_LIMIT, DS_INIT),
  loader [LOAD_BASE, M1_ARENA_BASE), M1 arena above.

## 2. Change (approved experiment)

- `r0_s1.h`: made `R0S1_DS_RESERVE` overridable (`#ifndef … #define 256`).
- `Makefile`: added `CODE_RESERVE ?= 256` and appended
  `-DR0S1_DS_RESERVE=$(CODE_RESERVE)` to `GLON_MEMORY_CFLAGS` (large profile).
- Default (constrained / `CODE_RESERVE=256`) layout is unchanged.

## 3. Result (after)

- With `CODE_RESERVE=128`: `R0S1_CODE_LIMIT = 16256` (verified in the error:
  "code limit 16256"). The define now reaches the binary (the earlier 16128 was a
  stale generated profile; removing `build/profile/*` fixed it).
- But `modules/json.glon` needs **521 code cells**; only **49** are free below
  16256. Even `CODE_RESERVE=0` (limit 16384) leaves ~177 free — still < 521.
- To fit 521 cells the code limit would have to exceed `DS_INIT` (16384), i.e.
  move the data stack — a memory-map change that alters frozen-S1 layout.

## 4. Blocker (fundamental)

`json.glon` requires **521 code cells**; the code region below `DS_INIT` (16384)
is already ~16,200 cells of libs (http+json+sqlite+sec+task) + app. No bounded
`CODE_RESERVE` reduction frees 521 cells without moving `DS_INIT` (frozen-S1
memory map). Per the task, I stop rather than change frozen S1 semantics.

The earlier "31 cells" (G3J.10/11) was a sub-block measurement; the full
`json.glon` module needs 521 cells.

## 5. Status vs criteria

- Audit memory map — DONE.
- Propose smallest safe expansion — DONE (CODE_RESERVE 256→128; limit 16256).
- No overlaps; default preserved — DONE.
- `json.glon` loads / `min_auth_task` reaches listen / setup — **BLOCKED**
  (json needs 521 cells, 49 free; cannot fit without moving DS_INIT).
- Frozen S1 / runtime / loader / capacity / auth design — unchanged.

## 6. Recommendation

Enabling task mode for the JSON-based auth app requires an **approved memory-map
change** (raise `R0S1_DS_INIT` / move the data stack to add code space) — a
scaling experiment beyond a bounded reserve tweak. That is the next decision
point; it is not achievable by `CODE_RESERVE` alone.

## 7. Reproduce

```
make glon MODULES=file,http,sqlite,sec,task MEM_PROFILE=large MEM_LOADER_CELLS=500000 \
     M1_TASKS=3 M1_TASK_CELLS=3400 M1_DS_OFF=400 M1_RS_OFF=3400 M1_WRAPPER_DELTA=256 CODE_RESERVE=128
./glon --http-tasks ... modules/json.glon tests/g3j/min_auth_task.glon
# -> code space exhausted: json.glon needs 521 cells, 49 free below 16256
```

## 8. Commit

Branch `experiment/glon-http-g3j12`, one local commit. Not pushed/merged/deployed.