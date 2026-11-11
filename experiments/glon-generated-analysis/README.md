# GLON generated-program analysis

This is an evidence-extraction and benchmarking directory. It does not modify
B1–B7, frozen S1, production Glon or any generated artifact.

## Contents

- `CODE-COMPARISON.md` — actual selected artifacts from B5/B6/B7, including
  tagged hexadecimal encodings and decoded role/operand explanations.
- `RUNTIME-COMPARISON.md` — VM code cells, instruction counts, execution and
  compile/decode medians for the selected final artifacts.
- `data/measurements.json` — machine-readable VM measurements, code hashes,
  artifact hashes, token/cost and trial paths.
- `data/trials.json` — selected trial paths, attempt diagnostics and hashes.
- `data/decoded/` — decoded B5 tagged artifacts (explanations only).
- `tools/measure.c` — B3/B4 measurement harness (count and timing builds).
- `tools/decode_b.py` — B4-B token-role decoder.
- `tools/run_measurements.py` — selected-artifact measurement collector.
- `tools/collect_trials.py` — selected-trial evidence collector.

## Machine/configuration

Text A/D/E use the unchanged B3 compiler. B uses the unchanged B4 tagged
decoder. All target convention A with CALL, RET, >R, R>, R@ and BRANCH. The
count build uses the B3 machine with `B3_COUNT`; the timing build compiles the
counters out. Execution timing excludes model generation, startup, conversion
and compilation. Compilation/decoding is measured separately.

## Reproduce

```
make
python3 tools/run_measurements.py
python3 tools/collect_trials.py
```

The selected artifacts and their seeds are documented in both reports. B5 B
artifacts are hex word lists converted with the existing B5
`tools/hex_to_bin.py` before decoding; that conversion is not part of B compile
time.
