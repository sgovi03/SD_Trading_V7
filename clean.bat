@echo off
REM ============================================================
REM SD Trading System V4.0 - Clean Build
REM ============================================================

echo.
echo ============================================================
echo   Cleaning build directory...
echo ============================================================
echo.

if exist build (
    echo Removing build directory...
    rmdir /s /q build
    echo Build directory removed!
) else (
    echo Build directory does not exist - nothing to clean.
)

echo.
echo Clean complete!
echo.
pause
