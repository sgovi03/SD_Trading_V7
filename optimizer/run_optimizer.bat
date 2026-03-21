@echo off
chcp 65001 >nul 2>&1
set PYTHONIOENCODING=utf-8

REM ==============================================================
REM SD Engine V6 Parameter Optimizer
REM ==============================================================
REM The engine reads config from a fixed path in system_config.json.
REM The optimizer overwrites that file before each trial run,
REM then restores it automatically when done.
REM ==============================================================

REM -- Project root (where system_config.json lives) --
set PROJECT_ROOT=D:\SD_System\SD_Volume_OI\SD_Trading_V6

REM -- Config path (must match "strategy_config" in system_config.json) --
set FIXED_CONFIG=conf/phase_6_config_v0_7_NEW.txt

REM -- Base config file to optimize from --
set CONFIG=.\optimization_runs_success\BEST\best_config.txt

REM -- Backtest command (runs from PROJECT_ROOT) --
set BACKTEST_CMD=%PROJECT_ROOT%\build\bin\Release\sd_trading_unified.exe --mode=backtest

REM -- Optimizer settings --
set OUTPUT_DIR=.\optimization_runs
set STRATEGY=genetic
set OBJECTIVE=composite
set TRIALS=150
set PARAMS_GROUPS=zone entry timing
set POP_SIZE=20
set TIMEOUT=300
set SEED=42

REM -- Uncomment to test without real backtests --
REM set DRY=--dry-run
set DRY=

echo ==================================================
echo   SD Engine V6 Parameter Optimizer
echo   Strategy: %STRATEGY%  Trials: %TRIALS%
echo   Project:  %PROJECT_ROOT%
echo ==================================================

python optimizer.py --config "%CONFIG%" --backtest-cmd "%BACKTEST_CMD%" --project-root "%PROJECT_ROOT%" --fixed-config-rel "%FIXED_CONFIG%" --output-dir "%OUTPUT_DIR%" --strategy %STRATEGY% --objective %OBJECTIVE% --trials %TRIALS% --params-groups %PARAMS_GROUPS% --pop-size %POP_SIZE% --timeout %TIMEOUT% --seed %SEED% %DRY%

echo.
echo Results: %OUTPUT_DIR%\all_results.csv
echo Best:    %OUTPUT_DIR%\BEST\best_config.txt
echo.
pause