#!/usr/bin/env python3
"""Analyze trades by has_profitable_structure flag"""

import json
import pandas as pd
import os

# Get the script's directory (project root)
script_dir = os.path.dirname(os.path.abspath(__file__))
zones_file = os.path.join(script_dir, 'results', 'live_trades', 'zones_live_master.json')
trades_file = os.path.join(script_dir, 'results', 'live_trades', 'trades.csv')

# Read zones JSON
with open(zones_file, 'r') as f:
    zones_data = json.load(f)

# Get zones with has_profitable_structure = false vs true
false_structure_zones = set()
true_structure_zones = set()

for zone in zones_data['zones']:
    if zone['has_profitable_structure'] == False:
        false_structure_zones.add(zone['zone_id'])
    else:
        true_structure_zones.add(zone['zone_id'])

# Read trades CSV
trades_df = pd.read_csv(trades_file)

# Analyze trades with has_profitable_structure = false
false_trades = trades_df[trades_df['Zone ID'].isin(false_structure_zones)]
true_trades = trades_df[trades_df['Zone ID'].isin(true_structure_zones)]

# Calculate statistics
false_wins = len(false_trades[false_trades['P&L'] > 0])
false_losses = len(false_trades[false_trades['P&L'] <= 0])
false_win_rate = (false_wins / len(false_trades) * 100) if len(false_trades) > 0 else 0

true_wins = len(true_trades[true_trades['P&L'] > 0])
true_losses = len(true_trades[true_trades['P&L'] <= 0])
true_win_rate = (true_wins / len(true_trades) * 100) if len(true_trades) > 0 else 0

print('===== ZONES WITH has_profitable_structure = FALSE =====')
print(f'Total zones: {len(false_structure_zones)}')
print(f'Trades using these zones: {len(false_trades)}')
print(f'  Winning trades: {false_wins}')
print(f'  Losing trades: {false_losses}')
print(f'  Win rate: {false_win_rate:.2f}%')
print(f'  Avg P&L per trade: {false_trades["P&L"].mean():.2f}')
print(f'  Total P&L: {false_trades["P&L"].sum():.2f}')
print()
print('===== ZONES WITH has_profitable_structure = TRUE =====')
print(f'Total zones: {len(true_structure_zones)}')
print(f'Trades using these zones: {len(true_trades)}')
print(f'  Winning trades: {true_wins}')
print(f'  Losing trades: {true_losses}')
print(f'  Win rate: {true_win_rate:.2f}%')
print(f'  Avg P&L per trade: {true_trades["P&L"].mean():.2f}')
print(f'  Total P&L: {true_trades["P&L"].sum():.2f}')
print()
print('===== COMPARISON =====')
print(f'Difference in win rate: {true_win_rate - false_win_rate:.2f}%')
diff_pl = true_trades["P&L"].mean() - false_trades["P&L"].mean()
print(f'Difference in avg P&L: {diff_pl:.2f}')
print()
print('===== VERDICT =====')
if true_win_rate > false_win_rate:
    print(f'✅ has_profitable_structure = TRUE is BETTER by {true_win_rate - false_win_rate:.2f}% win rate')
    print(f'   This proves the gate IS working to filter better trades')
else:
    print(f'❌ has_profitable_structure = FALSE has BETTER win rate ({false_win_rate:.2f}% vs {true_win_rate:.2f}%)')
    print(f'   This suggests the gate is rejecting good trades!')
