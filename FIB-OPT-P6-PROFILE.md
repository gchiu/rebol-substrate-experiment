# FIB-OPT-P6-PROFILE.md — where the ~9 seconds go (diagnostic)

P6 is a profiling/diagnostic phase only: no optimisation, no semantic change,
no S1 change. It answers one question — where is the time in naïve `fib 25`
under Glon P5?

## Objective

Locate the remaining ~9 s of Glon/S1 execution for `fib 25`, now that P5 has
removed allocation/GC (P2/P3) and linear name scanning (P4/P5).

## Build / flags

- Compiler: `cc` (gcc), `-std=c17 -Wall -Wextra -O0 -g` — **identical to the P5
  benchmark baseline** (unoptimised).
- Counting build: adds `-DR0_S1_PROFILE` (existing evaluator PF_* counters) and
  links a throwaway instrumented copy of the S1 machine (`s1_prof.c`) that counts
  opcode/HOST events. The frozen `s1.c` is untouched.

## Profiling tools

- `perf`, `valgrind`, `callgrind`, `gdb`: not installed.
- `gprof`: installed, but its `-pg` binary failed to emit `gmon.out` for this
  workload (profiling buffer/call-graph issue with 169M `host()` calls), so it
  could not be used.
- Primary evidence is therefore **structural event counters** (exact) plus
  `rdtsc` aggregate cycles and the baseline wall-clock timing. Time attribution
  below is estimated from counts + the measured total; it is not a sampled
  profile.

## Baseline timing confirmation

`fib 25` median ~8.8 s (reproduced; the matched P5 rerun was ~9.0 s). The
counting build runs ~12 s (the counters add ~37% overhead), so **counts are from
the counting build, wall-clock from the baseline build**.

## S1 opcode counts (`fib 25`, counting build)

| opcode | count | share |
|---|---|---|
| LIT | 484 599 132 | 43.77% |
| @ (FETCH) | 213 286 788 | 19.27% |
| HOST | 169 463 980 | 15.31% |
| ! (STORE) | 142 150 715 | 12.84% |
| 0BRANCH | 50 377 901 | 4.55% |
| DUP | 33 382 927 | 3.02% |
| DROP | 13 838 746 | 1.25% |
| HALT | 1 | ~0 |
| **total** | **1 107 100 190** | 100% |

## HOST service counts

| service | calls |
|---|---|
| ADD | 63 609 706 |
| EQ | 31 683 439 |
| SUB | 26 827 747 |
| MOD | 13 231 776 |
| LT | 11 410 894 |
| DIV | 8 983 036 |
| MUL | 5 948 244 |
| GT | 5 219 880 |
| NE | 1 578 101 |
| GE | 728 371 |
| LE | 242 786 |
| **total** | **169 463 980** |

## Evaluator events

`calls=242 785`, `subexpr=2 670 637`, `blkeval=485 572`,
`lookups=1 092 531`, `lex-direct=606 962`, `hash-probes=2 185 061`,
`natives=849 746` (`<=242 785`, `-242 784`, `+121 392`, `either 242 785`),
`allocs=1`, `max-depth=25`.

## Amplification ratios

| ratio | value |
|---|---|
| S1 instructions / fib call | **4560** |
| S1 instructions / subexpression | 414.6 |
| HOST calls / fib call | **698** |
| HOST arithmetic calls / fib arithmetic native | **~279×** |

The fib's *actual* arithmetic is only 606 961 native HOST calls; the other
~168.9M HOST calls are the S1 machinery's own internal arithmetic (register
addressing, loop counters, comparisons, 16-alignment padding).

## Where the time is

Total S1 work: **1.1 billion opcode dispatches** ≈ 4560 per fib call, ≈ 8.8 s,
≈ **~24 CPU cycles per opcode at `-O0`** (~8 ns). Breakdown by opcode class:

| area | evidence | approximate share |
|---|---|---|
| memory-mapped register access (LIT + @ + !) | 840M ops (76%) | dominant |
| HOST dispatch + arithmetic | 169M calls (15%) | ~10% (est.) |
| control flow (0BRANCH/DUP/DROP) | 97M ops | small |
| fib's own arithmetic | 606 961 HOST calls | negligible |

The 76% in LIT/@/! is the direct consequence of the S1 design: registers
(IP/SP/RP/HP) are memory-mapped, so every register read/write is a `LIT
<reg-addr>` + `@`/`!` triple. The derived words (CALL/EXIT/`>R`/`R>`) each expand
into ~10-20 such cells.

## Hotspots / architectural mapping

No sampled profile was obtainable, but the event counts are unambiguous:

1. **S1 dispatch loop** (`s1_run`, a C `switch`) — the single hottest C function;
   it executes 1.1B iterations.
2. **HOST dispatch** (`host`, a C `switch` over 16 services) — 169M crossings,
   of which ~168.9M are the interpreter's internal arithmetic, not fib's.
3. **Glon evaluator S1 code** — the source of the 1.1B instructions; block-eval
   (`blkeval`), subexpression dispatch (`subexpr`) and closure invocation are
   the generators.
4. **Hash lookup** — only 2.185M probes (≈0.2% of opcodes), now **minor**, not
   material.

## Hash lookup sanity check

P5's hash lookup is now **immaterial**: 2 185 061 probes out of 1.1B opcodes
(0.20%). P5 did not merely shift cost from linear scan to hashing; name
resolution is genuinely a tiny fraction of runtime.

## The key distinction: semantics vs implementation

- **Instruction amplification (4560× per fib call) is architectural.** It comes
  from expressing the Glon evaluator in S1, where every register/stack/memory
  operation is an explicit `LIT/@/!` sequence and every arithmetic/comparison is
  a HOST crossing. This is the cost of the frozen, memory-mapped S1 substrate.
- **~24 cycles per opcode at `-O0` is an implementation cost.** The `switch`
  dispatch and unoptimised memory access are fixable without changing S1
  semantics (`-O2`, a jump table / computed goto — explicitly out of P6 scope).

So the ~9 s is the product of **many S1 instructions × an unoptimised
interpreter**, and the larger lever is the *number* of instructions (4560×)
rather than the *cost* of each dispatch.

## Recommended P7 hypothesis

The evidence most strongly supports, in order:

1. **Reduce emitted S1 instruction count** — the 76% LIT/@/! register-access
   traffic and the ~15% HOST arithmetic are the amplification; fusing the
   register-access idiom or emitting denser evaluator code is the highest-leverage
   direction.
2. **Cheaper dispatch** — `-O2` / jump table / computed goto would cut the ~24
   cycles-per-opcode constant, but this is implementation, not semantics, and is
   a separate experiment.

No P7 is implemented here.

## Caveats

- `perf`/`gprof`/`gdb` unavailable, so time attribution is inferred from exact
  event counts + the measured total, not a sampled profile.
- The counting build is ~37% slower than the baseline; counts are exact, but any
  quoted cycle/time per opcode is approximate.
- R3 comparison (same PC): Rebol3 `fib 25` ~0.099 s vs Glon ~9.0 s (~91×) — the
  gap is the interpreted S1 layer + unoptimised dispatch, per the above.
