# AGENTS.md — instructions for coding agents

Before changing the Glon runtime (`r0_s1_*.c`, `r0_s1.h`, `m1_layout.h`), the
frozen S1 substrate, the memory layout, resource limits or language semantics,
read **`GLON-ARCHITECTURE-MEMORY.md`** and the authoritative documents it
cites for the area you are changing.

- **Decisions are not defects.** Entries marked **[DECISION]** there are
  deliberate. To change one, write an explicit proposal that cites the entry,
  and wait for approval.
- **The frozen S1 files never change:** `s1.c`, `s1.h`, `tests.c`,
  `adversarial.c`, `claims.c`. `./check-frozen-s1.sh` must pass.
- **`GLON-ALPHA-LAWS.md` is normative.** The implementation is the ground
  truth for addresses and sizes.
- **Raising a capacity or changing the 65,536-cell memory map** is a separate,
  explicitly approved scaling experiment, never an incidental fix.
- **Record new measurements** (dated, with their configuration) and decisions
  in `GLON-ARCHITECTURE-MEMORY.md`; record milestones in `MEMORY.md`.
