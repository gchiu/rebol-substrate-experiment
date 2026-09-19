# FIB-OPT-P10B — Promote S1 machine registers (compiled-S1 execution)

## 1. Hypothesis

P10A removed the interpreted fetch/decode/`switch` (~2.4×). The compiled-S1 path
still executes every opcode against the flat `M[]` array, so the architectural
registers `IP` (`M[0]`), `SP` (`M[1]`), `RP` (`M[2]`), `HP` (`M[3]`) are loaded
and stored through memory on almost every instruction. P10B tests:

> How much remaining cost is memory traffic for the S1 machine registers, versus
> holding them in C locals during compiled-S1 execution?

## 2. Measurement of the opportunity

Static counts of register-cell accesses in the P10A generated C (35,451 lines,
~4.1k instructions):

| register cell | direct occurrences | note |
|---|---|---|
| `M[0]` (IP) | 10,722 | `M[0]=next` write + `goto *T[M[0]-256]` read per instruction |
| `M[1]` (SP) | 6,672 | `M[--M[1]]` / `M[M[1]++]` on every push/pop |
| `M[2]` (RP) | 1 (direct) | reached indirectly via `LIT 2 @`/`LIT 2 !` (the derived `>R`/`R>`/`CALL`/`EXIT`) |
| `M[3]` (HP) | 3 | only `HOST_ALLOC` / `HOST_DUMP` |

Dynamically (`fib 25` ≈ 917M opcodes) this is ~1.8B IP accesses, ~1.4B SP
accesses, and ~0.7B RP accesses (inside the `@`/`!` register idioms) — the
dominant remaining memory traffic. **IP, SP, RP are the clear promotion
candidates.** HP is rare (allocation only) and is left in memory.

## 3. Design: locals + explicit synchronisation

The compiled `compiled_run` holds `cell ip, sp, rp;` in C locals (which `-O2`
then places in CPU registers). `HP` stays at `M[3]`.

- General instructions use the locals: `push`/`pop` become `M[--sp]`/`M[sp++]`;
  dispatch becomes `goto *T[ip - 256]`; `DROP` is `sp++`.
- **Register-access peephole**: the S1 stream always reaches a register through
  a literal `LIT 0/1/2` immediately followed by `@` (read) or `!` (write). The
  emitter recognises exactly these adjacent pairs and emits direct local access:
  `LIT k @` → `M[--sp] = <reg>`; `LIT k !` → `<reg> = M[sp++]`. This is not
  arbitrary fusion — it is the literal lowering of the frozen register ABI.
- **HOST**: the generated `host(id, sp)` takes `sp` by value and returns the
  updated `sp` (so no store/reload around each of the 142.9M HOST calls). It
  still uses `M[3]` for `HOST_ALLOC` and reads `M[0..3]` only for `HOST_DUMP`.
- **HALT / L_bad**: write back `M[0]=ip; M[1]=sp; M[2]=rp;` before returning.

## 4. Synchronisation boundaries

The only genuinely external observers of the memory-mapped registers are:

1. **`r0_s1_run_compiled`** (the C caller): writes the registers via `s1_reset()`
   before the run and reads `M[SP]` for the result after it. Both are at the
   compiled_run entry/exit, so the load at entry and the write-back at HALT are
   the complete synchronisation.
2. **`HOST_DUMP`** (the only HOST that reads IP/SP/RP/HP): the emitter emits
   `M[0]=ip; M[1]=sp; M[2]=rp;` before that specific HOST and reloads after. All
   other HOST operations only touch SP (passed by value) and HP (`M[3]`).
3. **RAW / debugger / tasks / GC / escape / RETURN** are all *compiled S1 code in
   the same `compiled_run`*; they reach the registers through the same `LIT k @`/
   `!` idiom, which the peephole lowers to the locals. They never go out of
   process, so no extra flush/reload is needed — the locals stay authoritative
   across those boundaries by construction.

## 5. `@` / `!` aliasing of register cells

The register cells are `M[0..3]`. A program can read or write them only by the
literal `LIT 0/1/2/3 @`/`!` (the RAW ABI's `REG_IP`/`REG_SP`/`REG_RP`/`REG_HP`
resolve to these literals). The peephole covers `0/1/2`; `3` (HP) is left
memory-mapped. There is no computed-address access to a register cell in the
current evaluator or RAW fragments, so the cached registers cannot be aliased
through a non-literal path. The RAW symbolic ABI remains valid (it is the same
literal pattern). This is documented, not silently assumed.

## 6. What is NOT changed

HOST is unchanged (no intrinsic lowering). No superinstructions, no
evaluator/frame/RAW ABI redesign, no JIT. The frozen seven-primitive set and all
Glon/S1 semantics are untouched; the interpreted runtime is unaffected.
