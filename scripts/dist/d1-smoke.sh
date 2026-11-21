#!/bin/sh
# D1 portable smoke test. Run from the extracted distribution root.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$here"
./glon examples/hello-d1.glon
./glon glon-lib/strings-ext.glon modules/json.glon examples/json-smoke.glon
./glon --io-root . --allow-file-read --allow-file-write examples/file-smoke.glon
if [ -x ./m1-smoke ]; then
    ./m1-smoke
fi
echo "D1_SMOKE_PASS"
