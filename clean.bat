@echo off
setlocal EnableExtensions
chcp 65001 >nul

cd /d "%~dp0"

:menu
cls
echo ============================================================
echo   Zalo Bot C++ - Clean Tool
echo ============================================================
echo.
echo  [1] Clean cache + logs         ^(an toan, chi xoa anh render^)
echo  [2] Clean cookies + accounts   ^(PUBLISH - xoa tai khoan Zalo^)
echo  [3] Clean data personal        ^(nong trai, husks, admin^)
echo  [4] Clean build                ^(xoa build de rebuild^)
echo  [5] FULL CLEAN                 ^(publish-ready, nhu moi clone^)
echo  [6] Xem chi tiet file se xoa
echo  [0] Thoat
echo.

set /p "choice=Chon [0-6]: "

if "%choice%"=="0" exit /b 0
if "%choice%"=="1" goto opt1
if "%choice%"=="2" goto opt2
if "%choice%"=="3" goto opt3
if "%choice%"=="4" goto opt4
if "%choice%"=="5" goto opt5
if "%choice%"=="6" goto opt6
goto menu

:opt1
powershell -NoProfile -ExecutionPolicy Bypass -File "clean.ps1" -Mode cache
goto done

:opt2
powershell -NoProfile -ExecutionPolicy Bypass -File "clean.ps1" -Mode cookies
goto done

:opt3
powershell -NoProfile -ExecutionPolicy Bypass -File "clean.ps1" -Mode personal
goto done

:opt4
powershell -NoProfile -ExecutionPolicy Bypass -File "clean.ps1" -Mode build
goto done

:opt5
powershell -NoProfile -ExecutionPolicy Bypass -File "clean.ps1" -Mode full
goto done

:opt6
powershell -NoProfile -ExecutionPolicy Bypass -File "clean.ps1" -Mode help
pause
goto menu

:done
echo.
pause
goto menu
