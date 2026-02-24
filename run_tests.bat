@echo off
REM ============================================================
REM Quick Test Runner - Finds and runs tests automatically
REM ============================================================

echo.
echo Looking for test executable...

cd build 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] build directory not found!
    echo Please run build_debug.bat first.
    pause
    exit /b 1
)

REM Try all possible locations
if exist bin\Debug\run_tests.exe (
    echo Found: bin\Debug\run_tests.exe
    echo.
    bin\Debug\run_tests.exe
    goto :done
)

if exist Debug\bin\run_tests.exe (
    echo Found: Debug\bin\run_tests.exe
    echo.
    Debug\bin\run_tests.exe
    goto :done
)

if exist bin\run_tests.exe (
    echo Found: bin\run_tests.exe
    echo.
    bin\run_tests.exe
    goto :done
)

if exist Debug\run_tests.exe (
    echo Found: Debug\run_tests.exe
    echo.
    Debug\run_tests.exe
    goto :done
)

REM Search recursively
echo Searching recursively...
for /r %%i in (run_tests.exe) do (
    echo Found: %%i
    echo.
    "%%i"
    goto :done
)

echo [ERROR] Test executable not found!
echo Make sure build completed successfully.

:done
echo.
pause
