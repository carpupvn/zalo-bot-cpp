@echo off
chcp 65001 >nul
cd /d "%~dp0"

:loop
echo [1/2] Dang tat cac tien trinh cu...
taskkill /F /IM zalo_bot.exe >nul 2>&1
taskkill /F /IM node.exe >nul 2>&1

echo [2/2] Khoi dong Python Watcher (guard.ps1)...
if exist "tools\guard.ps1" (
    start "" /B powershell -NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File "tools\guard.ps1"
)

echo Dang chay bot...
build\zalo_bot.exe

:: Neu ma thoat la 99 -> Reload
if %ERRORLEVEL% EQU 99 (
    echo.
    echo ==========================================
    echo [RELOAD] Bot yeu cau khoi dong lai...
    echo ==========================================
    timeout /t 2 /nobreak >nul
    goto loop
)

:: Neu thoat binh thuong -> Don dep
powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name = 'powershell.exe'\" | Where-Object { $_.CommandLine -match 'guard\.ps1' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }" >nul 2>&1
echo.
echo Bot da thoat.
pause