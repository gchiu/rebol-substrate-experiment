# apps/glon-fetch/usable_test.ps1 -- D10 usable Glon Fetch proof (Windows).
# Success, real cancellation, recovery, unsafe destinations, authority, and a
# real headless-Chromium run through the browser View.
$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Split-Path -Parent (Split-Path -Parent $here)
$exe = Join-Path $root "glon-desktop.exe"
$py = "C:\Python314\python.exe"
$chrome = "C:\Program Files\Google\Chrome\Application\chrome.exe"
$tmp = Join-Path $env:TEMP ("glond10_" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $tmp | Out-Null
$fx = Join-Path $tmp "fx"; New-Item -ItemType Directory -Path $fx | Out-Null
$data = Join-Path $tmp "appdata"
$fail = 0

$bytes = New-Object byte[] 524288
for ($i = 0; $i -lt $bytes.Length; $i++) { $bytes[$i] = [byte](65 + ($i % 26)) }
[System.IO.File]::WriteAllBytes((Join-Path $fx "ok.bin"), $bytes)
$expSha = (Get-FileHash (Join-Path $fx "ok.bin") -Algorithm SHA256).Hash.ToLower()

$fxp = Start-Process -FilePath $py -ArgumentList @("-m","http.server","8791","--bind","127.0.0.1","--directory",$fx) -PassThru -WindowStyle Hidden
$slow = Start-Process -FilePath $py -ArgumentList @((Join-Path $here "slow_server.py"),"8798") -PassThru -WindowStyle Hidden
Start-Sleep -Seconds 2

function Start-Host([int]$port, [string[]]$more, [string]$errfile) {
    $a = @("--no-browser", "--port", "$port") + $more
    $p = Start-Process -FilePath $exe -WorkingDirectory $root -ArgumentList $a -PassThru -RedirectStandardError $errfile -WindowStyle Hidden
    $ready = $false
    for ($i = 0; $i -lt 50; $i++) {
        Start-Sleep -Milliseconds 200
        try { Invoke-WebRequest -UseBasicParsing -Uri "http://127.0.0.1:$port/" -TimeoutSec 1 | Out-Null; $ready = $true; break } catch { }
    }
    if (-not $ready) { Write-Host "  (host $port did not become ready)"; Get-Content $errfile -ErrorAction SilentlyContinue | ForEach-Object { Write-Host "  err: $_" } }
    return $p
}
function Ok($m) { Write-Host "  ok: $m" }
function Bad($m) { Write-Host "  FAIL: $m"; $script:fail = 1 }
function PostFetch([int]$port, [string]$url, [string]$name) {
    $b = [System.Text.Encoding]::UTF8.GetBytes("$url`n$name")
    return (Invoke-WebRequest -UseBasicParsing -Uri "http://127.0.0.1:$port/api/fetch" -Method Post -Body $b -ContentType "text/plain" -TimeoutSec 120).Content
}

Write-Host "D10 (Windows): successful download"
$h = Start-Host 8853 @("--install","org.glon.fetch","--data",$data) (Join-Path $tmp "h.err")
$content = PostFetch 8853 "http://127.0.0.1:8791/ok.bin" "ok.bin"
if ($content -match "(?m)^done$") { Ok "download reported done" } else { Bad "no done line" }
if ($content -match "(?m)^sha256 [0-9a-f]{64}$") { Ok "SHA-256 returned" } else { Bad "no SHA-256 line" }
$dl = Join-Path $data "org.glon.fetch\downloads\ok.bin"
if ((Get-FileHash $dl -Algorithm SHA256).Hash.ToLower() -eq $expSha) { Ok "downloaded bytes match" } else { Bad "bytes mismatch" }

Write-Host "D10 (Windows): unsafe destinations"
try { Invoke-WebRequest -UseBasicParsing -Uri "http://127.0.0.1:8853/api/fetch" -Method Post -Body ([Text.Encoding]::UTF8.GetBytes("http://127.0.0.1:8791/ok.bin`n../../etc/passwd")) -ContentType "text/plain" -ErrorAction Stop | Out-Null; $code = 200 } catch { $code = [int]$_.Exception.Response.StatusCode.value__ }
if ($code -eq 400) { Ok "traversal destination rejected (400)" } else { Bad "traversal returned $code" }

Write-Host "D10 (Windows): real cancellation"
$cancel = Join-Path $data "org.glon.fetch\downloads\cancel.bin"
Remove-Item -Force $cancel -ErrorAction SilentlyContinue
& curl.exe -s --max-time 1 --data-binary "http://127.0.0.1:8798/slow`ncancel.bin" "http://127.0.0.1:8853/api/fetch" | Out-Null
Start-Sleep -Seconds 3
if (-not (Test-Path $cancel)) { Ok "partial file removed after cancel" } else { Bad "partial file remains" }
if ((Get-Content (Join-Path $tmp "h.err") -Raw) -match "download cancelled") { Ok "host terminated the download" } else { Bad "no cancel in host log" }

Write-Host "D10 (Windows): recovery after cancel"
$content2 = PostFetch 8853 "http://127.0.0.1:8791/ok.bin" "after.bin"
if ($content2 -match "(?m)^done$") { Ok "second download completed" } else { Bad "second download failed" }
if ((Get-FileHash (Join-Path $data "org.glon.fetch\downloads\after.bin") -Algorithm SHA256).Hash.ToLower() -eq $expSha) { Ok "second download bytes match" } else { Bad "second bytes mismatch" }

Write-Host "D10 (Windows): authority"
$perms = (Invoke-WebRequest -UseBasicParsing "http://127.0.0.1:8853/api/permissions").Content
if ($perms -match "effective:.*process/spawn") { Bad "process/spawn effective" } else { Ok "process/spawn remains absent" }
Stop-Process -Id $h.Id -Force -ErrorAction SilentlyContinue

Write-Host "D10 (Windows): open/folder required"
Set-Content -Path "$tmp\m_no_folder" -Encoding ascii -Value @("id: org.glon.fetch","name: Glon Fetch","version: 0.0","entry: app.glon","view: view.glon","module: strings","module: fetch","permission: net/connect","permission: file/app-write")
$h2 = Start-Host 8854 @("--install","org.glon.fetch","--manifest","$tmp\m_no_folder","--data",$data) (Join-Path $tmp "h2.err")
PostFetch 8854 "http://127.0.0.1:8791/ok.bin" "nf.bin" | Out-Null
try { Invoke-WebRequest -UseBasicParsing -Uri "http://127.0.0.1:8854/api/open" -Method Post -ErrorAction Stop | Out-Null; $code = 200 } catch { $code = [int]$_.Exception.Response.StatusCode.value__ }
if ($code -eq 403) { Ok "Open Folder denied without open/folder (403)" } else { Bad "open returned $code" }
Stop-Process -Id $h2.Id -Force -ErrorAction SilentlyContinue

Write-Host "D10 (Windows): real browser workflow (Chromium)"
# The browser proof runs in its own batch (its own fixtures/host) because a
# headless dump needs the workflow to hold the virtual clock.
Stop-Process -Id $fxp.Id -Force -ErrorAction SilentlyContinue
Stop-Process -Id $slow.Id -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1
& cmd.exe /c "`"$here\browser_test.bat`"" | Out-Null
$domPath = Join-Path $env:TEMP "glon-d10-dom.html"
if (Test-Path $domPath) {
    $dom = Get-Content $domPath -Raw
    if ($dom -match "WORKFLOW_OK") { Ok "browser View completed download, cancel, and second download" }
    elseif ($dom -match "WORKFLOW_RUNNING") { Bad "browser workflow did not finish within the dump window" }
    else { Bad "browser workflow did not run" }
} else {
    Bad "browser workflow produced no DOM"
}
Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
if ($fail -eq 0) { Write-Host "D10 USABLE TEST PASS" } else { Write-Host "D10 USABLE TEST FAIL" }
exit $fail
