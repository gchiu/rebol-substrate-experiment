# MASM vocabulary acceleration experiment

This directory is an isolated experiment. It does not change S1, HOST, the
evaluator, the parser, GC, M1 or the IO-3 implementation.

## Files

- `find.glon` — ordinary Glon `s/find-glon`, MASM `s/find-masm`, selected
  `s/find` alias.
- `file-stream-masm.glon` — copy of IO-3 `modules/file-stream.glon` with only
  `stream/find-first` replaced by `s/find`; the original is unchanged.
- `c_find.c` — standalone native C reference/benchmark (not linked to Glon).
- `cases.py` — shared conformance/differential tests for Glon reference, MASM
  and C.
- `bench_find.py` — identical-input performance comparison.
- `masm_size.c` — emits the S1 code-cell size of the MASM block.
- `prof_find.c` — optional profiled-runtime opcode/HOST counter.

## Build and run

```sh
make glon
cc -std=c17 -O2 -Wall -Wextra experiments/masm-vocabulary/c_find.c -o /tmp/c_find
python3 experiments/masm-vocabulary/cases.py --glon ./glon --c /tmp/c_find
python3 experiments/masm-vocabulary/bench_find.py --glon ./glon --c /tmp/c_find --calls 401

cc -std=c17 -O2 -I. experiments/masm-vocabulary/masm_size.c \
   r0_s1_runtime.c s1.c -o /tmp/masm_size
/tmp/masm_size experiments/masm-vocabulary/find.glon

cc -std=c17 -O2 -I. experiments/masm-vocabulary/prof_find.c \
   r0_s1_runtime.c s1_prof.c -o /tmp/prof_find
/tmp/prof_find /tmp/find-100calls.glon
```

The streaming comparison uses the module build and loads the experimental
libraries after the normal module libraries:

```sh
make glon MODULES=file
./glon --io-root "$root" --allow-file-read --allow-file-write \
  experiments/masm-vocabulary/find.glon \
  experiments/masm-vocabulary/file-stream-masm.glon \
  tests/io-file/fixtures/stream_recipe.glon
```

`s/find` is experimental and is not added to the published Glon vocabulary.
