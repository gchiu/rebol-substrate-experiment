# FIB-OPT-P10B — Results

## 1. Headline

Holding the S1 machine registers `IP`/`SP`/`RP` in C locals (rather than the
memory-mapped `M[0]/M[1]/M[2]`) during compiled-S1 execution removes **~5×**
from the P10A compiled baseline. `fib 25` falls from ~2.0 s (P10A, this session)
to **~0.43 s**; the R3 gap falls from ~32× (interpreted) to **~4.3×**.

## 2. Register access counts (static, generated C)

| register cell | P10A | P10B | note |
|---|---|---|---|
| `M[0]` (IP) | 10,722 | 52 | promoted to `ip` |
| `M[1]` (SP) | 6,672 | 53 | promoted to `sp` |
| `M[2]` (RP) | (indirect, via `@`/`!`) | 52 | promoted to `rp` |
| `M[3]` (HP) | 3 | 3 | left memory-mapped (allocation only) |

The remaining `M[0..2]` references are the synchronisation points (initial load,
`HALT`/`L_bad` write-back, `HOST_DUMP`). Dynamically this removes ~4–5 billion
memory accesses — the dominant remaining cost after P10A.

## 3. Mechanism

`compiled_run` holds `cell ip, sp, rp;`. General instructions use the locals
(`M[--sp]`/`M[sp++]`, `goto *T[ip-256]`). The frozen register idiom `LIT 0/1/2
@`/`!` is lowered to direct local access (`M[--sp]=ip`/`ip=M[sp++]`, etc.).
`HOST` takes `sp` by value and returns the updated `sp` (no store/reload around
each of the 142.9M HOST calls); only `HOST_DUMP` and `HALT`/`L_bad` sync to
`M[0..2]`. RAW/debugger/tasks/GC are all compiled S1 in the same function and
reach the registers through the same lowered idiom, so the locals stay
authoritative across those boundaries. HP (`M[3]`) is left memory-mapped.

## 4. Synchronisation model

- **Load** at entry: `ip=M[0]; sp=M[1]; rp=M[2]`.
- **Flush** at `HALT`, `L_bad`, and before `HOST_DUMP`; **reload** after
  `HOST_DUMP`.
- **HOST (all others)** passes `sp` by value, returns it; HP stays in `M[3]`.
- No flush per primitive, and no flush at RAW/task/GC boundaries (they are the
  same compiled function).

## 5. `@` / `!` aliasing

Register cells are reachable only by the literal `LIT 0/1/2/3 @`/`!` (the RAW
ABI `REG_IP/REG_SP/REG_RP/REG_HP` resolve to these literals). The peephole
covers `0/1/2`; `3` stays memory-mapped. There is no computed-address access to
a register cell in the current evaluator or RAW fragments, so cached registers
cannot be aliased through a non-literal path.

## 6. Correctness

All five workloads run identically through P10A and P10B:

| program | P10A | P10B |
|---|---|---|
| `fib 25` | 75025 | 75025 |
| `tree 25` | 75025 | 75025 |
| `raw add2 3 4` | 7 | 7 |
| `non-local return` | 42 | 42 |
| `closure capture` | 5 | 5 |

The complete interpreted test suite is unchanged: **326 ok, 0 fail**;
`./check-frozen-s1.sh` passes. The compiled path is a benchmark-only prototype
(executed through the new `r0_s1_run_compiled`); the interpreted runtime is
untouched.

## 7. Timing (`-O2`, same-session alternating, 8 rounds each)

| round | P10A median | P10B median | speedup |
|---|---|---|---|
| 1 | 1.845 s | 0.442 s | 4.18× |
| 2 | 2.018 s | 0.429 s | 4.70× |
| 3 | 2.613 s | 0.413 s | 6.33× |
| 4 | 2.532 s | 0.430 s | 5.89× |

P10B `fib 25` median ≈ **0.43 s** (stable); P10A varies 1.85–2.6 s with WSL
frequency. Speedup ≈ **4–6×**.

## 8. R3 comparison

```
P10B fib 25  ≈ 0.43 s
R3  fib 25    ≈ 0.099 s
ratio         ≈ 4.3×
```

(interpreted P9 ≈ 32×, P10A compiled ≈ 18–20×.)

## 9. HOST calls

Unchanged: **142,879,021** (identical S1 stream). HOST was deliberately not
inlined. After register promotion, the remaining cost is dominated by the
142.9M HOST dispatches (the 16-way `switch`) and the data/return-stack cell
traffic (`M[sp]`/`M[rp]`, which remain memory), plus the computed-goto dispatch.

## 10. Architectural conclusion

Register promotion is a **major** win (~5×) and confirms that the memory-mapped
register traffic was the dominant remaining cost after dispatch removal. The
interpreted layer cost ~2.4× (P10A); the register traffic cost ~5× (P10B).
Together they account for the bulk of the original ~32× R3 gap, leaving ~4.3×.

## 11. Recommendation

HOST is now the clear next target (142.9M dispatches, unchanged): **P10C should
expand common HOST arithmetic inline** (in the compiled path), which the frozen
HOST semantics permit if done as a mechanical lowering of the same operations.
This is a separate experiment and is not implemented here.
