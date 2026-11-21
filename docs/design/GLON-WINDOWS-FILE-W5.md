# GLON W5 — Native Windows File I/O and HTTPS Persistence

**Status:** experimental implementation, verified locally with MSVC and
MinGW-w64 and on the five-platform native CI matrix. Not a public release.
**Branch:** `experiment/glon-windows-file-w5`
**Base:** `a4ee5c900c4ffc306de306369d49aba751bfed99`

## 1. Architecture

The public layers are unchanged:

```text
Glon application
  -> modules/file.glon, modules/file-stream.glon
  -> glon_io_file_cli_* (host worker)
  -> glon_io_file.c (framed request/reply service)
  -> glon_io_file_posix.c OR glon_io_file_windows.c
  -> POSIX or Win32 filesystem
```

The frozen S1 substrate, parser, evaluator, HOST ids and M1 layout are not
changed. No Windows-only word is added to Glon.

The service owns the path root, handles, generations, per-handle byte limits,
temp slots and cleanup. The backend owns canonicalisation, byte I/O and OS
handle types. `glon_io_fd` is `intptr_t`: a POSIX file descriptor on POSIX and
a Win32 `HANDLE` on Windows. The service treats it as opaque.

## 2. Public vocabulary compatibility

`modules/file.glon` and `modules/file-stream.glon` are byte-for-byte unchanged:

- `io/open name 'read|'write|'read-write|'append`
- `io/read handle max`, `io/write handle bytes`, `io/close handle`
- `io/request`, `io/take-request`, `io/reply` request/reply state machine
- `file/read`, `file/write` whole-file convenience (1,024-byte immutable cap)
- `stream/replace-first` bounded replacement (400-byte chunks)

The host protocol v2 (open/read/write/close/temp-open/commit/discard) and all
status words are unchanged. A Glon program does not detect the operating
system through these words.

## 3. Native Windows backend

`glon_io_file_windows.c` implements the same backend contract:

- Root canonicalisation with `GetFinalPathNameByHandleW`.
- UTF-8 logical names converted to UTF-16 with `MB_ERR_INVALID_CHARS`.
- `/` is the logical separator; backslashes, drive/UNC prefixes, ADS colons,
  empty components, `.`/`..`, trailing dots/spaces, control characters and
  reserved device names (`CON`, `PRN`, `AUX`, `NUL`, `COM1..9`, `LPT1..9`) are
  rejected.
- Parent directories are canonicalised and checked against the canonical root.
- Final targets are opened with `FILE_FLAG_OPEN_REPARSE_POINT`; symlinks,
  junctions and other reparse points are rejected before and after opening.
- `CreateFileW`/`ReadFile`/`WriteFile`/`CloseHandle`; append uses
  `FILE_APPEND_DATA` with `OPEN_ALWAYS`.
- Temp replacement uses `CREATE_NEW` in the source directory, `GetFileInformationByHandle`
  identity/size/write-time checks, then `MoveFileExW(REPLACE_EXISTING |
  WRITE_THROUGH)`.
- A persistent Win32 worker thread replaces per-request thread creation. It
  reduced 1 MiB streaming from minutes to seconds on this host.
- Path storage remains bounded to `GLON_IO_PATH_CAP` (4,096 UTF-8 bytes).
- Windows ACLs are the authority. POSIX mode bits are not mapped; replacing a
  file does not claim to preserve POSIX modes.

`glon_http.c` also accepts `--http-stop-event NAME` on Windows. The named event
lets a test host request orderly shutdown; the server completes the current
dispatch, calls `mg_stop`, closes files/sockets and prints its report. This is
an OS integration detail, not a Glon semantic change.

## 4. Windows/POSIX differences

| Area | POSIX | Windows W5 |
|---|---|---|
| Path separator | `/` | `/` logical; `\` rejected |
| Case | Usually case-sensitive | Case-insensitive comparisons |
| Permissions | POSIX bits via `fchmod` | Windows ACLs; POSIX bits not preserved |
| Symlink/junction | `O_NOFOLLOW`/realpath | Reparse-point rejection on parent/target |
| Reserved names | Ordinary names | Rejected as Windows device names |
| Text mode | LF | CLI stdout/stderr forced binary; files are bytes |
| Shutdown | SIGTERM/SIGINT | Named event; SIGTERM still terminates |
| Long paths | OS limit | `\\?\` prefix; 4,096-byte stored cap |

Both implementations use the same logical-name grammar, root confinement,
handle generations, byte caps, temp/commit ordering and SIN! status mapping.

## 5. Build

MSVC (file-only):

```bat
call vcvars64.bat
cl /std:c17 /MT /O2 /D_CRT_SECURE_NO_WARNINGS /DGLON_MODULE_FILE /I. ^
  /Fe:glon.exe glon.c r0_s1_session.c r0_s1_show.c r0_s1_runtime.c s1.c ^
  glon_io_file.c glon_io_file_windows.c glon_io_file_cli_windows.c
```

Combined file+HTTPS adds `glon_http.c`, the CivetWeb object and
`/link ws2_32.lib`; G3A uses the large-profile defines plus
`build/profile/s1_profile.c`.

MinGW-w64 / Makefile:

```sh
make glon MODULES=file
make glon MODULES=file,http
make glon MODULES=file,http MEM_PROFILE=large MEM_LOADER_CELLS=500000
make io-file-windows-test
```

CI Windows uses MinGW-w64 GCC 16.2 with the MSYS2 OpenSSL development package.

## 6. Verification

Local MSVC 19.51.36256 x64 and MinGW-w64 GCC 16.2.0 x64:

- `tests/io-file/acceptance.py --glon glon.exe`: **38 passed, 0 failed** on
  both compilers.
- File cases include empty/small/large, binary zero bytes, Unicode names,
  spaces, long names, append, repeated open/close, EOF, missing files,
  permissions, traversal, absolute/UNC/drive paths, reserved names, junction
  escape, invalid/stale handles, max handles, max file, cleanup after SIN!,
  cancellation and progress.
- HTTPS G1, G2, G3 and G3A acceptance: **PASS** with MSVC and MinGW, including
  the file+HTTPS build and G3A restart/malformed/truncated-tail cases.
- Clean-directory runs with only `glon.exe`, `glon-lib/`, the bundled DLLs and
  `C:\Windows\System32;C:\Windows` in PATH: PASS. No MSYS2/Git Bash/development
  PATH was required.
- Linux POSIX file tests and G1/G2/G3 acceptance remain PASS.

## 7. Measurements

Local MinGW full Windows package (clean extraction):

| Item | Bytes |
|---|---:|
| `glon.exe` | 409,798 |
| `libssl-4-x64.dll` | 1,149,113 |
| `libcrypto-4-x64.dll` | 5,592,098 |
| `libwinpthread-1.dll` | 72,171 |
| Extracted distribution | 7,513,932 |
| ZIP archive | 2,997,680 |

MSVC sizes: file-only 287,232; file+HTTPS/G3A 494,080.
MinGW file-only 226,983; file+HTTPS/G3A 409,798.

G3A Windows (synthetic 200-record workload):
- Initial working set 24,223,744 B; after 300 GETs 24,195,072 B; peak
  24,330,240 B; handles 99 -> 100.
- GET `/status` mean 90.0 ms (new TLS connection per request).
- Append mean 1,276.6 ms, max 1,843.9 ms over 50 adds; Linux comparison
  1,847.8 ms. The app path, not the byte backend, dominates.
- Restart restore 2.768 s; Linux 6.366 s.
- Streaming replacement: 64 KiB 1.963 s, 256 KiB 6.951 s, 1 MiB 28.972 s
  (~0.035 MB/s); Linux 4.102/15.715/60.872 s (~0.017 MB/s). The Glon
  replacement loop and per-chunk request framing dominate; no performance claim.

`scripts/ci/check_windows_deps.py` reads the PE import table and fails if a
non-system DLL is absent beside `glon.exe`. It correctly reported a removed
`libssl-4-x64.dll`; it passes the complete package.

## 8. Security findings and limitations

- The POSIX backend's root/realpath/`O_NOFOLLOW` model is mirrored with Win32
  canonical parent handles and reparse-point rejection. The same TOCTOU
  caveat remains: this is a proof-of-concept host policy, not a hardened
  multi-user sandbox.
- Windows ACLs replace POSIX permission bits; mode preservation is not claimed.
- Reparse points, drive/UNC paths, ADS colons and device names are rejected.
- No fsync/durability guarantee.
- Named-event shutdown is Windows-specific; POSIX still uses SIGTERM/SIGINT.
- No static OpenSSL link is used in this experiment: the package carries
  OpenSSL 4 and MinGW runtime DLLs with their licence notices.
- Long paths above the 4,096-byte UTF-8 storage cap are rejected.
- No G3B, asynchronous networking or task migration work was done.

## 9. Proposals (not implemented)

- Static OpenSSL linking or a signed DLL search policy.
- ACL-to-capability introspection and per-user root policy.
- Durable append with `FlushFileBuffers`/fsync capability.
- Batched file requests to reduce per-chunk framing overhead.
