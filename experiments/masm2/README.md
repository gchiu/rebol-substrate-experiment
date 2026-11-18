# MASM-2 vocabulary acceleration experiment

Isolated experiment based on IO-4. The selected operation is the internal
big-endian `io/u32` request-field encoder, after profiling showed it dominated
`mk-string`/`s/+` activity in streaming callbacks.

Files:

- `u32.glon` — ordinary, fused-Glon control and MASM implementations.
- `u32_cases.py` — differential/GC/lifetime tests.
- `u32_bench.py` — standalone benchmark.
- `trace.glon`, `trace_report.glon` — invocation counters for vocabulary
  operations in the streaming callback.
- `stream_compare.py` — IO-3/MASM-1/MASM-2/copy comparison, including
  boundary-crossing cases.
- `prof_u32.c` — optional S1 opcode/HOST counter for the MASM-2 worker.

## Build/run

```sh
make glon MODULES=file
python3 experiments/masm2/u32_cases.py --glon ./glon
python3 experiments/masm2/u32_bench.py --glon ./glon --calls 401 --gc
python3 experiments/masm2/stream_compare.py --glon ./glon --include-io3
```

`stream_compare.py` loads the MASM-1 find/file-stream copy from
`experiments/masm-vocabulary/`, then adds `experiments/masm2/u32.glon` for the
MASM-2 variant. The original IO-3 and MASM-1 files are not modified.

The trace run (counts only, not timing) is:

```sh
./glon --io-root "$ROOT" --allow-file-read --allow-file-write \
  experiments/masm-vocabulary/find.glon \
  experiments/masm2/u32.glon \
  experiments/masm2/trace.glon \
  experiments/masm-vocabulary/file-stream-masm.glon \
  tests/io-file/fixtures/stream_recipe.glon \
  experiments/masm2/trace_report.glon
```
