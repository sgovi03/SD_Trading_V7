@echo off
setlocal enabledelayedexpansion

echo.
echo ========================================
echo   LIVE (UNIFIED) QUICK RUN - RELEASE
echo ========================================
echo.

set START_TIME=%TIME%
echo [START] %START_TIME%
echo.

echo Running: build\bin\Release\sd_trading_unified.exe --mode=live --duration=2
echo.
echo Automatically pressing Enter after 2 seconds...
echo.

REM Create a script to simulate Enter keypress
echo Set WshShell = WScript.CreateObject("WScript.Shell") > temp_press_enter.vbs
echo WScript.Sleep 2000 >> temp_press_enter.vbs
echo WshShell.SendKeys "{ENTER}" >> temp_press_enter.vbs

REM Start the program and the auto-enter script
start "Press Enter Script" /B wscript.exe temp_press_enter.vbs
build\bin\Release\sd_trading_unified.exe --mode=live --duration=2

REM Clean up
del temp_press_enter.vbs 2>nul

set END_TIME=%TIME%
echo.
echo [END] %END_TIME%
echo.
echo ========================================
echo Start: %START_TIME%
echo End:   %END_TIME%
echo ========================================
echo.

endlocal
