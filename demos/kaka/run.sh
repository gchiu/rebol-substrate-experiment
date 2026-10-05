#!/bin/sh
# demos/kaka/run.sh -- serve Attack of the Mutant Kaka (browser/WASM only).
here="$(cd "$(dirname "$0")" && pwd)"
echo "Attack of the Mutant Kaka: open http://127.0.0.1:8900/kaka.html"
exec python3 -m http.server 8900 --bind 127.0.0.1 --directory "$here"
