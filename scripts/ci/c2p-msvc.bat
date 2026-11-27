@echo off
rem scripts/ci/c2p-msvc.bat - MSVC compile and minimal executable smoke.
rem
rem The workflow runs this after ilammy/msvc-dev-cmd has provided the x64
rem toolchain.  Only the minimal core is built with MSVC; the optional POSIX /
rem pthread / OpenSSL modules remain MinGW or Linux checks.
setlocal
set ROOT=%~dp0..\..
pushd "%ROOT%"

if not exist glon-lib mkdir glon-lib
copy /Y jupyter\prelude.glon glon-lib\prelude.glon >nul
copy /Y demo\shop\strings.glon glon-lib\strings.glon >nul
copy /Y demo\shop\strings-ext.glon glon-lib\strings-ext.glon >nul

echo MSVC compile: minimal core
cl /nologo /std:c17 /O2 /D_CRT_SECURE_NO_WARNINGS /I. /Fe:glon.exe ^
    glon.c r0_s1_session.c r0_s1_show.c r0_s1_runtime.c s1.c
if errorlevel 1 (
    echo MSVC_BUILD FAILED
    popd
    exit /b 1
)

echo MSVC execute: examples\hello-d1.glon
glon.exe examples\hello-d1.glon
if errorlevel 1 (
    echo MSVC_SMOKE FAILED
    popd
    exit /b 1
)

echo MSVC_PASS
popd
endlocal
