$ErrorActionPreference = "Stop"
$openssl = $null
$cmd = Get-Command openssl.exe -ErrorAction SilentlyContinue
if ($cmd) { $openssl = $cmd.Source }
if (-not $openssl -and (Test-Path "C:\Program Files\Git\mingw64\bin\openssl.exe")) {
    $openssl = "C:\Program Files\Git\mingw64\bin\openssl.exe"
}
if (-not $openssl) {
    throw "openssl.exe not found. Install Git for Windows (which includes OpenSSL) or OpenSSL."
}
& $openssl req -x509 -newkey rsa:2048 -nodes -keyout key.pem -out cert.pem -days 2 -subj "/CN=localhost" -addext "subjectAltName=DNS:localhost,IP:127.0.0.1" | Out-Null
Get-Content cert.pem, key.pem | Set-Content -Encoding ascii server.pem
Write-Host "wrote server.pem (local test only)"
