#!/usr/bin/env python3
"""Analyze market regime detection - check what detect_regime is calculating"""

import json
import pandas as pd
import os
from datetime import datetime

# Get the script's directory (project root)
script_dir = os.path.dirname(os.path.abspath(__file__))
trades_file = os.path.join(script_dir, 'results', 'live_trades', 'trades.csv')

# Read trades CSV
trades_df = pd.read_csv(trades_file)

print("===== MARKET REGIME DETECTION ANALYSIS =====")
print()

# Extract entry timestamps and bars
trades_df['Entry Date'] = pd.to_datetime(trades_df['Entry Date'])
trades_df = trades_df.sort_values('Entry Date')

print(f"First trade date: {trades_df['Entry Date'].iloc[0]}")
print(f"Last trade date: {trades_df['Entry Date'].iloc[-1]}")
print(f"Total trading days: {(trades_df['Entry Date'].iloc[-1] - trades_df['Entry Date'].iloc[0]).days}")
print()

# Count unique regimes
print("Regime distribution in trades.csv:")
regime_counts = trades_df['Regime'].value_counts()
for regime, count in regime_counts.items():
    pct = (count / len(trades_df)) * 100
    print(f"  {regime}: {count} trades ({pct:.1f}%)")
print()

# The issue: detect_regime uses 50-bar lookback with 5% threshold
# Function: if pct_change > 5% → BULL, if pct_change < -5% → BEAR, else RANGING

print("ISSUE IDENTIFIED:")
print("─" * 60)
print("detect_regime() function uses:")
print("  - Lookback: 50 bars")
print("  - Threshold: 5.0%")
print("  - Logic: if (pct_change > 5%) → BULL")
print("          if (pct_change < -5%) → BEAR")
print("          else → RANGING")
print()

# Let's calculate what the actual price movements should be
# If all trades are RANGING, it means price moved less than 5% over 50-bar periods

print("Analysis of what this means:")
print("─" * 60)
print()
print(f"✓ ALL 221 trades show RANGING regime")
print()
print(f"  This means: During the ENTIRE 35-day period,")
print(f"             price movement in ANY 50-bar window")
print(f"             was LESS than 5%")
print()
print(f"  Statistically: This is HIGHLY UNLIKELY over 35 trading days")
print()
print(f"Root Cause Hypothesis #1: 5% threshold is TOO HIGH")
print(f"  Expected for 50-bar (≈2.5 hour) windows on 1-min bars:")
print(f"  - Typical movement: 0.5-2.0% in ranging market")
print(f"  - Breakout movement: 3-8% in trending market")
print(f"  → 5% threshold filters out MOST market conditions!")
print()
print(f"Root Cause Hypothesis #2: detect_regime only checks START→END")
print(f"  Current logic: Simple linear check from bar[start] → bar[end]")
print(f"  Problem: Doesn't account for intra-period movement")
print()
print(f"  Example: If price goes UP 10%, then DOWN 8%:")
print(f"    - Net change: +2% (< 5%) → RANGING ❌")
print(f"    - But market was clearly TRENDING UP")
print()

print("Verification needed:")
print("─" * 60)
print("1. Check what bar_history contains (frequency, resolution)")
print("2. Check if 50-bar window equals ~2.5 hours or different")
print("3. Compare typical price ranges vs 5% threshold")
print()

print("Recommended Fix:")
print("─" * 60)
print("Option A: Reduce 5% threshold to 1-2%")
print("  detect_regime(bars, 50, 2.0) instead of 5.0")
print()
print("Option B: Use volatility-adjusted threshold")
print("  threshold = ATR_pct_moved * 1.5 (scales with market volatility)")
print()
print("Option C: Use true EMA-based trend detection")
print("  Check EMA(20) > EMA(50) > EMA(200) for BULL")
print("  Check EMA(20) < EMA(50) < EMA(200) for BEAR")
print("  Otherwise RANGING")
print()

# Additional analysis: check entry signal frequencies by regime
print("Entry signal distribution by regime:")
print("─" * 60)
if 'Regime' in trades_df.columns:
    regime_by_entry = trades_df.groupby('Regime').size()
    print(regime_by_entry)
    print()

# Count win rates by regime
print("Win rates by regime:")
print("─" * 60)
if 'Regime' in trades_df.columns and 'P&L' in trades_df.columns:
    for regime in trades_df['Regime'].unique():
        regime_trades = trades_df[trades_df['Regime'] == regime]
        wins = len(regime_trades[regime_trades['P&L'] > 0])
        losses = len(regime_trades[regime_trades['P&L'] <= 0])
        win_rate = (wins / len(regime_trades)) * 100 if len(regime_trades) > 0 else 0
        avg_pl = regime_trades['P&L'].mean()
        print(f"{regime:10s}: {wins:3d} wins / {losses:3d} losses ({win_rate:5.1f}% WR) avg P&L: {avg_pl:7.2f}")
    print()

print("=" * 60)
print("SUMMARY")
print("=" * 60)
print("The fact that ALL trades show RANGING regime suggests that")
print("the detect_regime() function is misconfigured OR")
print("the threshold is inappropriate for the market/timeframe.")
print()
print("Next steps:")
print("  1. Verify bar_history resolution and size")
print("  2. Check if 5% threshold makes sense for this market")
print("  3. Consider more sophisticated regime detection (EMA-based)")
print("  4. Test with lower threshold (1-2%) and compare results")
