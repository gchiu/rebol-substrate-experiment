@echo off
rem demos/patrol/run.bat -- serve Glon Patrol (browser/WASM only).
setlocal
echo Glon Patrol: open http://127.0.0.1:8899/patrol.html
python -m http.server 8899 --bind 127.0.0.1 --directory "%~dp0"
endlocal
