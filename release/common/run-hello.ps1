$ErrorActionPreference = "Stop"
$dir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $dir
if (-not (Test-Path server.pem)) { .\make-cert.ps1 }
.\glon.exe --http-cert server.pem examples\1-hello.glon
