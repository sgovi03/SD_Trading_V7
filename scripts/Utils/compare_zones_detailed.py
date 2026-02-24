import json
import sys
from collections import defaultdict

def compare_zones_comprehensive(backtest_file, live_file):
    """Comprehensive zone comparison between backtest and live engines"""
    
    try:
        with open(backtest_file, 'r') as f:
            backtest = json.load(f)
    except FileNotFoundError:
        print(f"❌ Backtest file not found: {backtest_file}")
        return False
    
    try:
        with open(live_file, 'r') as f:
            live = json.load(f)
    except FileNotFoundError:
        print(f"❌ Live file not found: {live_file}")
        return False
    
    backtest_zones = {z['zone_id']: z for z in backtest['zones']}
    live_zones = {z['zone_id']: z for z in live['zones']}
    
    print("=" * 80)
    print("ZONE COMPARISON ANALYSIS")
    print("=" * 80)
    print(f"Backtest zones: {len(backtest_zones)}")
    print(f"Live zones: {len(live_zones)}")
    print(f"Mode: {'✅ IDENTICAL COUNT' if len(backtest_zones) == len(live_zones) else '❌ COUNT MISMATCH'}")
    print()
    
    # Zone count by type
    backtest_demand = sum(1 for z in backtest_zones.values() if z['type'] == 'DEMAND')
    backtest_supply = len(backtest_zones) - backtest_demand
    live_demand = sum(1 for z in live_zones.values() if z['type'] == 'DEMAND')
    live_supply = len(live_zones) - live_demand
    
    print("Zone Distribution:")
    print(f"  Backtest: {backtest_demand} DEMAND, {backtest_supply} SUPPLY")
    print(f"  Live:     {live_demand} DEMAND, {live_supply} SUPPLY")
    print()
    
    # Find zones in backtest but not in live
    only_in_backtest = set(backtest_zones.keys()) - set(live_zones.keys())
    only_in_live = set(live_zones.keys()) - set(backtest_zones.keys())
    common_zones = set(backtest_zones.keys()) & set(live_zones.keys())
    
    print("-" * 80)
    print("ZONE PRESENCE ANALYSIS")
    print("-" * 80)
    
    if only_in_backtest:
        print(f"\n❌ Zones ONLY in backtest ({len(only_in_backtest)}):")
        for zid in sorted(list(only_in_backtest)[:10]):
            z = backtest_zones[zid]
            print(f"  Zone {zid:3d}: {z['type']:6} @ bar {z['formation_bar']:5d} - [{z['distal_line']:8.2f}, {z['proximal_line']:8.2f}]")
        if len(only_in_backtest) > 10:
            print(f"  ... and {len(only_in_backtest) - 10} more")
    else:
        print("\n✅ All backtest zones present in live")
    
    if only_in_live:
        print(f"\n❌ Zones ONLY in live ({len(only_in_live)}):")
        for zid in sorted(list(only_in_live)[:10]):
            z = live_zones[zid]
            print(f"  Zone {zid:3d}: {z['type']:6} @ bar {z['formation_bar']:5d} - [{z['distal_line']:8.2f}, {z['proximal_line']:8.2f}]")
        if len(only_in_live) > 10:
            print(f"  ... and {len(only_in_live) - 10} more")
    else:
        print("\n✅ All live zones present in backtest")
    
    print(f"\nCommon zones: {len(common_zones)}")
    
    # Analyze state and value differences in common zones
    print("\n" + "-" * 80)
    print("STATE & VALUE COMPARISON (Common Zones)")
    print("-" * 80)
    
    state_mismatches = []
    value_mismatches = []
    
    for zid in common_zones:
        bz = backtest_zones[zid]
        lz = live_zones[zid]
        
        # State check
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
        
        # Value check
        compare_fields = ['distal_line', 'proximal_line', 'base_low', 'base_high', 'strength']
        for field in compare_fields:
            if abs(bz[field] - lz[field]) > 0.01:
                value_mismatches.append({
                    'zone_id': zid,
                    'field': field,
                    'backtest': bz[field],
                    'live': lz[field],
                    'diff': abs(bz[field] - lz[field])
                })
    
    # State mismatches
    if state_mismatches:
        print(f"\n⚠️  STATE MISMATCHES ({len(state_mismatches)}):")
        # Group by state difference
        fresh_to_tested = [m for m in state_mismatches if m['backtest_state'] == 'TESTED' and m['live_state'] == 'FRESH']
        tested_to_fresh = [m for m in state_mismatches if m['backtest_state'] == 'FRESH' and m['live_state'] == 'TESTED']
        other_mismatch = [m for m in state_mismatches if m not in fresh_to_tested and m not in tested_to_fresh]
        
        if fresh_to_tested:
            print(f"\n  Backtest=TESTED, Live=FRESH ({len(fresh_to_tested)}):")
            for m in fresh_to_tested[:10]:
                print(f"    Zone {m['zone_id']:3d} ({m['type']:6} @ bar {m['formation_bar']:5d}): BT touch={m['backtest_touch']}, Live touch={m['live_touch']}")
            if len(fresh_to_tested) > 10:
                print(f"    ... and {len(fresh_to_tested) - 10} more")
        
        if tested_to_fresh:
            print(f"\n  Backtest=FRESH, Live=TESTED ({len(tested_to_fresh)}):")
            for m in tested_to_fresh[:10]:
                print(f"    Zone {m['zone_id']:3d} ({m['type']:6} @ bar {m['formation_bar']:5d}): BT touch={m['backtest_touch']}, Live touch={m['live_touch']}")
            if len(tested_to_fresh) > 10:
                print(f"    ... and {len(tested_to_fresh) - 10} more")
        
        if other_mismatch:
            print(f"\n  Other state mismatches ({len(other_mismatch)}):")
            for m in other_mismatch[:10]:
                print(f"    Zone {m['zone_id']:3d}: Backtest={m['backtest_state']}, Live={m['live_state']}")
    else:
        print("\n✅ All zone states match perfectly")
    
    # Value mismatches
    if value_mismatches:
        print(f"\n⚠️  VALUE MISMATCHES ({len(value_mismatches)}):")
        grouped = defaultdict(list)
        for m in value_mismatches:
            grouped[m['field']].append(m)
        
        for field in ['distal_line', 'proximal_line', 'base_low', 'base_high', 'strength']:
            if field in grouped:
                matches = grouped[field]
                print(f"\n  {field} ({len(matches)} zones):")
                for m in matches[:5]:
                    print(f"    Zone {m['zone_id']:3d}: BT={m['backtest']:.4f}, Live={m['live']:.4f}, diff={m['diff']:.4f}")
                if len(matches) > 5:
                    print(f"    ... and {len(matches) - 5} more")
    else:
        print("\n✅ All zone values match perfectly")
    
    # Summary
    print("\n" + "=" * 80)
    print("SUMMARY")
    print("=" * 80)
    
    if not only_in_backtest and not only_in_live and not state_mismatches and not value_mismatches:
        print("✅ PERFECT MATCH - All zones are identical!")
        return True
    else:
        issues = 0
        if only_in_backtest:
            issues += len(only_in_backtest)
        if only_in_live:
            issues += len(only_in_live)
        if state_mismatches:
            issues += len(state_mismatches)
        if value_mismatches:
            issues += len(value_mismatches)
        
        print(f"❌ DISCREPANCIES FOUND ({issues} issues)")
        print(f"  - Zones only in backtest: {len(only_in_backtest)}")
        print(f"  - Zones only in live: {len(only_in_live)}")
        print(f"  - State mismatches: {len(state_mismatches)}")
        print(f"  - Value mismatches: {len(value_mismatches)}")
        return False

if __name__ == "__main__":
    backtest_file = r"d:\ClaudeAi\Unified_Engines\SD_Trading_V4_FIxing\results\backtest\zones_backtest_20260101.json"
    live_file = r"d:\ClaudeAi\Unified_Engines\SD_Trading_V4_FIxing\results\live_trades\zones_live.json"
    
    result = compare_zones_comprehensive(backtest_file, live_file)
    sys.exit(0 if result else 1)
