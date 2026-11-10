$ErrorActionPreference = "Continue"
$BotRoot = $PSScriptRoot
if (-not $BotRoot) { $BotRoot = (Get-Location).Path }
Set-Location $BotRoot

Write-Host ""
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "  Khoi tao data + config templates" -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan

$dirs = @("data", "data\cookies", "cache", "tools", "assets", "assets\emoji", "assets\game\baucua", "assets\game\keobuabao", "assets\game\taixiu")
foreach ($d in $dirs) {
    if (-not (Test-Path $d)) {
        New-Item -ItemType Directory -Path $d -Force | Out-Null
        Write-Host "  [Tao] $d"
    }
}

if ((Test-Path "config.json") -and -not (Test-Path "data\cookies\1.json")) {
    Copy-Item "config.json" "data\cookies\1.json"
    Write-Host "  [Migrate] config.json -> data/cookies/1.json" -ForegroundColor Green
}

if (-not (Test-Path "data\current_profile.txt")) {
    Set-Content -Path "data\current_profile.txt" -Value "1" -NoNewline -Encoding ASCII
    Write-Host "  [Tao] data/current_profile.txt" -ForegroundColor Green
}

if (-not (Test-Path "bot_info.json")) {
    $json = '{"name":"Zalo Bot","version":"1.0.4","description":"Bot Zalo C++","owner":"Unknown"}'
    [System.IO.File]::WriteAllText("$BotRoot\bot_info.json", $json, (New-Object System.Text.UTF8Encoding $false))
    Write-Host "  [Tao] bot_info.json (nho sua ten)" -ForegroundColor Green
}

if (-not (Test-Path "data\husks.json")) {
    $json = '{"chan_le":"1000000","tai_xiu":"1000000"}'
    [System.IO.File]::WriteAllText("$BotRoot\data\husks.json", $json, (New-Object System.Text.UTF8Encoding $false))
    Write-Host "  [Tao] data/husks.json" -ForegroundColor Green
}

if (-not (Test-Path "admins.txt")) {
    Set-Content -Path "admins.txt" -Value "" -Encoding UTF8
    Write-Host "  [Tao] admins.txt" -ForegroundColor Green
}

if (-not (Test-Path "data\scold.txt")) {
    Set-Content -Path "data\scold.txt" -Value "" -Encoding UTF8
    Write-Host "  [Tao] data/scold.txt" -ForegroundColor Green
}

if (-not (Test-Path "data\dictionary.txt")) {
    Set-Content -Path "data\dictionary.txt" -Value "" -Encoding UTF8
    Write-Host "  [Tao] data/dictionary.txt" -ForegroundColor Green
}

$missing = @()
foreach ($f in @("login-helper.cjs", "run-helper.bat", "tools\guard.ps1")) {
    if (-not (Test-Path $f)) { $missing += $f }
}
if ($missing.Count -gt 0) {
    Write-Host ""
    Write-Host "  [WARN] Thieu helper files:" -ForegroundColor Yellow
    foreach ($m in $missing) { Write-Host "    - $m" -ForegroundColor Yellow }
    Write-Host "  Copy tu may cu hoac tao thu cong." -ForegroundColor Yellow
}

if (-not (Test-Path "node_modules\gtts")) {
    Write-Host ""
    Write-Host "  [Cai] gtts (voice)" -ForegroundColor Cyan
    & cmd /c "npm install gtts --no-save 2>&1" | Out-Null
}

Write-Host ""
Write-Host "=== Kiem tra ===" -ForegroundColor Cyan
$checks = @(
    @{ Path = "build\zalo_bot.exe"; Desc = "Bot exe" },
    @{ Path = "bridge.js";          Desc = "Node bridge" },
    @{ Path = "node_modules\zca-js";Desc = "zca-js" },
    @{ Path = "node_modules\gtts";  Desc = "gtts" },
    @{ Path = "assets\BeVietnamPro-Bold.ttf"; Desc = "Font chinh" },
    @{ Path = "assets\TwemojiMozilla.ttf";    Desc = "Font emoji" },
    @{ Path = "bot_info.json";      Desc = "Bot info" },
    @{ Path = "data\current_profile.txt"; Desc = "Profile" }
)
$ok = 0
foreach ($c in $checks) {
    if (Test-Path $c.Path) {
        Write-Host "  [OK]   $($c.Desc)" -ForegroundColor Green
        $ok++
    } else {
        Write-Host "  [MISS] $($c.Desc) - $($c.Path)" -ForegroundColor Red
    }
}
Write-Host ""
Write-Host "  $ok/$($checks.Count) OK"
Write-Host ""
Write-Host "  Chay bot: run.bat"
Write-Host ""

if ($ok -eq $checks.Count) { exit 0 } else { exit 1 }