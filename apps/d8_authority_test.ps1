# apps/d8_authority_test.ps1 -- D8 per-application identity/authority proof (Windows).
# Central invariant: SAME REQUESTED PERMISSIONS + DIFFERENT APPLICATION ID
#                   != SAME AUTHORITY. Identity comes from trusted install state.
$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Split-Path -Parent $here
$exe = Join-Path $root "glon-desktop.exe"
$py = "C:\Python314\python.exe"
$tmp = Join-Path $env:TEMP ("glond8_" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $tmp | Out-Null
$data = Join-Path $tmp "appdata"
$fail = 0
$procs = @()

Write-Host "D8 (Windows): Fetch receives its own grants"
$fx = Join-Path $tmp "fixture"; New-Item -ItemType Directory -Path $fx | Out-Null
$bytes = New-Object byte[] 131072
for ($i = 0; $i -lt $bytes.Length; $i++) { $bytes[$i] = [byte](65 + ($i % 26)) }
[System.IO.File]::WriteAllBytes((Join-Path $fx "small.bin"), $bytes)
$expSha = (Get-FileHash (Join-Path $fx "small.bin") -Algorithm SHA256).Hash.ToLower()
$fix = Start-Process -FilePath $py -ArgumentList @("-m","http.server","8797","--bind","127.0.0.1","--directory",$fx) -PassThru -WindowStyle Hidden
Start-Sleep -Seconds 2

function Start-Host([int]$port, [string[]]$more) {
    $a = @("--no-browser", "--port", "$port") + $more
    $p = Start-Process -FilePath $exe -WorkingDirectory $root -ArgumentList $a -PassThru -WindowStyle Hidden
    Start-Sleep -Seconds 2
    $script:procs += $p
    return $p
}
function Stop-All { foreach ($p in $script:procs) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }; $script:procs = @() }
function Perms([int]$port) { return (Invoke-WebRequest -UseBasicParsing "http://127.0.0.1:$port/api/permissions").Content }
function HasPerm($perm, [string]$key, [string]$cap) { return ($perm -match ("(?m)^" + [regex]::Escape($key) + ":.*" + [regex]::Escape($cap))) }
function EmptyField($perm, [string]$key) { return ($perm -match ("(?m)^" + [regex]::Escape($key) + ":[ \t]*$")) }
function Ok($m) { Write-Host "  ok: $m" }
function Bad($m) { Write-Host "  FAIL: $m"; $script:fail = 1 }
function ExpectFail([string]$desc, [string[]]$more) {
    $a = @("--no-browser", "--port", "8899") + $more
    $p = Start-Process -FilePath $exe -WorkingDirectory $root -ArgumentList $a -PassThru -Wait -WindowStyle Hidden
    if ($p.ExitCode -ne 0) { Ok "$desc (exit $($p.ExitCode))" } else { Bad "$desc (expected failure)" }
}

$p = Start-Host 8861 @("--install","org.glon.fetch")
$perm = Perms 8861
if (HasPerm $perm "app-id" "org.glon.fetch") { Ok "app-id org.glon.fetch" } else { Bad "wrong app-id" }
if ((HasPerm $perm "requested" "net/connect") -and (HasPerm $perm "requested" "file/app-write") -and (HasPerm $perm "requested" "open/folder")) { Ok "requests 3" } else { Bad "requested wrong" }
if ((HasPerm $perm "granted" "net/connect") -and (HasPerm $perm "granted" "file/app-write") -and (HasPerm $perm "granted" "open/folder")) { Ok "granted 3" } else { Bad "granted wrong" }
if ((HasPerm $perm "effective" "net/connect") -and (HasPerm $perm "effective" "file/app-write") -and (HasPerm $perm "effective" "open/folder")) { Ok "effective 3" } else { Bad "effective wrong" }
if (HasPerm $perm "effective" "process/spawn") { Bad "process/spawn effective" } else { Ok "no process/spawn" }
Stop-All

Write-Host "D8 (Windows): same requests, different id, no authority"
$p = Start-Host 8862 @("--install","org.glon.authority-test")
$perm = Perms 8862
if (HasPerm $perm "app-id" "org.glon.authority-test") { Ok "app-id org.glon.authority-test" } else { Bad "wrong app-id" }
if (EmptyField $perm "granted") { Ok "granted none" } else { Bad "granted not empty" }
if (EmptyField $perm "effective") { Ok "effective none" } else { Bad "effective not empty" }
$body = [System.Text.Encoding]::UTF8.GetBytes("http://127.0.0.1:8797/small.bin`nn.bin")
try { $r = Invoke-WebRequest -UseBasicParsing -Uri "http://127.0.0.1:8862/api/fetch" -Method Post -Body $body -ContentType "text/plain" -ErrorAction Stop; $code = [int]$r.StatusCode } catch { $code = [int]$_.Exception.Response.StatusCode.value__ }
if ($code -eq 403) { Ok "authority-test cannot fetch (403)" } else { Bad "authority-test fetch returned $code" }
Stop-All

Write-Host "D8 (Windows): name and version are not authority"
Set-Content -Path "$tmp\m_fetch_named" -Encoding ascii -Value @("id: org.glon.fetch","name: Authority Test","version: 0.0","entry: app.glon","view: view.glon","module: strings","module: fetch","permission: net/connect","permission: file/app-write","permission: open/folder")
$p = Start-Host 8863 @("--install","org.glon.fetch","--manifest","$tmp\m_fetch_named")
if (HasPerm (Perms 8863) "effective" "net/connect") { Ok "name change keeps Fetch grants" } else { Bad "name changed authority" }
Stop-All
Set-Content -Path "$tmp\m_fetch_v9" -Encoding ascii -Value @("id: org.glon.fetch","name: Glon Fetch","version: 9.9.9","entry: app.glon","view: view.glon","module: strings","module: fetch","permission: net/connect","permission: file/app-write","permission: open/folder")
$p = Start-Host 8864 @("--install","org.glon.fetch","--manifest","$tmp\m_fetch_v9")
if (HasPerm (Perms 8864) "effective" "file/app-write") { Ok "version change keeps Fetch grants" } else { Bad "version changed authority" }
Stop-All

Write-Host "D8 (Windows): identity binding and prefix"
Set-Content -Path "$tmp\m_evil" -Encoding ascii -Value @("id: org.glon.fetch.evil","name: X","version: 0.0","entry: app.glon","view: view.glon","module: strings","permission: net/connect")
ExpectFail "manifest id mismatch rejected" @("--install","org.glon.fetch","--manifest","$tmp\m_evil")
Set-Content -Path "$tmp\install_evil" -Encoding ascii -Value @("install org.glon.fetch.evil apps/glon-fetch")
Set-Content -Path "$tmp\m_prefix" -Encoding ascii -Value @("id: org.glon.fetch.evil","name: Evil","version: 0.0","entry: app.glon","view: view.glon","module: strings","module: fetch","permission: net/connect","permission: file/app-write","permission: open/folder")
$p = Start-Host 8865 @("--install","org.glon.fetch.evil","--install-conf","$tmp\install_evil","--manifest","$tmp\m_prefix")
if (HasPerm (Perms 8865) "app-id" "org.glon.fetch.evil") { Ok "app-id org.glon.fetch.evil" } else { Bad "wrong app-id" }
if (EmptyField (Perms 8865) "granted") { Ok "prefix id inherits no Fetch grant" } else { Bad "prefix inherited" }
Stop-All
ExpectFail "unknown id rejected" @("--install","org.glon.unknown")

Write-Host "D8 (Windows): manifest cannot manufacture authority"
Set-Content -Path "$tmp\m_extras" -Encoding ascii -Value @("id: org.glon.fetch","name: Glon Fetch","version: 0.0","entry: app.glon","view: view.glon","module: strings","module: fetch","permission: net/connect","permission: file/app-write","permission: open/folder","permission: process/spawn","permission: bluetooth","permission: root/all")
$p = Start-Host 8866 @("--install","org.glon.fetch","--manifest","$tmp\m_extras")
$perm = Perms 8866
if (HasPerm $perm "effective" "process/spawn") { Bad "process/spawn manufactured" } else { Ok "process/spawn not effective" }
if (HasPerm $perm "effective" "bluetooth") { Bad "bluetooth manufactured" } else { Ok "bluetooth not effective" }
Stop-All

Write-Host "D8 (Windows): identity-scoped storage"
Set-Content -Path "$tmp\install_at_fetchcode" -Encoding ascii -Value @("install org.glon.authority-test apps/glon-fetch")
Set-Content -Path "$tmp\m_at_fetchcode" -Encoding ascii -Value @("id: org.glon.authority-test","name: AT","version: 0.0","entry: app.glon","view: view.glon","module: strings","module: fetch","permission: net/connect","permission: file/app-write")
Set-Content -Path "$tmp\grants_at" -Encoding ascii -Value @("app org.glon.authority-test","  grant net/connect","  grant file/app-write")
$p = Start-Host 8867 @("--install","org.glon.authority-test","--install-conf","$tmp\install_at_fetchcode","--manifest","$tmp\m_at_fetchcode","--grants","$tmp\grants_at","--data",$data)
$body = [System.Text.Encoding]::UTF8.GetBytes("http://127.0.0.1:8797/small.bin`niso.bin")
try { Invoke-WebRequest -UseBasicParsing -Uri "http://127.0.0.1:8867/api/fetch" -Method Post -Body $body -ContentType "text/plain" -TimeoutSec 60 | Out-Null } catch { }
Stop-All
$atFile = Join-Path $data "org.glon.authority-test\downloads\iso.bin"
$fetchFile = Join-Path $data "org.glon.fetch\downloads\iso.bin"
if (Test-Path $atFile) { Ok "write landed under its own identity dir" } else { Bad "identity-scoped file missing" }
if (Test-Path $fetchFile) { Bad "wrote into Fetch's appdata" } else { Ok "did not write into Fetch's appdata" }
if ((Get-FileHash $atFile -Algorithm SHA256).Hash.ToLower() -eq $expSha) { Ok "identity-scoped SHA matches" } else { Bad "SHA mismatch" }

Write-Host "D8 (Windows): malformed ids rejected"
foreach ($badid in @("org/glon","../evil","Org.glon","org..glon","org.")) {
    Set-Content -Path "$tmp\m_bad" -Encoding ascii -Value @("id: $badid","name: Bad","version: 0.0","entry: app.glon","view: view.glon","module: strings")
    ExpectFail "malformed id '$badid' rejected" @("--install","org.glon.fetch","--manifest","$tmp\m_bad")
}

Stop-Process -Id $fix.Id -Force -ErrorAction SilentlyContinue
Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
if ($fail -eq 0) { Write-Host "D8 AUTHORITY TEST PASS" } else { Write-Host "D8 AUTHORITY TEST FAIL" }
exit $fail
