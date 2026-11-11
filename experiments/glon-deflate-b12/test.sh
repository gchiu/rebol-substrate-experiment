#!/usr/bin/env bash
# B12: build, test nine and twelve profiles, and check frozen S1.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"
make all
make unit
cd ../glon-deflate
python3 -I tests/run_tests.py --host ../glon-deflate-b12/build/dzhost-nine
python3 -I tests/run_tests.py --host ../glon-deflate-b12/build/dzhost-twelve
cd ../..
./check-frozen-s1.sh
echo 'b12: PASS'
