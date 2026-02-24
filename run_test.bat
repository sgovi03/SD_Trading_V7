@echo off
setlocal enabledelayedexpansion

cd /d "D:\ClaudeAi\DryRun_DEV"
set SD_ALLOW_LARGE_BOOTSTRAP=1

echo Bootstrap Fix Test - Starting at %date% %time% >> test_run.log
echo. >> test_run.log

echo Running dryrun with bootstrap=100... >> test_run.log
call "build\bin\Debug\sd_trading_unified.exe" --mode=dryrun --data=data\live_data.csv --bootstrap-bars=100 >> test_run.log 2>&1

echo. >> test_run.log
echo Test completed at %date% %time% >> test_run.log

echo Checking for trades.json... >> test_run.log
if exist "results\dryrun\trades.json" (
    echo FOUND: results\dryrun\trades.json >> test_run.log
    dir "results\dryrun\trades.json" >> test_run.log
) else if exist "results\live_trades\trades.json" (
    echo FOUND: results\live_trades\trades.json >> test_run.log
    dir "results\live_trades\trades.json" >> test_run.log
) else (
    echo NOT FOUND: trades.json >> test_run.log
)

echo Test log written to test_run.log
