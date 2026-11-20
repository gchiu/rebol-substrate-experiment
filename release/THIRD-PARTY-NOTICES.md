# Third-party notices — Glon Developer Preview 1

## CivetWeb 1.16

Vendored under `third_party/civetweb/`. MIT licence. The full licence text is
in `third_party/civetweb/LICENSE.md`; copyright and contributor credits are in
`third_party/civetweb/CREDITS.md`.

## OpenSSL 3

Linux builds link dynamically to the system OpenSSL 3 (`libssl.so.3`,
`libcrypto.so.3`); Linux packages do not redistribute OpenSSL. Windows HTTPS
packages include `libssl-3-x64.dll` and `libcrypto-3-x64.dll` from the OpenSSL
3 build shipped with Git for Windows. OpenSSL is licensed under the Apache
License 2.0. The Apache 2.0 text is included as
`release/OPENSSL-APACHE-2.0.txt`.

## Microsoft C runtime

Windows executables are built with `/MT` and statically link the MSVC C
runtime, so the Visual C++ redistributable is not required. Windows system
libraries (`KERNEL32.dll`, `WS2_32.dll` and the OpenSSL DLL dependencies) are
part of Windows.

## Glon source

No separate root `LICENSE` file was present in the repository at the release
commit. The Glon source is published by the repository copyright holder. This
notice does not grant rights beyond those the copyright holder grants.

## Data and TLS material

The release contains synthetic example data only. No private keys, production
certificates, membership records, API tokens or private engineering memory are
included. Test certificates must be generated locally.
