# Glon Developer Preview 1

`v0.1.0-alpha.2` — an experimental execution substrate for AI-generated
applications.

## What is here

- `glon` / `glon.exe` — native interpreter
- `glon-lib/` — loaded Glon vocabularies
- `examples/` — verified examples
- `modules/` — HTTP/web/JSON/file source vocabularies
- `AGENTS.md`, `AI-QUICKSTART.md`, `VOCABULARY.json` — AI-agent documentation
- `WINDOWS-QUICKSTART.md` — native Windows instructions
- `RELEASE-MANIFEST.md`, `THIRD-PARTY-NOTICES.md`, `LICENSING.md`

## Linux quick start

```sh
./make-cert.sh
./run-hello.sh
# or:
./glon --http-cert server.pem examples/1-hello.glon
curl -k https://127.0.0.1:8443/
```

Packages: minimal, constrained HTTPS, and full (HTTPS + file + large profile +
membership). The full package is the only one with `run-membership.sh`.

## Windows quick start

```powershell
.\make-cert.ps1
.\glon.exe --http-cert .\server.pem .\examples\1-hello.glon
curl.exe -k https://127.0.0.1:8443/
```

See `WINDOWS-QUICKSTART.md`. The Windows file-enabled membership package is not
included because the current file module has no Win32 backend.

## Status

Alpha, experimental, pre-release. Do not expose the server directly to the
public internet. See the manifest for verified capabilities and limitations.
