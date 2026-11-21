# Glon D1 — Android ARM64 (Termux)

**Status: provisionally cross-compiled.** This package was built with the
Android NDK for Bionic/ARM64. It is not called verified until it has executed on
an Android device. No APK, Android Studio project or graphical interface is
required.

## Copy-paste on the phone

Install Termux from F-Droid (the Play Store build is commonly outdated), then:

```sh
pkg update
pkg install tar
cd ~
mkdir -p glon-d1
cd glon-d1
tar xzf ~/storage/downloads/glon-test-android-arm64-termux.tar.gz
cd glon-test-android-arm64-termux
chmod +x glon glon-minimal m1-smoke d1-smoke.sh
./glon examples/hello-d1.glon
./glon glon-lib/strings-ext.glon modules/json.glon examples/json-smoke.glon
./glon --io-root . --allow-file-read --allow-file-write examples/file-smoke.glon
./d1-smoke.sh
./m1-smoke
```

Report the device platform and Android version:

```sh
uname -m
getprop ro.product.cpu.abi
getprop ro.build.version.release
```

If a command fails, capture diagnostics:

```sh
./glon examples/hello-d1.glon >glon.out 2>glon.err
printf 'rc=%s\n' "$?"
cat glon.err
```

## Notes

- The executable is a native Android ARM64 Bionic binary, not a Linux glibc
  binary. Do not run it on a Linux distribution.
- The package is unsigned; Termux can execute files in its own private storage.
- File tests use the current directory as the sandbox root. Logical names are
  relative; `..`, absolute paths and backslashes are rejected.
- HTTPS is not included in the Android package because Android OpenSSL/Bionic
  TLS packaging was not verified in D1.
- `m1-smoke` is the focused M1 multitasking test body. It is packaged instead
  of the full repository `s1` suite because the full suite requires source-tree
  fixtures that are not part of a portable distribution.
- No long-term stability or production-support claim is made.
