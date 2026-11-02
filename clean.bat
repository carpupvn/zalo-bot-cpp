@echo off
chcp 65001 >nul
cd /d E:\zalo-bot-cpp

echo Xoa build/ hoan toan...
taskkill /F /IM zalo_bot.exe >nul 2>&1
taskkill /F /IM node.exe >nul 2>&1
timeout /t 1 /nobreak >nul
if exist build rmdir /S /Q build

echo Xoa data tam...
if exist data\qr_*.png del /Q data\qr_*.png >nul 2>&1
if exist data\help_*.png del /Q data\help_*.png >nul 2>&1
if exist data\info_*.png del /Q data\info_*.png >nul 2>&1
if exist data\reply_*.png del /Q data\reply_*.png >nul 2>&1

echo Goi start.bat...
call start.bat