@echo off
rem apps/glon-fetch/browser_test.bat -- real headless-Chromium D10 workflow.
rem Drives the real View through download -> cancel -> second download.
setlocal
set HERE=%~dp0
set ROOT=%~dp0..\..
set PY=C:\Python314\python.exe
set CHROME=C:\Program Files\Google\Chrome\Application\chrome.exe
set FX=%TEMP%\glon-d10-fx
set DATA=%TEMP%\glon-d10-data
set DOM=%TEMP%\glon-d10-dom.html
if not exist "%FX%" mkdir "%FX%"
if not exist "%DATA%" mkdir "%DATA%"
if exist "%DOM%" del "%DOM%" >nul 2>&1
rmdir /s /q "%TEMP%\glon-d10-chrome" >nul 2>&1
powershell -NoProfile -Command "$b=New-Object byte[] 8192; for($i=0;$i -lt $b.Length;$i++){$b[$i]=[byte](65+($i%%26))}; [IO.File]::WriteAllBytes('%FX%\ok.bin',$b)" >nul

start "" /b "%PY%" -m http.server 8791 --bind 127.0.0.1 --directory "%FX%"
start "" /b "%PY%" "%HERE%slow_server.py" 8798
start "" /b "%ROOT%\glon-desktop.exe" --install org.glon.fetch --no-browser --port 8856 --data "%DATA%"
ping -n 4 127.0.0.1 >nul

"%CHROME%" --headless=new --disable-gpu --no-sandbox --user-data-dir="%TEMP%\glon-d10-chrome" --virtual-time-budget=180000 --dump-dom "http://127.0.0.1:8856/e2e.html?slow=http://127.0.0.1:8798/slow&fast=http://127.0.0.1:8791/ok.bin&mode=full" > "%DOM%" 2> "%TEMP%\glon-d10-chrome.err"
echo CHROME_EXIT=%errorlevel%

taskkill /IM glon-desktop.exe /F >nul 2>&1
taskkill /IM python.exe /F >nul 2>&1
endlocal
