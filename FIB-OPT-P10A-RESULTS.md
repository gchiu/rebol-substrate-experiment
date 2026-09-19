# FIB-OPT-P10A — Results

## 1. Headline

The interpreted S1 layer — opcode fetch, decode, and `switch` dispatch in
`s1_run()` — costs about **2.4×** on `fib 25`. Mechanically compiling the frozen
S1 stream to a computed-goto C function (with `IP/SP/RP/HP` still
memory-mapped, and the frozen HOST dispatch unchanged) cuts `fib 25` from
~4.3 s (interpreted, this session) to **~1.8 s**, i.e. the R3 gap falls from
~32× to **~18×**.

## 2. Mechanism (recap)

The benchmark driver `r0_s1_p10a_bench.c`:
1. assembles the S1 stream exactly as normal (`r0_s1_init` + `r0_s1_parse`);
2. emits a literal computed-goto transcription of `M[CODE_BASE .. asm_here())`
   (one C label per instruction, operands as compile-time constants, a copy of
   the frozen `host()` switch, `goto *table[IP-256]` dispatch);
3. compiles it with `gcc -O2 -shared -fPIC`;
4. `dlopen`s it and runs it through the new `r0_s1_run_compiled` (identical
   setup to `r0_s1_run`, only the executor differs).

Compile time is a one-off **~126 s** (the transcription is ~35k lines / ~4.1k
instructions); it is recorded separately and not mixed into execution time.

## 3. Correctness

| program | interpreted | compiled | |
|---|---|---|---|
| `fib 25` | 75025 | 75025 | MATCH |
| `tree 25` | 75025 | 75025 | MATCH |
| `raw add2 3 4` | 7 | 7 | MATCH |

The `raw` case exercises a RAW fragment (runtime-assembled after the evaluator,
present in the compiled stream), confirming the mechanism is not fib-specific
and covers the full seven-primitive instruction set plus HOST. The complete
interpreted test suite is unchanged: **326 ok, 0 fail**; `./check-frozen-s1.sh`
passes.

## 4. Timing (same-session alternating, `-O2`, 8 rounds each)

| round | interpreted median | compiled median | speedup |
|---|---|---|---|
| 1 | 4.8189 s | 1.9555 s | 2.464× |
| 2 | 4.2319 s | 1.7738 s | 2.386× |
| 3 | 4.4595 s | 1.8069 s | 2.468× |

Median speedup **≈ 2.4×**; compiled `fib 25` median ≈ **1.8 s**. (This session's
interpreted time is higher than the historical P9 ~3.2 s due to WSL frequency
variation; the *relative* alternating comparison is the robust signal.)

## 5. R3 comparison

```
compiled fib 25  ≈ 1.8 s
R3    fib 25      ≈ 0.099 s
ratio             ≈ 18×
```

(interpreted P9 was ≈ 32×). Compiling the S1 layer removes a ~2.4× factor but
does not close the remaining ~18×.

## 6. HOST calls

Unchanged: **142,879,021** for `fib 25` (the compiled path executes the
*identical* S1 stream, so the HOST count is the same by construction; the fib's
own arithmetic natives remain 606,961). HOST was deliberately not inlined or
intrinsified — this experiment isolates dispatch removal only.

## 7. Remaining dominant cost

After removing fetch/decode/`switch`, the compiled path still executes the same
917,242,308 S1 opcodes against a memory-mapped `M[]`:

- every register access is `M[0]`/`M[1]`/`M[2]`/`M[3]` (IP/SP/RP/HP stay in
  memory — no register promotion);
- the 16-way HOST `switch` runs 142.9M times;
- dispatch is still an indirect `goto *table[IP]`.

These are exactly the P10B targets: register promotion and HOST intrinsic
expansion.

## 8. Architectural conclusion

The interpretation overhead (opcode fetch + decode + `switch` dispatch) is a
**~2.4×** factor — large enough to be worth removing, but far smaller than the
remaining memory-mapped-register + HOST + evaluator-amplification cost. This is
consistent with P6B (the `-O2` interpreter already had a jump-table switch) and
with P7–P9 (the dominant cost is the ~3.8k-opcode-per-activation S1 stream
itself, not its interpretation).

## 9. Recommendation

**Yes, P10B register promotion is justified.** Promoting `IP/SP/RP/HP` (and the
hot `RV_*` evaluator cells) into C locals / physical CPU registers, and
expanding common HOST arithmetic inline, are the next levers; the compiled-S1
baseline here is the correct starting point and comparison basis. Compilation
does not itself change the performance class (~18× still remains).
