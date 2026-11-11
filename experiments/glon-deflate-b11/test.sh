#!/usr/bin/env bash
# B11 full local verification: build, 480 conformance, 44 units, frozen S1.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"
make all
make unit
cd ../glon-deflate
python3 -I tests/run_tests.py --host ../glon-deflate-b11/build/dzhost-b11
cd ../..
./check-frozen-s1.sh
echo 'b11: PASS'
