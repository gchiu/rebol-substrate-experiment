#!/usr/bin/env bash
# B13: build both profiles and validate the available native applications.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
FIZZBUZZ_DIR="${FIZZBUZZ_DIR:-/tmp/opencode/glon-fizzbuzz/experiments/glon-fizzbuzz}"
cd "$HERE"
make all
if [ -f "$FIZZBUZZ_DIR/fizzbuzz.glon" ]; then
  for p in s1-7 s1-12; do
    "$HERE/build/glon-$p" "$FIZZBUZZ_DIR/fizzbuzz.glon" > "/tmp/b13-fizz-$p.out"
    cmp "/tmp/b13-fizz-$p.out" "$FIZZBUZZ_DIR/expected-output.txt"
  done
  cmp /tmp/b13-fizz-s1-7.out /tmp/b13-fizz-s1-12.out
  echo "fizzbuzz: PASS (100/100, profiles identical)"
else
  echo "fizzbuzz: skipped (set FIZZBUZZ_DIR to the existing worktree)"
fi

cd "$ROOT/experiments/glon-latex"
for p in s1-7 s1-12; do
  "$HERE/build/glon-$p" latex.glon test-helpers.glon latex-tests.glon > "/tmp/b13-latex-$p.out"
  "$HERE/build/glon-$p" latex.glon test-helpers.glon chris-samples.glon latex-v002-tests.glon \
    > "/tmp/b13-latexv2-$p.out"
done
cmp /tmp/b13-latex-s1-7.out /tmp/b13-latex-s1-12.out
cmp /tmp/b13-latexv2-s1-7.out /tmp/b13-latexv2-s1-12.out
echo "latex: PASS (both suites, profiles identical)"

cd "$ROOT"
./check-frozen-s1.sh
echo "b13: PASS"
