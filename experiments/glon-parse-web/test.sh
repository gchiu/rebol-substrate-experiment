#!/bin/sh
set -eu
cd "$(dirname "$0")"
NODE=${NODE:-node}
if command -v "$NODE" >/dev/null 2>&1 || [ -x "$NODE" ]; then
  "$NODE" test-node.mjs
else
  echo "Node 22+ is required for test-node.mjs; running native PARSE tests instead"
  make -C ../glon-parse-r2 test
fi
echo "Browser test: see README.md (local HTTP server + test-browser.mjs)."
