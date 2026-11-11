#!/bin/sh
# F2: reproduce the formerly failing multi-memlevel1 schedule and verify the
# 9,000-byte result against Python's zlib.
set -eu
cd "$(dirname "$0")/.."
make build/dzhost >/dev/null
fixture=build/fixtures/multi-memlevel1.deflate
out=/tmp/f2-multi-memlevel1.out
[ -f "$fixture" ] || python3 -I tests/run_tests.py --only multi-memlevel1 >/dev/null
build/dzhost -L ../.. -c 128 -r 100 -s -o "$out" "$fixture"
python3 - "$fixture" "$out" <<'PY'
import sys, zlib
want = zlib.decompress(open(sys.argv[1], "rb").read(), -15)
got = open(sys.argv[2], "rb").read()
if got != want:
    raise SystemExit("F2 FAIL: %d bytes, expected %d" % (len(got), len(want)))
print("F2 PASS: %d bytes match zlib" % len(got))
PY
