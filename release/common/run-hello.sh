#!/bin/sh
set -eu
dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$dir"
[ -f server.pem ] || ./make-cert.sh
exec ./glon --http-cert server.pem examples/1-hello.glon
