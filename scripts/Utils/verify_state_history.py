#!/usr/bin/env python3
"""
Verify Zone State History Synchronization
Compares state_history arrays between backtest and live zone files
"""

import json
from collections import Counter
from pathlib import Path


def load_zones(filepath):
    """Load zones from JSON file"""
    with open(filepath, 'r') as f:
        data = json.load(f)
    return {zone['zone_id']: zone for zone in data['zones']}


def compare_state_histories(backtest_zones, live_zones):
    """Compare state_history arrays between backtest and live zones"""
    
    print("=" * 60)
    print("ZONE STATE HISTORY VERIFICATION")
    print("=" * 60)
    
    # Get common zone IDs
    backtest_ids = set(backtest_zones.keys())
    live_ids = set(live_zones.keys())
    common_ids = backtest_ids & live_ids
    
    print(f"\n📊 Zone Count:")
    print(f"  Backtest zones: {len(backtest_ids)}")
    print(f"  Live zones:     {len(live_ids)}")
    print(f"  Common zones:   {len(common_ids)}")
    
    # Track state history differences
    history_match = 0
    history_mismatch = 0
    zones_with_history = 0
    total_events = 0
    event_types = Counter()
    
    mismatches = []
    
    for zone_id in sorted(common_ids):
        backtest_zone = backtest_zones[zone_id]
        live_zone = live_zones[zone_id]
        
        backtest_history = backtest_zone.get('state_history', [])
        live_history = live_zone.get('state_history', [])
        
        if backtest_history:
            zones_with_history += 1
            total_events += len(backtest_history)
            
            # Count event types
            for event in backtest_history:
                event_types[event['event']] += 1
        
        # Compare histories
        if backtest_history == live_history:
            history_match += 1
        else:
            history_mismatch += 1
            mismatches.append({
                'zone_id': zone_id,
                'backtest_events': len(backtest_history),
                'live_events': len(live_history),
                'backtest_history': backtest_history,
                'live_history': live_history
            })
    
    print(f"\n📈 State History Statistics:")
    print(f"  Zones with history:   {zones_with_history}")
    print(f"  Total events:         {total_events}")
    print(f"  Histories matching:   {history_match}")
    print(f"  Histories different:  {history_mismatch}")
    
    print(f"\n🔔 Event Types:")
    for event_type, count in event_types.most_common():
        print(f"  {event_type:20s}: {count:4d}")
    
    # Show first few zones with state history
    print(f"\n📝 Sample Zones with State History:")
    sample_count = 0
    for zone_id in sorted(common_ids):
        if sample_count >= 3:
            break
        
        backtest_zone = backtest_zones[zone_id]
        backtest_history = backtest_zone.get('state_history', [])
        
        if backtest_history:
            sample_count += 1
            print(f"\n  Zone {zone_id} ({backtest_zone['type']}) - State: {backtest_zone['state']}")
            print(f"    Touch count: {backtest_zone['touch_count']}")
            print(f"    Events: {len(backtest_history)}")
            for event in backtest_history:
                print(f"      • {event['event']:15s} @ {event['timestamp']:19s} | {event['old_state']:6s} → {event['new_state']:6s} | Touch #{event['touch_number']}")
    
    # Show mismatches if any
    if mismatches:
        print(f"\n⚠️  State History Mismatches:")
        for mismatch in mismatches[:5]:  # Show first 5
            print(f"\n  Zone {mismatch['zone_id']}:")
            print(f"    Backtest events: {mismatch['backtest_events']}")
            print(f"    Live events:     {mismatch['live_events']}")
    
    # Summary
    print("\n" + "=" * 60)
    if history_mismatch == 0:
        print("✅ SUCCESS: All state histories match perfectly!")
        print(f"   {zones_with_history} zones have state history")
        print(f"   {total_events} total events tracked")
    else:
        print(f"⚠️  WARNING: {history_mismatch} zones have different state histories")
    print("=" * 60)
    
    return history_mismatch == 0


def main():
    # Paths
    script_dir = Path(__file__).parent
    results_dir = script_dir.parent / "results"
    
    backtest_file = results_dir / "backtest" / "zones_backtest_20260102.json"
    live_file = results_dir / "live_trades" / "zones_live.json"
    
    print(f"\n📁 Loading zone files:")
    print(f"  Backtest: {backtest_file}")
    print(f"  Live:     {live_file}")
    
    # Load zones
    backtest_zones = load_zones(backtest_file)
    live_zones = load_zones(live_file)
    
    # Compare state histories
    success = compare_state_histories(backtest_zones, live_zones)
    
    return 0 if success else 1


if __name__ == "__main__":
    import sys
    sys.exit(main())
