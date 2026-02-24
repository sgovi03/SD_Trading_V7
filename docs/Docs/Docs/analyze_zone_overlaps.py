import json
import os
from collections import defaultdict

def load_zones(filepath):
    """Load zones from a JSON file."""
    with open(filepath, 'r') as f:
        data = json.load(f)
    return data.get('zones', [])

def check_overlap(zone1, zone2):
    """Check if two zones overlap in price range."""
    # Both zones use base_low and base_high to define their range
    low1, high1 = zone1['base_low'], zone1['base_high']
    low2, high2 = zone2['base_low'], zone2['base_high']
    
    # Check if ranges overlap
    return not (high1 < low2 or high2 < low1)

def get_overlap_amount(zone1, zone2):
    """Calculate the overlapping price range."""
    low1, high1 = zone1['base_low'], zone1['base_high']
    low2, high2 = zone2['base_low'], zone2['base_high']
    
    overlap_low = max(low1, low2)
    overlap_high = min(high1, high2)
    
    if overlap_high >= overlap_low:
        return overlap_high - overlap_low
    return 0

def analyze_zone_overlaps(zones):
    """Analyze zones for overlaps."""
    overlapping_groups = []
    exact_duplicates = []
    
    # Check each pair of zones
    for i, zone1 in enumerate(zones):
        for j in range(i + 1, len(zones)):
            zone2 = zones[j]
            
            if check_overlap(zone1, zone2):
                # Check if exact duplicate (same price range)
                if (zone1['base_low'] == zone2['base_low'] and 
                    zone1['base_high'] == zone2['base_high']):
                    exact_duplicates.append({
                        'zone1_id': zone1['zone_id'],
                        'zone2_id': zone2['zone_id'],
                        'type1': zone1['type'],
                        'type2': zone2['type'],
                        'price_low': zone1['base_low'],
                        'price_high': zone1['base_high'],
                        'zone1_formation': zone1.get('formation_datetime', 'N/A'),
                        'zone2_formation': zone2.get('formation_datetime', 'N/A'),
                        'zone1_state': zone1.get('state', 'N/A'),
                        'zone2_state': zone2.get('state', 'N/A'),
                        'zone1_active': zone1.get('is_active', 'N/A'),
                        'zone2_active': zone2.get('is_active', 'N/A')
                    })
                else:
                    # Partial overlap
                    overlap_amount = get_overlap_amount(zone1, zone2)
                    overlapping_groups.append({
                        'zone1_id': zone1['zone_id'],
                        'zone2_id': zone2['zone_id'],
                        'type1': zone1['type'],
                        'type2': zone2['type'],
                        'zone1_range': f"{zone1['base_low']} - {zone1['base_high']}",
                        'zone2_range': f"{zone2['base_low']} - {zone2['base_high']}",
                        'overlap_amount': overlap_amount,
                        'zone1_formation': zone1.get('formation_datetime', 'N/A'),
                        'zone2_formation': zone2.get('formation_datetime', 'N/A'),
                        'zone1_state': zone1.get('state', 'N/A'),
                        'zone2_state': zone2.get('state', 'N/A'),
                        'zone1_active': zone1.get('is_active', 'N/A'),
                        'zone2_active': zone2.get('is_active', 'N/A')
                    })
    
    return overlapping_groups, exact_duplicates

def print_report(zones, overlapping_groups, exact_duplicates, filename):
    """Print a detailed report of the analysis."""
    print(f"\n{'='*80}")
    print(f"ZONE OVERLAP ANALYSIS: {filename}")
    print(f"{'='*80}")
    print(f"\nTotal zones analyzed: {len(zones)}")
    print(f"Exact duplicates found: {len(exact_duplicates)}")
    print(f"Partial overlaps found: {len(overlapping_groups)}")
    
    # Print exact duplicates
    if exact_duplicates:
        print(f"\n{'='*80}")
        print("EXACT DUPLICATES (Same Price Range):")
        print(f"{'='*80}")
        for dup in exact_duplicates:
            print(f"\nZone {dup['zone1_id']} ({dup['type1']}) and Zone {dup['zone2_id']} ({dup['type2']})")
            print(f"  Price Range: {dup['price_low']} - {dup['price_high']}")
            print(f"  Zone {dup['zone1_id']}: Formation={dup['zone1_formation']}, State={dup['zone1_state']}, Active={dup['zone1_active']}")
            print(f"  Zone {dup['zone2_id']}: Formation={dup['zone2_formation']}, State={dup['zone2_state']}, Active={dup['zone2_active']}")
    
    # Print partial overlaps
    if overlapping_groups:
        print(f"\n{'='*80}")
        print("PARTIAL OVERLAPS:")
        print(f"{'='*80}")
        # Sort by overlap amount (descending)
        overlapping_groups.sort(key=lambda x: x['overlap_amount'], reverse=True)
        for overlap in overlapping_groups[:50]:  # Show top 50 overlaps
            print(f"\nZone {overlap['zone1_id']} ({overlap['type1']}) and Zone {overlap['zone2_id']} ({overlap['type2']})")
            print(f"  Zone {overlap['zone1_id']} Range: {overlap['zone1_range']}")
            print(f"  Zone {overlap['zone2_id']} Range: {overlap['zone2_range']}")
            print(f"  Overlap Amount: {overlap['overlap_amount']:.2f} points")
            print(f"  Zone {overlap['zone1_id']}: Formation={overlap['zone1_formation']}, State={overlap['zone1_state']}, Active={overlap['zone1_active']}")
            print(f"  Zone {overlap['zone2_id']}: Formation={overlap['zone2_formation']}, State={overlap['zone2_state']}, Active={overlap['zone2_active']}")
        
        if len(overlapping_groups) > 50:
            print(f"\n... and {len(overlapping_groups) - 50} more overlaps")
    
    if not exact_duplicates and not overlapping_groups:
        print("\n✓ No overlapping zones found - each price range is unique!")

def main():
    # Define file paths
    base_path = r"D:\SD_Engine\SD_Engine_v5.0\SD_Trading_V4\results\live_trades"
    files_to_analyze = [
        'zones_live_active.json',
        'zones_live_master.json'
    ]
    
    for filename in files_to_analyze:
        filepath = os.path.join(base_path, filename)
        
        if not os.path.exists(filepath):
            print(f"\nFile not found: {filepath}")
            continue
        
        # Load zones
        zones = load_zones(filepath)
        
        # Analyze overlaps
        overlapping_groups, exact_duplicates = analyze_zone_overlaps(zones)
        
        # Print report
        print_report(zones, overlapping_groups, exact_duplicates, filename)
    
    print("\n" + "="*80)
    print("Analysis complete!")
    print("="*80)

if __name__ == "__main__":
    main()
