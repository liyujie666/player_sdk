@echo off
chcp 65001 >nul 2>&1
setlocal enabledelayedexpansion

echo ============================================
echo   SmartPlayerSDK Build Script
echo ============================================
echo.

:: ===== 1. Find and init MSVC environment =====
set "VCVARS="

:: Method 1: Use vswhere (shipped with VS 2017+)
for %%p in ("C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" ^
            "C:\Program Files\Microsoft Visual Studio\Installer\vswhere.exe") do (
    if exist %%p (
        for /f "usebackq tokens=*" %%i in (`%%p -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
            if exist "%%i\VC\Auxiliary\Build\vcvars64.bat" (
                set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
            )
        )
    )
)

:: Method 1.5: Check custom install path
if not defined VCVARS (
    for %%e in (Professional Enterprise Community BuildTools Preview) do (
        if not defined VCVARS (
            if exist "D:\Visual Studio\Visual Studio 2026\%%e\VC\Auxiliary\Build\vcvars64.bat" (
                set "VCVARS=D:\Visual Studio\Visual Studio 2026\%%e\VC\Auxiliary\Build\vcvars64.bat"
            )
        )
    )
    :: Also check root directory (no edition subfolder)
    if not defined VCVARS (
        if exist "D:\Visual Studio\Visual Studio 2026\VC\Auxiliary\Build\vcvars64.bat" (
            set "VCVARS=D:\Visual Studio\Visual Studio 2026\VC\Auxiliary\Build\vcvars64.bat"
        )
    )
)

:: Method 2: Scan common install paths (VS 2015~2026)
if not defined VCVARS (
    for %%y in (2026 2022 2019 2017) do (
        for %%e in (Professional Enterprise Community BuildTools Preview) do (
            for %%d in ("D:\Visual Studio\Visual Studio" ^
                        "C:\Program Files\Microsoft Visual Studio" ^
                        "C:\Program Files (x86)\Microsoft Visual Studio" ^
                        "D:\Program Files\Microsoft Visual Studio" ^
                        "D:\Program Files (x86)\Microsoft Visual Studio" ^
                        "E:\Program Files\Microsoft Visual Studio" ^
                        "E:\Program Files (x86)\Microsoft Visual Studio") do (
                if not defined VCVARS (
                    if exist "%%~d\%%y\%%e\VC\Auxiliary\Build\vcvars64.bat" (
                        set "VCVARS=%%~d\%%y\%%e\VC\Auxiliary\Build\vcvars64.bat"
                    )
                )
            )
        )
    )
)

:: Method 3: Check VS 2015 (legacy directory structure)
if not defined VCVARS (
    for %%d in ("C:\Program Files (x86)\Microsoft Visual Studio 14.0" ^
                "C:\Program Files\Microsoft Visual Studio 14.0" ^
                "D:\Program Files (x86)\Microsoft Visual Studio 14.0" ^
                "D:\Program Files\Microsoft Visual Studio 14.0") do (
        if exist "%%~d\VC\vcvarsall.bat" (
            set "VCVARS=%%~d\VC\vcvarsall.bat"
            set "VCVARS_ARG=amd64"
        )
    )
)

if not defined VCVARS (
    echo [ERROR] Visual Studio not found. Please install VS 2015/2017/2019/2022/2026.
    exit /b 1
)

echo [1/4] Init MSVC environment...
echo        Using: !VCVARS!
if defined VCVARS_ARG (
    call "!VCVARS!" !VCVARS_ARG! >nul 2>&1
) else (
    call "!VCVARS!" >nul 2>&1
)

:: ===== 2. Determine CMake generator =====
:: Strip trailing backslash from %~dp0 to avoid \" escaping issues
set "SDK_DIR=%~dp0"
if "%SDK_DIR:~-1%"=="\" set "SDK_DIR=%SDK_DIR:~0,-1%"
set "DEPS_DIR=%SDK_DIR%\..\dependencies"

:: Check if Ninja is available
where ninja >nul 2>&1
if !errorlevel! equ 0 (
    set "GENERATOR=Ninja"
    set "BUILD_DIR=!SDK_DIR!\build"
    set "EXE_DIR=!SDK_DIR!\build"
) else (
    :: Select CMake generator based on detected VS version
    set "GENERATOR=Visual Studio 17 2022"
    echo "!VCVARS!" | findstr /i "2026" >nul && set "GENERATOR=Visual Studio 18 2026"
    echo "!VCVARS!" | findstr /i "2019" >nul && set "GENERATOR=Visual Studio 16 2019"
    echo "!VCVARS!" | findstr /i "2017" >nul && set "GENERATOR=Visual Studio 15 2017"
    echo "!VCVARS!" | findstr /i "14.0" >nul && set "GENERATOR=Visual Studio 14 2015"
    set "BUILD_DIR=!SDK_DIR!\build"
    set "EXE_DIR=!SDK_DIR!\build\Release"
)

echo [2/4] Generator: !GENERATOR!

:: ===== 3. CMake configure =====
echo [3/4] CMake configure...
if exist "!BUILD_DIR!" rmdir /s /q "!BUILD_DIR!"

if "!GENERATOR!"=="Ninja" (
    cmake -B "!BUILD_DIR!" -S "%SDK_DIR%" -G "Ninja" -DCMAKE_BUILD_TYPE=Release
) else if "!GENERATOR!"=="Visual Studio 14 2015" (
    cmake -B "!BUILD_DIR!" -S "%SDK_DIR%" -G "!GENERATOR!" -A x64
) else (
    cmake -B "!BUILD_DIR!" -S "%SDK_DIR%" -G "!GENERATOR!" -A x64
)

if !errorlevel! neq 0 (
    echo [ERROR] CMake configure failed
    exit /b 1
)

:: ===== 4. Build =====
echo [4/4] Building...
cmake --build "!BUILD_DIR!" --config Release
if !errorlevel! neq 0 (
    echo [ERROR] Build failed
    exit /b 1
)

:: ===== 5. Copy runtime DLLs =====
echo.
echo Copying runtime DLLs...
if not exist "!EXE_DIR!" mkdir "!EXE_DIR!"
copy /y "%DEPS_DIR%\bin\*.dll" "!EXE_DIR!\" >nul 2>&1

:: ===== 6. Verify output =====
echo.
echo ============================================
echo   Build succeeded!
echo ============================================
echo.
echo Output: !EXE_DIR!
echo.

if exist "!EXE_DIR!\SmartPlayerSDK.dll" (
    echo   [OK] SmartPlayerSDK.dll
) else (
    echo   [MISSING] SmartPlayerSDK.dll
)

if exist "!EXE_DIR!\SmartPlayerExample.exe" (
    echo   [OK] SmartPlayerExample.exe
) else (
    echo   [MISSING] SmartPlayerExample.exe
)

echo.
echo Run: run.bat [video_file]
echo.
