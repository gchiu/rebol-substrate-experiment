# GLON-DESKTOP-D6.md — native Glon desktop runtime with a browser/WASM View

**Status:** complete (D6). Proves the desktop architecture:

```
native Glon  = installed application/runtime   (control plane)
browser/WASM Glon = View/GUI                    (presentation)
```

The design rule for the whole milestone:

> **Glon decides. The host moves bytes. The browser presents them.**

## 1. Architecture

```
Browser Glon (WASM)                    Native Glon (installed)            Native host
-------------------                    -----------------------            -----------
selects a logical name   --control-->  maps name -> path        --serve-file-->  streams the
renders iframe/download  --control-->  decides the response                    file/socket
      ^                                                                              |
      |                         browser resource fetch (/resource?name=...)          |
      +----------------------------- bytes (data plane) ---------------------------+
```

- **native Glon is the application/runtime.** A `glon-desktop` process loads the
  Glon application (`desktop/app.glon`), binds a TCP listener on `127.0.0.1`,
  serves the View, and runs the application logic.
- **browser/WASM Glon is View.** The page runs the same Glon runtime compiled to
  WASM (`desktop/glon.wasm`) and renders the GUI.
- **GLON_LIVE is the small control/value channel** (`r0_s1_g1a_live.c`): small
  strings/values and commands cross between the two ends through the existing
  request/reply boundary. No redesign and no new S1 primitive.
- **Bulk resources use a separate native streaming data plane.** For large
  payloads Glon only chooses a logical resource; the host copies the file
  straight from disk to the socket. Bytes never enter the Glon managed heap or
  the event channel.
- **Logical-name -> filesystem-path authority lives in Glon.** `map-path` and
  `map-resource` (in `desktop/app.glon`) are the only places a name becomes a
  path. Unknown names resolve to no path and are rejected by the host.
- **C transports bytes and performs authorised primitive operations only**
  (TCP listen/accept/read/write/close, a filesystem read, stream a Glon-chosen
  file, process/open). It never invents a mapping and never reads a
  browser-supplied path directly.
- **JavaScript is browser transport/DOM glue only**: instantiate the WASM,
  supply the host imports, forward clicks to Glon, and (on the small control
  path) fetch the reply and hand the bytes back to `glon_event_bytes`. Bulk
  bytes are fetched by the browser engine itself from the resource URL.
- **Frozen S1 remains unchanged.** `check-frozen-s1.sh` passes; no S1, parser,
  evaluator, or `GLON_LIVE` change was made.

## 2. Channels

| channel | carrier | size | who decides | notes |
|---|---|---|---|---|
| control/value | GLON_LIVE `glon_event_bytes` / host requests | deliberately small | Glon | small strings, commands, logical choices, state |
| resource | native TCP byte stream (`/resource`) | unbounded | Glon picks the name; host streams | files, images, audio, video, downloads, large documents |

The control channel is intentionally small. Its reliable size is bounded by the
frozen S1 memory model (65536 cells, one byte per cell for `STRING!`). In
practice the existing `event_bytes` path is reliable only up to roughly 2 KB,
so the control fixture is kept deliberately small (`desktop/demo.txt`, 332 B).
Large payloads must use the resource channel.

## 3. Authority boundary

Only explicitly authorised logical names resolve to a filesystem path:

```
map-path:     "greeting" -> desktop/demo.txt   (anything else -> no path)
map-resource: "large"    -> desktop/big.txt    (anything else -> no path)
```

The C host refuses an empty path before touching the filesystem, so a
browser-supplied path-like or traversal string is never read.

## 4. Results

- Control channel (`/native/read?path=greeting`): returns `desktop/demo.txt`
  through Glon's value heap (small value proof).
- **D6 resource proof:** `/resource?name=large` streamed **2,046,016 bytes**
  (`desktop/big.txt`) from disk to the browser without entering the Glon heap.
- **POSIX:** response byte-identical to `desktop/big.txt`; native build clean.
- **Windows:** MSVC-built `glon-desktop.exe`; a real headless Chromium run
  loaded the View, selected the resource, and the iframe contained the full
  2,046,016-byte file (`iframe-bytes=2046016 marker=true`).
- **Security regression** (POSIX and Windows), all passing:

  ```
  /native/read?path=greeting          -> 200, body == desktop/demo.txt
  /native/read?path=desktop/big.txt   -> 404  (not authorised)
  /native/read?path=../../etc/passwd  -> 404  (not authorised)
  /resource?name=large                -> 200, body == desktop/big.txt
  /resource?name=nope                 -> 404  (not authorised)
  ```

## 5. Verification

```
./check-frozen-s1.sh          # frozen S1 substrate unchanged
make desktop-test             # headless application + View logic
make desktop-security-test    # HTTP-level authority regression (POSIX)
make glon-desktop             # native POSIX host
desktop\build-windows-msvc.bat ; desktop\security_test.bat   # Windows
make desktop-live             # stage GLON_LIVE wasm + fixture (needs emcc)
```

The desktop browser wasm uses the GLON_LIVE build (`make demo/shop/glon-live.wasm`,
pinned Emscripten 6.0.9), which exports `glon_event_bytes`; `browser-host.js`
uses it when present and falls back to `glon_event_value` otherwise.

## 6. Files

- `desktop/app.glon` — native application (control plane, authority).
- `desktop/view.glon` — browser Glon View.
- `desktop/glon_desktop.c` — host transport + dispatch + data-plane streamer.
- `desktop/glon_host.h`, `glon_host_posix.c`, `glon_host_windows.c` — OS boundary.
- `desktop/browser-host.js` — browser transport/DOM glue.
- `desktop/index.html`, `desktop/e2e.html`, `desktop/emit.glon`, `desktop/demo.txt`.
- `desktop/desktop_tests.c` — headless proof.
- `desktop/security_test.sh` / `.ps1` / `.bat` — authority regression.
- `desktop/make-big.sh` / `.bat` — generate the bulk fixture (not committed).
- `desktop/build-windows.bat`, `desktop/build-windows-msvc.bat` — Windows builds.
- `Makefile` — `glon-desktop`, `glon-desktop.exe`, `desktop-test`,
  `desktop-security-test`, `desktop-wasm`, `desktop-live`.

## 7. Deliberately not committed

Generated/build artifacts: `glon-desktop`, `glon-desktop.exe`, `desktop-test`,
`desktop/glon.wasm`, `desktop/prelude.glon`, `desktop/strings.glon`,
`desktop/big.txt`, `demo/shop/glon-live.wasm`, `demo/shop/glon.wasm`, `*.obj`.
`desktop/big.txt` is produced by `desktop/make-big.sh` / `.bat`.
