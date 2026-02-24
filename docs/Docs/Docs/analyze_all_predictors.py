#!/usr/bin/env python3
"""Analyze ALL numeric columns to find what actually predicts wins"""

import pandas as pd
import numpy as np
import os

# Get the script's directory (project root)
script_dir = os.path.dirname(os.path.abspath(__file__))
trades_file = os.path.join(script_dir, 'results', 'live_trades_after_gap_analysis', 'trades.csv')

print("=" * 80)
print("COMPREHENSIVE PREDICTOR ANALYSIS - Finding What Actually Works")
print("=" * 80)
print()

# Read trades CSV
trades_df = pd.read_csv(trades_file)
trades_df['Win'] = (trades_df['P&L'] > 0).astype(int)

print(f"Dataset: {len(trades_df)} trades, {trades_df['Win'].sum()} wins ({trades_df['Win'].mean()*100:.1f}%)")
print()

# Get all numeric columns
numeric_cols = trades_df.select_dtypes(include=[np.number]).columns.tolist()
# Remove target and ID columns
exclude_cols = ['Trade#', 'P&L', 'Win', 'Return %', 'Position Size', 'Entry Price', 'Exit Price']
predictor_cols = [col for col in numeric_cols if col not in exclude_cols]

print(f"Analyzing {len(predictor_cols)} numeric predictors...")
print()

results = []

for col in predictor_cols:
    valid_data = trades_df[[col, 'P&L', 'Win']].dropna()
    
    if len(valid_data) < 10:
        continue
    
    # Correlation
    corr_matrix = valid_data[[col, 'Win']].corr()
    corr_win = corr_matrix.loc[col, 'Win']
    
    # Check if correlation is actually meaningful
    n = len(valid_data)
    critical_value = 1.96 / np.sqrt(n)
    is_significant = abs(corr_win) > critical_value
    
    # Mean values
    winners = valid_data[valid_data['Win'] == 1]
    losers = valid_data[valid_data['Win'] == 0]
    
    mean_w = winners[col].mean()
    mean_l = losers[col].mean()
    diff = mean_w - mean_l
    
    # Z-score for difference
    if len(winners) > 0 and len(losers) > 0:
        std_w = winners[col].std()
        std_l = losers[col].std()
        pooled_std = np.sqrt((std_w**2 + std_l**2) / 2)
        if pooled_std > 0:
            z_score = diff / pooled_std
        else:
            z_score = 0
    else:
        z_score = 0
    
    results.append({
        'predictor': col,
        'corr': corr_win,
        'significant': is_significant,
        'mean_winners': mean_w,
        'mean_losers': mean_l,
        'difference': diff,
        'z_score': z_score,
        'abs_corr': abs(corr_win)
    })

# Sort by absolute correlation
results_df = pd.DataFrame(results)
results_df = results_df.sort_values('abs_corr', ascending=False)

print("=" * 100)
print("TOP PREDICTORS (sorted by |correlation|)")
print("=" * 100)
print()
print(f"{'Predictor':<25} {'Corr':<10} {'Sig?':<6} {'Mean(W)':<10} {'Mean(L)':<10} {'Diff':<10} {'Z-Score':<10}")
print("-" * 100)

for _, row in results_df.head(20).iterrows():
    pred = row['predictor'][:24]
    corr = row['corr']
    sig = "✓ YES" if row['significant'] else "✗ NO"
    mean_w = row['mean_winners']
    mean_l = row['mean_losers']
    diff = row['difference']
    z = row['z_score']
    
    # Color coding
    if abs(corr) > 0.15 and row['significant']:
        status = "🟢 STRONG"
    elif abs(corr) > 0.10 and row['significant']:
        status = "🟡 MODERATE"
    elif abs(corr) > 0.08:
        status = "🟠 WEAK"
    else:
        status = "🔴 NONE"
    
    print(f"{pred:<25} {corr:>9.4f} {sig:<6} {mean_w:>9.2f} {mean_l:>9.2f} {diff:>9.2f} {z:>9.2f}  {status}")

print()
print("=" * 100)
print("CRITICAL FINDINGS")
print("=" * 100)
print()

# Check if zone scores are broken
zone_score_corr = results_df[results_df['predictor'] == 'Zone Score']['corr'].values
if len(zone_score_corr) > 0:
    if abs(zone_score_corr[0]) < 0.08:
        print("🚨 CRITICAL: Zone Score has NO predictive power!")
        print(f"   Correlation: {zone_score_corr[0]:.4f}")
        print("   This means zone scoring system is fundamentally broken.")
        print()

# Identify actual predictors
strong_predictors = results_df[(results_df['abs_corr'] > 0.15) & (results_df['significant'])]
moderate_predictors = results_df[(results_df['abs_corr'] > 0.10) & (results_df['abs_corr'] <= 0.15) & (results_df['significant'])]

if len(strong_predictors) > 0:
    print("✅ STRONG PREDICTORS FOUND:")
    for _, row in strong_predictors.iterrows():
        direction = "higher" if row['difference'] > 0 else "lower"
        print(f"   • {row['predictor']}: Winners have {direction} values (corr={row['corr']:.4f})")
    print()

if len(moderate_predictors) > 0:
    print("⚠️  MODERATE PREDICTORS:")
    for _, row in moderate_predictors.iterrows():
        direction = "higher" if row['difference'] > 0 else "lower"
        print(f"   • {row['predictor']}: Winners have {direction} values (corr={row['corr']:.4f})")
    print()

if len(strong_predictors) == 0 and len(moderate_predictors) == 0:
    print("❌ NO STRONG OR MODERATE PREDICTORS FOUND")
    print()
    print("This indicates:")
    print("  1. Wins/losses are nearly random with respect to indicators")
    print("  2. Entry rules may be filtering out good signals")
    print("  3. Exit strategy might be the real issue (stop loss too tight?)")
    print("  4. Market regime (100% RANGING) may not match strategy design")
    print()

print()
print("=" * 100)
print("ACTIONABLE RECOMMENDATIONS")
print("=" * 100)
print()

# Find best separator
best_predictor = results_df.iloc[0]
print(f"1. BEST SINGLE PREDICTOR: {best_predictor['predictor']}")
print(f"   Correlation: {best_predictor['corr']:.4f}")
print(f"   Winners avg: {best_predictor['mean_winners']:.2f}")
print(f"   Losers avg:  {best_predictor['mean_losers']:.2f}")
print()

if abs(best_predictor['corr']) < 0.08:
    print("   ⚠️  WARNING: Even best predictor is weak!")
    print("   Consider:")
    print("     • Review entry timing (may be entering too early/late)")
    print("     • Check stop loss placement (too tight = forced losses)")
    print("     • Analyze exit rules (letting winners run?)")
    print("     • Test with TradeRules disabled to see if filtering is the problem")
else:
    print(f"   ✅ Use {best_predictor['predictor']} as primary filter")
    threshold = (best_predictor['mean_winners'] + best_predictor['mean_losers']) / 2
    if best_predictor['difference'] > 0:
        print(f"   Only trade if {best_predictor['predictor']} > {threshold:.2f}")
    else:
        print(f"   Only trade if {best_predictor['predictor']} < {threshold:.2f}")

print()
print("2. CURRENT ZONE SCORE:")
zone_score_row = results_df[results_df['predictor'] == 'Zone Score']
if len(zone_score_row) > 0:
    zs = zone_score_row.iloc[0]
    print(f"   Correlation: {zs['corr']:.4f}")
    print(f"   Predictive power: {'NONE' if abs(zs['corr']) < 0.08 else 'WEAK' if abs(zs['corr']) < 0.15 else 'MODERATE'}")
    print()
    if abs(zs['corr']) < 0.08:
        print("   ACTION: REBUILD SCORING SYSTEM")
        print("   Current weights are not working. Need to:")
        print("     • Identify which raw features (RSI, ADX, BB, etc.) actually predict wins")
        print("     • Rebuild score as weighted combination of ONLY predictive features")
        print("     • Remove all non-predictive components")

print()
print("=" * 100)
print("Analysis complete - review findings above")
print("=" * 100)
