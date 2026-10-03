@echo off
rem desktop/build-windows.bat -- build the native Windows desktop proof.
rem
rem Requires MinGW-w64 gcc in PATH (MSYS2, winlibs, w64devkit, ...).
rem Run from anywhere; it operates on the repository root.
setlocal
set ROOT=%~dp0..
pushd "%ROOT%"

if exist demo\shop\glon-live.wasm (
    copy /Y demo\shop\glon-live.wasm desktop\glon.wasm >nul
) else (
    copy /Y demo\shop\glon.wasm desktop\glon.wasm >nul
    echo NOTE: demo\shop\glon-live.wasm not found; staged base wasm ^(200-byte cap^).
)
copy /Y jupyter\prelude.glon desktop\prelude.glon >nul
copy /Y demo\shop\strings.glon desktop\strings.glon >nul
if not exist desktop\big.txt call desktop\make-big.bat

gcc -std=c17 -O2 -I. -o glon-desktop.exe ^
    desktop\glon_desktop.c desktop\glon_host_windows.c desktop\glon_app.c ^
    r0_s1_g1a_live.c r0_s1_g1a.c r0_s1_show.c r0_s1_runtime.c s1.c ^
    -lws2_32 -lshell32
if errorlevel 1 (
    echo build failed
    popd
    exit /b 1
)

echo Built glon-desktop.exe
echo Run:  glon-desktop.exe desktop\app.glon
popd
endlocal
