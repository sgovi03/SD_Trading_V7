#!/usr/bin/env python3
"""Analyze which zone score components predict winning trades"""

import pandas as pd
import numpy as np
import os

# Get the script's directory (project root)
script_dir = os.path.dirname(os.path.abspath(__file__))
trades_file = os.path.join(script_dir, 'results', 'live_trades_after_gap_analysis', 'trades.csv')

print("===== ZONE SCORE PREDICTIVENESS ANALYSIS =====")
print()

# Read trades CSV
trades_df = pd.read_csv(trades_file)

# Create binary win/loss column
trades_df['Win'] = (trades_df['P&L'] > 0).astype(int)

print(f"Total trades analyzed: {len(trades_df)}")
print(f"Winning trades: {trades_df['Win'].sum()} ({trades_df['Win'].mean()*100:.1f}%)")
print(f"Losing trades: {(1-trades_df['Win']).sum()} ({(1-trades_df['Win'].mean())*100:.1f}%)")
print()

# Extract score components
# Based on trades.csv structure, we have columns like:
# Zone Score, Entry %, etc.

print("Available columns in trades.csv:")
print(trades_df.columns.tolist())
print()

# Analyze correlations
print("=" * 80)
print("CORRELATION ANALYSIS: Score Components vs P&L")
print("=" * 80)
print()

score_columns = []
for col in trades_df.columns:
    if 'Score' in col or 'score' in col:
        score_columns.append(col)

if len(score_columns) == 0:
    print("WARNING: No score component columns found!")
    print("Available columns:", trades_df.columns.tolist())
else:
    print("Analyzing score components:")
    for col in score_columns:
        print(f"  - {col}")
    print()

correlation_results = []

# Analyze each score component
for col in score_columns:
    if col in trades_df.columns:
        # Skip non-numeric columns (like "Score Rationale")
        if not pd.api.types.is_numeric_dtype(trades_df[col]):
            print(f"⚠️  {col}: Skipping (non-numeric)")
            continue
            
        # Remove NaN values
        valid_data = trades_df[[col, 'P&L', 'Win']].dropna()
        
        if len(valid_data) < 10:
            print(f"⚠️  {col}: Insufficient data (n={len(valid_data)})")
            continue
        
        # Calculate Pearson correlation (using pandas corr)
        correlation_matrix = valid_data[[col, 'P&L', 'Win']].corr()
        pearson_pnl = correlation_matrix.loc[col, 'P&L']
        pearson_win = correlation_matrix.loc[col, 'Win']
        
        # Calculate p-values manually (simple approximation)
        n = len(valid_data)
        # For correlation significance: t = r * sqrt(n-2) / sqrt(1-r^2)
        # Then compare to t-distribution with n-2 df
        # Simplified: if |r| > 1.96/sqrt(n), it's significant at p<0.05
        critical_value = 1.96 / np.sqrt(n)
        pearson_pval = 0.05 if abs(pearson_win) > critical_value else 0.20
        
        # Calculate mean values for winners vs losers
        winners = valid_data[valid_data['Win'] == 1]
        losers = valid_data[valid_data['Win'] == 0]
        
        mean_winners = winners[col].mean()
        mean_losers = losers[col].mean()
        diff = mean_winners - mean_losers
        
        correlation_results.append({
            'component': col,
            'pearson_pnl': pearson_pnl,
            'pearson_pval': pearson_pval,
            'pearson_win': pearson_win,
            'mean_winners': mean_winners,
            'mean_losers': mean_losers,
            'difference': diff,
            'n': len(valid_data)
        })

# Sort by absolute Pearson correlation with Win
correlation_results.sort(key=lambda x: abs(x['pearson_win']), reverse=True)

print()
print("=" * 80)
print("RESULTS: Predictive Power of Score Components")
print("=" * 80)
print()

print(f"{'Component':<25} {'Corr(Win)':<12} {'P-value':<12} {'Mean(W)':<10} {'Mean(L)':<10} {'Diff':<10}")
print("-" * 80)

for result in correlation_results:
    comp = result['component'][:24]  # Truncate long names
    corr_win = result['pearson_win']
    pval = result['pearson_pval']
    mean_w = result['mean_winners']
    mean_l = result['mean_losers']
    diff = result['difference']
    
    # Determine predictiveness
    if abs(corr_win) > 0.15 and pval < 0.05:
        status = "✓ PREDICTIVE"
    elif abs(corr_win) > 0.08 and pval < 0.10:
        status = "⚠ WEAK"
    else:
        status = "✗ NOT PREDICTIVE"
    
    print(f"{comp:<25} {corr_win:>11.4f} {pval:>11.4f} {mean_w:>9.2f} {mean_l:>9.2f} {diff:>9.2f}  {status}")

print()
print("=" * 80)
print("INTERPRETATION GUIDE")
print("=" * 80)
print()
print("Correlation with Win (binary 0/1):")
print("  > 0.15 with p<0.05   → PREDICTIVE (higher score → more wins)")
print("  > 0.08 with p<0.10   → WEAK predictive power")
print("  < 0.08 or p>0.10     → NOT PREDICTIVE (random)")
print()
print("Mean Difference (Winners - Losers):")
print("  Positive difference  → Winners had higher values")
print("  Negative difference  → Winners had lower values")
print("  Near zero            → No separation between winners/losers")
print()

# Additional analysis: Check if total score is predictive
if 'Zone Score' in trades_df.columns:
    print("=" * 80)
    print("ZONE SCORE DISTRIBUTION ANALYSIS")
    print("=" * 80)
    print()
    
    # Bin scores into ranges
    bins = [0, 50, 60, 70, 80, 100]
    labels = ['0-50', '50-60', '60-70', '70-80', '80+']
    trades_df['Score_Bin'] = pd.cut(trades_df['Zone Score'], bins=bins, labels=labels)
    
    print(f"{'Score Range':<12} {'Trades':<8} {'Wins':<8} {'Losses':<8} {'Win Rate':<10} {'Avg P&L':<12}")
    print("-" * 70)
    
    for bin_label in labels:
        bin_trades = trades_df[trades_df['Score_Bin'] == bin_label]
        if len(bin_trades) > 0:
            wins = bin_trades['Win'].sum()
            losses = len(bin_trades) - wins
            win_rate = (wins / len(bin_trades)) * 100
            avg_pnl = bin_trades['P&L'].mean()
            
            print(f"{bin_label:<12} {len(bin_trades):<8} {wins:<8} {losses:<8} {win_rate:<9.1f}% {avg_pnl:<12.2f}")
    
    print()

print()
print("=" * 80)
print("RECOMMENDATIONS")
print("=" * 80)
print()

# Identify components to remove
non_predictive = [r for r in correlation_results if abs(r['pearson_win']) < 0.08 or r['pearson_pval'] > 0.10]
predictive = [r for r in correlation_results if abs(r['pearson_win']) > 0.15 and r['pearson_pval'] < 0.05]

if len(non_predictive) > 0:
    print("Components to REMOVE (not predictive):")
    for r in non_predictive:
        print(f"  ✗ {r['component']}: correlation={r['pearson_win']:.4f}, p={r['pearson_pval']:.4f}")
    print()

if len(predictive) > 0:
    print("Components to KEEP (predictive):")
    for r in predictive:
        print(f"  ✓ {r['component']}: correlation={r['pearson_win']:.4f}, p={r['pearson_pval']:.4f}")
    print()
else:
    print("⚠️  WARNING: NO COMPONENTS ARE PREDICTIVE!")
    print("   This suggests the entire scoring system needs redesign.")
    print()

print("Next steps:")
print("  1. Review non-predictive components and consider removal")
print("  2. Increase weights on predictive components")
print("  3. Consider adding new components based on winning trade characteristics")
print("  4. Test revised scoring on historical data")
print()
