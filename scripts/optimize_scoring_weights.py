"""
Scoring Engine Optimization - Test different weight combinations and backtest improvements
"""
import json
import csv
import pandas as pd
from collections import defaultdict
import itertools

def load_profitability_data():
    """Load the profitability analysis CSV"""
    df = pd.read_csv('D:/ClaudeAi/Unified_Engines/SD_Trading_V4_FIxing/results/zone_profitability_analysis.csv')
    return df

def load_original_zones():
    """Load original zones with original scores"""
    with open('D:/ClaudeAi/Unified_Engines/SD_Trading_V4_FIxing/results/backtest/zones_backtest_20260103.json', 'r') as f:
        data = json.load(f)
    
    zones_map = {}
    for zone in data['zones']:
        zones_map[zone['zone_id']] = zone
    return zones_map

def load_trades():
    """Load original trades"""
    trades = []
    with open('D:/ClaudeAi/Unified_Engines/SD_Trading_V4_FIxing/results/backtest/trades.csv', 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            if not row.get('P&L'):
                continue
            try:
                trade = {
                    'trade_num': int(row['Trade#']),
                    'zone_score': float(row['Zone Score']),
                    'entry_price': float(row['Entry Price']),
                    'pnl': float(row['P&L']),
                    'exit_reason': row['Exit Reason'],
                    'zone_id': row.get('Zone_ID', ''),
                }
                trades.append(trade)
            except:
                continue
    return trades

def apply_scoring_adjustments(df, zone_type_mult_demand=1.0, zone_type_mult_supply=1.0, 
                              strength_bonus=0, touch_penalty=0, strength_range=(0, 100)):
    """
    Apply scoring adjustments to the profitability data
    
    zone_type_mult_demand: multiplier for DEMAND zones (default 1.0)
    zone_type_mult_supply: multiplier for SUPPLY zones (default 1.0)
    strength_bonus: bonus for zones in strength_range (e.g., 0.2 = 20% bonus)
    touch_penalty: penalty for zones with 5+ touches (e.g., -0.1 = 10% penalty)
    strength_range: tuple of (min, max) strength to apply bonus
    """
    adjusted_df = df.copy()
    adjusted_scores = []
    
    for idx, row in adjusted_df.iterrows():
        new_score = float(row['Zone_Score'])
        
        # Apply zone type multiplier
        if row['Zone_Type'] == 'DEMAND':
            new_score *= zone_type_mult_demand
        else:
            new_score *= zone_type_mult_supply
        
        # Apply strength bonus
        if strength_range[0] <= row['Zone_Strength'] <= strength_range[1]:
            new_score *= (1.0 + strength_bonus)
        
        # Apply touch penalty/bonus
        if row['Touches'] >= 5:
            new_score *= (1.0 + touch_penalty)
        
        adjusted_scores.append(new_score)
    
    adjusted_df['Adjusted_Zone_Score'] = adjusted_scores
    return adjusted_df

def calculate_backtest_results(df, trades):
    """
    Simulate trading with new zone scores
    Hypothesis: Trades with higher zone scores should be more profitable
    """
    total_pnl = 0
    profitable_trades = 0
    total_trades = 0
    trade_details = []
    
    for trade in trades:
        # Find corresponding profitability record
        matching_records = df[df['Trade#'] == trade['trade_num']]
        
        if len(matching_records) == 0:
            continue
        
        record = matching_records.iloc[0]
        adjusted_score = record['Adjusted_Zone_Score']
        original_pnl = trade['pnl']
        
        # Simulate: Apply position sizing based on adjusted score
        # Higher scores = larger positions (more aggressive)
        # Score range: typically 20-90
        confidence_factor = (adjusted_score - 20) / 70  # Normalize to 0-1
        confidence_factor = max(0, min(1, confidence_factor))  # Clamp 0-1
        
        # Position sizing: 0.5x to 1.5x based on confidence
        position_multiplier = 0.5 + (confidence_factor * 1.0)
        
        # Apply position sizing to P&L
        adjusted_pnl = original_pnl * position_multiplier
        
        total_pnl += adjusted_pnl
        total_trades += 1
        if adjusted_pnl > 0:
            profitable_trades += 1
        
        trade_details.append({
            'trade_num': trade['trade_num'],
            'original_score': trade['zone_score'],
            'adjusted_score': adjusted_score,
            'confidence': confidence_factor,
            'position_mult': position_multiplier,
            'original_pnl': original_pnl,
            'adjusted_pnl': adjusted_pnl
        })
    
    win_rate = (profitable_trades / total_trades * 100) if total_trades > 0 else 0
    
    return {
        'total_pnl': total_pnl,
        'total_trades': total_trades,
        'profitable_trades': profitable_trades,
        'win_rate': win_rate,
        'avg_pnl': total_pnl / total_trades if total_trades > 0 else 0,
        'trades': trade_details
    }

def run_optimization_tests():
    """Run multiple optimization scenarios"""
    
    print("\n" + "="*100)
    print("SCORING ENGINE OPTIMIZATION - BACKTEST EXPERIMENTS")
    print("="*100 + "\n")
    
    df = load_profitability_data()
    trades = load_trades()
    zones_map = load_original_zones()
    
    # Original baseline
    original_pnl = sum(t['pnl'] for t in trades)
    original_wins = sum(1 for t in trades if t['pnl'] > 0)
    original_win_rate = original_wins / len(trades) * 100
    
    print("ORIGINAL BASELINE:")
    print(f"  Total P&L: ${original_pnl:,.2f}")
    print(f"  Profitable trades: {original_wins}/{len(trades)} ({original_win_rate:.1f}%)")
    print(f"  Avg P&L per trade: ${original_pnl/len(trades):,.2f}\n")
    
    # Test scenarios based on our analysis
    scenarios = [
        {
            'name': 'Scenario 1: Boost DEMAND zones (our strongest finding)',
            'params': {
                'zone_type_mult_demand': 1.3,
                'zone_type_mult_supply': 0.5,
                'strength_bonus': 0,
                'touch_penalty': 0,
                'strength_range': (0, 100)
            }
        },
        {
            'name': 'Scenario 2: Strength-based optimization (60-70 range)',
            'params': {
                'zone_type_mult_demand': 1.1,
                'zone_type_mult_supply': 0.8,
                'strength_bonus': 0.25,
                'touch_penalty': 0,
                'strength_range': (60, 70)
            }
        },
        {
            'name': 'Scenario 3: Penalize over-touched zones (5+ touches)',
            'params': {
                'zone_type_mult_demand': 1.2,
                'zone_type_mult_supply': 0.6,
                'strength_bonus': 0.15,
                'touch_penalty': -0.20,
                'strength_range': (60, 75)
            }
        },
        {
            'name': 'Scenario 4: Aggressive DEMAND + Strength + Touch balance',
            'params': {
                'zone_type_mult_demand': 1.4,
                'zone_type_mult_supply': 0.4,
                'strength_bonus': 0.20,
                'touch_penalty': -0.15,
                'strength_range': (60, 70)
            }
        },
        {
            'name': 'Scenario 5: Conservative approach (smaller adjustments)',
            'params': {
                'zone_type_mult_demand': 1.15,
                'zone_type_mult_supply': 0.85,
                'strength_bonus': 0.10,
                'touch_penalty': -0.10,
                'strength_range': (60, 70)
            }
        },
        {
            'name': 'Scenario 6: Strength-only optimization (minimal type bias)',
            'params': {
                'zone_type_mult_demand': 1.05,
                'zone_type_mult_supply': 0.95,
                'strength_bonus': 0.30,
                'touch_penalty': -0.25,
                'strength_range': (55, 75)
            }
        }
    ]
    
    results = []
    
    for scenario in scenarios:
        print("-" * 100)
        print(f"\n{scenario['name']}")
        print(f"Parameters:")
        print(f"  DEMAND weight: {scenario['params']['zone_type_mult_demand']:.2f}x")
        print(f"  SUPPLY weight: {scenario['params']['zone_type_mult_supply']:.2f}x")
        print(f"  Strength bonus: {scenario['params']['strength_bonus']*100:.0f}% for zones {scenario['params']['strength_range']}")
        print(f"  Touch penalty: {scenario['params']['touch_penalty']*100:.0f}% for 5+ touches")
        
        adjusted_df = apply_scoring_adjustments(df, **scenario['params'])
        backtest_results = calculate_backtest_results(adjusted_df, trades)
        
        improvement = backtest_results['total_pnl'] - original_pnl
        improvement_pct = (improvement / original_pnl * 100) if original_pnl > 0 else 0
        
        print(f"\nResults:")
        print(f"  Total P&L: ${backtest_results['total_pnl']:,.2f}")
        print(f"  Improvement: ${improvement:,.2f} ({improvement_pct:+.1f}%)")
        print(f"  Profitable trades: {backtest_results['profitable_trades']}/{backtest_results['total_trades']} ({backtest_results['win_rate']:.1f}%)")
        print(f"  Avg P&L per trade: ${backtest_results['avg_pnl']:,.2f}")
        
        results.append({
            'scenario': scenario['name'],
            'params': scenario['params'],
            'total_pnl': backtest_results['total_pnl'],
            'improvement': improvement,
            'improvement_pct': improvement_pct,
            'win_rate': backtest_results['win_rate'],
            'avg_pnl': backtest_results['avg_pnl'],
            'profitable_trades': backtest_results['profitable_trades']
        })
    
    # Rank scenarios
    print("\n" + "="*100)
    print("OPTIMIZATION RANKING (by improvement):")
    print("="*100 + "\n")
    
    results.sort(key=lambda x: x['improvement'], reverse=True)
    
    print(f"{'Rank':<6} {'Scenario':<50} {'P&L':<15} {'Improvement':<15} {'Win%':<8}")
    print("-" * 94)
    
    for i, result in enumerate(results, 1):
        scenario_name = result['scenario'].replace('Scenario ', 'S')
        print(f"{i:<6} {scenario_name:<50} ${result['total_pnl']:>12,.0f}   ${result['improvement']:>12,.0f}   {result['win_rate']:>6.1f}%")
    
    # Export detailed results
    export_optimization_results(results, df)
    
    # Best scenario analysis
    best = results[0]
    print("\n" + "="*100)
    print("RECOMMENDED IMPLEMENTATION:")
    print("="*100 + "\n")
    print(f"Best Scenario: {best['scenario']}")
    print(f"Expected improvement: ${best['improvement']:,.2f} ({best['improvement_pct']:+.1f}%)")
    print(f"New estimated P&L: ${best['total_pnl']:,.2f}")
    print(f"\nApply these weights to your scoring engine:")
    for key, value in best['params'].items():
        if key == 'strength_range':
            print(f"  {key}: {value}")
        elif key.startswith('zone_type'):
            zone_type = 'DEMAND' if 'demand' in key else 'SUPPLY'
            print(f"  {zone_type} multiplier: {value:.2f}x")
        else:
            print(f"  {key}: {value:+.0%}")

def export_optimization_results(results, df):
    """Export optimization results to CSV"""
    output_file = 'D:/ClaudeAi/Unified_Engines/SD_Trading_V4_FIxing/results/optimization_results.csv'
    
    with open(output_file, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow([
            'Rank', 'Scenario', 'Total_P&L', 'Improvement', 'Improvement_%',
            'Win_Rate_%', 'Profitable_Trades', 'DEMAND_Weight', 'SUPPLY_Weight',
            'Strength_Bonus_%', 'Touch_Penalty_%', 'Strength_Range_Min', 'Strength_Range_Max'
        ])
        
        for rank, result in enumerate(results, 1):
            params = result['params']
            writer.writerow([
                rank,
                result['scenario'],
                f"{result['total_pnl']:.2f}",
                f"{result['improvement']:.2f}",
                f"{result['improvement_pct']:.2f}",
                f"{result['win_rate']:.2f}",
                result['profitable_trades'],
                f"{params['zone_type_mult_demand']:.2f}",
                f"{params['zone_type_mult_supply']:.2f}",
                f"{params['strength_bonus']*100:.2f}",
                f"{params['touch_penalty']*100:.2f}",
                params['strength_range'][0],
                params['strength_range'][1]
            ])
    
    print(f"\n[+] Detailed optimization results exported to: optimization_results.csv")

if __name__ == '__main__':
    run_optimization_tests()
