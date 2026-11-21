# GLON D1 — Cross-Platform Test Distributions

**Status:** distribution/test build, not a stable release. Android ARM64 is
provisionally cross-compiled unless an actual device run is recorded.

## Targets

| Target | Build | Status |
|---|---|---|
| Windows x86-64 | MSYS2 UCRT64 GCC in GitHub Actions, MSVC local | verified |
| Linux x86-64 | native GCC/Clang | verified |
| Linux ARM64 | native arm64 runner | verified |
| macOS x86-64 | macos-15-intel, bundled OpenSSL dylibs | verified in CI |
| macOS ARM64 | macos-15, bundled OpenSSL dylibs | verified in CI |
| Android ARM64 | Android NDK/Bionic, Termux | provisional; x86_64 Android executed |

## Build

Desktop packages are built by `scripts/ci/package-platform.sh` in the existing
native matrix. Android is built by `scripts/dist/build-android-d1.sh` with an
Android NDK `aarch64-linux-android24-clang`; no APK is produced.

## Package contents and naming

`glon-test-<platform>-<arch>.<ext>` contains the native
`glon`/`glon.exe`, `glon-lib/`, optional vocabularies, examples, `d1-smoke`
scripts, quick-start, manifest, dependencies, licence notices and `SHA256SUMS`.
Android also carries `m1-smoke` and a Termux-specific guide.

## Tests

Desktop: package is extracted into a clean directory, the runtime dependency
check is run for Windows, `d1-smoke.sh`/`.ps1` executes hello/JSON/file/M1, and
the existing G1/G2/G3/G3A suites are part of the platform verification.

Android: the ARM64 package was cross-compiled with NDK 30.0.16248370
(`aarch64-linux-android24-clang`) and is **provisional** until run on the phone.
An equivalent x86_64 Android build was executed on an Android 17 emulator:
hello, JSON, file I/O and the focused M1 suite all passed. That validates the
Bionic source path, not the ARM64 instruction set.

Artifact: `glon-test-android-arm64-termux.tar.gz` (355,116 bytes at local build).
The package carries `glon`, `glon-minimal`, `m1-smoke`, Termux instructions and
the same examples/docs as the desktop packages.

## Limitations

- Unsigned macOS builds may be blocked by Gatekeeper; no bypass is automated.
- Windows bundles matching OpenSSL DLLs; do not rename DLL versions.
- Android HTTPS is not included.
- No stable release, long-term support or performance claim.
