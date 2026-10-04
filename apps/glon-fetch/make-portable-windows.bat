@echo off
rem apps/glon-fetch/make-portable-windows.bat -- assemble the portable Windows
rem bundle (dist\glon-fetch-windows\) and dist\glon-fetch-windows.zip.
rem
rem Requires a built glon-desktop.exe and a staged desktop\glon.wasm
rem (run desktop\build-windows-msvc.bat first). Developer-side only.
setlocal
set "HERE=%~dp0"
set "ROOT=%HERE%..\.."
pushd "%ROOT%"
if errorlevel 1 ( echo cannot find repo root & exit /b 1 )

if not exist "glon-desktop.exe" (
    echo glon-desktop.exe was not found. Build it first with:
    echo     desktop\build-windows-msvc.bat
    popd & exit /b 1
)
if not exist "desktop\glon.wasm" (
    echo desktop\glon.wasm was not found. Build/stage it first with:
    echo     desktop\build-windows-msvc.bat
    popd & exit /b 1
)

set "DIST=%ROOT%\dist\glon-fetch-windows"
if exist "%ROOT%\dist" rmdir /s /q "%ROOT%\dist"
mkdir "%DIST%"
mkdir "%DIST%\desktop"
mkdir "%DIST%\apps\glon-fetch"
mkdir "%DIST%\modules"
mkdir "%DIST%\glon-lib"

rem --- host binary + one-click launcher -------------------------------------
copy /Y "glon-desktop.exe" "%DIST%\glon-desktop.exe" >nul
copy /Y "apps\glon-fetch\run-glon-fetch.bat" "%DIST%\run-glon-fetch.bat" >nul

rem --- host runtime libraries -----------------------------------------------
copy /Y "jupyter\prelude.glon" "%DIST%\glon-lib\prelude.glon" >nul
copy /Y "jupyter\prelude.glon" "%DIST%\desktop\prelude.glon" >nul
copy /Y "desktop\emit.glon" "%DIST%\desktop\emit.glon" >nul

rem --- browser View runtime -------------------------------------------------
copy /Y "desktop\index.html" "%DIST%\desktop\index.html" >nul
copy /Y "desktop\browser-host.js" "%DIST%\desktop\browser-host.js" >nul
copy /Y "desktop\glon.wasm" "%DIST%\desktop\glon.wasm" >nul

rem --- trusted host config --------------------------------------------------
copy /Y "desktop\install.conf" "%DIST%\desktop\install.conf" >nul
copy /Y "desktop\grants.conf" "%DIST%\desktop\grants.conf" >nul
copy /Y "desktop\capabilities.conf" "%DIST%\desktop\capabilities.conf" >nul

rem --- modules + application ------------------------------------------------
copy /Y "modules\strings.glon" "%DIST%\modules\strings.glon" >nul
copy /Y "modules\fetch.glon" "%DIST%\modules\fetch.glon" >nul
copy /Y "apps\glon-fetch\glon-app.manifest" "%DIST%\apps\glon-fetch\glon-app.manifest" >nul
copy /Y "apps\glon-fetch\app.glon" "%DIST%\apps\glon-fetch\app.glon" >nul
copy /Y "apps\glon-fetch\view.glon" "%DIST%\apps\glon-fetch\view.glon" >nul

rem --- README ---------------------------------------------------------------
> "%DIST%\README.txt" (
    echo Glon Fetch
    echo.
    echo 1. Unzip this folder.
    echo 2. Double-click run-glon-fetch.bat.
    echo 3. Paste a direct download URL.
    echo 4. Choose a file name.
    echo 5. Click Download.
    echo 6. Click Open Folder to find the file.
    echo.
    echo Notes:
    echo  - Unsigned development build. Windows may show an unknown-publisher warning.
    echo  - Closing the console window stops Glon Fetch.
    echo  - Downloads are stored in desktop\appdata\org.glon.fetch\downloads.
    echo  - Uses only Windows-provided tools: curl.exe, certutil.exe, explorer.exe.
)

rem --- ZIP ------------------------------------------------------------------
powershell -NoProfile -Command "Compress-Archive -Path '%DIST%' -DestinationPath '%ROOT%\dist\glon-fetch-windows.zip' -Force"
if errorlevel 1 ( echo ZIP creation failed & popd & exit /b 1 )

echo.
echo Portable bundle: %DIST%
echo ZIP:             %ROOT%\dist\glon-fetch-windows.zip
popd
endlocal
