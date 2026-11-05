# ==============================================================================
# SCRIPT: Auto Setup & Build Zalo Bot C++ (Tự động cài Boost)
# ==============================================================================

param(
    [string]$BotRoot = $PSScriptRoot,
    [switch]$SkipNodeInstall,
    [switch]$DryRun
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

function Write-Section($title) {
    Write-Host "`n============================================================" -ForegroundColor Cyan
    Write-Host "  $title" -ForegroundColor Cyan
    Write-Host "============================================================" -ForegroundColor Cyan
}
function Write-OK($msg)    { Write-Host "  [OK]   $msg" -ForegroundColor Green }
function Write-Fail($msg)  { Write-Host "  [FAIL] $msg" -ForegroundColor Red;    $script:Issues   += $msg }
function Write-Warn($msg)  { Write-Host "  [WARN] $msg" -ForegroundColor Yellow; $script:Warnings += $msg }
function Write-Info($msg)  { Write-Host "  [INFO] $msg" -ForegroundColor Gray }
function Write-Action($msg){ Write-Host "  [DO]   $msg" -ForegroundColor Magenta }
function Test-Command($n)  { [bool](Get-Command $n -ErrorAction SilentlyContinue) }

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "`n[CANH BAO] Ban nen chay PowerShell bang quyen Administrator de script tu cai dat phan mem (winget)." -ForegroundColor Yellow
    Start-Sleep 3
}

Clear-Host
Write-Host "`n  Zalo Bot C++ - Auto Setup & Build" -ForegroundColor Cyan
Write-Host "  Bot root: $BotRoot`n" -ForegroundColor White

# ==============================================================================
# 1. KIỂM TRA NODE.JS & NPM
# ==============================================================================
Write-Section "1. Node.js & NPM"
$nodeOk = $false
if (Test-Command "node") {
    $nodeVer = (& node --version) -replace 'v',''
    $nodeMajor = [int]($nodeVer -split '\.')[0]
    if ($nodeMajor -ge 18) { Write-OK "Node.js $nodeVer"; $nodeOk = $true }
    else { Write-Warn "Node.js $nodeVer qua cu (can >= 18)" }
} else { Write-Warn "Chua cai Node.js" }

if (-not $nodeOk -and -not $DryRun -and -not $SkipNodeInstall) {
    Write-Action "Dang cai Node.js LTS qua winget..."
    winget install OpenJS.NodeJS.LTS --silent --accept-package-agreements --accept-source-agreements
    $env:Path = [System.Environment]::GetEnvironmentVariable("Path","Machine") + ";" + [System.Environment]::GetEnvironmentVariable("Path","User")
    Start-Sleep 2
    if (Test-Command "node") { Write-OK "Da cai Node.js"; $nodeOk = $true }
    else { Write-Fail "Cai Node.js that bai." }
}

# ==============================================================================
# 2. CÀI ĐẶT NPM PACKAGES
# ==============================================================================
Write-Section "2. NPM Packages"
if ($nodeOk) {
    if (-not (Test-Path "$BotRoot\node_modules\zca-js")) {
        Write-Action "Dang chay npm install..."
        & npm install
        if ($LASTEXITCODE -eq 0) { Write-OK "npm install thanh cong" }
        else { Write-Fail "npm install that bai" }
    } else { Write-OK "node_modules da day du" }
}

# ==============================================================================
# 3. KIỂM TRA & CÀI ĐẶT C++ BUILD TOOLS (CMake, MinGW)
# ==============================================================================
Write-Section "3. C++ Build Tools (CMake, MinGW)"
$cmakeOk = Test-Command "cmake"
$gccOk = Test-Command "gcc"

if ($cmakeOk) { Write-OK "Da co CMake" } else { Write-Warn "Thieu CMake" }
if ($gccOk) { Write-OK "Da co GCC (MinGW)" } else { Write-Warn "Thieu GCC (MinGW)" }

if ((-not $cmakeOk -or -not $gccOk) -and -not $DryRun) {
    Write-Action "Dang cai CMake va MinGW qua winget..."
    if (-not $cmakeOk) { winget install Kitware.CMake --silent --accept-package-agreements --accept-source-agreements }
    if (-not $gccOk) { winget install BrechtSanders.WinLibs.POSIX.UCRT --silent --accept-package-agreements --accept-source-agreements }
    
    $env:Path = [System.Environment]::GetEnvironmentVariable("Path","Machine") + ";" + [System.Environment]::GetEnvironmentVariable("Path","User")
    Start-Sleep 2
    $cmakeOk = Test-Command "cmake"
    $gccOk = Test-Command "gcc"
}

# ==============================================================================
# 4. KIỂM TRA & CÀI ĐẶT BOOST (Tự động qua MSYS2)
# ==============================================================================
Write-Section "4. Thu vien Boost C++"
$boostFound = $false
$boostRoot = ""

# Tìm Boost ở các đường dẫn phổ biến
$possibleBoostPaths = @(
    "C:\msys64\ucrt64\include\boost",
    "C:\msys64\mingw64\include\boost",
    "C:\local\boost*\boost",
    "C:\boost*\boost"
)

foreach ($p in $possibleBoostPaths) {
    $resolved = Resolve-Path $p -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($resolved) {
        $boostFound = $true
        if ($resolved.Path -match "C:\\msys64\\ucrt64") { $boostRoot = "C:/msys64/ucrt64" }
        elseif ($resolved.Path -match "C:\\msys64\\mingw64") { $boostRoot = "C:/msys64/mingw64" }
        else { $boostRoot = Split-Path (Split-Path $resolved.Path -Parent) -Parent }
        break
    }
}

if ($boostFound) {
    Write-OK "Da co Boost tai $boostRoot"
} else {
    Write-Warn "Thieu thu vien Boost (bat buoc de build)"
    if (-not $DryRun) {
        Write-Action "Dang cai dat MSYS2 & Boost (mat 2-5 phut)..."
        
        # 1. Cai MSYS2 neu chua co
        if (-not (Test-Path "C:\msys64\usr\bin\bash.exe")) {
            winget install MSYS2.MSYS2 --silent --accept-package-agreements --accept-source-agreements
            Start-Sleep 5
        }
        
        # 2. Cai Boost qua pacman
        if (Test-Path "C:\msys64\usr\bin\bash.exe") {
            Write-Action "Dang tai Boost qua MSYS2 pacman..."
            & "C:\msys64\usr\bin\bash.exe" -lc "pacman -S --noconfirm mingw-w64-ucrt-x86_64-boost"
            
            if (Test-Path "C:\msys64\ucrt64\include\boost") {
                $boostFound = $true
                $boostRoot = "C:/msys64/ucrt64"
                Write-OK "Da cai Boost thanh cong qua MSYS2"
            } else {
                Write-Fail "Cai Boost that bai qua MSYS2"
            }
        } else {
            Write-Fail "Khong the cai MSYS2 de lay Boost"
        }
    }
}

# Nếu có MSYS2, ưu tiên dùng GCC của MSYS2 để đồng bộ thư viện
if (Test-Path "C:\msys64\ucrt64\bin\gcc.exe") {
    $env:Path = "C:\msys64\ucrt64\bin;" + $env:Path
    $gccOk = Test-Command "gcc"
    if ($gccOk) { Write-OK "Uu tien su dung GCC tu MSYS2 (C:\msys64\ucrt64\bin)" }
}

# ==============================================================================
# 5. CẤU HÌNH CMAKELISTS.TXT
# ==============================================================================
Write-Section "5. Cau hinh CMakeLists.txt"
if ($boostFound -and $boostRoot) {
    $cmakePath = "$BotRoot\CMakeLists.txt"
    if (Test-Path $cmakePath) {
        $cmakeContent = Get-Content $cmakePath -Raw -Encoding UTF8
        if ($cmakeContent -notmatch 'BOOST_ROOT') {
            $boostConfig = @"

# Tu dong cau hinh Boost
set(BOOST_ROOT "$boostRoot")
set(BOOST_INCLUDEDIR "$boostRoot/include")
set(BOOST_LIBRARYDIR "$boostRoot/lib")
find_package(Boost REQUIRED)
include_directories(`${Boost_INCLUDE_DIRS})
"@
            $cmakeContent = $cmakeContent -replace '(project\([^\)]+\))', "`$1`n$boostConfig"
            Set-Content -Path $cmakePath -Value $cmakeContent -Encoding UTF8
            Write-OK "Da cap nhat CMakeLists.txt voi BOOST_ROOT=$boostRoot"
        } else {
            Write-OK "CMakeLists.txt da co cau hinh Boost"
        }
    }
}

# ==============================================================================
# 6. BUILD BOT
# ==============================================================================
Write-Section "6. Build Zalo Bot"
$exePath = "$BotRoot\build\zalo_bot.exe"
$needBuild = $true

# Nếu đã có exe VÀ không cài Boost mới thì bỏ qua build
if ((Test-Path $exePath) -and $boostFound -and $cmakeOk) {
    $exeTime = (Get-Item $exePath).LastWriteTime
    $srcTime = (Get-Item "$BotRoot\CMakeLists.txt").LastWriteTime
    if ($exeTime -gt $srcTime) {
        Write-OK "Da co build\zalo_bot.exe (Khong can build lai)"
        $needBuild = $false
    }
}

if ($needBuild) {
    if (-not $cmakeOk -or -not $gccOk) {
        Write-Fail "Khong the build vi thieu CMake hoac MinGW."
    } else {
        Write-Action "Dang build source code (mat 1-3 phut)..."
        
        # Xoa cache cu de CMake nhan dien compiler/Boost moi
        if (Test-Path "$BotRoot\build\CMakeCache.txt") {
            Remove-Item "$BotRoot\build\CMakeCache.txt" -Force -ErrorAction SilentlyContinue
        }
        if (-not (Test-Path "$BotRoot\build")) { New-Item -ItemType Directory -Path "$BotRoot\build" | Out-Null }
        
        Push-Location "$BotRoot\build"
        
        $generator = if (Test-Command "mingw32-make") { "MinGW Makefiles" } else { "Unix Makefiles" }
        Write-Info "Dung generator: $generator"
        & cmake .. -G "$generator" -DCMAKE_BUILD_TYPE=Release
        
        if ($LASTEXITCODE -eq 0) {
            & cmake --build . --config Release
            if ($LASTEXITCODE -eq 0) { Write-OK "Build thanh cong!" }
            else { Write-Fail "Build that bai (loi compile)." }
        } else {
            Write-Fail "CMake configure that bai."
        }
        Pop-Location
    }
}

# ==============================================================================
# 7. COPY DLLs TỪ MinGW SANG thư mục build
# ==============================================================================
Write-Section "7. Cau hinh DLLs"
$dlls = @("libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll")
foreach ($dll in $dlls) {
    if (-not (Test-Path "$BotRoot\build\$dll")) {
        $gccPath = (Get-Command gcc -ErrorAction SilentlyContinue).Source
        if ($gccPath) {
            $mingwBin = Split-Path $gccPath -Parent
            $srcDll = Join-Path $mingwBin $dll
            if (Test-Path $srcDll) {
                Copy-Item $srcDll "$BotRoot\build\$dll" -Force
                Write-OK "Da copy $dll"
            } else { Write-Warn "Khong tim thay $dll" }
        }
    } else {
        Write-OK "$dll da co san"
    }
}

# ==============================================================================
# 8. TỔNG KẾT
# ==============================================================================
Write-Section "8. TONG KET"
if ($script:Issues.Count -gt 0) {
    Write-Host "`n  CO LOI XAY RA:" -ForegroundColor Red
    $script:Issues | ForEach-Object { Write-Host "    X $_" -ForegroundColor Red }
} else {
    Write-Host "`n  🎉 MOI THU DA SAN SANG!" -ForegroundColor Green
    Write-Host "  Chay bot bang lenh: .\run.bat" -ForegroundColor Cyan
}
Write-Host ""