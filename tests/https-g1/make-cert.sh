#!/bin/sh
# Generate a local self-signed TLS certificate for the G1 acceptance tests.
# The private key and certificate are produced fresh (or reused while valid)
# under tests/https-g1/.cert/, which is git-ignored.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
dir="$here/.cert"
cert="$dir/server.pem"
mkdir -p "$dir"
if [ -s "$cert" ] && openssl x509 -in "$cert" -noout -checkend 3600 >/dev/null 2>&1; then
    exit 0
fi
openssl req -x509 -newkey rsa:2048 -nodes \
    -keyout "$dir/key.pem" -out "$dir/cert.pem" -days 2 \
    -subj "/CN=localhost" \
    -addext "subjectAltName=DNS:localhost,IP:127.0.0.1" >/dev/null 2>&1
cat "$dir/cert.pem" "$dir/key.pem" > "$cert"
chmod 600 "$dir/key.pem" "$cert"
