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
# Use a config file rather than -subj/-addext.  Git Bash and MSYS2 rewrite
# arguments beginning with "/" (e.g. /CN=localhost) as Windows paths, which
# makes openssl fail before it writes cert.pem.  The config file is portable and
# avoids that shell-specific rewriting entirely.
cat > "$dir/req.cnf" <<'CNF'
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
    -keyout "$dir/key.pem" -out "$dir/cert.pem" -days 2 \
    -config "$dir/req.cnf" >/dev/null 2>&1
cat "$dir/cert.pem" "$dir/key.pem" > "$cert"
chmod 600 "$dir/key.pem" "$cert"
