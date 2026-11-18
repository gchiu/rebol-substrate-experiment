# MASM vocabulary acceleration — experiment report

**Status:** completed isolated experiment; no production vocabulary, HOST
service, evaluator or IO-3 change.
**Branch:** `experiment/masm-vocabulary`
**Base:** `43600bb0f04f26bc4fce194d9aaf032bd724615b` (HOST investigation,
  itself based on IO-3 `72ff1ff`)
**Implementation:** `experiments/masm-vocabulary/`

The experiment asks whether bounded, reusable vocabulary operations can be
written in MASM using only the seven frozen S1 instructions and existing HOST
operations, with materially lower cost than ordinary Glon. The reference
operation is:

```glon
s/find text pattern
```

It returns the zero-based first byte offset, or `-1` for no match. Empty
pattern returns `0`. Non-STRING! arguments raise `SIN! 'type 's/find`. Neither
argument is mutated.

---

## 1. Result

MASM is viable for this class of operation.

- **[V]** The ordinary Glon reference and the MASM implementation agree on
  **221/221** differential cases, including empty text/pattern, pattern longer
  than text, binary bytes, overlapping candidates, last valid offset, and
  randomised inputs.
- **[V]** The C reference agrees on **221/221** cases.
- **[V]** Inputs are unchanged after search.
- **[V]** For a 400-byte no-match search, MASM is about **220–235× faster**
  than the segmented ordinary-Glon reference at `-O0`/`-O2`/`-Os`.
- **[V]** The MASM routine emits **241 S1 code cells (1,928 bytes)** and
  allocates no managed memory.
- **[V]** Streaming substitution reduced the current 1 MiB no-match run from
  **194.99 s** to **60.75 s** end-to-end (about 3.2×), while 4 MiB still ran
  successfully and live managed memory stayed bounded.
- **[V]** No frozen S1 file, HOST id, parser, evaluator, GC, M1 scheduler or
  IO-3 file was modified.

The remaining cost is dominated by host request/reply, string copying and the
Glon state machine, not by search. That is expected: MASM accelerates the
bounded computational portion only.

---

## 2. Existing MASM conventions used

The implementation follows established library practice rather than inventing
a new calling convention.

**[V] Arguments and results.** A `masm N` callable receives its arguments on
the data stack; the last argument is on top. It returns tagged R0 values and
ends with `ARITY n EXIT`. Examples: `s/raw-cat` and `s/raw-length`
(`demo/shop/strings.glon:26,54`), `s/raw-at` and `s/raw-slice`
(`demo/shop/strings-ext.glon:78,86`).

**[V] String representation.** A STRING! value has tag `T_STRING` (12). Its
payload pointer is obtained by subtracting `T_STRING`; the payload begins with
a raw byte count at `M[payload]`, followed by raw byte cells. `s/raw-at` uses
exactly this layout (`demo/shop/strings-ext.glon:78-80`). The experiment
depends on this representation.

**[V] Scratch cells.** Existing workers use `SCRATCH_A`, `SCRATCH_B`,
`SCRATCH_C`, `SCRATCH_F`; they avoid `SCRATCH_D/E`, which hold live output
state, and do not touch evaluator registers or task state. The workers are
non-reentrant and do not yield (`demo/shop/strings.glon` header,
`demo/shop/strings-ext.glon:65-75`).

**[V] Loops.** MASM has no high-level loops. It uses `LIT`, `@`, `!`,
`0BRANCH`/derived `BRANCH`, labels, and `CALL` to named managed entry points
(`alloc`, `collect`, `lookup`, ESC helpers) or labels in the same block
(`r0_s1_runtime.c` masm assembler, especially `560-640`).

**[V] Limits.** A masm block is limited by the assembler's token/label/reference
tables and code space; malformed blocks are rejected before emission
(`MACHINE-INTEGRITY-AUDIT.md:132-145`; `GLON-ARCHITECTURE-MEMORY.md:798-808`).

The small convention proposed **only for this experiment** is:

- `s/raw-find: masm 2` receives `(text pattern)`.
- It uses `SCRATCH_A/B` for payload pointers, `SCRATCH_C/F` for cursors, and
  one return-stack cell for the candidate start.
- It returns a tagged offset or tagged `-1`, with `ARITY 1 EXIT`.
- The public wrapper validates STRING! arguments. Calling the raw worker with
  non-strings is outside the contract.

---

## 3. Implementations

### A. Ordinary Glon reference — `s/find-glon`

Source: `experiments/masm-vocabulary/find.glon:14-73`.

It is the IO-3 segmented search adapted to the standalone contract:

- halves the candidate range into 64-start segments;
- compares first bytes with `s/raw-at`, then checks the remainder;
- continues after a partial match;
- returns `0` for an empty pattern and `-1` after the valid start limit.

It stays a portable reference and is the semantic oracle for differential
tests. It uses no substring allocation and no complete-string copy.

### B. MASM implementation — `s/raw-find` / `s/find-masm`

Source: `experiments/masm-vocabulary/find.glon:76-117`.

Algorithm is deliberately straightforward:

```text
payload_text = text - T_STRING
payload_pattern = pattern - T_STRING
text_cursor = payload_text + 1
pattern_cursor = payload_pattern + 1

outer:
  if text_cursor > payload_text + 1 + text_len - pattern_len: return -1
  pattern_cursor = payload_pattern + 1
  save candidate = text_cursor on R
inner:
  if pattern_cursor >= payload_pattern + 1 + pattern_len: return candidate - text_start
  if text_cursor[0] != pattern_cursor[0]: next candidate
  text_cursor++; pattern_cursor++; repeat

next:
  candidate++; restore text_cursor; repeat
```

Properties:

- No managed allocation, no `CALL alloc`, no GC during search.
- No writes to either string: only `@` reads and scratch/return-stack writes.
- One return-stack local for the candidate; balanced on every exit.
- Uses only existing HOST arithmetic/comparison; no new HOST id.
- Empty pattern matches at 0 naturally.
- Pattern longer than text exits at the outer bound.
- Binary bytes are raw cells; no signed-char or UTF-8 assumptions.
- The recursive stack growth of the reference is replaced by a loop.

The selected `s/find` is an explicit alias:

```glon
s/find: :s/find-masm
```

There is no automatic polymorphism. A caller can still call `s/find-glon`
directly; a library loader may choose the alias.

### C. Native C reference — `c_find.c`

Source: `experiments/masm-vocabulary/c_find.c`.

It reads text/pattern files, runs a straightforward C `find_first`, and can
repeat the search for timing. It is not linked into Glon, not a HOST service
and not available to Glon applications. Its only role is independent expected
results and a native performance reference.

---

## 4. Correctness and conformance

Command:

```sh
cc -std=c17 -O2 -Wall -Wextra experiments/masm-vocabulary/c_find.c -o /tmp/c_find
python3 experiments/masm-vocabulary/cases.py --glon ./glon --c /tmp/c_find
```

Result:

```text
PASS s/find-masm: 221 cases
PASS s/find-glon: 221 cases
PASS c_find: 221 cases
PASS input immutability
PASS invalid type
ALL PASS (221 differential cases)
```

Coverage includes:

- empty text and empty pattern;
- pattern longer than text;
- beginning/middle/end and last valid start;
- multiple and overlapping candidates;
- repeated-byte and repeated-prefix patterns;
- single-byte patterns;
- embedded zero and bytes above 127;
- 400-byte text (the safe streaming chunk scale);
- randomised binary differential cases;
- repeated invocations in one session (scratch/reuse check);
- invalid argument type (`SIN! 'type`).

The C reference was checked over all 221 cases, not only a sample.

---

## 5. Standalone performance

Method: `bench_find.py` generates the same bytes for all implementations,
executes Glon in batches with 401 calls and reports the slope after subtracting
a one-call run. C runs its own loop for 20,000 iterations. Values are
microseconds per call on the same WSL2 host; `-O0`/`-O2`/`-Os` are C compiler
optimization levels for both the Glon executable and `c_find`.

### 400-byte cases

| Case | Build | Glon reference | MASM | C reference |
|---|---|---:|---:|---:|
| early | `-O0` | 944.9 | 100.4 | 6.8 |
| early | `-O2` | 379.9 | 29.4 | 1.2 |
| early | `-Os` | 550.8 | 52.5 | 1.8 |
| no match | `-O0` | 57,607.8 | 245.0 | 995.1 |
| no match | `-O2` | 23,196.2 | 105.4 | 424.8 |
| no match | `-Os` | 30,034.2 | 129.1 | 324.7 |
| overlapping | `-O0` | 212,880.0 | 452.4 | 2,708.8 |
| overlapping | `-O2` | 90,107.9 | 190.9 | 548.6 |
| overlapping | `-Os` | 110,659.8 | 239.5 | 565.5 |
| long pattern | `-O0` | 53,351.6 | 232.5 | 1,050.3 |
| long pattern | `-O2` | 23,198.3 | 98.4 | 254.2 |
| long pattern | `-Os` | 28,422.8 | 151.8 | 251.3 |

At 128 bytes, MASM was roughly 31–207 µs per call and the reference roughly
350–64,860 µs, depending on match position and build; the full CSV-style data was produced by
`bench_find.py`.

C is a reference, not an upper-bound promise. Its very early-match result is
faster than MASM because it is optimized native code with a tight loop; its
simple bytewise no-match loop can be slower than MASM's tuned S1 loop. The
important comparison for this experiment is MASM versus the portable Glon
reference, which is roughly **9–13× faster for early matches and 220–235×
faster for the 400-byte no-match case** across `-O0`/`-O2`/`-Os`.

### S1 instruction/profile counts

Using `prof_find.c` with the instrumented `s1_prof.c` on 100 calls of a
400-byte no-match search:

| Implementation | S1 opcodes | HOST calls |
|---|---:|---:|
| `s/find-glon` | 522,471,203 | 87,505,296 |
| `s/find-masm` | 3,541,008 | 612,298 |
| ratio | **~147× fewer** | **~143× fewer** |

These counts include the Glon wrapper/benchmark expressions around the search,
not only the scanning loop. They show why the wall-clock gap is large.

Encoded size measured by `masm_size.c`:

```text
masm_code_cells 241
masm_code_bytes 1928
```

The experiment adds no C code to `glon`; the minimal executable is unchanged
(185,216 bytes at `-O0 -g`). For reference, the same minimal CLI is 111,488
bytes at `-O2` and 95,696 bytes at `-Os`; the standalone C benchmark is about
16.7 KiB.

---

## 6. Memory, GC and safety

**Allocation.** `s/raw-find` performs no managed allocation and calls no
`alloc`/`collect`. It reads string payloads and writes only
`SCRATCH_A/B/C/F` and one return-stack cell. `s/find-masm` is an ordinary Glon
function wrapper and necessarily uses the normal evaluator call machinery; the
100-call `--gc-report` run showed `collect=0` for both reference and MASM
(no collection was triggered).

**No mutation.** The routine never stores through a string payload pointer.
The conformance test prints both inputs after search and compares them.

**GC safety.** No host callback, allocation or GC safepoint occurs inside the
raw routine. The arguments are rooted on the data stack while the raw routine
runs. There is no retained raw address after return.

**Return stack.** One candidate pointer is pushed per candidate; it is popped
on both match and mismatch. `find-no` is reached only before a candidate is
pushed. The routine ends with `ARITY 1 EXIT` and a balanced return stack.

**Scratch and reentrancy.** The routine follows the existing MASM convention:
`SCRATCH_A/B/C/F` are shared and non-reentrant. It does not use `SCRATCH_D/E`.
It does not yield, so under the current M1 cooperative model it cannot be
interleaved with another task mid-search. It is not safe for arbitrary nested
masm calls or preemptive scheduling; that is an implementation convention, not
a language guarantee.

**Representation dependency.** The code depends on `T_STRING`, the payload
`[length, byte...]` layout, and the raw `masm` calling/`ARITY` convention. Those
are current library/substrate conventions, not a promise that every future
object representation works unchanged. Portability of the bytecode does not by
itself guarantee compatibility with a future object model.

**Invalid raw calls.** The public wrappers check `s/raw-string?` before calling
the raw worker. Calling `s/raw-find` directly with non-strings is unsafe and is
outside the published contract.

**Input limits.** The routine has no fixed internal text/pattern cap beyond
the STRING! length and cell arithmetic. Tests exercise 400-byte strings (the
streaming chunk and safe whole-source limit); larger strings require separate
measurement and memory availability. No allocation is made for search.

---

## 7. Streaming integration

`experiments/masm-vocabulary/file-stream-masm.glon` is a copy of IO-3's
`modules/file-stream.glon`. Only `stream/find-first` is replaced by a call to
`s/find`; the original file remains untouched. The copy is loaded after the
normal module library, so the original baseline remains available.

Command:

```sh
make glon MODULES=file
python3 experiments/masm-vocabulary/stream_integration.py --glon ./glon
```

Output checks compare exact `out.txt` bytes and verify `doc.txt` is unchanged.

| Input | Original IO-3 no-match | MASM no-match | Original match | MASM match |
|---|---:|---:|---:|---:|
| 1 KiB | 0.25 s | 0.14 s | — | 0.13 s |
| 64 KiB | 13.41 s | 3.82 s | — | 3.13 s |
| 1 MiB | 194.99 s | 60.75 s | ~55 s historical | 48.53 s |
| 4 MiB | not run | 283.82 s | not run | 220.54 s |

The 1 MiB baseline was remeasured under the current IO-3 build: **194.99 s**,
not the historical 246 s. The historical figure is therefore not used as a
matched baseline. The MASM end-to-end improvement is about **3.2×** for 1 MiB
no-match; the standalone search improvement is much larger because host
request/reply, copying and state-machine work dominate the remaining time.

Approximate split for 1 MiB MASM: match-at-start (copy-dominated) 48.53 s;
no-match 60.75 s; the extra ~12 s is search-attributable. The original no-match
baseline was 194.99 s, so upgrading the search removes ~134 s of the original
end-to-end cost. These are end-to-end observations, not a cycle-accurate
search-only profile.

4 MiB no-match with `--gc-report` took 317.90 s with reporting enabled (the
same run without reporting was 283.82 s), RSS 2,200 KB, post-collection live
3,456 cells, 10,487 collections, heap high 40,000. 1 MiB no-match measured
post-collection live 3,456, 2,622 collections, 3,776 free cells, RSS 1,880 KB.
Memory remains bounded and the original streaming behavior is preserved.

---

## 8. Regression and frozen constraints

Run:

```sh
./check-frozen-s1.sh
make io-file-test
make io-file-stream-test
make glon-smoke
make glon-smoke MODULES=file
python3 experiments/masm-vocabulary/cases.py --glon ./glon --c /tmp/c_find
```

Observed:

- `./check-frozen-s1.sh`: PASS.
- `make io-file-test`: 30/30, including the 1,024-byte whole-file cap.
- `make io-file-stream-test`: 24/24.
- `make glon-smoke` and `make glon-smoke MODULES=file`: PASS.
- Non-simulation native runner: 948 checks, all tests passed.
- `make wasm-test`: PASS.
- Full `./s1` still stalls in the long simulation tests; no simulation or
  runtime code was changed.

No frozen S1 file, HOST id, parser, evaluator, GC, M1 scheduler or IO-3 source
was modified. `s/find` is an experimental library name, not a published
vocabulary change.

---

## 9. Reusable development pattern

The experiment supports a small, repeatable pattern:

1. Write an ordinary Glon reference with a precise semantic contract.
2. Write a masm worker with explicit argument/result, scratch and layout
   assumptions.
3. Keep a public validated wrapper around the raw worker.
4. Keep both implementations callable for differential tests and fallback.
5. Run shared conformance tests, including invalid input and repeated calls.
6. Measure standalone and in-context behavior.
7. Select with an explicit alias/load choice; no automatic polymorphism.
8. Document the internal representation dependency and non-reentrancy.

This is enough for `s/find`. It is **not** a general acceleration framework.
Do not add a registry, plugin discovery or automatic type dispatch without a
second demonstrated need.

---

## 10. Recommendation

**MASM is suitable as a candidate acceleration layer for bounded,
no-allocation, byte-oriented vocabulary operations, subject to shared
conformance tests and explicit representation/scratch contracts.**

- Use it first for `s/find`-class routines, not for filesystem or network
  services.
- Keep the ordinary Glon implementation as the portable oracle and fallback.
- Keep IO-3 and the closed HOST table unchanged.
- If adopted more broadly, consider a small `s/` vocabulary module with
  reference and accelerated implementations selected at load time. Do not
  introduce a general framework until multiple operations justify it.
- The measured streaming gain is real but limited by host I/O; do not expect
  MASM to make file streaming match native C.
- The 400-byte no-match search is a strong positive result; early/first-byte
  matches have less headroom because the reference is already short.

No negative result blocks the experiment. The remaining work is integration
and publication policy, not evidence that MASM cannot implement the operation.
