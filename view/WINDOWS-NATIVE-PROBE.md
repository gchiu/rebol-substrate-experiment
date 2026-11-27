# Windows Native-Control Probe

**Status:** implemented and compile-checked on Linux; **not executed**.
Windows was not available in the GV-1 environment. No native-control
interoperability claim is made until this procedure has been run on Windows.

## Purpose

The M1 spike must decide whether the Windows native-control backend should be:

- **A:** SDL2 window + Win32 child controls;
- **B:** Win32 top-level window + Cairo for the View surface.

The probe verifies the primitive mechanism: create native EDIT and Rich Edit
4.1 controls as children of an SDL2/Win32 parent, set and get text, focus,
resize and destroy them.

## Build

### MinGW / MSYS2

```sh
gcc -std=c17 -Iinclude tests/native_probe.c src/gv_native_win32.c \
    -o build/native_probe.exe -luser32 -lgdi32
```

### MSVC developer prompt

```bat
cl /std:c17 /Iinclude tests\native_probe.c src\gv_native_win32.c ^
   /Fe:build\native_probe.exe user32.lib gdi32.lib
```

## Run

```bat
build\native_probe.exe
```

Expected output:

```text
PASS: Win32 native text controls (EDIT + Rich Edit 4.1)
```

The probe creates a parent window, a single-line EDIT, a multiline Rich Edit 4.1
control, sets and reads text, sets focus and destroys the controls.

## Manual acceptance

Repeat with a real SDL2 window and record results:

1. Create an SDL2 window; obtain its `HWND` with `SDL_GetWindowWMInfo`.
2. Create a child EDIT and a child Rich Edit 4.1 control.
3. Verify text get/set, focus, Tab order, resize and destroy.
4. Verify `WS_CLIPCHILDREN` and z-order with a Cairo-rendered View surface.
5. Verify per-monitor DPI (`WM_DPICHANGED`) and font reconstruction.
6. Verify TSF/IME composition with a Chinese, Japanese and Korean IME.
7. Verify UI Automation with Inspect or Accessibility Insights; check
   `IUIAutomationTextPattern`.
8. Verify Windows spell-checking and a third-party checker where available.
9. Verify third-party speech recognition/dictation where available.
10. Record what worked, what failed and which backend (A or B) was selected.

Do not claim TSF, UIA or third-party compatibility from the compile-only
probe. Those are separate acceptance items.

## Current result

- Linux: `gv_native_available()` returns 0; `make -C view probe` prints
  `SKIP: native controls require Windows`.
- Windows: pending.

## Related

- `../GLON-VIEW-BACKEND-SELECTION.md`
- `../GLON-VIEW-COMPLETENESS.md` section 2.18
- `../GLON-VIEW-GV1.md`