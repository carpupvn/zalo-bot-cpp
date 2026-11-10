@echo off
chcp 65001 >nul
cd /d "%~dp0"

set PROFILE=%1
if "%PROFILE%"=="" (
    echo Usage: run-helper.bat ^<profile_num^>
    exit /b 1
)

set LOG=data\cookies\%PROFILE%.log
set STARTED=data\cookies\%PROFILE%.started

echo [WRAPPER %TIME%] Start profile=%PROFILE% > "%STARTED%"
echo [WRAPPER %TIME%] Start profile=%PROFILE% > "%LOG%"
echo [WRAPPER] cwd=%CD% >> "%LOG%"
echo [WRAPPER] node path: >> "%LOG%"
where node >> "%LOG%" 2>&1

node login-helper.cjs data\cookies\%PROFILE%.json cache\qr_login_%PROFILE%.png >> "%LOG%" 2>&1
echo [WRAPPER %TIME%] exit=%ERRORLEVEL% >> "%LOG%"