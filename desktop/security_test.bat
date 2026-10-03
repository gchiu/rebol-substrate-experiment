@echo off
rem desktop/security_test.bat -- run the Windows authority regression.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0security_test.ps1" %*
