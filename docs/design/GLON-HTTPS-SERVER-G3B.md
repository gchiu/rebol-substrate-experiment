# GLON HTTPS Server — G3B Baseline and Persistence Proposal

**Status:** baseline established; no implementation started.
**Branch:** `experiment/glon-https-g3b`
**Baseline:** D1 `3a194a2ab7261e8fde183f5803848f91b251c248`
**Frozen S1:** `f90496c26dc45c7a387d8fc8639cd2781507d3d2`

## 1. Environment and build

- Host: WSL x86-64; GCC 11.4.0; Python 3.10.
- OpenSSL 3.0.2 headers from the matching Ubuntu `libssl-dev` package,
  extracted under `/mnt/d/tools/openssl-dev`; runtime OpenSSL 3.0.2.
- Build: `make glon MODULES=file,http MEM_PROFILE=large
  MEM_LOADER_CELLS=500000`.
- Default benchmark flags: `-O0 -g`; unstripped executable 5.3 MiB.
- `./check-frozen-s1.sh` and the complete `make test` suite pass.

## 2. Reproducible baseline measurements

G3A acceptance (`tests/https-g3a/acceptance.py`, absolute binary path) passed
all checks, including append, restart, malformed log and truncated tail.

Restore/scale (`tests/https-g3a/scale.py`, pre-generated NDJSON):

| Records | Startup restore |
|---:|---:|
| 10 | 0.723 s |
| 100 | 12.815 s |
| 1,000 | 264.077 s (first run) |
| 1,000 | 208.547 s (second run) |
| 10,000 | **fails during replay** |

The 1,000-record run peaked at **20,476 KiB RSS**. The 10,000-record replay
failed before listening:

```text
[dump] IP=9871 SP=16383 RP=16444 HP=1081344 top=33144
glon: io/reply: ** halted (no SIN!: a machine-level fail-stop)
glon: io: io/reply callback failed (status 4)
```

The failure is a processor stack/return-stack exhaustion during replay, not a
malformed-input SIN!.

## 3. Confirmed bottleneck

`examples/https-g3a/persist.glon` currently:

1. reads the NDJSON log in 2,048-byte chunks through `io/read`;
2. recursively calls `scan-lines` once per line inside a chunk;
3. calls `load-loop` again for the next chunk from the callback chain.

The 10,000-line replay reproduces the stack failure. The 10/100/1,000 timing
also grows superlinearly (roughly 18–20x per 10x records), indicating that
per-line replay work (JSON decode, register insertion, GC and recursive frames)
is the dominant cost; the file chunk round trip is a secondary contributor and
has not yet been isolated.

## 4. Proposed minimal implementation (not started)

Add a **bounded host-mediated line read** to the existing file module:

- New internal protocol operation `read-line` with a per-handle carry buffer
  and a fixed maximum line size.
- Returns one line, a `has-newline`/partial flag, and EOF; no application
  parsing moves into C.
- Add the ordinary Glon wrapper `io/read-line` and extend the existing
  `io/reply` dispatcher only for this operation.
- Rewrite `persist/load` to consume one line per asynchronous host turn:
  `read-line -> apply-line -> read-line ...`. No recursive `scan-lines` and no
  accumulated NDJSON buffer.
- Preserve `persist` behavior for malformed complete lines, truncated final
  lines, warning state and append/restart behavior.
- Keep the six-line HTTPS interface and all existing public file/HTTP words
  unchanged.

This is the smallest change that removes the confirmed recursion and bounds
memory. If 10,000-record restore still grows superlinearly after this step,
the next measurement will isolate JSON decode and register insertion from GC;
that is a separate change, not part of the first prototype.

## 5. Acceptance targets

- 10,000-record restore succeeds without stack exhaustion.
- 1,000-record replay is substantially below the 208–264 s baseline.
- Malformed complete lines still abort startup; a truncated final line is
  ignored with a warning; the log is not modified.
- Replay holds at most one bounded line plus application state, not the whole
  NDJSON file.
- Frozen S1, HTTP vocabulary and the six-line HTTPS application remain
  unchanged.
- No binary payload, M1 or concurrent-handler work in this phase.
