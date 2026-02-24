#!/usr/bin/env python3
"""Simple test to see what columns exist"""

import pandas as pd
import os

script_dir = os.path.dirname(os.path.abspath(__file__))
trades_file = os.path.join(script_dir, 'results', 'live_trades_after_gap_analysis', 'trades.csv')

print(f"Loading: {trades_file}")
trades_df = pd.read_csv(trades_file)

print(f"Loaded {len(trades_df)} rows")
print()
print("Columns:")
for i, col in enumerate(trades_df.columns):
    print(f"  {i+1}. {col}")
print()

# Show first row
print("First row data:")
print(trades_df.iloc[0])
