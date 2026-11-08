@echo off
chcp 65001 >nul
cd /d "%~dp0"

if not exist "build\zalo_bot.exe" (
    echo [X] Chua co build\zalo_bot.exe - chay setup truoc!
    pause
    exit /b 1
)

:loop
echo [1/2] Dang tat cac tien trinh cu...
taskkill /F /IM zalo_bot.exe >nul 2>&1
taskkill /F /IM node.exe >nul 2>&1
:: Kill guard cu (dung wmic don gian, khong dung powershell phuc tap)
wmic process where "name='powershell.exe' and commandline like '%%guard.ps1%%'" call terminate >nul 2>&1

echo [2/2] Khoi dong Python Watcher (guard.ps1)...
if exist "tools\guard.ps1" (
    start "" /B powershell -NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File "tools\guard.ps1"
)

echo Dang chay bot...
build\zalo_bot.exe
set BOT_EXIT_CODE=%ERRORLEVEL%

if not "%BOT_EXIT_CODE%"=="0" (
    echo.
    echo ==========================================
    echo [WATCHDOG] Bot vua thoat ^(Code: %BOT_EXIT_CODE%^). Khoi dong lai sau 2s...
    echo ==========================================
    timeout /t 2 /nobreak >nul
    goto loop
)

:: Neu thoat binh thuong (ma 0)
wmic process where "name='powershell.exe' and commandline like '%%guard.ps1%%'" call terminate >nul 2>&1
echo.
echo Bot da thoat binh thuong.
pause