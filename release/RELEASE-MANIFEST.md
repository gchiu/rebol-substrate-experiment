# Glon Developer Preview 1 — release manifest

- **Release identifier:** `v0.1.0-alpha.1`
- **Release name:** Glon Developer Preview 1
- **Status:** public alpha developer preview (pre-release)
- **Source branch:** `release/v0.1.0-alpha.1`
- **Source commit:** `abe00ec`
- **Base:** G3A implementation `546de49`, docs `492f55f`
- **Frozen S1:** `f90496c26dc45c7a387d8fc8639cd2781507d3d2` (unchanged)
- **Build host:** Linux/WSL2 x86-64; MSVC 14.51 BuildTools for Windows
- **Release date:** 2026-11-20

## Included capabilities and verification

| Capability | Status | Evidence |
|---|---|---|
| Frozen S1 seven-instruction substrate | included | `./check-frozen-s1.sh` |
| Native interpreter (Linux x86-64) | verified | minimal build + examples |
| Native interpreter (Windows x86-64) | verified | `glon-win-minimal.exe` ran `s/print` |
| Constrained 64K profile | included/default | G1/G2/G3 suites |
| Large profile (1,048,576 managed cells) | included | G3A suite, M1 large test |
| M1 multitasking | included | Linux large M1 tests pass; Windows runtime sources same but M1 Windows not separately instrumented |
| File I/O vocabulary | Linux verified | `io-file-c-test`, G1/G2 `file,http` |
| HTTPS server (CivetWeb + OpenSSL) | verified Linux and Windows | examples 1–4 |
| HTTP vocabulary | included | `modules/http.glon` |
| General JSON vocabulary | included | `make json-test`; examples 4 |
| Reusable web vocabulary | included | `modules/web.glon` |
| Four HTTPS examples | verified | Linux and Windows 1–4 |
| G3A membership register | Linux only | 200-record restart acceptance |
| NDJSON persistence | Linux only | restart/malformed/truncated tests |
| Windows file-enabled membership | **not included** | file module has no Win32 backend |

## Build configurations

| Package | Configuration |
|---|---|
| Linux minimal | `make glon MODULES=` |
| Linux constrained HTTPS | `make glon MODULES=http` |
| Linux full | `make glon MODULES=file,http MEM_PROFILE=large MEM_LOADER_CELLS=500000` |
| Windows minimal | MSVC C17, `/MT`, static S1 profile |
| Windows constrained HTTPS | MSVC C17, `/MT`, `GLON_MODULE_HTTP`, CivetWeb dynamic OpenSSL |
| Windows large HTTPS+JSON | same with large-profile flags |

Windows builds use CivetWeb's dynamic OpenSSL loading. The HTTPS packages
include `libssl-3-x64.dll` and `libcrypto-3-x64.dll`. The executables use the
static MSVC C runtime (`/MT`) and depend only on Windows system DLLs plus those
two OpenSSL DLLs.

## Measurements

| Artifact | Bytes | Notes |
|---|---:|---|
| Linux minimal stripped | 104,768 | 102.3 KiB |
| Linux constrained HTTPS stripped | 264,512 | 258.3 KiB |
| Linux full HTTPS+file stripped | 297,312 | 290.3 KiB |
| Windows minimal | 245,248 | native PE |
| Windows constrained HTTPS | 457,216 | native PE |
| Windows large HTTPS+JSON | 457,216 | native PE |
| Hello World app | 125 | 6 lines |
| Static HTML app | 227 | 7 lines |
| Routes app | 609 | 23 lines |
| POST+JSON app | 1,038 | 29 lines |
| HTTP vocabulary | 6,277 | 182 lines |
| Web vocabulary | 4,847 | 172 lines |
| JSON vocabulary | 26,752 | 782 lines |
| Membership entry point | 19,031 | 466 lines |

Linux sizes exclude dynamically linked OpenSSL and supporting Glon source
libraries. Windows sizes exclude the OpenSSL DLLs and `glon-lib` source.
Large-profile BSS is approximately 13.2 MB.

## Excluded experiments

- Android/GCS graphical and 3D tracks: separate toolchains and assets.
- Desktop/raylib/OpenKTG hosts: separate native dependencies.
- Experimental browser/PARSE work: not part of the HTTPS release.
- Windows file-enabled membership: blocked by the POSIX-only file module.
- 10,000-record restore and 100,000-request endurance: attempted/not completed;
  see limitations.

## Known limitations

HTTPS handlers are serialised; keep-alive can starve connections; the membership
register passed a 200-record restart but 10,000 restore failed with return-stack
exhaustion; 1,000-record restore took 450.143 s; 100,000-request endurance was
not completed; JSON depth/item limits remain; no fsync/power-failure guarantee;
authentication, authorisation, CSRF and production hardening are absent.
