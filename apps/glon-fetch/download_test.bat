@echo off
rem apps/glon-fetch/download_test.bat -- run the Windows download + browser E2E.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0download_test.ps1" %*
