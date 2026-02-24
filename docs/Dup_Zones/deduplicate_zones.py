#!/usr/bin/env python3
"""
Zone Deduplication Script
Merges overlapping zones into single representative zones
"""

import json
import sys
from datetime import datetime

def calculate_overlap_pct(z1, z2):
    """Calculate percentage overlap between two zones"""
    if z1['type'] != z2['type']:
        return 0.0
    
    z1_min = min(z1['distal_line'], z1['proximal_line'])
    z1_max = max(z1['distal_line'], z1['proximal_line'])
    z2_min = min(z2['distal_line'], z2['proximal_line'])
    z2_max = max(z2['distal_line'], z2['proximal_line'])
    
    overlap_start = max(z1_min, z2_min)
    overlap_end = min(z1_max, z2_max)
    
    if overlap_start >= overlap_end:
        return 0.0
    
    overlap_range = overlap_end - overlap_start
    min_range = min(z1_max - z1_min, z2_max - z2_min)
    
    return (overlap_range / min_range) * 100.0 if min_range > 0 else 0.0

def should_merge(z1, z2, overlap_threshold=70.0, proximity_threshold=20.0):
    """Determine if two zones should be merged"""
    if z1['type'] != z2['type']:
        return False
    
    # Check overlap percentage
    overlap_pct = calculate_overlap_pct(z1, z2)
    if overlap_pct >= overlap_threshold:
        return True
    
    # Check proximity (distance between zones)
    z1_min = min(z1['distal_line'], z1['proximal_line'])
    z1_max = max(z1['distal_line'], z1['proximal_line'])
    z2_min = min(z2['distal_line'], z2['proximal_line'])
    z2_max = max(z2['distal_line'], z2['proximal_line'])
    
    distance = min(abs(z1_max - z2_min), abs(z2_max - z1_min))
    
    if distance <= proximity_threshold:
        return True
    
    return False

def merge_zones(z1, z2, strategy='keep_strongest'):
    """Merge two zones into one"""
    if strategy == 'keep_strongest':
        # Keep zone with higher strength
        if z1['strength'] >= z2['strength']:
            primary, secondary = z1, z2
        else:
            primary, secondary = z2, z1
        
        merged = primary.copy()
        merged['touch_count'] = z1['touch_count'] + z2['touch_count']
        merged['is_elite'] = z1['is_elite'] or z2['is_elite']
        
        return merged
    
    elif strategy == 'keep_freshest':
        # Keep zone with fewer touches (fresher)
        if z1['touch_count'] <= z2['touch_count']:
            primary, secondary = z1, z2
        else:
            primary, secondary = z2, z1
        
        merged = primary.copy()
        merged['is_elite'] = z1['is_elite'] or z2['is_elite']
        
        return merged
    
    elif strategy == 'merge_properties':
        # Create composite zone
        merged = z1.copy()
        
        # Expand boundaries
        z1_min = min(z1['distal_line'], z1['proximal_line'])
        z1_max = max(z1['distal_line'], z1['proximal_line'])
        z2_min = min(z2['distal_line'], z2['proximal_line'])
        z2_max = max(z2['distal_line'], z2['proximal_line'])
        
        merged_min = min(z1_min, z2_min)
        merged_max = max(z1_max, z2_max)
        
        if merged['type'] == 'SUPPLY':
            merged['proximal_line'] = merged_max
            merged['distal_line'] = merged_min
        else:  # DEMAND
            merged['distal_line'] = merged_min
            merged['proximal_line'] = merged_max
        
        merged['base_low'] = min(z1['base_low'], z2['base_low'])
        merged['base_high'] = max(z1['base_high'], z2['base_high'])
        
        # Average strength
        merged['strength'] = (z1['strength'] + z2['strength']) / 2.0
        
        # Sum touch counts
        merged['touch_count'] = z1['touch_count'] + z2['touch_count']
        
        # Combine elite status
        merged['is_elite'] = z1['is_elite'] or z2['is_elite']
        
        # Keep oldest formation time
        try:
            dt1 = datetime.strptime(z1['formation_datetime'], '%Y-%m-%d %H:%M:%S')
            dt2 = datetime.strptime(z2['formation_datetime'], '%Y-%m-%d %H:%M:%S')
            if dt2 < dt1:
                merged['formation_bar'] = z2['formation_bar']
                merged['formation_datetime'] = z2['formation_datetime']
        except:
            pass
        
        return merged
    
    else:
        # Default: keep first zone
        return z1.copy()

def deduplicate_zones(zones, overlap_threshold=70.0, proximity_threshold=20.0, strategy='keep_strongest'):
    """Deduplicate overlapping zones"""
    
    # Separate by type
    supply_zones = [z for z in zones if z['type'] == 'SUPPLY']
    demand_zones = [z for z in zones if z['type'] == 'DEMAND']
    
    # Deduplicate each type
    supply_dedup = deduplicate_by_type(supply_zones, overlap_threshold, proximity_threshold, strategy)
    demand_dedup = deduplicate_by_type(demand_zones, overlap_threshold, proximity_threshold, strategy)
    
    return supply_dedup + demand_dedup

def deduplicate_by_type(zones, overlap_threshold, proximity_threshold, strategy):
    """Deduplicate zones of same type"""
    if not zones:
        return []
    
    # Sort by proximal line for efficient processing
    sorted_zones = sorted(zones, key=lambda z: max(z['distal_line'], z['proximal_line']))
    
    result = []
    merged = [False] * len(sorted_zones)
    
    for i in range(len(sorted_zones)):
        if merged[i]:
            continue
        
        current = sorted_zones[i].copy()
        
        # Find all zones that should be merged with this one
        for j in range(i + 1, len(sorted_zones)):
            if merged[j]:
                continue
            
            if should_merge(current, sorted_zones[j], overlap_threshold, proximity_threshold):
                current = merge_zones(current, sorted_zones[j], strategy)
                merged[j] = True
        
        result.append(current)
        merged[i] = True
    
    return result

def main():
    if len(sys.argv) < 2:
        print("Usage: python deduplicate_zones.py <input_file> [output_file] [options]")
        print("\nOptions:")
        print("  --overlap=70.0           Overlap threshold percentage (default: 70.0)")
        print("  --proximity=20.0         Proximity threshold in points (default: 20.0)")
        print("  --strategy=keep_strongest  Merge strategy: keep_strongest, keep_freshest, merge_properties")
        sys.exit(1)
    
    input_file = sys.argv[1]
    output_file = sys.argv[2] if len(sys.argv) > 2 and not sys.argv[2].startswith('--') else 'zones_deduplicated.json'
    
    # Parse options
    overlap_threshold = 70.0
    proximity_threshold = 20.0
    strategy = 'keep_strongest'
    
    for arg in sys.argv[2:]:
        if arg.startswith('--overlap='):
            overlap_threshold = float(arg.split('=')[1])
        elif arg.startswith('--proximity='):
            proximity_threshold = float(arg.split('=')[1])
        elif arg.startswith('--strategy='):
            strategy = arg.split('=')[1]
    
    # Load zones
    print(f"Loading zones from: {input_file}")
    with open(input_file, 'r') as f:
        data = json.load(f)
    
    original_zones = data['zones']
    print(f"Original zones: {len(original_zones)}")
    
    # Deduplicate
    print(f"\nDeduplicating with:")
    print(f"  Overlap threshold: {overlap_threshold}%")
    print(f"  Proximity threshold: {proximity_threshold} points")
    print(f"  Merge strategy: {strategy}")
    
    deduplicated = deduplicate_zones(
        original_zones,
        overlap_threshold,
        proximity_threshold,
        strategy
    )
    
    print(f"\nDeduplicated zones: {len(deduplicated)}")
    print(f"Removed: {len(original_zones) - len(deduplicated)} zones")
    print(f"Reduction: {(1 - len(deduplicated)/len(original_zones))*100:.1f}%")
    
    # Save
    data['zones'] = deduplicated
    with open(output_file, 'w') as f:
        json.dump(data, f, indent=2)
    
    print(f"\nSaved to: {output_file}")
    
    # Statistics
    supply_dedup = [z for z in deduplicated if z['type'] == 'SUPPLY']
    demand_dedup = [z for z in deduplicated if z['type'] == 'DEMAND']
    
    print(f"\nFinal breakdown:")
    print(f"  Supply zones: {len(supply_dedup)}")
    print(f"  Demand zones: {len(demand_dedup)}")

if __name__ == '__main__':
    main()
