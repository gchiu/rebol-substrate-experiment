# IO-4 host-boundary performance experiment

Isolated profiling and benchmarks. No S1, HOST, parser, evaluator, GC, M1,
IO-3 or MASM-1 baseline source is changed.

## Build

```sh
make glon MODULES=file

cc -std=c17 -O2 -Wall -Wextra -I. experiments/io4/c_copy.c \
   glon_io_file.c glon_io_file_posix.c glon_io_file_cli.c -pthread \
   -o /tmp/c_copy

cc -std=c17 -O0 -g -DGLON_MODULE_FILE -I. \
   -o ./profile_glon \
   experiments/io4/profile_glon.c r0_s1_session.c r0_s1_show.c \
   r0_s1_runtime.c s1.c glon_io_file.c glon_io_file_posix.c \
   glon_io_file_cli.c -pthread
```

`profile_glon` is a copied/instrumented `glon.c`; it must run beside
`glon-lib/`. It prints a `PROFILE ...` line at exit.

## Benchmarks

```sh
# Native POSIX, direct file-module, and worker transport copies
/tmp/c_copy native 1048576 1
/tmp/c_copy service 1048576 1
/tmp/c_copy transport 1048576 1

# Glon streaming copy (no search), exact output check
python3 experiments/io4/stream_copy_check.py --glon ./glon

# Loader lifetime under repeated asynchronous replies
python3 experiments/io4/loader_lifetime.py --glon ./glon

# Empty Glon callback dispatch
python3 experiments/io4/empty_callback.py --glon ./glon --calls 100

# Complete host profile for a streaming job
python3 - <<'PY'
from pathlib import Path
import tempfile
with tempfile.TemporaryDirectory() as d:
    Path(d,'doc.txt').write_bytes(b'x'*65536)
    print(d)
    input()
PY
./profile_glon --io-root "$ROOT" --allow-file-read --allow-file-write \
  experiments/masm-vocabulary/find.glon \
  experiments/masm-vocabulary/file-stream-masm.glon \
  tests/io-file/fixtures/stream_recipe.glon
```

The independent find/MASM conformance and standalone benchmark remain in
`experiments/masm-vocabulary/`.
