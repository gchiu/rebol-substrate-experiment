# FRAGMENTATION.md — current DEFLATE status

## Verified current result

Command run during C1:

```
make -C experiments/glon-deflate test
```

Result:

- unit tests: **44/44 passed**;
- conformance: **479/480 runs passed**;
- reference classes: malformed 222/222, ok 229/230, truncated 24/24,
  window-limit 4/4;
- one failure: `multi-memlevel1`, schedule `c128/r100`,
  `glon=host-failure`, 4047 bytes produced before halt, zlib expected 9000
  bytes.

Diagnostic:

```
[dump] IP=4046 SP=16366 RP=21506 HP=40000 top=34668
dzhost: dispatch failed (machine halt 0; last collection: 4288 live, 2944 free in 19 blocks)
```

A focused rerun (`--only multi-memlevel1`) produced 3/4 runs passing; the same
c128/r100 schedule failed, with the same diagnostic. The failure is a
**heap-fragmentation host/machine fail-stop**, not a wrong-byte decode. It
occurs in the full decoder's managed-heap/window path.

## Relationship to C1 kernel

`hf/left` does not use strings, input buffering, decoder continuation,
bit-buffer state, Huffman symbol tables, LZ77 copying, host chunk delivery or
heap allocation. It is a pure integer/count-table function over a harness block.
It therefore does not exercise the fragmentation path. C1 compiled and original
paths agree on all ten vectors.

No repair was attempted. This remains an existing decoder/runtime issue and
must not be confused with a compiler translation defect.
