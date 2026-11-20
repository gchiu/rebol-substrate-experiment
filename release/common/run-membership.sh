#!/bin/sh
set -eu
dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$dir"
[ -f server.pem ] || ./make-cert.sh
exec ./glon --http-cert server.pem \
  --allow-file-read --allow-file-write --io-root . \
  modules/web.glon modules/json.glon \
  examples/https-g3a/slots.glon examples/https-g3a/reg.glon \
  examples/https-g3a/persist.glon examples/https-g3a/server.glon
