# MASM-2 — profile-guided vocabulary acceleration

**Status:** completed isolated experiment. No production vocabulary, HOST
service, parser, evaluator, GC, M1, IO-3 or MASM-1 baseline was changed.
**Branch:** `experiment/masm-vocabulary-2`
**Base:** `24c10b2dd1439359b2b151678bd956998b28cb47` (IO-4)
**Implementation:** `experiments/masm2/`

The selected operation is the internal big-endian `io/u32` request-field
encoder, not a new public string operation. Profiling showed that it is the
dominant source of `mk-string` and `s/+` activity in streaming callbacks.

The result is positive: fusing u32 construction into one MASM allocation
reduced 1 MiB MASM-1 no-match streaming from **78.98 s to 34.76 s** in a
matched run, and reduced the 1 MiB streaming copy from **35.93 s to 19.55 s**.
A pure-Glon fused control was also measured; the MASM worker remains about
5× faster than that control.

---

## 1. Vocabulary-level profiling

`experiments/masm2/trace.glon` wraps the ordinary library words with
invocation counters. It is loaded after the normal libraries and after the
ordinary/MASM implementations, before the application. The wrappers add
overhead and are used **only for counts**, never for timing.

64 KiB MASM-1 no-match, 334 I/O requests:

| Operation | Calls (before MASM-2) | Calls (after u32 MASM) |
|---|---:|---:|
| `mk-string` | 3,490 | **174** |
| `s/+` | 2,987 | **499** |
| `s/slice` | 328 | 328 |
| `s/length` | 1,481 | 1,481 |
| `s/raw-at` | 0 | 0 |
| `s/=` | 1 | 1 |
| `block-at` | 2,000 | 2,000 |
| `block-len` | 0 | 0 |
| `io/request` | 334 | 334 |
| `io/u32` | **829** | 829 (MASM) |
| `io/handle-field` | 332 | 332 |
| `io/reply` | 334 | 334 |
| `s/find` | 164 | 164 |

Most of the original `mk-string`/`s/+` activity was request-field encoding:
829 `io/u32` calls times four one-byte `mk-string` calls plus three `s/+`
results, plus `io/handle-field` combining two u32 strings. Optimising u32
removes ~95% of `mk-string` and ~83% of `s/+` calls.

`reduce` was not counted: wrapping the native through a closure caused a
machine fail-stop, so that count is omitted rather than replaced with an
unsafe workaround.

### Ranked hotspots before MASM-2

1. `io/u32` / request-field construction: 829 calls, ~3,316 `mk-string` and
   ~2,487 `s/+` calls, plus `io/handle-field` concatenations.
2. `s/+` as a general operation: 2,987 calls, mostly from u32.
3. `mk-string`: 3,490 calls, mostly from u32.
4. `block-at`: 2,000 calls (pending records/field blocks).
5. `s/length`: 1,481 calls.
6. `s/slice`: 328 calls (streaming carry/safe prefixes).
7. `s/find`: 164 MASM calls (already accelerated).
8. `io/request`: 334 calls, each creating request/pending blocks.

This ranking is based on invocation counts and allocation/copy structure; it
is not a claim that every call has equal cost. The u32 experiment below
confirms the hypothesis end-to-end.

---

## 2. Selected operation and contract

`io/u32` is an internal request-field encoder; it is not published Glon
vocabulary. Its external contract is precise:

- Input: one tagged integer `n`, used by request construction for values
  `0..0xFFFFFFFF`.
- Output: a new four-byte big-endian STRING!.
- Does not accept or mutate text; no implicit conversions.
- Length is fixed at 4; empty/one-byte inputs are not applicable.
- Does not use a HOST service.
- Allocates exactly one managed STRING! in the MASM implementation.
- Invalid types/negative/out-of-range values are outside the internal contract;
  the raw worker is unchecked, as with other MASM workers. Callers pass
  protocol integers.

The ordinary implementations remain:

- `io/u32-glon`: existing four one-byte strings + three `s/+` calls;
- `io/u32-fused`: one `mk-string reduce [b0 b1 b2 b3]` (ordinary Glon control).

`io/u32` is explicitly selected as `:io/u32-masm` by `experiments/masm2/u32.glon`.

---

## 3. MASM implementation and safety

Source: `experiments/masm2/u32.glon:12-29`.

```text
n -> untagged n
allocate 16 cells (GC_KIND_STRING) using the documented alloc convention
push result = payload + T_STRING        (root before writes)
write length 4
write big-endian b0..b3
ARITY 1 EXIT
```

Safety:

- **One allocation** through the existing managed allocator; no undocumented
  header/M[] construction.
- **No existing string is mutated.** The new payload is written only after
  allocation.
- The result is pushed/rooted **before** its bytes are written; no further
  allocation occurs, so no GC can invalidate the pointer.
- If `alloc` triggers GC, the only argument is a tagged integer, not a managed
  root; this is safe.
- Scratch: `SCRATCH_A`, `SCRATCH_F`; no return stack; no HOST call.
- Non-reentrant, like other MASM workers; it does not yield.
- Depends on `T_STRING`, `GC_KIND_STRING`, the 16-cell allocation extent and
  the payload `[length, byte...]` layout. The seven-opcode portability does not
  imply object-layout portability.
- Allocation failure follows existing fail-stop semantics.

Encoded MASM size: **162 cells / 1,296 bytes**.

---

## 4. Correctness

`u32_cases.py`:

```text
PASS u32 differential: 217 values
PASS u32 survives allocation pressure
```

Cases include 0, 1, 127/128, 255/256/257, 16-bit and 24-bit boundaries,
`0x7FFFFFFF`, `0x80000000`, `0xFFFFFFFF`, and 200 random 32-bit values.
The ordinary, fused-Glon and MASM outputs are compared byte-for-byte.
Allocation pressure retains a result across 120 subsequent string allocations
with GC reporting enabled.

MASM-1's 221-case search differential remains passing. Existing input
immutability and invalid-type tests remain passing.

---

## 5. Standalone performance

`u32_bench.py`, 401 calls after subtracting a one-call setup run:

| Build | `io/u32-glon` | `io/u32-fused` | `io/u32-masm` | MASM speedup |
|---|---:|---:|---:|---:|
| `-O0` | 1,322.7 µs | 506.5 µs | 98.8 µs | ~13.4× vs ordinary |
| `-O2` | 648.6 µs | — | 31.9 µs | ~20.3× vs ordinary |
| `-Os` | 665.2 µs | — | 48.0 µs | ~13.9× vs ordinary |

The pure-Glon fused control is much better than the original but the MASM
worker is still ~5× faster than fused Glon at `-O0`; it also avoids the
temporary `reduce` block.

Instruction counts for 100 u32 calls (`s1_prof`):

| Implementation | S1 opcodes | HOST calls |
|---|---:|---:|
| `io/u32-glon` | 13,175,782 | 1,897,382 |
| `io/u32-masm` | 589,836 | 94,444 |
| ratio | **~22.3× fewer** | **~20.1× fewer** |

Allocation/GC in a 401-call run: the ordinary implementation triggered 23
collections versus about 2 for the MASM version. The fused-Glon control still
allocates a reduce block and one string; MASM allocates one string and no
temporary block.

---

## 6. Streaming integration

`stream_compare.py` compares, with identical inputs and exact output checks:

1. original IO-3;
2. MASM-1 (`s/find` only);
3. MASM-2 (`s/find` + fused MASM `io/u32`);
4. streaming copy without search, with/without u32.

Matched run, seconds:

| Input/case | IO-3 | MASM-1 | MASM-2 | copy | copy+u32 |
|---|---:|---:|---:|---:|---:|
| 4 KiB match | 0.325 | 0.349 | 0.202 | — | — |
| 4 KiB no-match | 0.826 | 0.334 | 0.223 | 0.295 | 0.140 |
| 4 KiB boundary | 0.305 | 0.342 | 0.224 | — | — |
| 64 KiB match | 3.908 | 6.823 | 3.509 | — | — |
| 64 KiB no-match | 12.897 | 4.223 | 2.305 | 2.286 | 1.248 |
| 64 KiB boundary | 2.842 | 3.236 | 1.666 | — | — |
| 1 MiB match | 42.325 | 46.041 | 23.836 | — | — |
| 1 MiB no-match | 184.016 | 78.978 | 34.762 | 35.930 | 19.547 |
| 1 MiB boundary | 43.717 | 51.276 | 25.616 | — | — |
| 4 MiB match | — | 220.539 | 118.313 | — | — |
| 4 MiB no-match | — | 283.825 | 153.508 | — | — |

Boundary cases use a pattern crossing the 400-byte request boundary. Every
case verified exact output bytes and an unchanged source file.

MASM-2 improves the 1 MiB no-match result over MASM-1 by about **2.3×** and
over original IO-3 by about **5.3×**. Match-at-start improves from 42.3 s
(IO-3) to 23.8 s (MASM-2), and boundary crossing from 43.7 s to 25.6 s.
The copy-only path improves from 35.93 s to 19.55 s with u32, independent of
search.

Results are host-load sensitive. Earlier lower-load MASM-2 runs measured
1 MiB at ~25 s match and ~35.5 s no-match, consistent with the matched
34.76/23.84 s pair above.

---

## 7. Post-MASM-2 profile and remaining bottlenecks

1 MiB MASM-2 no-match, `profile_glon`:

| Stage | Seconds | Share |
|---|---:|---:|
| Reply callback/state run | 26.894 | ~77.6% |
| Worker wait/join (1 ms polling) | 5.759 | ~16.6% |
| Worker submit | 0.725 | ~2.1% |
| Request extraction | 0.141 | ~0.4% |
| Parser | 0.096 | ~0.3% |
| Reply source build | 0.094 | ~0.3% |
| Loader cells (transient) | 1,303,203 | |
| Host calls | 76,074 | |
| GC collections | 1,310 | |
| Post-collection live | 2,640 cells | |

Before MASM-2 the same 1 MiB shape was ~64 s total, ~56.9 s run, 6.0 s wait,
233,502 host calls and 2,622 collections. u32 removed roughly half the run
cost and about 95% of `mk-string` calls.

Remaining hot operations after u32: `block-at` (2,000 calls/64 KiB),
`s/length` (1,481), `s/slice` (328), `io/request` (334), and the residual
`mk-string`/`s/+` calls. Their individual times were not separately
instrumented; the worker wait is now ~17% of the 1 MiB run and is the clearest
measured next target. This experiment does not remove polling or accelerate a
second operation.

---

## 8. Allocation/copying architecture

- The ordinary u32 path creates four one-byte strings and three intermediate
  `s/+` strings, plus the final result.
- The fused ordinary control creates one `reduce` block plus one string.
- The MASM path creates one 16-cell managed STRING! and writes four bytes.
- This is a general protocol-field encoding helper, not an application-specific
  fused primitive; it is useful for any request construction using u32 fields.
- The allocation is required because the protocol needs a new managed string;
  the optimization removes redundant intermediate immutable strings, not
  immutability itself.
- The same approach could apply to other fixed-width field encoders, but no
  second operation is implemented here.

---

## 9. Vocabulary development precedent

For allocation-free computational operations (MASM-1 `s/find`):

1. ordinary Glon oracle;
2. MASM worker with no allocation/GC, scratch A/B/C/F and bounded stack;
3. public validated wrapper;
4. shared differential tests;
5. explicit alias/load selection;
6. document layout/scratch/non-reentrancy dependencies.

For allocating operations (MASM-2 u32):

1. same oracle/wrapper/test discipline;
2. use only the documented `alloc`/GC kind path;
3. push/root the new managed result before writing its bytes;
4. perform no allocation after rooting;
5. allocate one object of a fixed, provable extent;
6. test allocation pressure, GC, result lifetime and input immutability;
7. document allocator and object-layout dependencies.

The current MASM interface is sufficient for this class of fixed-width managed
value construction. It is not sufficient to claim general representation
independence or arbitrary fused string builders; malformed/nonnative object
construction remains forbidden.

---

## 10. Recommendation

**Adopt the u32 MASM encoder as the primary MASM-2 result** for the optional
I/O module experiment, while keeping IO-3/MASM-1 untouched.

- It is safely implementable with the existing allocator and seven S1
  instructions.
- It has a clear, narrow contract and independent correctness/GC tests.
- It gives the largest measured end-to-end gain of the one-operation scope.
- It does not alter the wire protocol, chunk size, HOST table, parser,
  evaluator, GC or M1 scheduler.

The next highest-value engineering task is not another automatic vocabulary
acceleration: it is to profile the remaining `block-at`/`io/request`/`s/slice`
path and separately remove the 1 ms worker polling (the latter remained
explicitly out of scope here). Do not integrate MASM-2 into IO-3 without
review.

---

## 11. Limitations

- WSL2 host load produced large absolute-time variability; matched/relative
  results and stage shares are the reliable evidence.
- Trace wrappers change execution and only count calls; they are not a timing
  build.
- `reduce` could not be counted safely through a wrapper and is omitted.
- The remaining `block-at`/`io/request`/`s/slice` costs are inferred from
  counts and the profile stage, not separately timed.
- `io/u32` is an internal helper; no published vocabulary API is added.
- The MASM worker depends on current string allocation/layout conventions.
- The worker is non-reentrant and does not yield; nested/preemptive use is
  unsupported.
- Negative/out-of-range u32 values are outside the internal contract; the raw
  worker is unchecked.
- Allocation failure remains a machine fail-stop under existing semantics.
- Cooperative-task contexts were not instrumented.
- Full `./s1` still stalls in the documented long simulation tests; no
  simulation/runtime change was made.
