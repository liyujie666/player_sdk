@echo off
chcp 65001 >nul 2>&1
setlocal enabledelayedexpansion

echo ============================================
echo   SmartPlayerSDK Build Script
echo ============================================
echo.

:: ===== 1. 查找并初始化 MSVC 环境 =====
set "VCVARS="

:: 方法1: 使用 vswhere 精确查找（VS 2017+自带）
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

:: 方法1.5: 检查用户自定义安装路径
if not defined VCVARS (
    for %%e in (Professional Enterprise Community BuildTools Preview) do (
        if not defined VCVARS (
            if exist "D:\Visual Studio\Visual Studio 2026\%%e\VC\Auxiliary\Build\vcvars64.bat" (
                set "VCVARS=D:\Visual Studio\Visual Studio 2026\%%e\VC\Auxiliary\Build\vcvars64.bat"
            )
        )
    )
    :: 也检查直接在根目录下的情况（无 edition 子目录）
    if not defined VCVARS (
        if exist "D:\Visual Studio\Visual Studio 2026\VC\Auxiliary\Build\vcvars64.bat" (
            set "VCVARS=D:\Visual Studio\Visual Studio 2026\VC\Auxiliary\Build\vcvars64.bat"
        )
    )
)

:: 方法2: 遍历常见安装路径（VS 2015~2026）
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

:: 方法3: 检查 VS 2015（旧版目录结构）
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
    echo [ERROR] 未找到 Visual Studio，请安装 VS 2015/2017/2019/2022
    echo         已搜索路径: C/D/E 盘 Program Files 目录
    echo         也尝试了 vswhere 工具查找
    exit /b 1
)

echo [1/4] 初始化 MSVC 环境...
echo        使用: !VCVARS!
if defined VCVARS_ARG (
    call "!VCVARS!" !VCVARS_ARG! >nul 2>&1
) else (
    call "!VCVARS!" >nul 2>&1
)

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
    :: 根据找到的 VS 版本选择对应的 CMake 生成器
    set "GENERATOR=Visual Studio 17 2022"
    echo "!VCVARS!" | findstr /i "2026" >nul && set "GENERATOR=Visual Studio 18 2026"
    echo "!VCVARS!" | findstr /i "2019" >nul && set "GENERATOR=Visual Studio 16 2019"
    echo "!VCVARS!" | findstr /i "2017" >nul && set "GENERATOR=Visual Studio 15 2017"
    echo "!VCVARS!" | findstr /i "14.0" >nul && set "GENERATOR=Visual Studio 14 2015"
    set "BUILD_DIR=!SDK_DIR!\build"
    set "EXE_DIR=!SDK_DIR!\build\Release"
)

echo [2/4] 使用生成器: !GENERATOR!

:: ===== 3. CMake 配置 =====
echo [3/4] CMake 配置...
if exist "!BUILD_DIR!" rmdir /s /q "!BUILD_DIR!"

if "!GENERATOR!"=="Ninja" (
    cmake -B "!BUILD_DIR!" -S "%SDK_DIR%" -G "Ninja" -DCMAKE_BUILD_TYPE=Release
) else if "!GENERATOR!"=="Visual Studio 14 2015" (
    cmake -B "!BUILD_DIR!" -S "%SDK_DIR%" -G "!GENERATOR!" -A x64
) else (
    cmake -B "!BUILD_DIR!" -S "%SDK_DIR%" -G "!GENERATOR!" -A x64
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
