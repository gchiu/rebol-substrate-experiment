# CivetWeb 1.16 (vendored, minimal)

Source: https://github.com/civetweb/civetweb/releases/tag/v1.16
Release date: 2023-04-10

Only the files needed to build the optional Glon HTTPS module are vendored:

- `civetweb.c`, `include/civetweb.h` (stored here as `civetweb.h`)
- the `.inl` fragments included by `civetweb.c`
- `LICENSE.md`, `CREDITS.md`

CivetWeb is MIT licensed (see `LICENSE.md`). The Glon HTTPS module builds it
with `-DNO_CGI -DNO_CACHING -DNO_SSL_DL` and links against the system OpenSSL
3 (`libssl`, `libcrypto`). No Lua, Duktape, zlib, WebSocket or mbedTLS support
is enabled.

To refresh this directory, download the same release tarball and copy the
files listed above without editing them.
