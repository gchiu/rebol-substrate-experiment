# desktop/security_test.ps1 -- HTTP-level authority regression for the Windows
# desktop proof. Only logical names native Glon authorises resolve to a file.
param([int]$Port = 8899)
$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Split-Path -Parent $here
$exe = Join-Path $root "glon-desktop.exe"

$proc = Start-Process -FilePath $exe -WorkingDirectory $root `
    -ArgumentList "--no-browser", "--port", "$Port" -PassThru -WindowStyle Hidden
Start-Sleep -Seconds 2

$fail = 0
function Check([string]$desc, [int]$want, [string]$url) {
    try { $r = Invoke-WebRequest -UseBasicParsing -Uri $url -ErrorAction Stop; $got = [int]$r.StatusCode }
    catch { $got = [int]$_.Exception.Response.StatusCode.value__ }
    if ($got -eq $want) { Write-Host "  ok: $desc ($got)" }
    else { Write-Host "  FAIL: $desc (want $want, got $got)"; $script:fail = 1 }
}
function BodyIs([string]$desc, [string]$url, [string]$file) {
    $bytes = (Invoke-WebRequest -UseBasicParsing -Uri $url).Content
    $want = [System.IO.File]::ReadAllText($file)
    if ($bytes -eq $want) { Write-Host "  ok: $desc" }
    else { Write-Host "  FAIL: $desc"; $script:fail = 1 }
}

Write-Host "desktop authority regression (port $Port)"
Check "/native/read?path=greeting -> 200" 200 "http://127.0.0.1:$Port/native/read?path=greeting"
BodyIs "greeting returns exactly desktop/demo.txt" "http://127.0.0.1:$Port/native/read?path=greeting" (Join-Path $here "demo.txt")
Check "/native/read?path=desktop/big.txt -> 404" 404 "http://127.0.0.1:$Port/native/read?path=desktop/big.txt"
Check "/native/read?path=../../etc/passwd -> 404" 404 "http://127.0.0.1:$Port/native/read?path=../../etc/passwd"
Check "/resource?name=large -> 200" 200 "http://127.0.0.1:$Port/resource?name=large"
BodyIs "large returns exactly desktop/big.txt" "http://127.0.0.1:$Port/resource?name=large" (Join-Path $here "big.txt")
Check "/resource?name=nope -> 404" 404 "http://127.0.0.1:$Port/resource?name=nope"

Stop-Process -Id $proc.Id -Force
if ($fail -eq 0) { Write-Host "SECURITY TEST PASS" } else { Write-Host "SECURITY TEST FAIL" }
exit $fail
