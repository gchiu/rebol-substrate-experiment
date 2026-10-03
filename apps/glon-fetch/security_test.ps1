# apps/glon-fetch/security_test.ps1 -- app-model authority regression (Windows).
# Brokered model: Glon Fetch has NO process/spawn; the trusted host service
# downloads. Manifest requests never grant authority.
$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Split-Path -Parent (Split-Path -Parent $here)
$exe = Join-Path $root "glon-desktop.exe"
$app = $here
$fail = 0
$tmp = Join-Path $env:TEMP ("glonsec_" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $tmp | Out-Null

function Start-Host([int]$port, [string[]]$extra) {
    $a = @("--app", $app, "--no-browser", "--port", "$port") + $extra
    $p = Start-Process -FilePath $exe -WorkingDirectory $root -ArgumentList $a -PassThru -WindowStyle Hidden
    Start-Sleep -Seconds 2
    return $p
}
function Stop-Host($p) { if ($p) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue } }
function Check([string]$desc, [int]$want, [string]$url, [string]$body) {
    try {
        if ($body -ne $null) {
            $bytes = [System.Text.Encoding]::UTF8.GetBytes($body)
            $r = Invoke-WebRequest -UseBasicParsing -Uri $url -Method Post -Body $bytes -ContentType "text/plain" -ErrorAction Stop
        } else { $r = Invoke-WebRequest -UseBasicParsing -Uri $url -ErrorAction Stop }
        $got = [int]$r.StatusCode
    } catch { $got = [int]$_.Exception.Response.StatusCode.value__ }
    if ($got -eq $want) { Write-Host "  ok: $desc ($got)" } else { Write-Host "  FAIL: $desc (want $want got $got)"; $script:fail = 1 }
}
function Perms([int]$port) { return (Invoke-WebRequest -UseBasicParsing -Uri "http://127.0.0.1:$port/api/permissions").Content }

function Mk-Manifest([string]$file, [string[]]$perms) {
    $lines = @("name: Variant","version: 0.0","entry: app.glon","view: view.glon","module: strings","module: fetch")
    foreach ($p in $perms) { $lines += "permission: $p" }
    Set-Content -Path $file -Encoding ascii -Value $lines
}
$m_no_net    = Join-Path $tmp "m_no_net";    Mk-Manifest $m_no_net    @("file/app-write","open/folder")
$m_no_write  = Join-Path $tmp "m_no_write";  Mk-Manifest $m_no_write  @("net/connect","open/folder")
$m_no_folder = Join-Path $tmp "m_no_folder"; Mk-Manifest $m_no_folder @("net/connect","file/app-write")
$m_extra     = Join-Path $tmp "m_extra";     Mk-Manifest $m_extra     @("net/connect","file/app-write","open/folder","process/spawn","bluetooth","root/all")
$m_missing   = Join-Path $tmp "m_missing_module"
Set-Content -Path $m_missing -Encoding ascii -Value @("name: Missing","version: 0.0","entry: app.glon","view: view.glon","module: strings","module: pdf","permission: net/connect")

Write-Host "app-model: brokered default (no process/spawn)"
$p = Start-Host 8831 @()
$perms = Perms 8831
if ($perms -match "effective:.*net/connect") { Write-Host "  ok: effective includes net/connect" } else { Write-Host "  FAIL: lacks net/connect"; $fail = 1 }
if ($perms -match "effective:.*file/app-write") { Write-Host "  ok: effective includes file/app-write" } else { Write-Host "  FAIL: lacks file/app-write"; $fail = 1 }
if ($perms -match "effective:.*open/folder") { Write-Host "  ok: effective includes open/folder" } else { Write-Host "  FAIL: lacks open/folder"; $fail = 1 }
if ($perms -match "effective:.*process/spawn") { Write-Host "  FAIL: effective includes process/spawn"; $fail = 1 } else { Write-Host "  ok: effective EXCLUDES process/spawn" }
Check "browser cannot name an executable -> 400" 400 "http://127.0.0.1:8831/api/fetch" "http://127.0.0.1:1/x`nn.bin`nexe=/bin/sh"
Stop-Host $p

Write-Host "app-model: missing fetch authority"
$p = Start-Host 8832 @("--manifest",$m_no_net)
Check "fetch denied without net/connect" 403 "http://127.0.0.1:8832/api/fetch" "http://127.0.0.1:1/x`nb.bin"
Stop-Host $p
$p = Start-Host 8833 @("--manifest",$m_no_write)
Check "fetch denied without file/app-write" 403 "http://127.0.0.1:8833/api/fetch" "http://127.0.0.1:1/x`nb.bin"
Stop-Host $p

Write-Host "app-model: open/folder is separate"
$p = Start-Host 8834 @("--manifest",$m_no_folder)
Check "open denied without open/folder" 403 "http://127.0.0.1:8834/api/open" "x"
Stop-Host $p

Write-Host "app-model: manifest cannot escalate"
$p = Start-Host 8835 @("--manifest",$m_extra)
$perms = Perms 8835
if ($perms -match "effective:.*process/spawn") { Write-Host "  FAIL: process/spawn became effective"; $fail = 1 } else { Write-Host "  ok: process/spawn is not effective" }
if ($perms -match "effective:.*bluetooth") { Write-Host "  FAIL: bluetooth became effective"; $fail = 1 } else { Write-Host "  ok: bluetooth is not effective" }
if ($perms -match "effective:.*root/all") { Write-Host "  FAIL: root/all became effective"; $fail = 1 } else { Write-Host "  ok: root/all is not effective" }
Stop-Host $p

Write-Host "app-model: missing required module"
$proc = Start-Process -FilePath $exe -WorkingDirectory $root -ArgumentList @("--app",$app,"--no-browser","--port","8836","--manifest",$m_missing) -PassThru -Wait -WindowStyle Hidden
if ($proc.ExitCode -ne 0) { Write-Host "  ok: missing required module exits nonzero ($($proc.ExitCode))" } else { Write-Host "  FAIL: missing module did not fail"; $fail = 1 }

Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
if ($fail -eq 0) { Write-Host "APP SECURITY TEST PASS" } else { Write-Host "APP SECURITY TEST FAIL" }
exit $fail
