#!/bin/sh
# F1: reproduce the remaining DEFLATE conformance failure with heap diagnostics.
set -eu
cd "$(dirname "$0")/.."
make build/dzhost >/dev/null
GLON_F1_DIAG=1 build/dzhost -L ../.. -c 128 -r 100 \
  -o /tmp/f1-multi-memlevel1.out build/fixtures/multi-memlevel1.deflate
