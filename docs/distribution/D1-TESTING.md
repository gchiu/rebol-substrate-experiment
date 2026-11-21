# Glon D1 portable test package

Run the platform smoke script from the extracted directory:

- Linux/macOS/Android: `./d1-smoke.sh`
- Windows PowerShell: `.\d1-smoke.ps1`

The package contains:

- `glon` or `glon.exe`: native interpreter
- `glon-lib/`: prelude, strings, strings-ext and optional vocabularies
- `modules/`: file, HTTP, JSON, web and helper vocabularies
- `examples/`: hello, JSON, file, HTTPS and G3A examples
- `m1-smoke` where built: focused native M1 diagnostic
- `RELEASE-MANIFEST.md`, `SHA256SUMS`, dependency and licence notices
- `GLON-WINDOWS-FILE-W5.md` where applicable

Capabilities differ by platform. In particular, Android D1 has no HTTPS build,
while Windows uses the native W5 file backend and bundled OpenSSL DLLs.
