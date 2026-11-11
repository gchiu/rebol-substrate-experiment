# B10 — DEFLATE VM-operation attribution

This isolated experiment profiles the existing seven-primitive DEFLATE runtime.
It does not modify frozen S1, production Glon, the runtime, the decoder, or the
deployed `/inflate/` page.

## Build and run

From the repository root, the normal B9 baseline host must exist:

```
make -C experiments/glon-deflate build/dzhost
```

Then:

```
cd experiments/glon-deflate-b10
make
make profile        # writes baseline/profile-results.json
make test           # profile + frozen-S1 check + fixed workload smoke
```

`build/b10-host-ops` links the **unprofiled** current runtime and an
instrumented S1 dispatcher (`s1_prof.c`). It gives exact opcode and HOST counts.
`build/b10-host-pf` additionally compiles the runtime with `-DR0_S1_PROFILE`,
providing `PF_*` counters (subexpression dispatch, block evaluation, lookup,
closure invocation, allocation, lexical access, hashing). It is used for
categories only, never for elapsed-time claims.

The three workloads and schedules are copied from B9: fixed, dynamic and
streaming. `profile_workloads.py` verifies every output against zlib SHA-256
before writing results.

## Limits

There is no symbol map from runtime-emitted S1 code to Glon function names, so
B10 does not claim a ten-function ranking. The `PF_*` counters and opcode
classes are the reliable attribution units. The `-DR0_S1_PROFILE` build adds
instrumentation operations and is not used for timings; B9's preserved normal
timings are the time baseline.
