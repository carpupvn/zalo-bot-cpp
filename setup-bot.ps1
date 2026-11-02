#Requires -Version 5.1
<#
.SYNOPSIS
    Setup bot Zalo C++ tren may moi - kiem tra va cai dat dependencies.

.PARAMETER BotRoot
    Thu muc chua bot (mac dinh: folder chua script nay).

.PARAMETER SkipNodeInstall
    Khong auto install Node.js - chi bao loi neu thieu.

.PARAMETER ForceNpmInstall
    Ep buoc chay npm install du node_modules da ton tai.

.PARAMETER DryRun
    Chi kiem tra, khong cai dat gi.

.PARAMETER Run
    Tu dong chay bot sau khi setup thanh cong.
#>

param(
    [string]$BotRoot = $PSScriptRoot,
    [switch]$SkipNodeInstall,
    [switch]$ForceNpmInstall,
    [switch]$DryRun,
    [switch]$Run
)

try {
    [Console]::OutputEncoding = [System.Text.Encoding]::UTF8
    $OutputEncoding = [System.Text.Encoding]::UTF8
    chcp 65001 2>$null | Out-Null
} catch {}

if (-not $BotRoot -or -not (Test-Path $BotRoot)) { $BotRoot = (Get-Location).Path }
Set-Location $BotRoot

$ErrorActionPreference = "Continue"
$script:Issues    = @()
$script:Warnings  = @()
$script:Installed = @()

function Write-Section($title) {
    Write-Host ""
    Write-Host "============================================================" -ForegroundColor Cyan
    Write-Host "  $title" -ForegroundColor Cyan
    Write-Host "============================================================" -ForegroundColor Cyan
}
function Write-OK($msg)    { Write-Host "  [OK]   $msg" -ForegroundColor Green }
function Write-Fail($msg)  { Write-Host "  [FAIL] $msg" -ForegroundColor Red;    $script:Issues   += $msg }
function Write-Warn($msg)  { Write-Host "  [WARN] $msg" -ForegroundColor Yellow; $script:Warnings += $msg }
function Write-Info($msg)  { Write-Host "  [INFO] $msg" -ForegroundColor Gray }
function Write-Action($msg){ Write-Host "  [DO]   $msg" -ForegroundColor Magenta }
function Test-Command($n)  { [bool](Get-Command $n -ErrorAction SilentlyContinue) }

Clear-Host
Write-Host ""
Write-Host "  Zalo Bot C++ - Setup Script" -ForegroundColor Cyan
Write-Host "  Bot root: $BotRoot" -ForegroundColor White
Write-Host "  Time: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
Write-Host ""
if ($DryRun) { Write-Host "  *** DRY RUN MODE ***" -ForegroundColor Yellow; Write-Host "" }

# ===== 1. ENV =====
Write-Section "1. Kiem tra moi truong"
$os = Get-CimInstance Win32_OperatingSystem
Write-Info "OS: $($os.Caption) $($os.Version)"
if ($os.OSArchitecture -notmatch "64") { Write-Fail "Can Windows 64-bit" } else { Write-OK "Windows 64-bit" }

if (-not (Test-Path "$BotRoot\bridge.js")) {
    Write-Fail "Khong tim thay bridge.js trong $BotRoot"
    exit 1
}
Write-OK "Thu muc bot: $BotRoot"

# ===== 2. NODE =====
Write-Section "2. Node.js"
$nodeOk = $false
$nodeMin = 18
if (Test-Command "node") {
    $nodeVerStr = (& node --version 2>$null) -replace 'v',''
    try { $nodeMajor = [int]($nodeVerStr -split '\.')[0] } catch { $nodeMajor = 0 }
    if ($nodeMajor -ge $nodeMin) {
        Write-OK "Node.js $nodeVerStr (>= v$nodeMin)"
        $nodeOk = $true
    } else {
        Write-Warn "Node.js $nodeVerStr qua cu (can >= v$nodeMin)"
    }
} else { Write-Warn "Node.js chua duoc cai" }

if (-not $nodeOk -and -not $DryRun -and -not $SkipNodeInstall) {
    Write-Action "Thu cai Node.js..."
    if (Test-Command "winget") {
        Write-Info "Dung winget cai Node.js LTS..."
        try {
            winget install OpenJS.NodeJS.LTS --silent --accept-package-agreements --accept-source-agreements 2>&1 | Out-Host
            $env:Path = [System.Environment]::GetEnvironmentVariable("Path","Machine") + ";" + [System.Environment]::GetEnvironmentVariable("Path","User")
            Start-Sleep -Seconds 2
            if (Test-Command "node") {
                $ver = & node --version
                Write-OK "Da cai Node.js $ver"
                $nodeOk = $true
                $script:Installed += "Node.js $ver"
            } else { Write-Warn "Da cai nhung 'node' chua trong PATH - mo terminal moi" }
        } catch { Write-Warn "winget cai that bai: $_" }
    } else {
        Write-Fail "Khong co winget de tu cai Node.js"
        Write-Info "Tai: https://nodejs.org/en/download/ (LTS Windows x64 MSI)"
    }
} elseif (-not $nodeOk -and $DryRun) { Write-Info "[DRY] Se cai Node.js qua winget" }
elseif (-not $nodeOk -and $SkipNodeInstall) { Write-Fail "Node.js thieu va -SkipNodeInstall da bat" }

if ($nodeOk) { $npmVer = & npm --version 2>$null; Write-OK "npm $npmVer" }

# ===== 3. NPM PACKAGES =====
Write-Section "3. NPM packages"
if (-not $nodeOk) {
    Write-Fail "Bo qua vi Node.js chua san sang"
} else {
    $reqModules = @("zca-js", "qrcode", "@steipete/sweet-cookie")
    $missing = @()
    foreach ($m in $reqModules) {
        if (Test-Path (Join-Path "$BotRoot\node_modules" $m)) { Write-OK $m }
        else { Write-Warn "$m (thieu)"; $missing += $m }
    }
    $needInstall = $missing.Count -gt 0 -or $ForceNpmInstall
    if ($needInstall -and -not $DryRun) {
        if (-not (Test-Path "$BotRoot\package.json")) {
            Write-Fail "Khong co package.json"
        } else {
            Write-Action "Chay npm install (2-5 phut)..."
            Push-Location $BotRoot
            try {
                & npm install 2>&1 | ForEach-Object {
                    $l = $_.ToString()
                    if ($l -match "added|updated") { Write-Info $l }
                    if ($l -match "npm ERR") { Write-Warn $l }
                }
                if ($LASTEXITCODE -eq 0) {
                    Write-OK "npm install thanh cong"
                    $script:Installed += "npm packages"
                } else { Write-Fail "npm install fail (exit $LASTEXITCODE)" }
            } finally { Pop-Location }
        }
    } elseif ($needInstall -and $DryRun) {
        Write-Info "[DRY] Se chay npm install (thieu: $($missing -join ', '))"
    }
}

# ===== 4. BINARY =====
Write-Section "4. Binary va DLLs"
$exePath = Join-Path "$BotRoot\build\zalo_bot.exe"
if (Test-Path $exePath) {
    $sz = [math]::Round((Get-Item $exePath).Length/1MB, 2)
    Write-OK "build\zalo_bot.exe ($sz MB)"
} else {
    if (Test-Path (Join-Path $BotRoot "zalo_bot.exe")) { Write-OK "zalo_bot.exe (root)" }
    else { Write-Fail "Khong tim thay zalo_bot.exe" }
}
$dlls = @("libgcc_s_seh-1.dll","libstdc++-6.dll","libwinpthread-1.dll")
foreach ($dll in $dlls) {
    $ok = $false
    foreach ($loc in @("$BotRoot\build","$BotRoot")) {
        if (Test-Path (Join-Path $loc $dll)) { $ok = $true; break }
    }
    if ($ok) { Write-OK $dll } else { Write-Warn "$dll (thieu)" }
}

# ===== 5. FILES =====
Write-Section "5. Files va assets"
$files = @(
    @{P="bridge.js";    N="bridge.js"},
    @{P="package.json"; N="package.json"},
    @{P="admins.txt";   N="admins.txt"},
    @{P="bot_info.json";N="bot_info.json"},
    @{P="run.bat";      N="run.bat"},
    @{P="assets";       N="assets/"},
    @{P="data";         N="data/"}
)
foreach ($f in $files) {
    if (Test-Path (Join-Path $BotRoot $f.P)) { Write-OK $f.N }
    else { Write-Warn "$($f.N) - THIEU" }
}

$configPath = Join-Path $BotRoot "config.json"
if (Test-Path $configPath) {
    try {
        $cfg = Get-Content $configPath -Raw -Encoding UTF8 | ConvertFrom-Json
        $cnt = 0
        if ($cfg.cookie -is [array]) { $cnt = $cfg.cookie.Count }
        elseif ($cfg.cookie -and $cfg.cookie.cookies) { $cnt = $cfg.cookie.cookies.Count }
        if ($cnt -gt 0 -and $cfg.imei) { Write-OK "config.json ($cnt cookies, imei=YES)" }
        else { Write-Warn "config.json thieu cookie/imei -> se dung QR login" }
    } catch { Write-Warn "config.json loi parse: $_" }
} else { Write-Warn "config.json THIEU -> QR login lan dau" }

foreach ($sub in @("fonts","emoji","game","repo")) {
    if (Test-Path (Join-Path "$BotRoot\assets" $sub)) { Write-OK "assets\$sub\" }
    else { Write-Warn "assets\$sub\ (thieu)" }
}

foreach ($df in @("bot.db","accounts.db","nong_trai.json","husks.json")) {
    $p = Join-Path "$BotRoot\data" $df
    if (Test-Path $p) {
        $s = [math]::Round((Get-Item $p).Length/1KB, 1)
        Write-OK "data\$df ($s KB)"
    } else { Write-Info "data\$df (chua co - bot se tao)" }
}

# ===== 6. PROCESS =====
Write-Section "6. Process conflict"
$procs = Get-Process zalo_bot,node -ErrorAction SilentlyContinue
if ($procs) {
    Write-Warn "Process dang chay: $(($procs | ForEach-Object { $_.Name + '#' + $_.Id }) -join ', ')"
} else { Write-OK "Khong co process cu" }

# ===== 7. SMOKE TEST =====
Write-Section "7. Smoke test"
if ($nodeOk -and -not $DryRun) {
    $testJsLines = @(
        'try {',
        '  require("zca-js");',
        '  console.log("zca-js OK");',
        '  require("qrcode");',
        '  console.log("qrcode OK");',
        '  require("@steipete/sweet-cookie");',
        '  console.log("sweet-cookie OK");',
        '  console.log("ALL_PACKAGES_OK");',
        '} catch (e) {',
        '  console.error("FAIL: " + e.message);',
        '  process.exit(1);',
        '}'
    )
    $testJs = $testJsLines -join "`n"
    $testFile = Join-Path $env:TEMP "bot_smoke_test.js"
    Set-Content -Path $testFile -Value $testJs -Encoding UTF8
    Push-Location $BotRoot
    try {
        $out = & node $testFile 2>&1
        if ($out -match "ALL_PACKAGES_OK") { Write-OK "Ca 3 packages load duoc" }
        else { Write-Fail "Smoke test fail: $($out -join ' | ')" }
    } finally {
        Pop-Location
        Remove-Item $testFile -Force -ErrorAction SilentlyContinue
    }
} else { Write-Info "Bo qua smoke test" }

# ===== 8. SUMMARY =====
Write-Section "8. TONG KET"
Write-Host ""
if ($script:Installed.Count -gt 0) {
    Write-Host "  Da cai dat moi:" -ForegroundColor Green
    foreach ($i in $script:Installed) { Write-Host "    + $i" -ForegroundColor Green }
    Write-Host ""
}
if ($script:Warnings.Count -gt 0) {
    Write-Host "  Canh bao ($($script:Warnings.Count)):" -ForegroundColor Yellow
    foreach ($w in $script:Warnings) { Write-Host "    ! $w" -ForegroundColor Yellow }
    Write-Host ""
}
if ($script:Issues.Count -gt 0) {
    Write-Host "  LOI ($($script:Issues.Count)):" -ForegroundColor Red
    foreach ($i in $script:Issues) { Write-Host "    X $i" -ForegroundColor Red }
    Write-Host ""
    Write-Host "  KET LUAN: CHUA SAN SANG" -ForegroundColor Red
    Write-Host ""
    exit 1
}

Write-Host "  KET LUAN: SAN SANG CHAY BOT!" -ForegroundColor Green
Write-Host ""
Write-Host "  Lenh chay bot:" -ForegroundColor Cyan
Write-Host "    cd $BotRoot" -ForegroundColor White
Write-Host "    .\run.bat" -ForegroundColor White
Write-Host ""

if ($Run -and -not $DryRun) {
    Write-Host "  Dang chay bot..." -ForegroundColor Magenta
    Write-Host ""
    if (Test-Path "$BotRoot\run.bat") { & "$BotRoot\run.bat" }
    else { Write-Fail "Khong co run.bat" }
}
exit 0