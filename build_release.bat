@echo off
REM ============================================================
REM SD Trading System V4.0 - Release Build Script
REM Builds with vcpkg CURL support
REM ============================================================

setlocal

echo ============================================================
echo   SD Trading System V4.0 - Release Build (Optimized)
echo ============================================================
echo.

REM Change to project root directory
cd /d "%~dp0"

REM Create and enter build directory
if not exist "build" (
    echo Creating build directory...
    mkdir build
) else (
    echo Using existing build directory...
)

cd build

echo Configuring project with CMake (Release + Optimizations)...
echo.

REM Configure with CMake using vcpkg toolchain
cmake -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=17 -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake ..

if errorlevel 1 (
    echo.
    echo [ERROR] CMake configuration failed!
    echo.
    echo Troubleshooting:
    echo 1. Verify vcpkg curl is installed: C:\vcpkg\vcpkg list ^| findstr curl
    echo 2. Check toolchain file exists: dir C:\vcpkg\scripts\buildsystems\vcpkg.cmake
    echo 3. Make sure Visual Studio 2022 is installed
    echo 4. Try cleaning: rmdir /S /Q build
    echo.
    pause
    exit /b 1
)

echo.
echo [OK] CMake configuration successful!
echo.

REM Build the project
echo Building Release configuration...
echo.

cmake --build . --config Release -- /m /p:CL_MPcount=4

if errorlevel 1 (
    echo.
    echo [ERROR] Build failed!
    echo Check the error messages above for details.
    echo.
    pause
    exit /b 1
)

echo.
echo ============================================================
echo   BUILD SUCCESSFUL!
echo ============================================================
echo.
echo Executables are in: build\Release\
echo.
echo To test HTTP client:
echo   cd build\Release
echo   test_order_submitter.exe
echo.
echo To run live engine:
echo   cd build\Release
echo   sd_engine_live.exe
echo.

cd ..
pause