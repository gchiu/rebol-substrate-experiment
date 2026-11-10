# GLON-EXECUTION-ARCHITECTURE measurement tools

These are the instrumentation and analysis tools behind
[`GLON-EXECUTION-ARCHITECTURE.md`](../../GLON-EXECUTION-ARCHITECTURE.md)
(2026-11-10). They are evidence tools only: no runtime, frozen-S1, memory-map,
host-ABI or application change is part of this directory.

## What is measured

- **S1 instructions executed per code cell** by an instrumented copy of the
  frozen machine (`measure/s1_arch.c`).
- **Which derived macro emitted each runtime cell** (`CALL`, `EXIT`, `BRANCH`,
  `>R`, `R>`, `R@`, `SWAP`).
- **Which C emitter produced each emitted cell**, using GCC
  `-finstrument-functions` (`measure/hooks.c`).
- **Runtime-state memory-variable traffic:** executed `LIT addr @` and
  `LIT addr !` pairs whose address is in `[RV_BASE, GC_HEAP_BASE)`.

The frozen `s1.c` is copied, not modified. The copy is compiled only into this
experiment's driver.

## Build

From the repository root:

```
make -C experiments/glon-execution-architecture
```

This creates `experiments/glon-execution-architecture/build/drv` and
`build/syms.txt`. Regenerate `syms.txt` after every relink: dump entries hold
function addresses.

## Run a workload

Run `build/drv` from the repository root. Its first argument is the dump file;
later arguments are files to load, or `-e EXPR` to evaluate an expression.
`jupyter/prelude.glon` and `demo/shop/strings.glon` are loaded before the
counter is reset, so they are not part of the workload count.

```
D=experiments/glon-execution-architecture/build
$D/drv "$D/plus.dump" -e '+ 1 2'
$D/drv "$D/fib.dump" -e 'fib: func [n] [ either < n 2 [ n ] [ + fib - n 1 fib - n 2 ] ] fib 18'
$D/drv "$D/deflate.dump" demo/shop/strings-ext.glon \
  experiments/glon-deflate/inflate.glon experiments/glon-deflate/tests/unit-tests.glon
$D/drv "$D/latex.dump" \
  experiments/glon-latex/latex.glon experiments/glon-latex/test-helpers.glon \
  experiments/glon-latex/latex-tests.glon
$D/drv "$D/mem.dump" demo/shop/strings-ext.glon \
  experiments/glon-mem/mem.glon experiments/glon-mem/mem-tests.glon
$D/drv "$D/binary.dump" demo/shop/strings-ext.glon integrity/binary-strings.glon
```

Forth-style masm fib (the comparison in the report). It is naive recursion,
the same algorithm as the Glon definition; it works on untagged integers and
tags only at the Glon boundary:

```
$D/drv "$D/masmfib.dump" -e '
fibm: masm 1 [ LIT 16 DIV CALL F LIT 16 MUL ARITY 1 EXIT
  F: DUP LIT 2 LT ZBRANCH R EXIT
  R: DUP LIT 1 SUB CALL F >R LIT 2 SUB CALL F R> ADD EXIT ]
fibm 18'
```

To count a trace (keep it outside the repository; a large workload's trace is
hundreds of millions of lines):

```
$D/drv "$D/plus.dump" -e 0 -t /tmp/plus.trace -e '+ 1 2'
```

## Analyse

`measure/analyse.py` takes the build directory, a dump, and an optional number
of categories to print:

```
python3 experiments/glon-execution-architecture/measure/analyse.py "$D" "$D/fib.dump"
```

It prints:

- code cells in the runtime and in masm libraries;
- executed instructions, and the runtime-state memory-variable count;
- dynamic share of each derived macro;
- runtime code cells by emitter category (a partition, not an overlapping
  estimate);
- the conservative return-stack scan's share of execution.

`trace.py` and `tsum.py` summarise a trace by routine and macro.

## Reading the numbers

- **Static code cells** come from the dump of the emitted code; the categories
  in `analyse.py` partition the 12,861 runtime cells. The `CALL/EXIT/BRANCH/RS`
  macro line is a **subset** of those categories, not an additional category,
  because a macro can be emitted inside any routine.
- **Executed shares** count S1 instruction dispatches, not wall time. A macro's
  expansion has several dispatches; a primitive would still have one.
- **`emit_call_fwd`** (a masm `CALL label`) is not parenthesised by the frozen
  assembler as a `CALL` macro, so the instrumentation records its internal
  `>R` cells but not the whole expansion as `CALL`. Shares are therefore
  slightly conservative for workloads with masm calls.
- **The memory-variable count** counts each `LIT addr @`/`!` pair as two
  instructions and uses the runtime-state range, not the low registers or
  scratch cells.
- **Timing is not measured here.** The report does not claim wall-time
  comparisons; the instruction counts are deterministic.

## Workloads in the report

| Name | Source |
|---|---|
| `plus` | `+ 1 2` |
| `fib` | naive recursive `fib 18`, defined above |
| `deflate` | DEFLATE unit tests (44) |
| `latex` | LaTeX v001 tests |
| `mem` | `mem/` v002 tests (66) |
| `binary` | `integrity/binary-strings.glon` |

Results recorded with these tools are in [`results.txt`](results.txt).
