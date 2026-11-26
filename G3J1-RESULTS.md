# G3J.1 — Symbol Table Capacity Experiment (SYM_CAP 512 → 1024)

Date: 2026-11-26
Branch: `experiment/glon-http-g3j1`
Baseline: G3J, commit `c50f85b`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM.

## 1. Audit of SYM_CAP and dependents

- `static const char *syms[512]` (`r0_s1_runtime.c:22`); `nsyms` (23);
  `SYM_CAP = sizeof syms / sizeof syms[0]` (56); `intern()` (57–64).
- **Word encoding is not fixed-width:** `mk_word(id) = (id*16 + T_WORD)`
  (`r0_s1.h:30`), `word_id(v)=v/16`. Symbol ids are stored as `id*16`, so ids
  up to 1023 (and far beyond) fit in a cell with no layout change.
- **Context hash index** (`hash[i] = word_id*256 + slot`, slot < 16) is
  unambiguous for any word_id (256 > slot range); no size assumption.
- **Dependent `512` uses:** the block/string-literal cap (`R0S1_PARSE_TOO_LARGE`,
  lines 659–4020) is a *separate* limit and was left at 512. Only the debug-dump
  bounds (`wid < 512) ? syms[...]`, D2A) referenced the symbol table; updated to
  `SYM_CAP`.
- `r0_s1_sym_name` uses `id < nsyms` (already correct). No other dependency on a
  512 symbol limit.

## 2. Change (safe, no frozen-S1 change)

- `syms[512]` → `syms[1024]` (SYM_CAP auto-derives to 1024).
- Debug-dump bounds `< 512) ? syms[` → `< SYM_CAP) ? syms[`.
- Static cost: +512 pointers (~4 KiB). No memory-map, GC, word-encoding or
  frozen-S1 change. `r0_s1_runtime.c` is not a frozen file; `./check-frozen-s1.sh`
  passes.

## 3. Result: the symbol table no longer blocks

- Build `MODULES=file,http,sqlite,sec` — OK.
- `tests/g3j/server-auth-attempt.glon` **no longer fails with
  `symbol_table_full`**; it now **parses** (bracket-balanced, depth 0). The G3J
  blocker (the 512-symbol table) is resolved.
- The baseline G3H server + `sec` still loads and serves (platform healthy).

## 4. Remaining issue (code defect, not an architectural limit)

- The auth server parses but **does not reach `http/listen`**: the startup chain
  (file read → sqlite open → migration exec → listen) does not complete. The
  top-level prints only `#[func]` (the app/install closure) and no `listening`.
- This is a **bracket/structure defect in the generated auth code**
  (`tests/g3j/server-auth-attempt.glon`), not another platform limit: the file
  parses, the G3H server + sec listens, and the symbol table is no longer full.
  The stopgap appended `]` that made the file parse also mis-nests one inner
  block, so the migration/listen code is not executed as intended.
- Per the task's stop rule, this is **not** an architectural limit revealed by the
  larger table, so it does not trigger a stop; it is an implementation bug to fix
  in the auth source (correct the inner unclosed `[`, remove the stopgap `]`).

## 5. Measurements

- Executable (sec build): **524,848 bytes** (~512 KiB).
- Symbol table: capacity 1024 (was 512); the auth vocabulary now interns
  (previously exhausted at 512, `nsyms=512 name=b5`).
- Static memory: +4 KiB for `syms[1024]`; no heap/mmap change.
- No stress test run (out of scope).

## 6. Regression (PASS)

- `./check-frozen-s1.sh` — frozen S1 unchanged — PASS.
- `make g3h-test` (G3H smoke: users, logging, restart, non-loopback 403) — PASS.
- The 512→1024 change is transparent to non-auth builds.

## 7. Status vs criteria

- SYM_CAP raised safely to 1024 — DONE.
- Auth server no longer blocked by the symbol table; it parses — DONE.
- Auth server reaches `http/listen` and serves — **NOT DONE** (auth-code bracket
  defect; see §4).
- G3J auth acceptance tests — **NOT RUN** (server does not listen yet).
- G3H regression — PASS.

## 8. Next

Fix the bracket nesting in `tests/g3j/server-auth-attempt.glon` (close the inner
unclosed `[` in the auth helpers, remove the stopgap trailing `]`), then re-run
the G3J auth acceptance. No further symbol/capacity change is needed.

## 9. Commit

Branch `experiment/glon-http-g3j1`, one local commit. Not pushed/merged/deployed.