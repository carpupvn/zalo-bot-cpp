@echo off
chcp 65001 >nul
cd /d E:\zalo-bot-cpp

if not exist "build\zalo_bot.exe" (
    echo [X] Chua co build\zalo_bot.exe - chay start.bat truoc!
    pause
    exit /b 1
)

taskkill /F /IM zalo_bot.exe >nul 2>&1
taskkill /F /IM node.exe >nul 2>&1
timeout /t 1 /nobreak >nul

echo Chay bot...
build\zalo_bot.exe
echo.
echo Bot da thoat.
pause