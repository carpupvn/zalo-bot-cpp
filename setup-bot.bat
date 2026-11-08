@echo off
setlocal EnableExtensions

REM ==========================================================
REM  Zalo Bot C++ - Auto Setup and Build (BAT wrapper)
REM  Tu nang quyen Administrator roi goi setup-bot.ps1
REM ==========================================================

cd /d "%~dp0"

REM --- Kiem tra PowerShell ---
where powershell >nul 2>&1
if errorlevel 1 (
    echo [LOI] Khong tim thay powershell.exe tren he thong.
    pause
    exit /b 1
)

REM --- Tu nang quyen Administrator neu can ---
net session >nul 2>&1
if errorlevel 1 (
    echo.
    echo [INFO] Dang yeu cau quyen Administrator...
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs -WorkingDirectory '%~dp0'"
    exit /b
)

REM --- Kiem tra setup-bot.ps1 ---
if not exist "%~dp0setup-bot.ps1" (
    echo [LOI] Khong tim thay setup-bot.ps1 cung thu muc.
    pause
    exit /b 1
)

echo.
echo ============================================================
echo   Zalo Bot C++ - Auto Setup and Build
echo ============================================================
echo.

REM --- Chay PS1 voi ExecutionPolicy Bypass ---
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup-bot.ps1"
set "RC=%ERRORLEVEL%"

echo.
if "%RC%"=="0" (
    echo [XONG] Setup hoan tat. Chay bot bang: run.bat
) else (
    echo [CANH BAO] Setup ket thuc voi ma loi %RC%. Xem log ben tren.
)
echo.
pause
exit /b %RC%