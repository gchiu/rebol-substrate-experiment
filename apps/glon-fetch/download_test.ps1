# apps/glon-fetch/download_test.ps1 -- Windows real-download proof.
# Starts a local HTTP fixture and the Glon Fetch host, asks it to fetch a large
# file, waits for the streamed completion, and verifies the downloaded bytes.
$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Split-Path -Parent (Split-Path -Parent $here)
$exe = Join-Path $root "glon-desktop.exe"
$app = $here
$py = "C:\Python314\python.exe"
$fx = Join-Path $root "desktop\appdata\glon-fetch\fixture"
$port = 8827
$fxport = 8794
$fail = 0

if (-not (Test-Path (Join-Path $fx "big.bin"))) {
    Write-Host "  (generating fixture)"
    $bytes = New-Object byte[] (16 * 1024 * 1024)
    for ($i = 0; $i -lt $bytes.Length; $i++) { $bytes[$i] = [byte](65 + ($i % 26)) }
    [System.IO.File]::WriteAllBytes((Join-Path $fx "big.bin"), $bytes)
}

$fix = Start-Process -FilePath $py -ArgumentList @("-m","http.server","$fxport","--bind","127.0.0.1","--directory",$fx) -PassThru -WindowStyle Hidden
$h = Start-Process -FilePath $exe -WorkingDirectory $root -ArgumentList @("--install","org.glon.fetch","--no-browser","--port","$port") -PassThru -RedirectStandardOutput "$root\_win_host.out" -RedirectStandardError "$root\_win_host.err" -WindowStyle Hidden
$ready = $false
for ($i = 0; $i -lt 50; $i++) {
    Start-Sleep -Milliseconds 200
    try { Invoke-WebRequest -UseBasicParsing -Uri "http://127.0.0.1:$port/" -TimeoutSec 1 | Out-Null; $ready = $true; break } catch { }
}
if (-not $ready) { Write-Host "  FAIL: host did not start"; Stop-Process -Id $h.Id -Force -EA SilentlyContinue; Stop-Process -Id $fix.Id -Force -EA SilentlyContinue; exit 1 }

$perms = (Invoke-WebRequest -UseBasicParsing -Uri "http://127.0.0.1:$port/api/permissions").Content
if ($perms -match "effective:.*process/spawn") { Write-Host "  FAIL: effective permissions include process/spawn"; $fail = 1 }
else { Write-Host "  ok: effective permissions EXCLUDE process/spawn (brokered service)" }

$dl = Join-Path $root "desktop\appdata\org.glon.fetch\downloads\win.bin"
Remove-Item -Force $dl -ErrorAction SilentlyContinue
$body = [System.Text.Encoding]::UTF8.GetBytes("http://127.0.0.1:$fxport/big.bin`nwin.bin")
Write-Host "fetching 16 MB fixture through the app..."
$resp = Invoke-WebRequest -UseBasicParsing -Uri "http://127.0.0.1:$port/api/fetch" -Method Post -Body $body -ContentType "text/plain" -TimeoutSec 120
$content = $resp.Content

if ($content -match "(?m)^done$") { Write-Host "  ok: streamed status reached DONE exit=0" } else { Write-Host "  FAIL: no DONE exit=0"; $fail = 1 }
if ($content -match "(?m)^sha256 [0-9a-f]{64}$") { Write-Host "  ok: streamed status includes a SHA-256" } else { Write-Host "  FAIL: no SHA-256 in status"; $fail = 1 }

if (Test-Path $dl) {
    $len = (Get-Item $dl).Length
    $exp = (Get-FileHash (Join-Path $fx "big.bin") -Algorithm SHA256).Hash.ToLower()
    $act = (Get-FileHash $dl -Algorithm SHA256).Hash.ToLower()
    Write-Host "  downloaded bytes: $len"
    if ($act -eq $exp) { Write-Host "  ok: downloaded file SHA-256 matches ($act)" }
    else { Write-Host "  FAIL: SHA-256 mismatch exp=$exp act=$act"; $fail = 1 }
} else {
    Write-Host "  FAIL: downloaded file not found"; $fail = 1
}

Stop-Process -Id $h.Id -Force -ErrorAction SilentlyContinue
Stop-Process -Id $fix.Id -Force -ErrorAction SilentlyContinue
if ($fail -eq 0) { Write-Host "WINDOWS FETCH TEST PASS" } else { Write-Host "WINDOWS FETCH TEST FAIL" }
exit $fail
