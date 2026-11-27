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
chmod 600 "$dir/cert.pem" "$dir/key.pem"
chmod 640 "$dir/localhost.pem"
if [ "$(id -u)" -eq 0 ]; then
    # The service user must read the combined cert+key file. The separate
    # key/cert files stay root-only.
    chown root:root "$dir/cert.pem" "$dir/key.pem"
    chown root:glon-worlds "$dir/localhost.pem"
fi
echo "wrote $dir/localhost.pem" >&2
