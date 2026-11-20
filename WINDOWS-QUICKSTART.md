# Glon on Windows x86-64

This preview includes native Windows executables built with MSVC. They do not
require WSL, Cygwin, a Linux compatibility layer, Python or Node.js.

## Packages

- `glon-v0.1.0-alpha.2-windows-x86_64-minimal.zip` — interpreter only.
- `glon-v0.1.0-alpha.2-windows-x86_64-https.zip` — constrained HTTPS (examples 1–3).
- `glon-v0.1.0-alpha.2-windows-x86_64-https-large.zip` — large HTTPS + JSON
  (example 4). This package does **not** include file persistence; the current
  file module has no Win32 backend.

Each package contains `glon.exe`, the required `glon-lib/` vocabulary, a local
test-certificate script, examples, `AGENTS.md`, `AI-QUICKSTART.md`,
`VOCABULARY.json` and licence notices.

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

- The executables are statically linked against the MSVC C runtime (`/MT`).
- OpenSSL is loaded dynamically from `libssl-3-x64.dll` and
  `libcrypto-3-x64.dll` shipped in the HTTPS packages.
- Paths containing spaces are supported when quoted.
- The current `file` module is POSIX-only. Native Windows file-backed
  membership persistence is therefore not part of this preview. The Linux
  `full` package includes it.
- Do not expose this experimental server directly to the public internet.
