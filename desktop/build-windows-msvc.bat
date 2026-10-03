@echo off
rem desktop/build-windows-msvc.bat -- build the native Windows proof with MSVC.
rem
rem Uses vswhere to locate the latest VC tools. If vswhere is unavailable,
rem run this from a "x64 Native Tools Command Prompt for VS" instead.
setlocal enabledelayedexpansion
set ROOT=%~dp0..
pushd "%ROOT%"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
)
if defined VSDIR (
    call "!VSDIR!\VC\Auxiliary\Build\vcvars64.bat" >nul
) else (
    echo NOTE: vswhere not found; assuming a Developer Command Prompt environment.
)

if exist demo\shop\glon-live.wasm (
    copy /Y demo\shop\glon-live.wasm desktop\glon.wasm >nul
) else (
    copy /Y demo\shop\glon.wasm desktop\glon.wasm >nul
    echo NOTE: demo\shop\glon-live.wasm not found; staged base wasm ^(200-byte cap^).
)
copy /Y jupyter\prelude.glon desktop\prelude.glon >nul
copy /Y demo\shop\strings.glon desktop\strings.glon >nul
if not exist desktop\big.txt call desktop\make-big.bat

cl /nologo /std:c17 /O2 /D_CRT_SECURE_NO_WARNINGS /I. /Fe:glon-desktop.exe ^
    desktop\glon_desktop.c desktop\glon_host_windows.c ^
    r0_s1_g1a_live.c r0_s1_g1a.c r0_s1_show.c r0_s1_runtime.c s1.c ^
    ws2_32.lib shell32.lib
if errorlevel 1 (
    echo build failed
    popd
    exit /b 1
)

echo Built glon-desktop.exe
echo Run:  glon-desktop.exe desktop\app.glon
popd
endlocal
