#!/bin/sh
# demos/patrol/run.sh -- serve Glon Patrol (browser/WASM only, no native host).
here="$(cd "$(dirname "$0")" && pwd)"
echo "Glon Patrol: open http://127.0.0.1:8899/patrol.html"
exec python3 -m http.server 8899 --bind 127.0.0.1 --directory "$here"
