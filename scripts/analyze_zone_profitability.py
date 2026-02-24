"""
Zone Profitability Analysis - Maps trades to zones and identifies scoring patterns
"""
import json
import csv
from datetime import datetime
from collections import defaultdict
import statistics

def load_trades():
    """Load trades from backtest CSV"""
    trades = []
    with open('D:/ClaudeAi/Unified_Engines/SD_Trading_V4_FIxing/results/backtest/trades.csv', 'r') as f:
        reader = csv.DictReader(f)
        for row_num, row in enumerate(reader, 2):  # Start at 2 (after header)
            # Skip rows with missing critical data
            if not row.get('P&L') or not row.get('Trade#'):
                print(f"Warning: Skipping row {row_num} - missing P&L data")
                continue
            
            try:
                trade = {
                    'trade_num': int(row['Trade#']),
                    'direction': row['Direction'],
                    'entry_price': float(row['Entry Price']),
                    'exit_price': float(row['Exit Price']),
                    'pnl': float(row['P&L']),
                    'return_pct': float(row['Return %']),
                    'exit_reason': row['Exit Reason'],
                    'zone_score': float(row['Zone Score']),
                    'aggressiveness': float(row['Aggressiveness']),
                    'zone_formation': row['Zone Formation'],
                    'zone_distal': float(row['Zone Distal']),
                    'zone_proximal': float(row['Zone Proximal']),
                    'entry_date': row['Entry Date'],
                    'exit_date': row['Exit Date']
                }
                trades.append(trade)
            except (ValueError, TypeError) as e:
                print(f"Warning: Error parsing row {row_num}: {e}")
                continue
    
    return trades

def load_zones():
    """Load zones from backtest JSON"""
    with open('D:/ClaudeAi/Unified_Engines/SD_Trading_V4_FIxing/results/backtest/zones_backtest_20260103.json', 'r') as f:
        data = json.load(f)
    return data['zones']

def match_trade_to_zone(trade, zones):
    """
    Match a trade to a zone based on:
    1. Zone formation datetime proximity to trade entry
    2. Zone type matching trade direction
    3. Price level proximity
    """
    best_match = None
    best_score = -float('inf')
    
    # Infer zone type from trade direction (LONG trades use DEMAND zones, SHORT uses SUPPLY)
    if trade['direction'] == 'LONG':
        target_types = ['DEMAND']
    else:
        target_types = ['SUPPLY']
    
    trade_entry_dt = datetime.strptime(trade['entry_date'], '%Y-%m-%d %H:%M:%S')
    
    for zone in zones:
        # Only match zones of correct type
        if zone['type'] not in target_types:
            continue
        
        formation_dt = datetime.strptime(zone['formation_datetime'], '%Y-%m-%d %H:%M:%S')
        
        # Zone must form BEFORE trade entry
        if formation_dt >= trade_entry_dt:
            continue
        
        # Calculate time proximity (prefer recent zone formations)
        time_diff_minutes = (trade_entry_dt - formation_dt).total_seconds() / 60
        if time_diff_minutes > 10080:  # More than 1 week old, ignore
            continue
        
        # Calculate price proximity
        zone_mid = (zone['distal_line'] + zone['proximal_line']) / 2
        entry_price = trade['entry_price']
        price_diff = abs(entry_price - zone_mid)
        price_diff_pct = (price_diff / zone_mid) * 100
        
        # Score: recent zones with closer prices score higher
        time_score = 1.0 - (time_diff_minutes / 10080)  # 0-1 scale
        price_score = 1.0 - min(price_diff_pct / 2.0, 1.0)  # 0-1 scale (2% max tolerance)
        
        # Combine scores (weighted toward recent formation)
        match_score = (time_score * 0.6) + (price_score * 0.4)
        
        if match_score > best_score and match_score > 0.3:  # Minimum threshold
            best_score = match_score
            best_match = {
                'zone_id': zone['zone_id'],
                'zone_type': zone['type'],
                'formation_bar': zone['formation_bar'],
                'strength': zone['strength'],
                'touch_count': zone['touch_count'],
                'departure_imbalance': zone['departure_imbalance'],
                'retest_speed': zone['retest_speed'],
                'bars_to_retest': zone['bars_to_retest'],
                'state': zone['state'],
                'match_score': best_score
            }
    
    return best_match

def analyze_profitability_patterns(trades, zones):
    """Analyze profitability based on zone characteristics"""
    
    # Create zone lookup
    zone_map = {z['zone_id']: z for z in zones}
    
    # Match trades to zones
    trades_with_zones = []
    matched_count = 0
    
    for trade in trades:
        matched_zone = match_trade_to_zone(trade, zones)
        if matched_zone:
            matched_count += 1
            trades_with_zones.append({
                **trade,
                **matched_zone
            })
    
    print(f"\n{'='*80}")
    print(f"ZONE PROFITABILITY ANALYSIS - BACKTEST")
    print(f"{'='*80}")
    print(f"\nTrades Analysis:")
    print(f"  Total trades: {len(trades)}")
    print(f"  Matched to zones: {matched_count} ({matched_count/len(trades)*100:.1f}%)")
    print(f"  Unmatched: {len(trades) - matched_count}")
    
    if not trades_with_zones:
        print("No trades could be matched to zones.")
        return
    
    # Aggregate by zone
    zone_pnl = defaultdict(lambda: {'pnl_list': [], 'trades': [], 'count': 0, 'win_count': 0})
    
    for trade in trades_with_zones:
        zone_id = trade['zone_id']
        zone_pnl[zone_id]['pnl_list'].append(trade['pnl'])
        zone_pnl[zone_id]['trades'].append(trade)
        zone_pnl[zone_id]['count'] += 1
        if trade['pnl'] > 0:
            zone_pnl[zone_id]['win_count'] += 1
    
    # Calculate per-zone metrics
    print(f"\n{'='*80}")
    print("TOP 10 MOST PROFITABLE ZONES:")
    print(f"{'='*80}")
    print(f"{'Zone ID':<8} {'Type':<8} {'Trades':<8} {'Win%':<8} {'Avg P&L':<12} {'Total P&L':<12} {'Strength':<10} {'Touches':<8}")
    print(f"{'-'*88}")
    
    zone_stats = []
    for zone_id, data in zone_pnl.items():
        total_pnl = sum(data['pnl_list'])
        avg_pnl = total_pnl / data['count']
        win_pct = (data['win_count'] / data['count']) * 100
        
        zone_info = trades_with_zones[0]  # Get sample for zone type/strength
        for t in trades_with_zones:
            if t['zone_id'] == zone_id:
                zone_info = t
                break
        
        zone_stats.append({
            'zone_id': zone_id,
            'type': zone_info['zone_type'],
            'strength': zone_info['strength'],
            'touch_count': zone_info['touch_count'],
            'trade_count': data['count'],
            'win_pct': win_pct,
            'avg_pnl': avg_pnl,
            'total_pnl': total_pnl,
            'win_count': data['win_count']
        })
    
    # Sort by total P&L
    zone_stats.sort(key=lambda x: x['total_pnl'], reverse=True)
    
    for stat in zone_stats[:10]:
        print(f"{stat['zone_id']:<8} {stat['type']:<8} {stat['trade_count']:<8} {stat['win_pct']:<8.1f} {stat['avg_pnl']:<12.2f} {stat['total_pnl']:<12.2f} {stat['strength']:<10.2f} {stat['touch_count']:<8}")
    
    # Pattern analysis: Zone type profitability
    print(f"\n{'='*80}")
    print("ZONE TYPE PROFITABILITY:")
    print(f"{'='*80}")
    
    type_stats = defaultdict(lambda: {'pnl_list': [], 'count': 0, 'win_count': 0})
    for trade in trades_with_zones:
        zone_type = trade['zone_type']
        type_stats[zone_type]['pnl_list'].append(trade['pnl'])
        type_stats[zone_type]['count'] += 1
        if trade['pnl'] > 0:
            type_stats[zone_type]['win_count'] += 1
    
    print(f"{'Zone Type':<12} {'Trades':<10} {'Wins':<10} {'Win%':<10} {'Avg P&L':<12} {'Total P&L':<12}")
    print(f"{'-'*66}")
    for zone_type in ['DEMAND', 'SUPPLY']:
        if zone_type in type_stats:
            stats = type_stats[zone_type]
            total = sum(stats['pnl_list'])
            avg = total / stats['count']
            win_pct = (stats['win_count'] / stats['count']) * 100
            print(f"{zone_type:<12} {stats['count']:<10} {stats['win_count']:<10} {win_pct:<10.1f} {avg:<12.2f} {total:<12.2f}")
    
    # Zone strength correlation
    print(f"\n{'='*80}")
    print("ZONE STRENGTH CORRELATION WITH PROFITABILITY:")
    print(f"{'='*80}")
    
    strength_buckets = defaultdict(lambda: {'pnl_list': [], 'count': 0})
    for trade in trades_with_zones:
        strength = trade['strength']
        bucket = int(strength / 10) * 10  # 0-10, 10-20, 20-30, etc.
        strength_buckets[bucket]['pnl_list'].append(trade['pnl'])
        strength_buckets[bucket]['count'] += 1
    
    print(f"{'Strength Range':<20} {'Trades':<10} {'Avg P&L':<12} {'Win%':<10}")
    print(f"{'-'*52}")
    for bucket in sorted(strength_buckets.keys()):
        stats = strength_buckets[bucket]
        avg_pnl = sum(stats['pnl_list']) / stats['count']
        win_count = sum(1 for p in stats['pnl_list'] if p > 0)
        win_pct = (win_count / stats['count']) * 100
        print(f"{bucket}-{bucket+10:<18} {stats['count']:<10} {avg_pnl:<12.2f} {win_pct:<10.1f}")
    
    # Touch count correlation
    print(f"\n{'='*80}")
    print("TOUCH COUNT CORRELATION WITH PROFITABILITY:")
    print(f"{'='*80}")
    
    touch_buckets = defaultdict(lambda: {'pnl_list': [], 'count': 0})
    for trade in trades_with_zones:
        touches = trade['touch_count']
        if touches >= 5:
            bucket = '5+'
        else:
            bucket = str(touches)
        touch_buckets[bucket]['pnl_list'].append(trade['pnl'])
        touch_buckets[bucket]['count'] += 1
    
    print(f"{'Touch Count':<20} {'Trades':<10} {'Avg P&L':<12} {'Win%':<10}")
    print(f"{'-'*52}")
    for bucket in ['0', '1', '2', '3', '4', '5+']:
        if bucket in touch_buckets:
            stats = touch_buckets[bucket]
            avg_pnl = sum(stats['pnl_list']) / stats['count']
            win_count = sum(1 for p in stats['pnl_list'] if p > 0)
            win_pct = (win_count / stats['count']) * 100
            print(f"Touches={bucket:<16} {stats['count']:<10} {avg_pnl:<12.2f} {win_pct:<10.1f}")
    
    # Exit reason profitability
    print(f"\n{'='*80}")
    print("EXIT REASON PROFITABILITY:")
    print(f"{'='*80}")
    
    exit_reason_stats = defaultdict(lambda: {'pnl_list': [], 'count': 0})
    for trade in trades_with_zones:
        reason = trade['exit_reason']
        exit_reason_stats[reason]['pnl_list'].append(trade['pnl'])
        exit_reason_stats[reason]['count'] += 1
    
    print(f"{'Exit Reason':<15} {'Trades':<10} {'Avg P&L':<12} {'Total P&L':<12} {'Win%':<10}")
    print(f"{'-'*59}")
    for reason in ['TP', 'SL']:
        if reason in exit_reason_stats:
            stats = exit_reason_stats[reason]
            avg_pnl = sum(stats['pnl_list']) / stats['count']
            total_pnl = sum(stats['pnl_list'])
            win_count = sum(1 for p in stats['pnl_list'] if p > 0)
            win_pct = (win_count / stats['count']) * 100
            print(f"{reason:<15} {stats['count']:<10} {avg_pnl:<12.2f} {total_pnl:<12.2f} {win_pct:<10.1f}")
    
    # Zone Score correlation
    print(f"\n{'='*80}")
    print("ZONE SCORE CORRELATION WITH PROFITABILITY:")
    print(f"{'='*80}")
    
    score_buckets = defaultdict(lambda: {'pnl_list': [], 'count': 0})
    for trade in trades_with_zones:
        score = trade['zone_score']
        bucket = int(score / 10) * 10  # 0-10, 10-20, etc.
        score_buckets[bucket]['pnl_list'].append(trade['pnl'])
        score_buckets[bucket]['count'] += 1
    
    print(f"{'Zone Score Range':<20} {'Trades':<10} {'Avg P&L':<12} {'Win%':<10}")
    print(f"{'-'*52}")
    for bucket in sorted(score_buckets.keys()):
        stats = score_buckets[bucket]
        avg_pnl = sum(stats['pnl_list']) / stats['count']
        win_count = sum(1 for p in stats['pnl_list'] if p > 0)
        win_pct = (win_count / stats['count']) * 100
        print(f"{bucket}-{bucket+10:<18} {stats['count']:<10} {avg_pnl:<12.2f} {win_pct:<10.1f}")
    
    # Export detailed results
    export_detailed_results(trades_with_zones, zone_stats)
    
    # Recommendations
    print_recommendations(zone_stats, type_stats, strength_buckets, touch_buckets)

def export_detailed_results(trades_with_zones, zone_stats):
    """Export detailed results to CSV for further analysis"""
    output_file = 'D:/ClaudeAi/Unified_Engines/SD_Trading_V4_FIxing/results/zone_profitability_analysis.csv'
    
    with open(output_file, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow([
            'Trade#', 'Zone_ID', 'Zone_Type', 'Zone_Strength', 'Touches', 
            'Entry_Price', 'Exit_Price', 'P&L', 'Return%', 'Exit_Reason',
            'Zone_Score', 'Aggressiveness', 'Match_Score'
        ])
        
        for trade in trades_with_zones:
            writer.writerow([
                trade['trade_num'],
                trade['zone_id'],
                trade['zone_type'],
                f"{trade['strength']:.2f}",
                trade['touch_count'],
                f"{trade['entry_price']:.2f}",
                f"{trade['exit_price']:.2f}",
                f"{trade['pnl']:.2f}",
                f"{trade['return_pct']:.2f}",
                trade['exit_reason'],
                f"{trade['zone_score']:.2f}",
                f"{trade['aggressiveness']:.2f}",
                f"{trade['match_score']:.2f}"
            ])
    
    print(f"\n[+] Detailed results exported to: zone_profitability_analysis.csv")

def print_recommendations(zone_stats, type_stats, strength_buckets, touch_buckets):
    """Print actionable recommendations based on analysis"""
    
    print(f"\n{'='*80}")
    print("RECOMMENDATIONS TO IMPROVE SCORING ENGINE:")
    print(f"{'='*80}\n")
    
    # Find best performing zone characteristics
    demand_trades = [z for z in zone_stats if z['type'] == 'DEMAND']
    supply_trades = [z for z in zone_stats if z['type'] == 'SUPPLY']
    
    if demand_trades:
        demand_avg = sum(z['avg_pnl'] for z in demand_trades) / len(demand_trades)
        print(f"1. ZONE TYPE OPTIMIZATION:")
        print(f"   - DEMAND zones avg P&L: {demand_avg:.2f}")
        if supply_trades:
            supply_avg = sum(z['avg_pnl'] for z in supply_trades) / len(supply_trades)
            print(f"   - SUPPLY zones avg P&L: {supply_avg:.2f}")
            if demand_avg > supply_avg:
                print(f"   >> RECOMMENDATION: Increase weight for DEMAND zones (better by {demand_avg-supply_avg:.2f})")
            else:
                print(f"   >> RECOMMENDATION: Increase weight for SUPPLY zones (better by {supply_avg-demand_avg:.2f})")
    
    # Strength analysis
    print(f"\n2. ZONE STRENGTH SCORING:")
    strength_avgs = {}
    for bucket in sorted(strength_buckets.keys()):
        stats = strength_buckets[bucket]
        avg = sum(stats['pnl_list']) / stats['count']
        strength_avgs[bucket] = avg
    
    best_strength = max(strength_avgs.items(), key=lambda x: x[1])
    print(f"   - Best strength range: {best_strength[0]}-{best_strength[0]+10} (Avg P&L: {best_strength[1]:.2f})")
    print(f"   >> RECOMMENDATION: Boost scoring for zones with strength in {best_strength[0]}-{best_strength[0]+10} range")
    
    # Touch count analysis
    print(f"\n3. TOUCH COUNT OPTIMIZATION:")
    touch_avgs = {}
    for bucket in ['0', '1', '2', '3', '4', '5+']:
        if bucket in touch_buckets:
            stats = touch_buckets[bucket]
            avg = sum(stats['pnl_list']) / stats['count']
            touch_avgs[bucket] = avg
    
    best_touch = max(touch_avgs.items(), key=lambda x: x[1])
    print(f"   - Best touch count: {best_touch[0]} touches (Avg P&L: {best_touch[1]:.2f})")
    print(f"   >> RECOMMENDATION: Increase score for zones with {best_touch[0]} touches")
    
    # Win rate focus
    print(f"\n4. WIN RATE OPTIMIZATION:")
    high_win_zones = [z for z in zone_stats if z['win_pct'] >= 60]
    if high_win_zones:
        avg_strength = statistics.mean(z['strength'] for z in high_win_zones)
        avg_touches = statistics.mean(z['touch_count'] for z in high_win_zones)
        print(f"   - High win-rate zones (60%+ wins): {len(high_win_zones)} zones")
        print(f"   - Average strength: {avg_strength:.2f}")
        print(f"   - Average touches: {avg_touches:.1f}")
        print(f"   >> RECOMMENDATION: Emphasize zones with strength~{avg_strength:.0f} and {avg_touches:.0f} touches")
    
    print(f"\n5. ZONE SCORE ANALYSIS:")
    if zone_stats and len(zone_stats) > 0:
        zone_scores = [z.get('zone_score', 0) for z in zone_stats if 'zone_score' in z or z.get('zone_score')]
        if zone_scores:
            print(f"   - Current Zone Score range in trades: {min(zone_scores):.1f} - {max(zone_scores):.1f}")
            print(f"   >> RECOMMENDATION: Review scoring formula to better correlate with profitability")
    
    print(f"\n{'='*80}\n")

if __name__ == '__main__':
    trades = load_trades()
    zones = load_zones()
    analyze_profitability_patterns(trades, zones)
