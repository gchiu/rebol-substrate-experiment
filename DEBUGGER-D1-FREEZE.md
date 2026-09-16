# DEBUGGER D1 — Freeze Baseline

Annotated immutable milestone for the accepted D1/D1.1 cooperative debugger.

## Frozen identifiers

| item | value |
|---|---|
| debugger commit (HEAD at freeze) | `5ba330060903427b5d307989d82681b7984d71e6` |
| annotated tag | `debugger-d1-frozen-v1` |
| S1 frozen commit | `f90496c26dc45c7a387d8fc8639cd2781507d3d2` (`s1-frozen-v1`) |
| R0 evaluator/trapdoor reference | `9ce3d82` (`r0-trapdoor-v2`) |

## Final verification

- **Tests:** 156 `ok:` checks, **0 failures**, exit 0.
- **Stress:** 100 suspend / inspect-with-recursive-closure / resume cycles pass
  with the correct result and a clean return stack every cycle.
- **No** unexpected `bad opcode`, **no** unbound-word `[dump]`, **no** hang.
- **Freeze check:** `./check-frozen-s1.sh` passes.
- **Runtime/trapdoor diff:** `git diff --exit-code r0-trapdoor-v2 --
  r0_s1_runtime.c r0_s1.h` is **empty** — `r0_s1_runtime.c` and `r0_s1.h` are
  byte-for-byte unchanged.

## State assertions

- Ordinary R0 closures (recursive, nested, and the lexical chain-walk
  `lookup`/`find-in`) **work during suspended inspection** — no longer only the
  RAW `ctx-lookup`/`frame-depth` diagnostic workaround.
- **Runtime/trapdoor unchanged.**
- **HOST unchanged** (no new HOST service).
- **S1 unchanged** (still frozen at `s1-frozen-v1`).
- **No eighth primitive required.**

## D1.1 bug and fix (summary)

The debugger and debuggee originally shared a single `HP` (the S1 heap): the
debugger's own ordinary closures (created in the debugger block) and its
inspection-time allocations collided with the debuggee's live frames/contexts,
producing address-dependent `bad opcode` / unbound-word / hang corruption.

Fix (debugger-only, no evaluator change): the debugger block now begins by
moving `HP` to a private region —

```
set-hp: raw [ LIT 50000 LIT REG_HP ! ARITY 0 EXIT ]
set-hp
```

— so the two worlds' heaps are disjoint, and the incorrect D1 "HP-fix" line in
`continue` was removed. See `DEBUGGER-D1-HARDENING.md` for the full root-cause
investigation, state audit, and memory-region audit.
