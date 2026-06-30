@echo off
chcp 65001 >nul 2>&1
setlocal enabledelayedexpansion

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

:: ===== Parse arguments =====
set "VIDEO="
set "HW="
set "SPEED="
set "SIZE="

:parse_args
if "%~1"=="" goto done_args
if /i "%~1"=="--hw" (
    set "HW=--hw"
    shift
    goto parse_args
)
if /i "%~1"=="--speed" (
    set "SPEED=--speed %~2"
    shift
    shift
    goto parse_args
)
if /i "%~1"=="--size" (
    set "SIZE=--size %~2"
    shift
    shift
    goto parse_args
)
if /i "%~1"=="--help" goto show_help
if /i "%~1"=="-h" goto show_help
:: Assume it's the video file
set "VIDEO=%~f1"
shift
goto parse_args

:show_help
echo Usage: run.bat [video_file] [options]
echo.
echo Options:
echo   --hw              Enable hardware decoding (GPU acceleration)
echo   --speed ^<val^>     Set initial playback speed (0.5 / 1.0 / 1.5 / 2.0)
echo   --size ^<WxH^>      Set window size (e.g. 1920x1080, default 1280x720)
echo   --help, -h        Show this help
echo.
echo Controls (in player window):
echo   Space       Pause / Resume
echo   Left/Right  Seek -/+ 10s
echo   Up/Down     Volume +/- 5
echo   M           Toggle mute
echo   S           Cycle speed (1.0 -^> 1.5 -^> 2.0 -^> 0.5 -^> 1.0)
echo   ESC         Quit
echo.
echo Examples:
echo   run.bat video.mp4
echo   run.bat video.mp4 --hw
echo   run.bat video.mp4 --hw --speed 1.5
echo   run.bat video.mp4 --size 1920x1080
exit /b 0

:done_args

:: ===== Resolve video file =====
if "%VIDEO%"=="" (
    if exist "%SDK_DIR%\..\test_video.mp4" (
        set "VIDEO=%SDK_DIR%\..\test_video.mp4"
    ) else (
        echo [ERROR] No video file specified and test_video.mp4 not found.
        echo Usage: run.bat [video_file] [options]
        echo Run "run.bat --help" for more info.
        exit /b 1
    )
)

:: ===== Run =====
echo ============================================
echo   SmartPlayer SDK Example Player
echo ============================================
echo Video: %VIDEO%
if defined HW echo Hardware Decode: ON
if defined SPEED echo Speed: %SPEED:~8%
if defined SIZE echo Window: %SIZE:~7%
echo.
echo Controls:
echo   Space       Pause / Resume
echo   Left/Right  Seek -/+ 10s
echo   Up/Down     Volume +/- 5
echo   M           Toggle mute
echo   S           Cycle speed
echo   ESC         Quit
echo ============================================
echo.

cd /d "%EXE_DIR%"
SmartPlayerExample.exe "%VIDEO%" %HW% %SPEED% %SIZE%
