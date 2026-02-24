import json
import sys

def compare_zones(backtest_file, live_file):
    """Compare zones between backtest and live engines"""
    
    with open(backtest_file, 'r') as f:
        backtest = json.load(f)
    
    with open(live_file, 'r') as f:
        live = json.load(f)
    
    backtest_zones = {z['zone_id']: z for z in backtest['zones']}
    live_zones = {z['zone_id']: z for z in live['zones']}
    
    print(f"Backtest zones: {len(backtest_zones)}")
    print(f"Live zones: {len(live_zones)}")
    print()
    
    # Find zones in backtest but not in live
    only_in_backtest = set(backtest_zones.keys()) - set(live_zones.keys())
    if only_in_backtest:
        print(f"❌ Zones ONLY in backtest ({len(only_in_backtest)}):")
        for zid in sorted(only_in_backtest)[:10]:  # Show first 10
            z = backtest_zones[zid]
            print(f"  Zone {zid}: {z['type']} at bar {z['formation_bar']} - {z['distal_line']:.2f}/{z['proximal_line']:.2f}")
        if len(only_in_backtest) > 10:
            print(f"  ... and {len(only_in_backtest) - 10} more")
        print()
    
    # Find zones in live but not in backtest
    only_in_live = set(live_zones.keys()) - set(backtest_zones.keys())
    if only_in_live:
        print(f"❌ Zones ONLY in live ({len(only_in_live)}):")
        for zid in sorted(only_in_live)[:10]:
            z = live_zones[zid]
            print(f"  Zone {zid}: {z['type']} at bar {z['formation_bar']} - {z['distal_line']:.2f}/{z['proximal_line']:.2f}")
        if len(only_in_live) > 10:
            print(f"  ... and {len(only_in_live) - 10} more")
        print()
    
    # Find zones in both but with different states
    common_zones = set(backtest_zones.keys()) & set(live_zones.keys())
    state_mismatches = []
    
    for zid in common_zones:
        bz = backtest_zones[zid]
        lz = live_zones[zid]
        
        if bz['state'] != lz['state'] or bz['touch_count'] != lz['touch_count']:
            state_mismatches.append({
                'zone_id': zid,
                'type': bz['type'],
                'formation_bar': bz['formation_bar'],
                'backtest_state': bz['state'],
                'live_state': lz['state'],
                'backtest_touch': bz['touch_count'],
                'live_touch': lz['touch_count']
            })
    
    if state_mismatches:
        print(f"⚠️  STATE MISMATCHES ({len(state_mismatches)}):")
        for m in state_mismatches[:20]:  # Show first 20
            print(f"  Zone {m['zone_id']} ({m['type']} @ bar {m['formation_bar']}):")
            print(f"    Backtest: {m['backtest_state']:7} (touch={m['backtest_touch']})")
            print(f"    Live:     {m['live_state']:7} (touch={m['live_touch']})")
        if len(state_mismatches) > 20:
            print(f"  ... and {len(state_mismatches) - 20} more state mismatches")
        print()
    
    # Check for field mismatches in common zones
    field_mismatches = []
    compare_fields = ['distal_line', 'proximal_line', 'base_low', 'base_high', 'strength']
    
    for zid in common_zones:
        bz = backtest_zones[zid]
        lz = live_zones[zid]
        
        for field in compare_fields:
            if abs(bz[field] - lz[field]) > 0.01:  # Allow small floating point differences
                field_mismatches.append({
                    'zone_id': zid,
                    'field': field,
                    'backtest': bz[field],
                    'live': lz[field],
                    'diff': abs(bz[field] - lz[field])
                })
    
    if field_mismatches:
        print(f"⚠️  FIELD VALUE MISMATCHES ({len(field_mismatches)}):")
        for m in field_mismatches[:20]:  # Show first 20
            print(f"  Zone {m['zone_id']} - {m['field']}: backtest={m['backtest']:.4f}, live={m['live']:.4f} (diff={m['diff']:.4f})")
        if len(field_mismatches) > 20:
            print(f"  ... and {len(field_mismatches) - 20} more field mismatches")
        print()
    
    print("\n" + "="*60)
    if only_in_backtest or only_in_live or state_mismatches or field_mismatches:
        print("✅ ANALYSIS COMPLETE - Discrepancies found")
    else:
        print("✅ PERFECT MATCH - No discrepancies!")

if __name__ == "__main__":
    backtest_file = "d:\\ClaudeAi\\Unified_Engines\\SD_Trading_V4_FIxing\\results\\backtest\\zones_backtest_20260101.json"
    live_file = "d:\\ClaudeAi\\Unified_Engines\\SD_Trading_V4_FIxing\\results\\live_trades\\zones_live.json"
    
    try:
        compare_zones(backtest_file, live_file)
    except FileNotFoundError as e:
        print(f"❌ Error: {e}")
        print(f"Make sure both JSON files exist:")
        print(f"  - {backtest_file}")
        print(f"  - {live_file}")
