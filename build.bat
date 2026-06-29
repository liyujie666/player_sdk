@echo off
chcp 65001 >nul 2>&1
setlocal enabledelayedexpansion

echo ============================================
echo   SmartPlayerSDK Build Script
echo ============================================
echo.

:: ===== 1. 查找并初始化 MSVC 环境 =====
set "VCVARS="
for %%y in (2022 2019 2017) do (
    for %%e in (Professional Enterprise Community BuildTools) do (
        if exist "C:\Program Files\Microsoft Visual Studio\%%y\%%e\VC\Auxiliary\Build\vcvars64.bat" (
            set "VCVARS=C:\Program Files\Microsoft Visual Studio\%%y\%%e\VC\Auxiliary\Build\vcvars64.bat"
        )
        if exist "C:\Program Files (x86)\Microsoft Visual Studio\%%y\%%e\VC\Auxiliary\Build\vcvars64.bat" (
            set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\%%y\%%e\VC\Auxiliary\Build\vcvars64.bat"
        )
    )
)

if not defined VCVARS (
    echo [ERROR] 未找到 Visual Studio，请安装 VS 2017/2019/2022
    exit /b 1
)

echo [1/4] 初始化 MSVC 环境...
call "!VCVARS!" >nul 2>&1

:: ===== 2. 确定 CMake 生成器 =====
:: %~dp0 末尾带 \，去掉它避免 \" 转义引号
set "SDK_DIR=%~dp0"
if "%SDK_DIR:~-1%"=="\" set "SDK_DIR=%SDK_DIR:~0,-1%"
set "DEPS_DIR=%SDK_DIR%\..\dependencies"

:: 检查 Ninja 是否可用
where ninja >nul 2>&1
if !errorlevel! equ 0 (
    set "GENERATOR=Ninja"
    set "BUILD_DIR=!SDK_DIR!\build"
    set "EXE_DIR=!SDK_DIR!\build"
) else (
    set "GENERATOR=Visual Studio 17 2022"
    set "BUILD_DIR=!SDK_DIR!\build"
    set "EXE_DIR=!SDK_DIR!\build\Release"
)

echo [2/4] 使用生成器: !GENERATOR!

:: ===== 3. CMake 配置 =====
echo [3/4] CMake 配置...
if exist "!BUILD_DIR!" rmdir /s /q "!BUILD_DIR!"

if "!GENERATOR!"=="Ninja" (
    cmake -B "!BUILD_DIR!" -S "%SDK_DIR%" -G "Ninja" -DCMAKE_BUILD_TYPE=Release
) else (
    cmake -B "!BUILD_DIR!" -S "%SDK_DIR%" -G "Visual Studio 17 2022" -A x64
)

if !errorlevel! neq 0 (
    echo [ERROR] CMake 配置失败
    exit /b 1
)

:: ===== 4. 编译 =====
echo [4/4] 编译中...
cmake --build "!BUILD_DIR!" --config Release
if !errorlevel! neq 0 (
    echo [ERROR] 编译失败
    exit /b 1
)

:: ===== 5. 复制运行时 DLL =====
echo.
echo 复制运行时 DLL...
if not exist "!EXE_DIR!" mkdir "!EXE_DIR!"
copy /y "%DEPS_DIR%\bin\*.dll" "!EXE_DIR!\" >nul 2>&1

:: ===== 6. 验证产物 =====
echo.
echo ============================================
echo   构建成功!
echo ============================================
echo.
echo 产物位置: !EXE_DIR!
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
echo 运行示例: run.bat [视频文件路径]
echo.
