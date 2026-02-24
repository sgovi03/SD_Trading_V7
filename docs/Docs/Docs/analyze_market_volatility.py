#!/usr/bin/env python3
"""Analyze actual price movements and market volatility during live trading period"""

import pandas as pd
import os
from datetime import datetime, timedelta

# Get the script's directory (project root)
script_dir = os.path.dirname(os.path.abspath(__file__))

# Check which CSV file we're using
trades_file = os.path.join(script_dir, 'results', 'live_trades', 'trades.csv')
live_data_file = os.path.join(script_dir, 'data', 'LiveTest', 'live_data-DRYRUN.csv')

print("===== MARKET VOLATILITY & MOVEMENT ANALYSIS =====")
print()

# Load live data
print(f"Loading live data from: {live_data_file}")
try:
    live_df = pd.read_csv(live_data_file)
    live_df['DateTime'] = pd.to_datetime(live_df['DateTime'])
    live_df = live_df.sort_values('DateTime')
    print(f"✓ Loaded {len(live_df)} bars")
    print()
except Exception as e:
    print(f"ERROR loading live data: {e}")
    exit(1)

# Load trades
trades_df = pd.read_csv(trades_file)
trades_df['Entry Date'] = pd.to_datetime(trades_df['Entry Date'])
trades_df = trades_df.sort_values('Entry Date')

# Find the trading period in the live data
first_trade_date = trades_df['Entry Date'].iloc[0]
last_trade_date = trades_df['Entry Date'].iloc[-1]

print(f"Trading period: {first_trade_date} to {last_trade_date}")
print()

# Filter live data to trading period
period_start = first_trade_date - timedelta(hours=1)  # Start 1 hour before first trade
period_end = last_trade_date + timedelta(hours=1)    # End 1 hour after last trade
trading_period_df = live_df[(live_df['DateTime'] >= period_start) & (live_df['DateTime'] <= period_end)]

print(f"Live data in trading period:")
print(f"  Bars available: {len(trading_period_df)}")
print(f"  Time range: {trading_period_df['DateTime'].iloc[0]} to {trading_period_df['DateTime'].iloc[-1]}")
print()

if len(trading_period_df) == 0:
    print("ERROR: No live data found in trading period!")
    exit(1)

# Analyze 50-bar windows
print("===== 50-BAR WINDOW ANALYSIS =====")
print()

price_changes = []
lookback = 50

for i in range(lookback, len(trading_period_df)):
    start_price = trading_period_df.iloc[i - lookback]['Close']
    end_price = trading_period_df.iloc[i]['Close']
    pct_change = ((end_price - start_price) / start_price) * 100.0
    
    # Also track high/low within window
    window_high = trading_period_df.iloc[i - lookback:i + 1]['High'].max()
    window_low = trading_period_df.iloc[i - lookback:i + 1]['Low'].min()
    range_pct = ((window_high - window_low) / start_price) * 100.0
    
    price_changes.append({
        'datetime': trading_period_df.iloc[i]['DateTime'],
        'start_price': start_price,
        'end_price': end_price,
        'pct_change': pct_change,
        'window_range_pct': range_pct,
        'high': window_high,
        'low': window_low
    })

pc_df = pd.DataFrame(price_changes)

print(f"Total 50-bar windows analyzed: {len(pc_df)}")
print()

# Count regimes that would be detected
bull_windows = len(pc_df[pc_df['pct_change'] > 5.0])
bear_windows = len(pc_df[pc_df['pct_change'] < -5.0])
ranging_windows = len(pc_df[(pc_df['pct_change'] >= -5.0) & (pc_df['pct_change'] <= 5.0)])

print(f"Regime classification (using 5% threshold):")
print(f"  BULL (pct_change > 5.0%):  {bull_windows:5d} windows ({bull_windows/len(pc_df)*100:5.1f}%)")
print(f"  BEAR (pct_change < -5.0%): {bear_windows:5d} windows ({bear_windows/len(pc_df)*100:5.1f}%)")
print(f"  RANGING:                    {ranging_windows:5d} windows ({ranging_windows/len(pc_df)*100:5.1f}%)")
print()

print(f"Price change statistics (over 50-bar windows):")
print(f"  Min:    {pc_df['pct_change'].min():7.3f}%")
print(f"  Max:    {pc_df['pct_change'].max():7.3f}%")
print(f"  Mean:   {pc_df['pct_change'].mean():7.3f}%")
print(f"  Median: {pc_df['pct_change'].median():7.3f}%")
print(f"  Std:    {pc_df['pct_change'].std():7.3f}%")
print()

print(f"Window range statistics (high-low within 50-bar window):")
print(f"  Min:    {pc_df['window_range_pct'].min():7.3f}%")
print(f"  Max:    {pc_df['window_range_pct'].max():7.3f}%")
print(f"  Mean:   {pc_df['window_range_pct'].mean():7.3f}%")
print(f"  Median: {pc_df['window_range_pct'].median():7.3f}%")
print()

# Show percentile distribution
print(f"Percentile distribution of price changes:")
for pct in [5, 25, 50, 75, 95]:
    value = pc_df['pct_change'].quantile(pct / 100.0)
    print(f"  {pct:3d}th percentile: {value:7.3f}%")
print()

# Verify the threshold makes sense
print("=" * 60)
print("THRESHOLD ANALYSIS")
print("=" * 60)
print()
print("Expected behavior if threshold is appropriate:")
print(f"  ✓ Most windows should be -5% to +5% (RANGING)")
print(f"  ✓ Only extreme moves should exceed ±5%")
print()
print(f"Actual behavior:")
if ranging_windows / len(pc_df) > 0.95:
    print(f"  ✗ {ranging_windows/len(pc_df)*100:.1f}% windows are RANGING (almost all!)")
    print(f"  → This could mean:")
    print(f"    1. Market was genuinely very ranging (possible but unlikely)")
    print(f"    2. 5% threshold is too high for 50-minute windows")
    print(f"    3. Bar history has limited/consolidated data")
else:
    print(f"  ✓ {ranging_windows/len(pc_df)*100:.1f}% windows are RANGING (reasonable)")

print()
print("=" * 60)
print("RECOMMENDATION")
print("=" * 60)
print()

# If market is truly ranging, maybe we should use different threshold
if ranging_windows / len(pc_df) > 0.95:
    print("The market during the trading period was genuinely RANGING.")
    print()
    print("Options:")
    print("  A. Accept RANGING regime detection (current behavior)")
    print("     → All trades in RANGING market")
    print("     → TradeRules for RANGING should be stricter")
    print()
    print("  B. Lower threshold to detect finer-grained trends")
    print("     → Use 2-3% threshold instead of 5%")
    print("     → Would provide BULL/BEAR detection even in choppy market")
    print()
    print(f"  C. Use volatility-relative threshold")
    print(f"     → Calculate ATR and use ATR-based threshold")
    print(f"     → Adapts to market conditions")
else:
    print("The market showed reasonable trends.")
    print("Actual regime distribution should match calculations above.")

print()
print(f"Analysis Date: {pd.Timestamp.now()}")
