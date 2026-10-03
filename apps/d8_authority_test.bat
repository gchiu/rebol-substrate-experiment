@echo off
rem apps/d8_authority_test.bat -- D8 identity/authority proof (Windows).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0d8_authority_test.ps1" %*
