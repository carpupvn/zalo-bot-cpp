@echo off
setlocal EnableExtensions

REM ==========================================================
REM  Zalo Bot C++ - Auto Setup + Build + Data Init
REM ==========================================================

cd /d "%~dp0"

where powershell >nul 2>&1
if errorlevel 1 (
    echo [LOI] Khong tim thay powershell.exe
    pause
    exit /b 1
)

net session >nul 2>&1
if errorlevel 1 (
    echo [INFO] Dang yeu cau quyen Administrator...
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs -WorkingDirectory '%~dp0'"
    exit /b
)

if not exist "%~dp0setup-bot.ps1" (
    echo [LOI] Khong tim thay setup-bot.ps1
    pause
    exit /b 1
)

echo.
echo ============================================================
echo   BUOC 1/2: Cai dat toolchain + build bot
echo ============================================================
echo.

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup-bot.ps1"
set "RC1=%ERRORLEVEL%"

if not "%RC1%"=="0" (
    echo.
    echo [CANH BAO] setup-bot.ps1 loi ^(Code: %RC1%^)
    pause
    exit /b %RC1%
)

echo.
echo ============================================================
echo   BUOC 2/2: Khoi tao data + config templates
echo ============================================================
echo.

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup-data.ps1"
set "RC2=%ERRORLEVEL%"

echo.
echo ============================================================
if "%RC2%"=="0" (
    echo   [XONG] Setup hoan tat!
    echo   Chay bot: run.bat
    echo   Lan dau: QR hien tai cache\qr_login.png
) else (
    echo   [CANH BAO] setup-data.ps1 loi ^(Code: %RC2%^)
)
echo ============================================================
echo.
pause
exit /b %RC2%
