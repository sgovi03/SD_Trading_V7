#!/usr/bin/env python3
"""
EMA Exit Grid Search for SD Trading System V4

Searches EMA fast/slow pairs and ranks by:
- Maximize profit factor
- Minimize stop-loss total (less negative is better)
- Maximize total P&L

Usage:
    python scripts/ema_grid_search.py
    python scripts/ema_grid_search.py --fast-start 5 --fast-end 21 --fast-step 2 --slow-start 15 --slow-end 61 --slow-step 3
    python scripts/ema_grid_search.py --slice-bars 20000

Output:
    scripts/ema_grid_results.csv
"""

import argparse
import csv
import subprocess
import sys
from pathlib import Path
from datetime import datetime

def main():
    parser = argparse.ArgumentParser(description='EMA Exit Grid Search')
    parser.add_argument('--fast-start', type=int, default=5, help='Fast EMA start (default: 5)')
    parser.add_argument('--fast-end', type=int, default=21, help='Fast EMA end exclusive (default: 21)')
    parser.add_argument('--fast-step', type=int, default=3, help='Fast EMA step (default: 3)')
    parser.add_argument('--slow-start', type=int, default=15, help='Slow EMA start (default: 15)')
    parser.add_argument('--slow-end', type=int, default=61, help='Slow EMA end exclusive (default: 61)')
    parser.add_argument('--slow-step', type=int, default=5, help='Slow EMA step (default: 5)')
    parser.add_argument('--slice-bars', type=int, default=20000, help='Use last N bars only (0=all, default: 20000)')
    parser.add_argument('--config', type=Path, default=Path('conf/phase1_enhanced_v3_1_config_FIXED_more_trades.txt'))
    parser.add_argument('--data', type=Path, default=Path('data/LiveTest/live_data-DRYRUN.csv'))
    parser.add_argument('--exe', type=Path, default=Path('build/bin/Release/sd_trading_unified.exe'))
    parser.add_argument('--output', type=Path, default=Path('scripts/ema_grid_results.csv'))
    args = parser.parse_args()

    # Validate
    if not args.exe.exists():
        print(f"ERROR: Missing executable: {args.exe}")
        return 1
    if not args.config.exists():
        print(f"ERROR: Missing base config: {args.config}")
        return 1
    if not args.data.exists():
        print(f"ERROR: Missing data: {args.data}")
        return 1

    # Create temp data slice if needed
    data_path = args.data
    if args.slice_bars > 0:
        temp_slice_path = Path('data/LiveTest/_grid_temp_slice.csv')
        print(f"Creating data slice: last {args.slice_bars} bars...")
        with open(args.data, 'r', newline='', encoding='utf-8') as f_in:
            rows = list(csv.reader(f_in))
            header = rows[0]
            data_rows = rows[1:]
            slice_rows = data_rows[-args.slice_bars:] if len(data_rows) > args.slice_bars else data_rows
        with open(temp_slice_path, 'w', newline='', encoding='utf-8') as f_out:
            writer = csv.writer(f_out)
            writer.writerow(header)
            writer.writerows(slice_rows)
        data_path = temp_slice_path
        print(f"  Slice saved: {temp_slice_path} ({len(slice_rows)} bars)")

    # Generate pairs
    fast_vals = list(range(args.fast_start, args.fast_end, args.fast_step))
    slow_vals = list(range(args.slow_start, args.slow_end, args.slow_step))
    pairs = [(f, s) for f in fast_vals for s in slow_vals if f < s]
    print(f"\nTotal EMA pairs to test: {len(pairs)}")
    print(f"Starting at: {datetime.now()}\n")

    tmp_config_path = Path('conf/_grid_tmp_config.txt')
    base_lines = args.config.read_text(encoding='utf-8').splitlines()

    results = []
    for idx, (fast, slow) in enumerate(pairs, 1):
        # Update config
        lines = []
        found_fast = False
        found_slow = False
        for line in base_lines:
            stripped = line.strip()
            if stripped.startswith('exit_ema_fast_period'):
                lines.append(f"exit_ema_fast_period = {fast}")
                found_fast = True
            elif stripped.startswith('exit_ema_slow_period'):
                lines.append(f"exit_ema_slow_period = {slow}")
                found_slow = True
            else:
                lines.append(line)
        if not found_fast:
            lines.append(f"exit_ema_fast_period = {fast}")
        if not found_slow:
            lines.append(f"exit_ema_slow_period = {slow}")

        tmp_config_path.write_text('\n'.join(lines) + '\n', encoding='utf-8')

        # Run dryrun
        cmd = [str(args.exe), '--mode=dryrun', f'--config={tmp_config_path}', f'--data={data_path}', '--duration=0']
        completed = subprocess.run(cmd, capture_output=True, text=True)

        if completed.returncode != 0:
            results.append({
                'fast': fast,
                'slow': slow,
                'profit_factor': 0,
                'total_pnl': 0,
                'stop_loss_total': 0,
                'error': (completed.stderr or completed.stdout)[:200]
            })
            print(f"[{idx}/{len(pairs)}] fast={fast}, slow={slow} → ERROR")
            continue

        # Parse metrics
        metrics_path = Path('results/backtest/metrics.csv')
        trades_path = Path('results/backtest/trades.csv')
        if not metrics_path.exists() or not trades_path.exists():
            results.append({
                'fast': fast,
                'slow': slow,
                'profit_factor': 0,
                'total_pnl': 0,
                'stop_loss_total': 0,
                'error': 'missing output'
            })
            print(f"[{idx}/{len(pairs)}] fast={fast}, slow={slow} → ERROR (no output)")
            continue

        metrics = {}
        with metrics_path.open(newline='', encoding='utf-8') as f:
            reader = csv.reader(f)
            for row in reader:
                if len(row) >= 2:
                    metrics[row[0].strip()] = row[1].strip()

        profit_factor = float(metrics.get('Profit Factor', '0') or 0)
        total_pnl = float(metrics.get('Total P&L', '0') or 0)

        # Compute stop-loss total
        stop_loss_total = 0.0
        with trades_path.open(newline='', encoding='utf-8') as f:
            reader = csv.DictReader(f)
            for row in reader:
                reason = (row.get('Exit Reason') or '').strip().upper()
                if reason in {'STOP_LOSS', 'SL', 'TRAIL_SL'}:
                    stop_loss_total += float(row.get('P&L') or 0)

        results.append({
            'fast': fast,
            'slow': slow,
            'profit_factor': profit_factor,
            'total_pnl': total_pnl,
            'stop_loss_total': stop_loss_total,
            'error': ''
        })

        print(f"[{idx}/{len(pairs)}] fast={fast}, slow={slow} → PF={profit_factor:.2f}, StopLoss={stop_loss_total:.2f}, PnL={total_pnl:.2f}")

    # Rank results
    ranked = sorted(
        [r for r in results if not r['error']],
        key=lambda r: (r['profit_factor'], r['stop_loss_total'], r['total_pnl']),
        reverse=True
    )

    # Write output
    with open(args.output, 'w', newline='', encoding='utf-8') as f:
        writer = csv.DictWriter(f, fieldnames=['rank', 'fast', 'slow', 'profit_factor', 'total_pnl', 'stop_loss_total', 'error'])
        writer.writeheader()
        for rank, r in enumerate(ranked, 1):
            writer.writerow({
                'rank': rank,
                'fast': r['fast'],
                'slow': r['slow'],
                'profit_factor': round(r['profit_factor'], 2),
                'total_pnl': round(r['total_pnl'], 2),
                'stop_loss_total': round(r['stop_loss_total'], 2),
                'error': r.get('error', '')
            })
        # Also write errors at the end
        for r in results:
            if r.get('error'):
                writer.writerow({
                    'rank': '',
                    'fast': r['fast'],
                    'slow': r['slow'],
                    'profit_factor': '',
                    'total_pnl': '',
                    'stop_loss_total': '',
                    'error': r['error']
                })

    print(f"\n{'='*60}")
    print(f"Grid search complete!")
    print(f"Results saved to: {args.output}")
    print(f"Finished at: {datetime.now()}")
    print(f"{'='*60}\n")

    print("Top 10 EMA pairs:")
    for rank, r in enumerate(ranked[:10], 1):
        print(f"{rank}. fast={r['fast']}, slow={r['slow']} | PF={r['profit_factor']:.2f} | StopLoss={r['stop_loss_total']:.2f} | PnL={r['total_pnl']:.2f}")

    errors = [r for r in results if r.get('error')]
    if errors:
        print(f"\nError count: {len(errors)}")

    print(f"\nFull results: {args.output}")
    return 0

if __name__ == '__main__':
    sys.exit(main())
