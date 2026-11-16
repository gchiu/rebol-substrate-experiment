@echo off
cd /d "%~dp0"
set "DOOM_ROOT=%~dp0"
doom.exe %*
if errorlevel 1 pause
