# MASM-3 vocabulary acceleration experiment

Isolated experiment based on MASM-2. The selected operation is
`s/slice2`, a new reusable immutable-string operation that slices the virtual
concatenation of two strings without materialising the concatenation. It is
used in an experimental streaming copy to remove the intermediate
carry+chunk string from the no-match path.

Frozen S1, the HOST table, the parser, evaluator, GC, M1, IO-3 and the MASM-1/
MASM-2 baselines are unchanged. Nothing here is published vocabulary.

## Files

- `slice2.glon` — ordinary reference, MASM worker, checked wrappers, full
  error classification; used by the differential tests.
- `slice2-min.glon` — deployment-minimal worker + checked wrapper (two global
  definitions) used by the streaming benchmark. The full library's extra
  helper closures are permanent GC roots and measurably increase every
  collection; see the report.
- `file-stream-masm3.glon` — experimental streaming copy: MASM `s/index-of` (formerly `s/find`),
  `s/slice2`, and a zero-carry fast path that writes the chunk directly.
- `slice2_cases.py` — differential, error, max-size, allocation-lifetime and
  cooperative-task tests.
- `slice2_bench.py` — standalone reference/MASM benchmark.
- `stream_compare.py` — IO-3/MASM-1/MASM-2/MASM-3/copy comparison with exact
  output and source-preservation checks.
- `opcost.c`, `make_benches.py`, `opbenches.glon` — operation-cost harness used
  to measure per-operation wall time, S1 opcodes, HOST calls and GC. Build the
  timed variant with `s1.c`; build the counting variant with
  `-DUSE_S1_PROF` and `s1_prof.c`.
- `profile_stream.c` — a copy of the IO-4 profiling driver with GC-region/
  opcode/load-count reporting. It needs the instrumented runtime copy
  `r0_s1_runtime_geo.c` (measurement only; not part of any build/product).
- `coop-core.glon` — the M1 task core extracted from the shipped tuple-space
  demo, used by the cooperative-task test.

## Build and run

```sh
make glon MODULES=file
python3 experiments/masm3/slice2_cases.py --glon ./glon
python3 experiments/masm3/slice2_bench.py --glon ./glon --calls 201
python3 experiments/masm3/stream_compare.py --glon ./glon
python3 experiments/masm3/stream_compare.py --glon ./glon --four-mib \
    --only masm2,masm3,copy,copy-u32
```

Build the operation-cost tools beside `glon-lib/`:

```sh
cc -std=c17 -O2 -I. experiments/masm3/opcost.c \
   r0_s1_session.c r0_s1_show.c r0_s1_runtime.c s1.c -o ./opcost
cc -std=c17 -O2 -I. -DUSE_S1_PROF experiments/masm3/opcost.c \
   r0_s1_session.c r0_s1_show.c r0_s1_runtime.c s1_prof.c -o ./opcost-prof
python3 experiments/masm3/make_benches.py
./opcost --lib experiments/masm3/slice2.glon --word slice2-stream \
   experiments/masm3/opbenches.glon
```

Build the streaming profiler beside `glon-lib/`:

```sh
cc -std=c17 -O2 -DGLON_MODULE_FILE -DMASM3_GEO -DR0_S1_PROFILE -I. \
   -o ./profile_stream experiments/masm3/profile_stream.c \
   r0_s1_session.c r0_s1_show.c experiments/masm3/r0_s1_runtime_geo.c \
   s1_prof.c glon_io_file.c glon_io_file_posix.c glon_io_file_cli.c -pthread
```

See `docs/design/GLON-MASM-VOCABULARY-ACCELERATION-3.md` for the investigation,
measurements, selection rationale, conventions and limitations.
