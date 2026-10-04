@echo off
rem apps/glon-fetch/run-glon-fetch.bat -- one-click launcher for Glon Fetch.
rem Double-click this file to run the application. No command line needed.
rem (Optional extra arguments are forwarded to glon-desktop.exe for testing.)
setlocal
rem Works both in the source tree (apps\glon-fetch\run-glon-fetch.bat) and in
rem the portable bundle (run-glon-fetch.bat beside glon-desktop.exe).
set "HERE=%~dp0"
if exist "%HERE%glon-desktop.exe" (
    set "ROOT=%HERE%"
) else (
    set "ROOT=%HERE%..\.."
)
pushd "%ROOT%"
if errorlevel 1 (
    echo Could not find glon-desktop.exe next to this launcher.
    pause
    exit /b 1
)

if not exist "glon-desktop.exe" (
    echo.
    echo glon-desktop.exe was not found.
    echo Build the desktop host first with one of these scripts:
    echo     desktop\build-windows-msvc.bat   ^(Visual Studio / MSVC^)
    echo     desktop\build-windows.bat       ^(MinGW-w64 gcc^)
    echo.
    pause
    popd
    exit /b 1
)

echo Launching Glon Fetch...
echo Close this window to stop Glon Fetch.
glon-desktop.exe --install org.glon.fetch %*
set RC=%ERRORLEVEL%
popd
endlocal & exit /b %RC%
