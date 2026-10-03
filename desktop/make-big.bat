@echo off
rem desktop/make-big.bat -- generate the bulk-streaming test fixture (~2 MB).
setlocal
powershell -NoProfile -Command "$s = 0..32999 | ForEach-Object { 'line {0:000000}: the native host streams these bytes without Glon' -f $_ }; $s += 'END-OF-BIG-FILE'; Set-Content -Path '%~dp0big.txt' -Value $s -Encoding ascii"
for %%A in ("%~dp0big.txt") do echo %%~zA bytes
endlocal
