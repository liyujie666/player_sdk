@echo off
chcp 65001 >nul 2>&1
setlocal

:: ===== Find build output =====
set "SDK_DIR=%~dp0"
if "%SDK_DIR:~-1%"=="\" set "SDK_DIR=%SDK_DIR:~0,-1%"

:: Ninja output (build/) or VS output (build/Release/)
if exist "%SDK_DIR%\build\SmartPlayerExample.exe" (
    set "EXE_DIR=%SDK_DIR%\build"
) else if exist "%SDK_DIR%\build\Release\SmartPlayerExample.exe" (
    set "EXE_DIR=%SDK_DIR%\build\Release"
) else (
    echo [ERROR] SmartPlayerExample.exe not found. Run build.bat first.
    exit /b 1
)

:: ===== Ensure DLLs exist =====
set "DEPS_DIR=%SDK_DIR%\..\dependencies"
if not exist "%EXE_DIR%\SDL2.dll" (
    copy /y "%DEPS_DIR%\bin\*.dll" "%EXE_DIR%\" >nul 2>&1
)

:: ===== Resolve video file to absolute path =====
if "%~1"=="" (
    :: No argument: use default test video
    if exist "%SDK_DIR%\..\test_video.mp4" (
        set "VIDEO=%SDK_DIR%\..\test_video.mp4"
    ) else (
        echo [ERROR] No video file specified and test_video.mp4 not found.
        echo Usage: run.bat [video_file_path]
        exit /b 1
    )
) else (
    :: Convert relative path to absolute path
    set "VIDEO=%~f1"
)

:: ===== Run =====
echo ============================================
echo   SmartPlayer SDK Example Player
echo ============================================
echo Video: %VIDEO%
echo.
echo Controls:
echo   Space  - Pause / Resume
echo   Left   - Seek -10s
echo   Right  - Seek +10s
echo   ESC    - Quit
echo ============================================
echo.

cd /d "%EXE_DIR%"
SmartPlayerExample.exe "%VIDEO%"
