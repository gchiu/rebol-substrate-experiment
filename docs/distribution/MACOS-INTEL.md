# Glon D1 — macOS Intel x86-64

This package is for an unsigned, unnotarised Intel Mac test build.

1. Download `glon-test-macos-x86_64.tar.gz`.
2. Extract it in Finder or with:

```sh
tar xzf glon-test-macos-x86_64.tar.gz
cd glon-test-macos-x86_64
```

3. Run from the extracted directory:

```sh
./d1-smoke.sh
./glon examples/hello-d1.glon
```

The HTTPS package expects its bundled OpenSSL dylibs in `lib/` and the
executable uses `@executable_path/lib/...`; no Homebrew path is needed. Verify:

```sh
otool -L ./glon | grep -E 'libssl|libcrypto'
./glon --http-cert ./.cert/server.pem examples/1-hello.glon
```

A local certificate may be generated with the system `openssl` if a test
certificate is not present. Do not disable Gatekeeper globally. If macOS
blocks the unsigned binary, use System Settings → Privacy & Security → Open
Anyway, or build/sign it yourself. The package does not automate a Gatekeeper
bypass.

Unsigned macOS binaries may be blocked by Gatekeeper. This is an intentional
test-build limitation, not a Glon failure.
