@echo off
REM demos\kaka\run.bat -- serve Attack of the Mutant Kaka locally.
cd /d "%~dp0"
echo Attack of the Mutant Kaka: open http://127.0.0.1:8900/kaka.html
python -m http.server 8900 --bind 127.0.0.1
