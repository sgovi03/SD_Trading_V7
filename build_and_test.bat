@echo off
REM ============================================================
REM SD Trading System V4.0 - Quick Build & Test
REM ============================================================

echo.
echo ============================================================
echo   Quick Build and Test
echo ============================================================
echo.

if not exist build mkdir build
cd build

cmake .. 
if %ERRORLEVEL% NEQ 0 exit /b 1

cmake --build . --config Debug
if %ERRORLEVEL% NEQ 0 exit /b 1

if exist Debug\bin\run_tests.exe (
    Debug\bin\run_tests.exe
) else if exist bin\run_tests.exe (
    bin\run_tests.exe
)

pause
