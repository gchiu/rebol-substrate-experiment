# Constrained-memory measurement helper

`measure_memory.py` builds a temporary host probe in `/tmp` from the existing
`experiments/glon-deflate/dzhost.c`, scans existing heap headers after a run,
and reports live/free/largest-free/heap high-water/GC counts for the B9
DEFLATE fixtures. It also records native process RSS for the existing B13
FizzBuzz/LaTeX executables. It does not modify runtime or application source,
does not run new GC algorithms, and does not measure GC pauses or per-allocation
latency (those need runtime timestamp instrumentation).

Run from the repository root:

```
python3 experiments/glon-memory-audit/measure_memory.py
```

`measurements.json` is generated locally and intentionally not committed.
