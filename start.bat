@echo off
chcp 65001 >nul
setlocal enabledelayedexpansion
cd /d E:\zalo-bot-cpp

echo.
echo ========================================
echo    ZALO BOT - CLEAN BUILD + RUN
echo ========================================
echo.

echo [1/4] Kill process cu...
taskkill /F /IM zalo_bot.exe >nul 2>&1
taskkill /F /IM node.exe >nul 2>&1
timeout /t 1 /nobreak >nul

echo [2/4] Xoa build cu...
if exist build rmdir /S /Q build >nul 2>&1

echo [3/4] Configure CMake...
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 (
    echo [X] CONFIGURE THAT BAI!
    pause
    exit /b 1
)

echo.
echo Build... (bat dau: %TIME%)
cmake --build build --config Release -- -j4
if errorlevel 1 (
    echo [X] BUILD THAT BAI!
    pause
    exit /b 1
)
echo     Build xong luc: %TIME%

set MINGW_BIN=D:\CodeBlocks\MinGW\bin
if exist "%MINGW_BIN%\libstdc++-6.dll" (
    for %%D in (libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll) do (
        if not exist "build\%%D" (
            copy /Y "%MINGW_BIN%\%%D" "build\%%D" >nul 2>&1
            echo     Da copy %%D
        )
    )
)

echo.
echo [4/4] Chay bot...
echo ========================================
echo.

build\zalo_bot.exe

echo.
echo Bot da thoat.
pause