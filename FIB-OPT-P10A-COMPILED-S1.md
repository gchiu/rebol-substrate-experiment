# FIB-OPT-P10A — Compiled S1 baseline

## 1. Hypothesis

P7/P8/P9 showed sharply diminishing returns from evaluator-density work. P10A
isolates one controlled question:

> How much execution cost is caused by interpreting each frozen S1 opcode
> through `s1_run()` rather than executing a mechanically compiled equivalent?

This is an architectural measurement, not a production compiler.

## 2. Inspection findings

### S1 representation

S1 programs are a flat `cell` array `M[]` (65536 cells). Code lives at
`CODE_BASE = 256` and grows upward as a contiguous stream of opcode/operand
cells. The only opcodes are `LIT DUP DROP @ ! 0BRANCH HOST` plus the harness
`HALT`. Machine registers are memory-mapped: `REG_IP=0 REG_SP=1 REG_RP=2
REG_HP=3` (i.e. `M[0..3]`).

### How `s1_run` executes

```c
void s1_run(cell start) {
    IP = start;
    for (;;) {
        cell op = M[IP++];          /* fetch + decode */
        switch (op) {               /* dispatch (jump table at -O2) */
        case OP_LIT:    push(M[IP++]); break;
        case OP_DUP:   { cell v = M[SP]; push(v); break; }
        case OP_DROP:   SP++; break;
        case OP_FETCH: { cell a = pop(); cell v = M[a]; push(v); break; }
        case OP_STORE: { cell a = pop(); cell v = pop(); M[a] = v; break; }
        case OP_ZBRANCH:{ cell f = pop(); cell t = M[IP++]; if (f==0) IP=t; break; }
        case OP_HOST:   host(M[IP++]); break;
        case OP_HALT:   return;
        }
    }
}
```

Per instruction this is: fetch opcode (`M[IP++]`), decode via `switch`, execute
(which for `LIT`/`0BRANCH`/`HOST` does a second `M[IP++]` operand fetch). The
operands (literals, branch targets, host ids) are all present in `M[]`.

### Branch-target representation

`0BRANCH` carries its target inline as the cell after the opcode (`cell t =
M[IP++]`). `CALL`/`EXIT`/derived `BRANCH` all manipulate `REG_IP` (and `REG_RP`)
directly, so control transfer is a store to `M[REG_IP]`. Targets are always
instruction addresses (never operand cells).

### HOST dispatch

`static void host(cell id)` in frozen `s1.c` — a 16-way switch over arithmetic,
comparison, `HOST_ALLOC`, `HOST_PUTCHAR`, `HOST_PRINT`, `HOST_DUMP`. It reads
and writes only `M[]` (via the memory-mapped `SP`/`HP`). P10A keeps HOST
semantics byte-for-byte; the generated code includes an identical copy (the
frozen `host` is `static` and cannot be linked from a shared object).

### Self-modification / runtime code generation

None. All code is assembled and all forward-call/RAW patches are resolved before
`r0_s1_run`:
- `r0_s1_init()` emits the evaluator and patches the `to_subexpr` /
  `to_block_eval` / mark-frame forward calls (in `r0_s1_init`).
- `r0_s1_parse()` assembles any RAW fragments (appended after `code_end`) and
  patches their label references (in `assemble_raw`).

During execution the code region `[CODE_BASE, asm_here())` is only read, never
written (the evaluator writes to RV cells at 24595+, the data stack at
16384+, the return stack at 24576+, and the heap at 32768+ — all disjoint). So
the entire S1 stream is fixed before the benchmark and can be compiled ahead of
time. For `fib 25` there are no RAW fragments, so the stream is exactly the
evaluator code.

### Smallest valid compiled representation

A **computed-goto ("direct threading") C function** is the smallest faithful
lowering: one C label per S1 instruction, each performing exactly the opcode's
semantics against the shared `M[]`, with a `goto *table[IP - CODE_BASE]`
dispatch for the dynamic `IP` transfers. Operands become compile-time
constants (eliminating the operand fetch); the opcode fetch/decode/`switch` is
replaced by the direct dispatch. `IP/SP/RP/HP` stay in `M[]` (no register
promotion — that is P10B).

## 3. Mechanism

1. `r0_s1_init()` + `r0_s1_parse(fib)` produce the final S1 stream.
2. A driver emits `compiled_s1.c`: the frozen `host` copy + `compiled_run(cell
   *vm, cell start)` with per-instruction labels and a dispatch table over
   `[CODE_BASE, asm_here())`.
3. `gcc -O2 -shared -fPIC compiled_s1.c -o compiled_s1.so`.
4. The driver `dlopen`s the object and calls `compiled_run(M, main_entry)`
   through a new `r0_s1_run_compiled` entry (identical setup to `r0_s1_run`,
   only the executor differs).
5. Interpreted (`s1_run`) and compiled (`compiled_run`) are timed alternately on
   the same `M[]`.

## 4. Semantics preserved

The generated code is a literal transcription of `s1_run`'s per-opcode
behaviour: identical `push`/`pop`/`M[a]` access, identical `0BRANCH`/`HOST`/
`HALT` handling, identical memory-mapped register updates. Branch targets are
the same inline operands; the dispatch table resolves `IP` identically. HOST is
the same switch. The frozen S1 instruction set, RAW ABI, frame ABI, and all
Glon semantics are untouched. The compiled path is executed only in a
benchmark driver — the existing interpreted runtime and tests are unchanged.
