@echo off
REM ============================================================
REM SD Engine V6 — Optimizer Launcher (Windows)
REM ============================================================
REM Edit the variables below then double-click or run from cmd

REM ── CONFIG ──────────────────────────────────────────────────
set CONFIG=.\phase_6_config_v0_7_NEW.txt
set BACKTEST_CMD=backtest.exe {config}
set OUTPUT_DIR=.\optimization_runs
set STRATEGY=genetic
set OBJECTIVE=composite
set TRIALS=100
set PARAMS_GROUPS=risk trailing entry
set POP_SIZE=20
set TIMEOUT=300
set SEED=42

REM ── Uncomment for dry-run test ──
REM set DRY=--dry-run
set DRY=

echo ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
echo   SD Engine V6 Parameter Optimizer
echo   Strategy: %STRATEGY% ^| Objective: %OBJECTIVE% ^| Trials: %TRIALS%
echo ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

python optimizer.py ^
    --config "%CONFIG%" ^
    --backtest-cmd "%BACKTEST_CMD%" ^
    --output-dir "%OUTPUT_DIR%" ^
    --strategy %STRATEGY% ^
    --objective %OBJECTIVE% ^
    --trials %TRIALS% ^
    --params-groups %PARAMS_GROUPS% ^
    --pop-size %POP_SIZE% ^
    --timeout %TIMEOUT% ^
    --seed %SEED% ^
    %DRY%

echo.
echo Results -^> %OUTPUT_DIR%\all_results.csv
echo Best    -^> %OUTPUT_DIR%\BEST\best_config.txt
echo.
pause
