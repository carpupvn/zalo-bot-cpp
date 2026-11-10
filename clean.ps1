param(
    [string]$Mode = 'help',
    [switch]$Force
)

$ErrorActionPreference = 'Continue'
$BotRoot = $PSScriptRoot
if (-not $BotRoot) { $BotRoot = (Get-Location).Path }
Set-Location $BotRoot

function Kill-Bot {
    Write-Host "  Kill zalo_bot.exe, node.exe..." -ForegroundColor Yellow
    cmd /c "taskkill /F /IM zalo_bot.exe >nul 2>&1"
    cmd /c "taskkill /F /IM node.exe >nul 2>&1"
    Start-Sleep -Seconds 1
}

function Show-Files {
    param([string[]]$Patterns, [string]$Title)
    Write-Host "`n  $Title" -ForegroundColor Cyan
    $total = 0
    $totalSize = 0
    foreach ($p in $Patterns) {
        $items = Get-ChildItem $p -File -ErrorAction SilentlyContinue
        if ($items) {
            $size = ($items | Measure-Object Length -Sum).Sum
            $total += $items.Count
            $totalSize += $size
            $mb = [Math]::Round($size / 1KB, 1)
            Write-Host ("    {0,-40} {1,3} file {2,8} KB" -f $p, $items.Count, $mb) -ForegroundColor Gray
        }
    }
    if ($total -eq 0) {
        Write-Host "    (khong co file)" -ForegroundColor DarkGray
    } else {
        $mb = [Math]::Round($totalSize / 1KB, 1)
        Write-Host "    TONG: $total file, $mb KB" -ForegroundColor Yellow
    }
}

function Confirm-Delete {
    param([string]$Action)
    if ($Force) { return $true }
    Write-Host ""
    Write-Host "  Ban sap: $Action" -ForegroundColor Yellow
    $r = Read-Host "  Tiep tuc? (y/N)"
    return ($r -match '^[yY]')
}

function Delete-Files {
    param([string[]]$Patterns, [string]$Desc)
    $count = 0
    $size = 0
    foreach ($p in $Patterns) {
        Get-ChildItem $p -File -ErrorAction SilentlyContinue | ForEach-Object {
            $count++
            $size += $_.Length
            Remove-Item $_.FullName -Force -ErrorAction SilentlyContinue
        }
    }
    $mb = [Math]::Round($size / 1KB, 1)
    Write-Host "  [Xoa] $Desc : $count file, $mb KB" -ForegroundColor Green
}

function Delete-Dir {
    param([string]$Path, [string]$Desc)
    if (Test-Path $Path) {
        $size = (Get-ChildItem $Path -Recurse -File -ErrorAction SilentlyContinue | Measure-Object Length -Sum).Sum
        $mb = [Math]::Round($size / 1MB, 1)
        Remove-Item $Path -Recurse -Force -ErrorAction SilentlyContinue
        Write-Host "  [Xoa] $Desc : $mb MB" -ForegroundColor Green
    } else {
        Write-Host "  [SKIP] $Desc (khong ton tai)" -ForegroundColor DarkGray
    }
}

# ============================================================
# MODE: HELP
# ============================================================
if ($Mode -eq 'help') {
    Write-Host ""
    Write-Host "============================================================" -ForegroundColor Cyan
    Write-Host "  Zalo Bot C++ - Clean Tool" -ForegroundColor Cyan
    Write-Host "============================================================" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "Cac mode:" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "  cache    - Xoa cache/*.png + logs (an toan, khong mat data)" -ForegroundColor White
    Write-Host "  cookies  - Xoa cookie Zalo + accounts.db + reset profile" -ForegroundColor White
    Write-Host "             -> Dung khi PUBLISH bot cho nguoi khac" -ForegroundColor DarkGray
    Write-Host "  personal - Xoa data ca nhan (nong trai, husks, admin list)" -ForegroundColor White
    Write-Host "  build    - Xoa build/ (rebuild tu dau)" -ForegroundColor White
    Write-Host "  full     - Xoa het (nhu moi clone, can setup-bot.bat lai)" -ForegroundColor White
    Write-Host ""
    Write-Host "Chi tiet file moi mode:" -ForegroundColor Yellow
    Write-Host ""

    Show-Files -Patterns @("cache\*.png","cache\*.log","cache\*.started","cache\spawn_debug.log","cache\*.url") -Title "[cache] - Anh render + logs"
    Show-Files -Patterns @("data\cookies\*.json","data\cookies\*.meta.json","data\cookies\*.log","data\cookies\*.started") -Title "[cookies] - Cookie Zalo"
    Show-Files -Patterns @("data\accounts.db","data\accounts.db.bak","data\accounts.txt","data\bot.db") -Title "[cookies] - Database tai khoan"
    Show-Files -Patterns @("data\nong_trai.json","data\husks.json","data\auto_state.json","admins.txt","data\scold.txt") -Title "[personal] - Data ca nhan"

    if (Test-Path "build") {
        $size = (Get-ChildItem "build" -Recurse -File -ErrorAction SilentlyContinue | Measure-Object Length -Sum).Sum
        $mb = [Math]::Round($size / 1MB, 1)
        Write-Host "`n  [build] - Build dir" -ForegroundColor Cyan
        Write-Host "    build\ : $mb MB" -ForegroundColor Gray
    }

    Write-Host ""
    Write-Host "  [full] = cache + cookies + personal + build + node_modules + profiles" -ForegroundColor Magenta
    Write-Host ""
    return
}

# ============================================================
# KILL BOT TRUOC KHI XOA
# ============================================================
Kill-Bot

# ============================================================
# MODE: CACHE
# ============================================================
if ($Mode -eq 'cache') {
    Show-Files -Patterns @("cache\*.png","cache\*.log","cache\*.started","cache\spawn_debug.log","cache\*.url","test_helper_out.txt","test_helper_err.txt") -Title "Se xoa: Cache + logs"
    if (-not (Confirm-Delete "xoa cache/*.png + logs")) { Write-Host "  Huy." -ForegroundColor Yellow; return }
    Delete-Files -Patterns @("cache\*.png","cache\*.log","cache\*.started","cache\spawn_debug.log","cache\*.url") -Desc "cache/"
    Delete-Files -Patterns @("test_helper_out.txt","test_helper_err.txt") -Desc "test files"
    Write-Host "`n  [OK] Xong." -ForegroundColor Green
    return
}

# ============================================================
# MODE: COOKIES (PUBLISH)
# ============================================================
if ($Mode -eq 'cookies') {
    Show-Files -Patterns @("data\cookies\*.json","data\cookies\*.meta.json","data\cookies\*.log","data\cookies\*.started") -Title "Se xoa: Cookies Zalo"
    Show-Files -Patterns @("data\accounts.db","data\accounts.db.bak","data\accounts.txt","data\bot.db") -Title "Se xoa: Database tai khoan"
    if (-not (Confirm-Delete "xoa TOAN BO cookie + database tai khoan (PUBLISH mode)")) {
        Write-Host "  Huy." -ForegroundColor Yellow; return
    }
    Delete-Files -Patterns @("data\cookies\*.json","data\cookies\*.meta.json","data\cookies\*.log","data\cookies\*.started") -Desc "cookies"
    Delete-Files -Patterns @("data\accounts.db","data\accounts.db.bak","data\accounts.txt","data\bot.db") -Desc "database"
    Delete-Dir -Path "data\profiles" -Desc "data/profiles (legacy)"
    Delete-Dir -Path "cache\profiles" -Desc "cache/profiles (legacy)"

    if (Test-Path "data\current_profile.txt") {
        Set-Content -Path "data\current_profile.txt" -Value "1" -NoNewline -Encoding ASCII
        Write-Host "  [Reset] data/current_profile.txt = 1" -ForegroundColor Green
    }

    Write-Host "`n  [OK] Da xoa cookies + accounts. Lan chay toi se hien QR." -ForegroundColor Green
    return
}

# ============================================================
# MODE: PERSONAL
# ============================================================
if ($Mode -eq 'personal') {
    Show-Files -Patterns @("data\nong_trai.json","data\husks.json","data\auto_state.json","admins.txt","data\scold.txt") -Title "Se xoa: Data ca nhan"
    if (-not (Confirm-Delete "xoa data ca nhan (nong trai, husks, admin list...)")) {
        Write-Host "  Huy." -ForegroundColor Yellow; return
    }
    Delete-Files -Patterns @("data\nong_trai.json","data\auto_state.json") -Desc "nong trai + auto state"
    Delete-Files -Patterns @("data\husks.json") -Desc "husks (se dung default 1M)"
    Delete-Files -Patterns @("admins.txt") -Desc "admin list (HOST se tu add khi login)"
    Delete-Files -Patterns @("data\scold.txt") -Desc "scold list"
    Write-Host "`n  [OK] Xong." -ForegroundColor Green
    return
}

# ============================================================
# MODE: BUILD
# ============================================================
if ($Mode -eq 'build') {
    if (-not (Confirm-Delete "xoa build/ (se build lai tu dau)")) {
        Write-Host "  Huy." -ForegroundColor Yellow; return
    }
    Delete-Dir -Path "build" -Desc "build/"
    Write-Host "`n  [OK] Xong. Chay cmake -S . -B build va cmake --build build" -ForegroundColor Green
    return
}

# ============================================================
# MODE: FULL (publish-ready)
# ============================================================
if ($Mode -eq 'full') {
    Show-Files -Patterns @("cache\*.png","cache\*.log","data\cookies\*.json","data\cookies\*.meta.json","data\accounts.db","data\accounts.txt","data\bot.db","data\nong_trai.json","data\husks.json","data\auto_state.json","admins.txt","data\scold.txt") -Title "Se xoa: TOAN BO data"
    if (Test-Path "build") {
        $size = (Get-ChildItem "build" -Recurse -File -ErrorAction SilentlyContinue | Measure-Object Length -Sum).Sum
        $mb = [Math]::Round($size / 1MB, 1)
        Write-Host "`n  Se xoa: build\ ($mb MB)" -ForegroundColor Cyan
    }
    if (Test-Path "node_modules") {
        $size = (Get-ChildItem "node_modules" -Recurse -File -ErrorAction SilentlyContinue | Measure-Object Length -Sum).Sum
        $mb = [Math]::Round($size / 1MB, 1)
        Write-Host "  Se xoa: node_modules\ ($mb MB)" -ForegroundColor Cyan
    }
    Write-Host "`n  GIU LAI: source code, assets, bridge.js, package.json, scripts, .bat, .ps1" -ForegroundColor Magenta

    if (-not (Confirm-Delete "FULL CLEAN - xoa het data + build + node_modules")) {
        Write-Host "  Huy." -ForegroundColor Yellow; return
    }

    Delete-Files -Patterns @("cache\*.png","cache\*.log","cache\*.started","cache\spawn_debug.log","cache\*.url") -Desc "cache/"
    Delete-Dir -Path "cache\profiles" -Desc "cache/profiles (legacy)"
    Delete-Files -Patterns @("data\cookies\*.json","data\cookies\*.meta.json","data\cookies\*.log","data\cookies\*.started") -Desc "cookies"
    Delete-Files -Patterns @("data\accounts.db","data\accounts.db.bak","data\accounts.txt","data\bot.db") -Desc "database"
    Delete-Files -Patterns @("data\nong_trai.json","data\auto_state.json") -Desc "nong trai + auto"
    Delete-Files -Patterns @("data\husks.json") -Desc "husks"
    Delete-Files -Patterns @("admins.txt","data\scold.txt") -Desc "admin + scold"
    Delete-Files -Patterns @("test_helper_out.txt","test_helper_err.txt") -Desc "test files"
    Delete-Dir -Path "data\profiles" -Desc "data/profiles (legacy)"
    Delete-Dir -Path "build" -Desc "build/"
    Delete-Dir -Path "node_modules" -Desc "node_modules/"

    if (Test-Path "data\current_profile.txt") {
        Set-Content -Path "data\current_profile.txt" -Value "1" -NoNewline -Encoding ASCII
    }
    if (Test-Path "bot_info.json") {
        $json = '{"name":"Zalo Bot","version":"1.0.4","description":"Bot Zalo C++","owner":"Unknown"}'
        [System.IO.File]::WriteAllText("$BotRoot\bot_info.json", $json, (New-Object System.Text.UTF8Encoding $false))
        Write-Host "  [Reset] bot_info.json ve default" -ForegroundColor Green
    }

    Write-Host "`n  [OK] FULL CLEAN xong. Chay setup-bot.bat de cai lai." -ForegroundColor Green
    return
}

Write-Host "Mode khong hop le: $Mode" -ForegroundColor Red