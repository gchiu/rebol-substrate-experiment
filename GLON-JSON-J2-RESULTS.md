# GLON-JSON-J2 — Bounded-stack JSON decoding (checkpoint: core done, verification partial)

Date: 2026-11-26. Branch `experiment/glon-json-j2` from `daa444a`. Task stacks unchanged (3 × 3,400).
This commit was made when the session's usage limit was reached; §5 lists what is still unverified.

## 1. Original behaviour (measured, main stack, RV_RPMIN probe `tests/json-j2/depth.glon`)

| input | depth (cells) |
|---|---|
| 1 / 4 / 16 / 64 escapes | 620 / 956 / 2,300 / 7,676; 1,024 escapes exhausts an 8,192-cell stack |
| array 1 / 8 / 32 items | 887 / 1,671 / 4,359 (≈112 per item) |
| object 1 / 8 / 32 pairs | 871 / 1,767 / 4,839 (≈128 per pair) |
| 4 nested arrays | 1,607 (≈240 per level, structural) |

Recursion sites: `j/parse-string-loop` (once per escape, unbounded);
`j/array-rest`, `j/object-rest`, `j/pair-has-key?` (once per element, bounded by
`json/max-items` but too deep for a 3,000-cell task: a 65-byte `[1,1,…]` body
needs 4,359 cells); `j/parse-value` (per nesting level, structural, bounded by
`json/max-depth`).

## 2. Implementation (existing Glon only; parse-escape/parse-unicode unchanged)

- **Strings:** the old loop body became `j/ps-once`; `j/ps-run` runs it up to
  2^k times at `do` depth k, and `j/ps-drive` runs levels 0,1,2,… until the
  string ends, so n escapes nest ≈2·log2(n) `do` levels (logarithmic, never per
  escape). Output goes through a ≤512-byte chunk, avoiding a full re-copy per escape.
- **Arrays/objects/duplicate check:** J1's per-activation `do`-walk (~30 cells per
  element). Elements are collected in reverse and rebuilt in order; the object
  rebuild checks duplicates last-to-first, the old unwinding order, so the same
  pair raises `'duplicate-key` and parse errors still take precedence.

## 3. Measured after

| input | before | after |
|---|---|---|
| 1 / 16 / 512 / 2,048 escapes | 620 / 2,300 / exhausted / exhausted | 636 / 732 / 860 / 908 |
| 1,024 escapes (depth probe) | exhausted | 892 |
| 10,000 plain bytes | 505 | 519 |
| array 1 / 32 items | 887 / 4,359 | 871 / 1,799 |
| object 1 / 32 pairs | 871 / 4,839 | 871 / 2,103 |
| G3H POST body `{"text":"hello"}` | 729 | 775 |

## 4. Tests run (final source)

- `make json-j2-test`: **JSON_J2_TESTS PASS (94 checks)** (~3 min). Covers results
  and errors against the frozen original decoder (`tests/json-j2/reference.glon`),
  every escape, surrogate pairs, malformed `\u`/escapes/UTF-8 with exact positions,
  512 consecutive escapes, 1,000/4,000/10,240-character strings with escapes,
  stack-growth assertions, containers, duplicate keys, limits, and round trips.
- `make json-test`: PASS. `make json-j1-test`: PASS (73 checks).
- G3H task mode, expanded profile, 3 × 3,400: **9/9 routes correct**, server alive.

## 5. Not yet done

- Not run on this commit: `make h3e-test`, `make g3h-test`, H3C acceptance,
  default constrained build, json-j2 suite on the expanded build, and a check that
  the J2 suite fails against the original decoder.
- Task-mode POST bodies with many escapes (within the ~500-byte bridge limit)
  not yet exercised over HTTP.
- Fail-stop safety (§6 of the brief) not yet assessed. Expected risk:
  nesting still costs ~240 cells per level, so a body nested near
  `json/max-depth` (16) likely exceeds a task stack and fail-stops the
  server. Needs measuring.

## 6. Known limitations

- Throughput: ~4 ms per escape in this interpreter (the original was the same,
  ~2.2 s vs 1.9 s for 512 escapes); 10,000 escapes take ~100 s. Objects cost ~44 ms
  per pair at 32 pairs (O(n²) duplicate check, as before).
- Pre-existing asymmetry: the decoder accepts 16 nesting levels, but `json/encode`
  advances depth by 2 per level and rejects more than 8.
- Task-mode request bodies over ~500 bytes still stop the server (HTTP bridge, out of scope).
