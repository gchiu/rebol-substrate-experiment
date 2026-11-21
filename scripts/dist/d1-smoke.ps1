$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot
.\glon.exe .\examples\hello-d1.glon
.\glon.exe .\glon-lib\strings-ext.glon .\modules\json.glon .\examples\json-smoke.glon
.\glon.exe --io-root . --allow-file-read --allow-file-write .\examples\file-smoke.glon
if (Test-Path .\m1-smoke.exe) { .\m1-smoke.exe }
Write-Output "D1_SMOKE_PASS"
