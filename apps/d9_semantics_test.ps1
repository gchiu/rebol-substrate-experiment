# apps/d9_semantics_test.ps1 -- D9 semantic permission contracts (Windows).
# The app may explain WHY it wants authority; it may not define WHAT it means.
$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Split-Path -Parent $here
$exe = Join-Path $root "glon-desktop.exe"
$tmp = Join-Path $env:TEMP ("glond9_" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $tmp | Out-Null
$fail = 0
$procs = @()

function Start-Host([int]$port, [string[]]$more) {
    $a = @("--no-browser", "--port", "$port") + $more
    $p = Start-Process -FilePath $exe -WorkingDirectory $root -ArgumentList $a -PassThru -RedirectStandardError "$tmp\h$port.err" -WindowStyle Hidden
    Start-Sleep -Seconds 2
    $script:procs += $p
    return $p
}
function Stop-All { foreach ($p in $script:procs) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }; $script:procs = @() }
function Perms([int]$port) { return (Invoke-WebRequest -UseBasicParsing "http://127.0.0.1:$port/api/permissions").Content }
function Ok($m) { Write-Host "  ok: $m" }
function Bad($m) { Write-Host "  FAIL: $m"; $script:fail = 1 }
function Has($p, $s) { if ($p -match [regex]::Escape($s)) { Ok $s } else { Bad $s } }

Write-Host "D9 (Windows): trusted catalogue + Fetch purpose"
$p = Start-Host 8871 @("--install","org.glon.fetch")
$perm = Perms 8871
Has $perm "label: Connect to network services"
Has $perm "class: general"
Has $perm "risk: medium"
Has $perm "allows: outbound-network"
Has $perm "purpose: Download URLs selected by the user"
Has $perm "class: scoped"
Has $perm "class: brokered-service"
Has $perm "effective: yes"
Stop-All

Write-Host "D9 (Windows): app cannot redefine canonical semantics"
Set-Content -Path "$tmp\m_redef" -Encoding ascii -Value @(
"id: org.glon.authority-test","name: AT","version: 0.0","entry: app.glon","view: view.glon",
"module: strings","module: fetch","permission: process/spawn","permission: teleport/moon",
"purpose: process/spawn Display the current time","class: brokered-service","risk: low",
"label: Harmless clock feature","allows: harmless-clock")
$p = Start-Host 8872 @("--install","org.glon.authority-test","--manifest","$tmp\m_redef")
$perm = Perms 8872
Has $perm "label: Run arbitrary child programs"
Has $perm "class: privileged"
Has $perm "risk: high"
Has $perm "allows: arbitrary-child-process"
Has $perm "purpose: Display the current time"
Has $perm "label: (unknown capability)"
Has $perm "known: no"
$err = Get-Content "$tmp\h8872.err" -Raw
if ($err -match "unknown capability: teleport/moon") { Ok "startup diagnostic names unknown capability" } else { Bad "no diagnostic" }
try { $r = Invoke-WebRequest -UseBasicParsing -Uri "http://127.0.0.1:8872/api/fetch" -Method Post -Body ([System.Text.Encoding]::UTF8.GetBytes("http://127.0.0.1:1/x`nz.bin")) -ContentType "text/plain" -ErrorAction Stop; $code = [int]$r.StatusCode } catch { $code = [int]$_.Exception.Response.StatusCode.value__ }
if ($code -eq 403) { Ok "misleading purpose does not grant fetch (403)" } else { Bad "fetch returned $code" }
Stop-All

Write-Host "D9 (Windows): unknown capability fails closed even if granted"
Set-Content -Path "$tmp\grants_unknown" -Encoding ascii -Value @("app org.glon.authority-test","  grant teleport/moon")
Set-Content -Path "$tmp\m_unknown" -Encoding ascii -Value @("id: org.glon.authority-test","name: AT","version: 0.0","entry: app.glon","view: view.glon","module: strings","module: fetch","permission: teleport/moon")
$p = Start-Host 8873 @("--install","org.glon.authority-test","--manifest","$tmp\m_unknown","--grants","$tmp\grants_unknown")
$perm = Perms 8873
Has $perm "id: teleport/moon"
Has $perm "known: no"
if ($perm -match "(?m)^effective:[ \t]*$") { Ok "unknown capability is not effective despite a grant" } else { Bad "unknown became effective" }
Stop-All

Write-Host "D9 (Windows): catalogue-only capability is not implemented"
Set-Content -Path "$tmp\grants_bt" -Encoding ascii -Value @("app org.glon.authority-test","  grant bluetooth/scan")
Set-Content -Path "$tmp\m_bt" -Encoding ascii -Value @("id: org.glon.authority-test","name: AT","version: 0.0","entry: app.glon","view: view.glon","module: strings","permission: bluetooth/scan")
$p = Start-Host 8877 @("--install","org.glon.authority-test","--manifest","$tmp\m_bt","--grants","$tmp\grants_bt")
$perm = Perms 8877
Has $perm "label: Scan for Bluetooth devices"
Has $perm "class: general"
Has $perm "known: yes"
Has $perm "implemented: no"
Has $perm "effective: no"
if ($perm -match "(?m)^effective:[ \t]*$") { Ok "effective set is empty" } else { Bad "effective not empty" }
$err = Get-Content "$tmp\h8877.err" -Raw
if ($err -match "catalogue capability not implemented by host: bluetooth/scan") { Ok "diagnostic names the unimplemented capability" } else { Bad "no unimplemented diagnostic" }
Stop-All

Write-Host "D9 (Windows): host implementation without catalogue metadata fails closed"
$lines = Get-Content (Join-Path $root "desktop\capabilities.conf")
$out = New-Object System.Collections.ArrayList
$skip = $false
foreach ($l in $lines) {
    if ($l -match '^capability ') { $skip = ($l -match '^capability net/connect\s*$') }
    if (-not $skip) { [void]$out.Add($l) }
}
Set-Content -Path "$tmp\cat_no_net" -Value $out -Encoding ascii
$p = Start-Host 8878 @("--install","org.glon.fetch","--capabilities","$tmp\cat_no_net")
$perm = Perms 8878
if ($perm -match "(?m)^effective:.*net/connect") { Bad "net/connect effective without catalogue metadata" } else { Ok "net/connect ineffective without catalogue metadata" }
if ($perm -match "(?m)^effective:.*file/app-write") { Ok "other permissions remain effective" } else { Bad "file/app-write lost" }
$err = Get-Content "$tmp\h8878.err" -Raw
if ($err -match "host implementation without trusted catalogue metadata: net/connect") { Ok "diagnostic names the metadata-less implementation" } else { Bad "no metadata-less diagnostic" }
Stop-All

Write-Host "D9 (Windows): Fetch effective permissions unchanged"
$p = Start-Host 8874 @("--install","org.glon.fetch")
$perm = Perms 8874
if ($perm -match "(?m)^effective: net/connect file/app-write open/folder$") { Ok "effective = the three intended permissions" } else { Bad "effective mismatch" }
Stop-All

Write-Host "D9 (Windows): per-app isolation remains intact"
$p = Start-Host 8875 @("--install","org.glon.authority-test")
$perm = Perms 8875
if ($perm -match "(?m)^granted:[ \t]*$") { Ok "authority-test granted none" } else { Bad "authority-test has grants" }
Stop-All

Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
if ($fail -eq 0) { Write-Host "D9 SEMANTICS TEST PASS" } else { Write-Host "D9 SEMANTICS TEST FAIL" }
exit $fail
