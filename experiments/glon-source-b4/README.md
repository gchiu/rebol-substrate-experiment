# GLON B4 — source representation experiment

**Status:** isolated experiment, 2026-11-10, branch `glon-shop-g1a`.
B1–B3, frozen S1, production Glon, memory layout, host ABI and applications are
unchanged. No architectural migration is proposed.

## Question

Which program representation gives the best combination of compactness,
correctness, compiler simplicity and suitability for LLM-generated software?

B4 compares three representations on the same B3 target:

- **A — textual Glon**, using the unchanged B3 compiler.
- **B — semantically tagged 32-bit words**, with explicit token roles.
- **C — direct machine-oriented 32-bit words**, with labels and relocation.

The target is B3 convention A, with the six native operations CALL, RET, >R,
R>, R@ and BRANCH (the B3 `all six` configuration).

## Result summary

All three representations produced **identical machine code** for all seven
tasks: same code cells, same instruction counts, same results and same stack
balances. The comparison is therefore about the front-end representation, not
different runtime code.

| Measure | A text | B tagged | C direct |
|---|---:|---:|---:|
| Source bytes, total / median | 507 / 67 | 672 / 92 | 1,064 / 132 |
| Source tokens, total / median | 187 / 25 | **161 / 22** | 259 / 32 |
| Source processing median | 68.79 µs | 0.30 µs | **0.28 µs** |
| Compiler/decoder/assembler lines | 615 | 263 | **172** |
| Compiler text bytes | 8,553 | 4,559 | **2,505** |

- **A is the smallest in bytes** for these short programs and has the strongest
  language-level diagnostics, but its text parser/compiler is the largest and
  costs about 50 µs on these small inputs.
- **B has the fewest tokens**, a much smaller decoder/emitter than A and good
  structural/semantic validation. Its fixed four-byte token width makes it
  larger in bytes than A on these small tasks.
- **C has the smallest assembler and the largest source.** Its assembler does
  not perform lowering: the C artifact already contains parameter reads,
  prologue/epilogue, calls, drops and labels. That work is moved to the
  artifact/authoring path, so C's small assembler is not evidence that C is
  simpler overall.

Execution is identical because the generated code is identical: fib ~289 µs,
sum ~34 µs, max3 ~0.10 µs, fact ~0.50 µs, diff ~0.07 µs, nested ~0.05 µs,
forward ~0.03 µs. These are wall-clock medians from three unpinned rounds;
variation is in `results.txt`. Byte/lexical-token counts are **not** model
tokens.

## The common target

B4 links the unchanged B3 compiler and machine. The target is:

- convention A: parameters are saved on the return stack; parameter `i` is at
  `RP + i`; result on the data stack; epilogue `R> DROP` per parameter;
- native CALL, RET, >R, R>, R@ and BRANCH;
- no new primitive and no source-specific optimisation.

The B and C front-ends emit exactly the same operation sequences as B3, so
`A/B/C` code is identical for every task. This is checked in the test suite.

## A — textual Glon

The B3 subset and compiler are used unchanged. A is:

```
name: func [params] [ body ]
```

with integers, parameters, calls, `+ - *`, `< <= = > >=`, and
`either cond [then] [else]`. It does not support closures, dynamic binding,
escape laws, blocks as values, locals, strings, errors/`judge`, division or
nested functions.

A's front-end includes lexical analysis, prefix-expression parsing, function
arity resolution, forward-reference handling, AST validation and code
generation. Its diagnostics are the most useful of the three because it knows
the language, not just the token stream.

## B — semantically tagged tokens

B is a little-endian sequence of 32-bit words:

```
word = (role << 24) | operand
magic = 0x42344231
```

| Role | Meaning |
|---|---|
| 1 `FUNC` | operand `id << 8 | arity` |
| 2 `SEQ_BEGIN`, 3 `SEQ_END` | expression sequence |
| 4 `TOP_BEGIN`, 5 `TOP_END` | top-level sequence |
| 6 `INT` | signed 24-bit integer |
| 7 `PARAM` | zero-based parameter index |
| 8 `CALL` | operand `id << 8 | arity`; arguments follow |
| 9 `BIN` | `0 +`, `1 -`, `2 *`, `3 <`, `4 <=`, `5 =`, `6 >`, `7 >=` |
| 10 `EITHER` | condition, then block, else block follow |
| 11–14 | `THEN_BEGIN`, `THEN_END`, `ELSE_BEGIN`, `ELSE_END` |

Function definitions precede the top-level sequence. Function and argument
roles are explicit; there is no textual spelling to parse. Calls carry their
arity, so forward calls can be decoded without a pre-scan; the decoder patches
function ids after all definitions. The decoder validates magic, word
alignment, roles, duplicate function ids, parameter bounds, undefined calls and
call-arity mismatches, then emits B3 operations.

B does not contain names or lexical comments. Diagnostics are by token index,
function id and role rather than source text.

## C — direct machine words

C is a little-endian sequence of 32-bit words:

```
word = (op << 24) | operand
magic = 0x43344231
```

| Op | Meaning |
|---|---|
| 1 `ENTRY` | entry label id |
| 2 `LABEL` | define a label at the current code address |
| 3 `LIT` | raw S1 cell |
| 4 `DUP`, 5 `DROP`, 6 `FETCH`, 7 `STORE` | frozen primitives |
| 8 `ZBRANCH` | label id; frozen conditional branch |
| 9 `HOST` | raw HOST id |
| 10 `CALL` | label id; native CALL |
| 11 `RET`, 12 `TO_R`, 13 `FROM_R`, 14 `R_FETCH`, 15 `BRANCH` | native B3 operations |
| 16 `HALT` | frozen halt |

The C artifact contains all executable operations and stack work. It uses
labels and relocation for calls and branches but no textual Glon parser. The
assembler validates magic, word alignment, operation ids, HOST ids, duplicate
labels, undefined labels and a defined entry label. It does **not** know
function arity, language types or stack balance; it trusts the lowered
operations. A malformed but label-valid C artifact can assemble and corrupt a
stack at run time. This is a representation limitation, not hidden runtime
checking.

## Test suite and correctness

Seven tasks (`tasks/`), each with independent expected results:

| Task | Exercises | Expected |
|---|---|---:|
| `fib` | recursion, comparison, arithmetic, conditional | 2584 |
| `sum` | two-parameter recursion and accumulator | 125250 |
| `max3` | multiple arguments and nested conditionals | 7 |
| `fact` | recursion and tagged multiplication | 3628800 |
| `diff` | absolute difference and comparisons | 25 |
| `nested` | nested calls and two functions | 7 |
| `forward` | forward reference to a later function | 42 |

B4 tests check, for every task and representation:

- identical results and final stack balance;
- deterministic recompilation;
- identical generated code across A, B and C;
- malformed A text;
- malformed B tokens: bad magic, truncation, unknown role, duplicate function,
  undefined call, parameter out of range;
- malformed C words: bad magic, unknown operation, undefined entry, duplicate
  label, undefined label.

B1 tests (31 checks), B2 tests (32), B3 tests (33) and B4 tests (21) all pass;
ASan/UBSan passes for B1–B4; frozen S1 passes.

## Measurements

### Source size and tokens

| Task | A bytes/tokens | B bytes/tokens | C bytes/tokens |
|---|---:|---:|---:|
| fib | 72 / 27 | 104 / 25 | 120 / 29 |
| sum | 77 / 27 | 100 / 24 | 160 / 39 |
| max3 | 107 / 41 | 152 / 37 | 312 / 77 |
| fact | 67 / 24 | 92 / 22 | 132 / 32 |
| diff | 67 / 25 | 92 / 22 | 168 / 41 |
| nested | 66 / 22 | 68 / 16 | 88 / 21 |
| forward | 51 / 21 | 64 / 15 | 84 / 20 |

Token-count method:

- A: lexical tokens (words/integers, `[` and `]`), comments removed.
- B: 32-bit payload words, excluding the 4-byte magic.
- C: 32-bit payload words, excluding the 4-byte magic; includes ENTRY/LABEL
  metadata.

A wins bytes here because its names and syntax are short; B wins token count
because it has no names/punctuation, but its fixed 4-byte words make it larger
in bytes. C is largest in both. These are **not** model tokens.

### Code and execution

All representations generated identical code cells and instructions:

| Task | Code cells | Instructions | Result | Data/return stack |
|---|---:|---:|---:|---:|
| fib | 39 | 129,595 | 2584 | 11 / 36 |
| sum | 53 | 12,023 | 125250 | 3 / 1,503 |
| max3 | 108 | 38 | 7 | 3 / 4 |
| fact | 46 | 213 | 3628800 | 11 / 20 |
| diff | 55 | 29 | 25 | 3 / 3 |
| nested | 23 | 24 | 7 | 2 / 4 |
| forward | 21 | 16 | 42 | 2 / 4 |

Execution medians, identical across representations: fib ~288 µs, sum ~34 µs,
max3 ~0.10 µs, fact ~0.50 µs, diff ~0.07 µs, nested ~0.05 µs, forward ~0.03 µs.
The differences between representations are timing noise; the code is the same.

### Source processing

| Representation | Median source processing | Task-median range |
|---|---:|---:|
| A text | 68.79 µs | 52.89–88.60 µs |
| B tagged | 0.30 µs | 0.26–0.43 µs |
| C direct | 0.28 µs | 0.23–0.46 µs |

A's time is text parsing plus code generation. B's is token decoding plus code
generation. C's is direct assembly. A is roughly 230× slower on these tiny
inputs, but the absolute times are small; for fib, A's ~53 µs is well below the
~288 µs execution time, while for the smallest tasks it is larger than
execution. The B/C times are near timer resolution and should be read as
order-of-magnitude, not precise.

The B3 API exposes only a combined parse-and-compile call; A cannot be split
into parse-only and codegen-only times without changing B3, which B4 does not
do. B and C are single-pass decode+emit / assemble pipelines. The table is
therefore a comparable **source-processing total**, not an internal phase
breakdown for all three.

**This is not an authoring-time comparison.** B/C fixtures were prepared by
`tools/gen_fixtures.py` (267 lines, shared authoring tooling). The cost of
producing the artifacts is not in the front-end measurements. C's lowering is
especially delegated to that artifact/generation path, and B's source is
pre-parsed into semantic roles.

### Implementation size

| Component | Lines | Text bytes |
|---|---:|---:|
| A/B3 compiler | 615 | 8,553 |
| B decoder/emitter | 263 | 4,559 |
| C assembler | 172 | 2,505 |
| Common B3 machine (fast) | 276 | 5,505 |

Total front-end + machine text: A 14,058 bytes, B 10,064, C 8,010. C's assembler
is smallest but has the least semantic validation and relies on pre-lowered
artifacts. B does structured decoding plus code generation. A does the most
front-end work but gives language-level diagnostics.

## Generation and repair

**No LLM trial was run.** No LLM was available through this development
environment. The harness is in `generation/`:

- task specs in `tasks/`;
- equivalent format prompts for A/B/C in `generation/prompts/`;
- `generation/run_trial.sh`, which invokes an external `LLM_CMD` and then
  verifies the artifact with `b4 --verify`;
- `generation/results.md`, which records that trials remain unperformed.

No claim is made that one representation is easier for LLMs. Byte sizes and
lexical token counts are not model tokens; an identified tokenizer and a
controlled generation/repair experiment are required.

## Assessment

This experiment does not select a final winner. The evidence points in
different directions:

- **A textual Glon** is the most compact in bytes for these small programs and
  has the best language-level diagnostics, but the largest front-end and a
  ~50 µs parse+compile cost. It is the reference representation and remains
  necessary for human-readable source.
- **B semantically tagged tokens** is the most compact in token count, has a
  much smaller decoder/emitter than A, and retains semantic structure and good
  structural validation. It merits further investigation as the balanced
  candidate, especially if controlled LLM/tokenizer trials support it.
- **C direct machine words** has the simplest assembler and the most explicit
  execution content, but the largest source, no language-level validation, and
  its lowering complexity is moved into authoring/generation. It is more
  naturally a compiler target/relocation format than a primary source for
  LLM-generated software.

**Recommendation:** investigate B further, with A as the reference and C as a
low-level target. Do not choose a winner before generation-and-repair evidence
exists. Any future work still needs decisions about checked stacks/K1, the
frozen machine and production integration.

## Limitations

- Restricted integer subset only; no closures, dynamic binding, escape laws,
  managed values/GC, errors/`judge`, locals or loader integration.
- A uses the full B3 compiler, which also contains the B3 convention-B support;
  its 615-line / 8,553-byte size therefore overstates a convention-A-only text
  compiler. This is the required existing B3 textual compiler, not a minimised
  new one.
- Equivalent code was produced by construction in this experiment; no
  representation-specific optimisation or code-quality difference was tested.
- B/C front-end times are near timer resolution; A is clearly slower on these
  tasks, but absolute costs are still small.
- Fixed-width B/C encodings were not compared with variable-length or
  compressed variants.
- C cannot validate arity or stack balance; its assembler checks labels and
  operations only.
- No LLM trials were performed. The harness is provided, but LLM productivity
  remains unanswered.
- All execution uses unchecked stacks; K1 remains open.
- B4 links the unchanged B3 compiler/machine; no production file was changed.

## Reproduce

From the repository root:

```
make -C experiments/glon-source-b4 fixtures
make -C experiments/glon-source-b4 test
make -C experiments/glon-source-b4 measure
make -C experiments/glon-source-b4 bench
make -C experiments/glon-source-b4 size
make -C experiments/glon-source-b4 sanitize
make -C experiments/glon-source-b4 reproduce
```

Raw count, timing and size output is in [`results.txt`](results.txt).

## Files

| File | Purpose |
|---|---|
| `b4_tagged.c/.h` | B decoder/emitter |
| `b4_direct.c/.h` | C label/relocation assembler |
| `b4_main.c` | tests, counts, timing, `--verify` |
| `b4_common.h` | common target flags and code type |
| `tools/gen_fixtures.py` | deterministic A/B/C fixture generator |
| `tasks/` | seven tasks in all three representations |
| `generation/` | unperformed LLM generation/repair harness |
| `results.txt` | raw and summarised measurements |
