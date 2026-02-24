#!/usr/bin/env python3
"""
Analyze State Histories of Non-FRESH Zones
Shows detailed state change histories for TESTED and VIOLATED zones
"""

import json
from collections import Counter, defaultdict
from pathlib import Path


def load_zones(filepath):
    """Load zones from JSON file"""
    with open(filepath, 'r') as f:
        data = json.load(f)
    return data['zones']


def analyze_state_histories(zones, engine_name):
    """Analyze state histories for non-FRESH zones"""
    
    print("\n" + "=" * 80)
    print(f"{engine_name.upper()} ENGINE - STATE HISTORY ANALYSIS")
    print("=" * 80)
    
    # Categorize zones by state
    zones_by_state = defaultdict(list)
    for zone in zones:
        zones_by_state[zone['state']].append(zone)
    
    print(f"\n📊 Zone Distribution by State:")
    for state in ['FRESH', 'TESTED', 'VIOLATED']:
        count = len(zones_by_state[state])
        print(f"  {state:10s}: {count:4d} zones")
    
    # Analyze TESTED zones
    tested_zones = zones_by_state['TESTED']
    if tested_zones:
        print(f"\n" + "─" * 80)
        print(f"🔵 TESTED ZONES ({len(tested_zones)} zones)")
        print("─" * 80)
        
        # Statistics
        event_counts = Counter()
        touch_counts = []
        zones_with_history = 0
        
        for zone in tested_zones:
            history = zone.get('state_history', [])
            if history:
                zones_with_history += 1
                event_counts.update(e['event'] for e in history)
                touch_counts.append(zone['touch_count'])
        
        print(f"\n📈 TESTED Zones Statistics:")
        print(f"  Zones with history:     {zones_with_history}/{len(tested_zones)}")
        print(f"  Avg touches per zone:   {sum(touch_counts)/len(touch_counts):.1f}" if touch_counts else "  Avg touches per zone:   N/A")
        print(f"  Max touches:            {max(touch_counts)}" if touch_counts else "  Max touches:            N/A")
        print(f"  Min touches:            {min(touch_counts)}" if touch_counts else "  Min touches:            N/A")
        
        print(f"\n🔔 Event Types in TESTED Zones:")
        for event_type, count in event_counts.most_common():
            print(f"  {event_type:20s}: {count:5d}")
        
        # Show detailed examples
        print(f"\n📝 Sample TESTED Zones with Full History:")
        
        # Show zones with different touch patterns
        samples = []
        
        # Find zone with most touches
        most_touched = max(tested_zones, key=lambda z: z['touch_count'])
        samples.append(("Most Touched", most_touched))
        
        # Find zone with elite status
        elite_zones = [z for z in tested_zones if z.get('is_elite', False)]
        if elite_zones:
            samples.append(("Elite", elite_zones[0]))
        
        # Find recent zone
        recent_zones = sorted([z for z in tested_zones if z.get('state_history', [])], 
                            key=lambda z: z.get('state_history', [{}])[-1].get('timestamp', ''), 
                            reverse=True)
        if recent_zones and recent_zones[0] != most_touched:
            samples.append(("Recent Activity", recent_zones[0]))
        
        for label, zone in samples[:5]:  # Show up to 5 samples
            history = zone.get('state_history', [])
            if not history:
                continue
                
            print(f"\n  ┌─ Zone {zone['zone_id']} ({zone['type']}) - {label}")
            print(f"  │  Proximal: {zone['proximal_line']:.2f}, Distal: {zone['distal_line']:.2f}")
            print(f"  │  Formation: {zone['formation_datetime']}")
            print(f"  │  Strength: {zone['strength']:.1f}, Touch count: {zone['touch_count']}")
            print(f"  │  Elite: {zone.get('is_elite', False)}")
            print(f"  │")
            print(f"  │  State History ({len(history)} events):")
            
            for i, event in enumerate(history, 1):
                timestamp = event['timestamp']
                event_type = event['event']
                old_state = event.get('old_state', '')
                new_state = event['new_state']
                price = event['price']
                touch = event['touch_number']
                
                symbol = "├─" if i < len(history) else "└─"
                print(f"  │  {symbol} #{i:2d}  {timestamp:19s}  {event_type:15s}  {old_state:6s} → {new_state:6s}  Price: {price:8.2f}  Touch: {touch}")
    
    # Analyze VIOLATED zones
    violated_zones = zones_by_state['VIOLATED']
    if violated_zones:
        print(f"\n" + "─" * 80)
        print(f"🔴 VIOLATED ZONES ({len(violated_zones)} zones)")
        print("─" * 80)
        
        # Statistics
        event_counts = Counter()
        touch_counts = []
        zones_with_history = 0
        violation_from_fresh = 0
        violation_from_tested = 0
        
        for zone in violated_zones:
            history = zone.get('state_history', [])
            if history:
                zones_with_history += 1
                event_counts.update(e['event'] for e in history)
                touch_counts.append(zone['touch_count'])
                
                # Check violation source
                violation_events = [e for e in history if e['event'] == 'VIOLATED']
                if violation_events:
                    old_state = violation_events[0].get('old_state', '')
                    if old_state == 'FRESH':
                        violation_from_fresh += 1
                    elif old_state == 'TESTED':
                        violation_from_tested += 1
        
        print(f"\n📈 VIOLATED Zones Statistics:")
        print(f"  Zones with history:      {zones_with_history}/{len(violated_zones)}")
        print(f"  Violated from FRESH:     {violation_from_fresh}")
        print(f"  Violated from TESTED:    {violation_from_tested}")
        print(f"  Avg touches before viol: {sum(touch_counts)/len(touch_counts):.1f}" if touch_counts else "  Avg touches before viol: N/A")
        
        print(f"\n🔔 Event Types in VIOLATED Zones:")
        for event_type, count in event_counts.most_common():
            print(f"  {event_type:20s}: {count:5d}")
        
        # Show detailed examples
        print(f"\n📝 Sample VIOLATED Zones with Full History:")
        
        # Show different violation patterns
        samples = []
        
        # Find zone violated from FRESH
        fresh_violated = [z for z in violated_zones 
                         if any(e['event'] == 'VIOLATED' and e.get('old_state') == 'FRESH' 
                               for e in z.get('state_history', []))]
        if fresh_violated:
            samples.append(("Violated from FRESH", fresh_violated[0]))
        
        # Find zone violated from TESTED with most touches
        tested_violated = [z for z in violated_zones 
                          if any(e['event'] == 'VIOLATED' and e.get('old_state') == 'TESTED' 
                                for e in z.get('state_history', []))]
        if tested_violated:
            most_touched_violated = max(tested_violated, key=lambda z: z['touch_count'])
            samples.append(("Violated after multiple touches", most_touched_violated))
        
        # Recent violation
        recent_violated = sorted([z for z in violated_zones if z.get('state_history', [])], 
                                key=lambda z: z.get('state_history', [{}])[-1].get('timestamp', ''), 
                                reverse=True)
        if recent_violated and recent_violated[0] not in [s[1] for s in samples]:
            samples.append(("Recent Violation", recent_violated[0]))
        
        for label, zone in samples[:5]:  # Show up to 5 samples
            history = zone.get('state_history', [])
            if not history:
                continue
                
            print(f"\n  ┌─ Zone {zone['zone_id']} ({zone['type']}) - {label}")
            print(f"  │  Proximal: {zone['proximal_line']:.2f}, Distal: {zone['distal_line']:.2f}")
            print(f"  │  Formation: {zone['formation_datetime']}")
            print(f"  │  Strength: {zone['strength']:.1f}, Touch count: {zone['touch_count']}")
            print(f"  │  Elite: {zone.get('is_elite', False)}")
            print(f"  │")
            print(f"  │  State History ({len(history)} events):")
            
            for i, event in enumerate(history, 1):
                timestamp = event['timestamp']
                event_type = event['event']
                old_state = event.get('old_state', '')
                new_state = event['new_state']
                price = event['price']
                touch = event['touch_number']
                
                # Highlight violation event
                if event_type == 'VIOLATED':
                    symbol = "└─⚠️ " if i == len(history) else "├─⚠️ "
                else:
                    symbol = "├─" if i < len(history) else "└─"
                
                print(f"  │  {symbol} #{i:2d}  {timestamp:19s}  {event_type:15s}  {old_state:6s} → {new_state:6s}  Price: {price:8.2f}  Touch: {touch}")


def main():
    # Paths
    script_dir = Path(__file__).parent
    results_dir = script_dir.parent / "results"
    
    backtest_file = results_dir / "backtest" / "zones_backtest_20260102.json"
    live_file = results_dir / "live_trades" / "zones_live.json"
    
    print("=" * 80)
    print("STATE HISTORY ANALYSIS FOR NON-FRESH ZONES")
    print("=" * 80)
    print(f"\n📁 Zone Files:")
    print(f"  Backtest: {backtest_file}")
    print(f"  Live:     {live_file}")
    
    # Analyze backtest zones
    backtest_zones = load_zones(backtest_file)
    analyze_state_histories(backtest_zones, "BACKTEST")
    
    # Analyze live zones
    live_zones = load_zones(live_file)
    analyze_state_histories(live_zones, "LIVE")
    
    print("\n" + "=" * 80)
    print("✅ ANALYSIS COMPLETE")
    print("=" * 80)


if __name__ == "__main__":
    main()
