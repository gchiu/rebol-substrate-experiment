#!/bin/sh
# Generate the loopback-only TLS certificate used by the Glon process.
# Caddy keeps its own separately managed certificate; this file is unrelated.
set -eu
dir=${1:-/etc/glon-worlds}
mkdir -p "$dir"
tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT
cat > "$tmp" <<'CNF'
[req]
distinguished_name = dn
x509_extensions = v3
prompt = no
[dn]
CN = localhost
[v3]
subjectAltName = DNS:localhost,IP:127.0.0.1
CNF
openssl req -x509 -newkey rsa:2048 -nodes \
    -keyout "$dir/key.pem" -out "$dir/cert.pem" -days 3650 \
    -config "$tmp" >/dev/null 2>&1
cat "$dir/cert.pem" "$dir/key.pem" > "$dir/localhost.pem"
chmod 600 "$dir/key.pem" "$dir/localhost.pem"
if [ "$(id -u)" -eq 0 ]; then
    chown root:glon-worlds "$dir/cert.pem" "$dir/key.pem" "$dir/localhost.pem" 2>/dev/null || true
fi
echo "wrote $dir/localhost.pem" >&2
