#!/bin/sh
# Generate a local self-signed test certificate. Never distribute the key.
set -eu
openssl req -x509 -newkey rsa:2048 -nodes \
  -keyout key.pem -out cert.pem -days 2 \
  -subj "/CN=localhost" \
  -addext "subjectAltName=DNS:localhost,IP:127.0.0.1" >/dev/null 2>&1
cat cert.pem key.pem > server.pem
chmod 600 key.pem server.pem
echo "wrote server.pem (local test only)"
