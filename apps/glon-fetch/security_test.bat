@echo off
rem apps/glon-fetch/security_test.bat -- run the Windows app-model regression.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0security_test.ps1" %*
