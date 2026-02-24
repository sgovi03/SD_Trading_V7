# Run Unified Live Engine and auto-start trading
# - Sends Enter to bypass prompt
# - Runs for a specified duration then stops
# - Intended for DRY-run order logging into results/live_trades

param(
    [int]$DurationSeconds = 120
)

$exe = Join-Path $PWD "build/bin/Release/sd_trading_unified.exe"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  UNIFIED LIVE RUN (DRY-RUN)" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Executable: $exe"
Write-Host "Duration:   $DurationSeconds s"

if (-not (Test-Path $exe)) {
    Write-Host "ERROR: Executable not found: $exe" -ForegroundColor Red
    exit 1
}

$start = Get-Date
Write-Host "[$(Get-Date -Format 'HH:mm:ss.fff')] Starting..." -ForegroundColor Yellow

# Start unified exe in live mode
$proc = Start-Process -FilePath $exe -ArgumentList "--mode=live" -PassThru -WorkingDirectory $PWD -WindowStyle Normal
Start-Sleep -Seconds 2

# Try to send Enter to proceed past any prompt
try {
    Add-Type -AssemblyName System.Windows.Forms | Out-Null
    [System.Windows.Forms.SendKeys]::SendWait("{ENTER}")
    Write-Host "[$(Get-Date -Format 'HH:mm:ss.fff')] Sent Enter key" -ForegroundColor Green
} catch {
    Write-Host "[$(Get-Date -Format 'HH:mm:ss.fff')] Failed to send Enter (continuing)" -ForegroundColor Yellow
}

Write-Host "[$(Get-Date -Format 'HH:mm:ss.fff')] Running..." -ForegroundColor Gray

$deadline = (Get-Date).AddSeconds($DurationSeconds)
while ((Get-Date) -lt $deadline -and -not $proc.HasExited) {
    Start-Sleep -Seconds 2
}

Write-Host "[$(Get-Date -Format 'HH:mm:ss.fff')] Stopping..." -ForegroundColor Yellow
if (-not $proc.HasExited) {
    $proc | Stop-Process -Force -ErrorAction SilentlyContinue
}

$end = Get-Date
$elapsed = ($end - $start).TotalSeconds
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  SUMMARY" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Elapsed: $([Math]::Round($elapsed,2))s"
Write-Host "Orders CSV: results/live_trades/simulated_orders.csv"
Write-Host "Reports:    results/live_trades"