# Glon on Windows x86-64

This guide covers the W5 native Windows file-I/O and HTTPS builds. The binaries
are native Win32 x86-64 and do not require WSL, Cygwin, a Linux compatibility
layer, Python or Node.js to execute.

## Packages

- minimal — interpreter only.
- file — constrained file I/O.
- file+HTTPS — constrained file I/O and HTTPS.
- full — large-profile file I/O, HTTPS, JSON and the G3A membership examples.

The W5 full package includes `glon.exe`, `glon-lib/`, examples, the required
OpenSSL and MinGW runtime DLLs beside `glon.exe`, licence notices, and the
native file vocabulary. The published alpha.2 packages predate W5 and do not
contain the Windows file backend.

## Extract and run

1. Right-click the ZIP and choose **Extract All**.
2. Open PowerShell in the extracted directory.
3. Run the interpreter:

```powershell
.\glon.exe .\examples\1-hello.glon
```

The HTTPS packages need a local certificate. The script uses `openssl.exe` from
Git for Windows if it is not in `PATH`:

```powershell
.\make-cert.ps1
.\glon.exe --http-cert .\server.pem .\examples\1-hello.glon
```

In a second PowerShell window:

```powershell
curl.exe -k https://127.0.0.1:8443/
```

Expected:

```text
Hello World
```

## Other examples

```powershell
# HTML page (constrained HTTPS package)
.\glon.exe --http-cert .\server.pem .\examples\2-static.glon
curl.exe -k https://127.0.0.1:8443/

# HTML and JSON routes
.\glon.exe --http-cert .\server.pem .\examples\3-routes.glon
curl.exe -k https://127.0.0.1:8443/
curl.exe -k https://127.0.0.1:8443/api

# POST + JSON (large HTTPS package)
.\glon.exe --http-cert .\server.pem .\glon-lib\json.glon .\examples\4-post-json.glon
curl.exe -k -X POST -H "Content-Type: application/json" --data "{\"name\":\"Ada\"}" https://127.0.0.1:8443/
```

Stop a running server with Ctrl+C or let `--http-max-requests N` end it.

## Notes

- MSVC builds use `/MT`; MinGW builds carry the required runtime DLLs beside
  `glon.exe`. Do not rely on Git for Windows, MSYS2 or a development PATH.
- HTTPS packages must carry their matching OpenSSL DLLs. Do not rename an
  unrelated OpenSSL DLL to satisfy the loader.
- Paths containing spaces are supported when quoted. Logical file names are
  relative and UTF-8; `/` is the separator. Drive letters, UNC paths,
  backslashes, `..`, reserved device names and reparse-point targets are
  rejected by the host sandbox.
- Windows uses ACLs rather than POSIX permission bits.
- `--http-stop-event NAME` provides an orderly Windows test shutdown. Ctrl+C
  and `--http-max-requests` are also supported.
- Do not expose this experimental server directly to the public internet.
