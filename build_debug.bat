@echo off
REM ============================================================
REM SD Trading System V4.0 - Debug Build Script
REM ============================================================

echo.
echo ============================================================
echo   SD Trading System V4.0 - Debug Build
echo ============================================================
echo.

REM Check if build directory exists
if not exist build (
    echo Creating build directory...
    mkdir build
)

cd build

REM Configure with CMake (auto-detect Visual Studio)
echo Configuring project with CMake...
cmake .. -DCMAKE_BUILD_TYPE=Debug
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [ERROR] CMake configuration failed!
    echo.
    echo Possible solutions:
    echo 1. Install Visual Studio with C++ Desktop Development
    echo 2. Or try: cmake .. -G "MinGW Makefiles"
    echo 3. Or install MSYS2 and use Unix Makefiles
    pause
    exit /b 1
)

REM Build the project
echo.
echo Building project (Debug)...
cmake --build . --config Debug
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [ERROR] Build failed!
    pause
    exit /b 1
)

echo.
echo ============================================================
echo   Build Successful!
echo ============================================================
echo.

REM Run tests
echo Running unit tests...
echo.

REM Try different possible locations for the test executable
if exist bin\Debug\run_tests.exe (
    bin\Debug\run_tests.exe
) else if exist Debug\bin\run_tests.exe (
    Debug\bin\run_tests.exe
) else if exist bin\run_tests.exe (
    bin\run_tests.exe
) else if exist Debug\run_tests.exe (
    Debug\run_tests.exe
) else (
    echo [WARNING] Test executable location not detected automatically.
    echo Attempting to find it...
    for /r %%i in (run_tests.exe) do (
        echo Found: %%i
        "%%i"
        goto :tests_done
    )
    echo [ERROR] Could not find run_tests.exe
    echo Please run manually from: build\bin\Debug\run_tests.exe
)

:tests_done

echo.
echo ============================================================
echo   Build script complete!
echo ============================================================
echo.
pause
