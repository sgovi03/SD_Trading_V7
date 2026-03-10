#!/usr/bin/env python3
"""
SD Trading System Health Check
Quick diagnostic for common issues
"""

import json
import sys
from pathlib import Path

def check_zones_file(filepath):
    """Analyze zones_live_master.json for common issues"""
    print("\n" + "="*60)
    print("ZONE FILE ANALYSIS")
    print("="*60)
    
    if not Path(filepath).exists():
        print(f"❌ Zone file not found: {filepath}")
        return False
    
    with open(filepath) as f:
        data = json.load(f)
    
    zones = data.get('zones', [])
    
    print(f"\n✅ Zone file loaded: {len(zones)} zones")
    
    # Zone count check
    if len(zones) < 10:
        print(f"⚠️  WARNING: Only {len(zones)} zones (expected 15-30)")
        print("   → Detection parameters may be too strict")
    elif len(zones) > 100:
        print(f"⚠️  WARNING: {len(zones)} zones (expected 15-30)")
        print("   → Detection parameters may be too lenient")
    else:
        print(f"✅ Zone count OK: {len(zones)}")
    
    # Volume score analysis
    vol_scores = [z.get('volume_profile', {}).get('volume_score', -999) for z in zones]
    vol_scores = [v for v in vol_scores if v > -999]
    
    if vol_scores:
        print(f"\n📊 Volume Scores:")
        print(f"   Min: {min(vol_scores)}")
        print(f"   Max: {max(vol_scores)}")
        print(f"   Avg: {sum(vol_scores)/len(vol_scores):.1f}")
        
        # Check if threshold is realistic
        passing_20 = sum(1 for v in vol_scores if v >= 20)
        passing_10 = sum(1 for v in vol_scores if v >= 10)
        passing_0 = sum(1 for v in vol_scores if v >= 0)
        
        print(f"\n   Zones passing threshold:")
        print(f"   >= 20: {passing_20}/{len(vol_scores)} ({passing_20*100//len(vol_scores)}%)")
        print(f"   >= 10: {passing_10}/{len(vol_scores)} ({passing_10*100//len(vol_scores)}%)")
        print(f"   >= 0:  {passing_0}/{len(vol_scores)} ({passing_0*100//len(vol_scores)}%)")
        
        if passing_20 < len(vol_scores) * 0.3:
            print("\n   ⚠️  WARNING: Threshold 20 blocks >70% of zones")
            print("   → Consider lowering to 10 or 0")
    
    # Institutional index analysis
    inst_indexes = [z.get('institutional_index', 0) for z in zones]
    
    print(f"\n📊 Institutional Index:")
    print(f"   Min: {min(inst_indexes)}")
    print(f"   Max: {max(inst_indexes)}")
    print(f"   Avg: {sum(inst_indexes)/len(inst_indexes):.1f}")
    
    passing_40 = sum(1 for i in inst_indexes if i >= 40)
    passing_10 = sum(1 for i in inst_indexes if i >= 10)
    
    print(f"\n   Zones passing threshold:")
    print(f"   >= 40: {passing_40}/{len(inst_indexes)} ({passing_40*100//len(inst_indexes)}%)")
    print(f"   >= 10: {passing_10}/{len(inst_indexes)} ({passing_10*100//len(inst_indexes)}%)")
    
    if passing_40 < len(inst_indexes) * 0.3:
        print("\n   ⚠️  WARNING: Threshold 40 blocks >70% of zones")
        print("   → Consider lowering to 10")
    
    # Departure ratio analysis
    dep_ratios = [z.get('volume_profile', {}).get('departure_volume_ratio', 0) for z in zones]
    
    print(f"\n📊 Departure Volume Ratio:")
    print(f"   Min: {min(dep_ratios):.2f}")
    print(f"   Max: {max(dep_ratios):.2f}")
    print(f"   Avg: {sum(dep_ratios)/len(dep_ratios):.2f}")
    
    passing_13 = sum(1 for d in dep_ratios if d >= 1.3)
    passing_06 = sum(1 for d in dep_ratios if d >= 0.6)
    
    print(f"\n   Zones passing threshold:")
    print(f"   >= 1.3: {passing_13}/{len(dep_ratios)} ({passing_13*100//len(dep_ratios)}%)")
    print(f"   >= 0.6: {passing_06}/{len(dep_ratios)} ({passing_06*100//len(dep_ratios)}%)")
    
    if passing_13 < len(dep_ratios) * 0.3:
        print("\n   ⚠️  WARNING: Threshold 1.3 blocks >70% of zones")
        print("   → Consider lowering to 0.6")
    
    # Touch count analysis
    touch_counts = [z.get('touch_count', 0) for z in zones]
    
    print(f"\n📊 Touch Counts:")
    print(f"   Min: {min(touch_counts)}")
    print(f"   Max: {max(touch_counts)}")
    print(f"   Avg: {sum(touch_counts)/len(touch_counts):.1f}")
    
    exhausted = sum(1 for t in touch_counts if t > 100)
    if exhausted > 0:
        print(f"\n   ⚠️  WARNING: {exhausted} zones with >100 touches (exhausted)")
        print("   → Consider setting max_zone_touch_count = 50")
    
    # State distribution
    states = [z.get('state', 'UNKNOWN') for z in zones]
    from collections import Counter
    state_counts = Counter(states)
    
    print(f"\n📊 Zone States:")
    for state, count in state_counts.items():
        print(f"   {state}: {count} ({count*100//len(zones)}%)")
    
    # Baseline check
    baselines_zero = sum(1 for z in zones 
                        if z.get('volume_profile', {}).get('avg_volume_baseline', 0) == 0)
    
    if baselines_zero > 0:
        print(f"\n❌ WARNING: {baselines_zero} zones have zero volume baseline!")
        print("   → Volume calculations will be incorrect")
        print("   → Check volume_baseline_file loading")
    else:
        print(f"\n✅ All zones have valid volume baselines")
    
    return True


def check_config(filepath):
    """Check config file for common issues"""
    print("\n" + "="*60)
    print("CONFIG FILE ANALYSIS")
    print("="*60)
    
    if not Path(filepath).exists():
        print(f"❌ Config file not found: {filepath}")
        return False
    
    with open(filepath) as f:
        lines = f.readlines()
    
    config = {}
    for line in lines:
        line = line.strip()
        if '=' in line and not line.startswith('#'):
            key, value = line.split('=', 1)
            config[key.strip()] = value.strip()
    
    print(f"✅ Config loaded: {len(config)} parameters")
    
    # Critical checks
    issues = []
    
    # Check volume filter
    min_vol_score = int(config.get('min_volume_entry_score', -50))
    if min_vol_score == -50:
        issues.append("❌ CRITICAL: min_volume_entry_score = -50 (DISABLED)")
        issues.append("   → Change to 10 to activate pullback volume filter")
        issues.append("   → Expected: +10-15% win rate improvement")
    else:
        print(f"✅ Volume filter active: min_volume_entry_score = {min_vol_score}")
    
    # Check vol exhaustion exit
    vol_exit = config.get('enable_volume_exhaustion_exit', 'YES')
    if vol_exit == 'YES':
        issues.append("❌ CRITICAL: enable_volume_exhaustion_exit = YES")
        issues.append("   → This causes 83% of trades to exit immediately (Bug #VOL_EXIT)")
        issues.append("   → Change to NO immediately")
    else:
        print(f"✅ Volume exhaustion exit disabled (avoiding Bug #VOL_EXIT)")
    
    # Check zone detection strictness
    min_strength = int(config.get('min_zone_strength', 50))
    if min_strength > 45:
        issues.append(f"⚠️  WARNING: min_zone_strength = {min_strength} (may be too strict)")
        issues.append("   → Consider 35-40 for more zones")
    
    min_width = float(config.get('min_zone_width_atr', 1.3))
    if min_width > 1.0:
        issues.append(f"⚠️  WARNING: min_zone_width_atr = {min_width} (may be too strict)")
        issues.append("   → Consider 0.6-0.8 for more zones")
    
    # Check touch limit
    max_touches = int(config.get('max_zone_touch_count', 25000))
    if max_touches > 1000:
        issues.append(f"⚠️  WARNING: max_zone_touch_count = {max_touches} (too high)")
        issues.append("   → Consider 50-100 to prevent exhausted zones")
    
    # Print all issues
    if issues:
        print(f"\n🔍 Found {len(issues)//2} issues:")
        for issue in issues:
            print(issue)
    else:
        print("\n✅ No critical config issues found")
    
    return True


def main():
    """Run full diagnostic"""
    print("\n" + "="*60)
    print("SD TRADING SYSTEM HEALTH CHECK")
    print("="*60)
    
    # Check zones file
    zones_file = sys.argv[1] if len(sys.argv) > 1 else "zones_live_master.json"
    check_zones_file(zones_file)
    
    # Check config
    config_file = sys.argv[2] if len(sys.argv) > 2 else "phase_6_config_v0_6_NEW.txt"
    check_config(config_file)
    
    print("\n" + "="*60)
    print("RECOMMENDATIONS")
    print("="*60)
    print("""
Priority 1 (Do Today):
1. Set min_volume_entry_score = 10
2. Set enable_volume_exhaustion_exit = NO
3. Lower thresholds if <10 zones detected

Priority 2 (This Week):
1. Verify volume baseline file loads
2. Check actual zone distribution vs thresholds
3. Run backtest to validate changes

Priority 3 (Long-term):
1. Implement approach velocity filter
2. Implement liquidity sweep signal
3. Add multi-timeframe support
""")


if __name__ == "__main__":
    main()
