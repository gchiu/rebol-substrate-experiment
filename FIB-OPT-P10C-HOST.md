# FIB-OPT-P10C — Hot pure HOST intrinsic expansion

## 1. Hypothesis

P10B reduced `fib 25` to ~0.43 s (~4.3× R3) with 142,879,021 HOST dispatches
remaining. P10C tests:

> How much of the remaining compiled-S1 cost is the generic HOST dispatcher,
> versus the operations themselves — and can hot, pure HOST operations be
> lowered directly to generated C?

This does not eliminate HOST as an architectural escape boundary; it only lets
the compiler know what some HOST operations mean.

## 2. Dynamic HOST distribution (`fib 25`)

From the P6 profiler (`s1_prof_host_count`), non-counting total 142,879,021
(counting build 144,578,516, +~1% pf-instrumentation; relative shares identical):

| HOST op | calls | % | cum % |
|---|---|---|---|
| ADD | 57,297,290 | 39.6% | 39.6% |
| EQ | 28,284,451 | 19.6% | 59.2% |
| SUB | 19,422,805 | 13.4% | 72.6% |
| MOD | 11,896,461 | 8.2% | 80.8% |
| DIV | 7,647,721 | 5.3% | 86.1% |
| GE | 5,341,278 | 3.7% | 89.8% |
| MUL | 5,219,889 | 3.6% | 93.4% |
| LT | 4,612,919 | 3.2% | 96.6% |
| GT | 3,034,815 | 2.1% | 98.7% |
| NE | 1,578,101 | 1.1% | 99.8% |
| LE | 242,786 | 0.2% | 100% |

**100%** of fib's HOST calls are pure arithmetic/comparison; `ALLOC`,
`PUTCHAR`, `PRINT`, `DUMP`, `NEG` do not occur in fib.

## 3. HOST classification

| class | ops | P10C treatment |
|---|---|---|
| A. pure deterministic | `ADD SUB MUL DIV MOD NEG EQ NE LT GT LE GE` | intrinsic candidates |
| B. runtime/internal | `ALLOC` (heap/GC side effect) | generic HOST |
| C. external | `PUTCHAR PRINT DUMP` (I/O, debug) | generic HOST |

## 4. Selection

Only the class-A operations satisfy the rules (hot, pure, deterministic, no
external state, exactly understood, mechanically expressible). All 12 are
lowered. `ALLOC`/`PUTCHAR`/`PRINT`/`DUMP` remain generic HOST — the escape
boundary is fully preserved.

## 5. Lowering mechanism

The P10C generator, at each `HOST id` site with `id ∈ 0..11`, emits the exact
C code of the frozen `host()` switch case inline (using the P10B local `sp`):

```c
{ cell b = M[sp++]; cell a = M[sp++]; M[--sp] = a + b; }   /* ADD */
{ cell b = M[sp++]; cell a = M[sp++]; M[--sp] = a / b; }   /* DIV */
{ cell b = M[sp++]; cell a = M[sp++]; M[--sp] = (a < b) ? 1 : 0; }  /* LT */
...
```

`HOST_DUMP` keeps the P10B flush/reload; `ALLOC`/`PUTCHAR`/`PRINT` keep the
generic `sp = host(id, sp)`. The S1 stream still says `HOST` — this is a
lowering, not an instruction-set change.

## 6. Semantic equivalence

Each intrinsic is a **verbatim transcription** of the frozen `host()` case, so
signedness, overflow/wrapping (whatever the C compiler does), division/modulo
truncation, comparison representation (`1`/`0`), tagging and stack effects are
identical by construction. No operation is rewritten.

## 7. Register-state model

Unchanged from P10B. Intrinsic operations are pure and operate only on the
local `sp` and `M[]` stack cells; no new flush/reload points are introduced.
The only flush/reload remains at entry/`HALT`/`HOST_DUMP`.

## 8. Result

See `FIB-OPT-P10C-RESULTS.md`. In short: **no speedup (~1.0×)** — the generic
HOST dispatcher was *not* the bottleneck; the cost is inside the operations
(the arithmetic) and the data-stack cell traffic (`M[sp]`), which are identical
before and after.
