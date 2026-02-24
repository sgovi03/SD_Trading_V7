"""
Add OI_Fresh and OI_Age_Seconds columns to 9-column CSV for V6.0 compatibility.

This script:
1. Reads the 9-column CSV
2. Calculates OI_Fresh as the change in OpenInterest from previous bar
3. Sets OI_Age_Seconds to 0 (assuming fresh data)
4. Writes to new 11-column CSV

Usage:
    python add_oi_columns.py <input_csv> <output_csv>
    
Example:
    python add_oi_columns.py data/LiveTest/live_data-DRYRUN.csv data/LiveTest/live_data-DRYRUN-11col.csv
"""

import sys
import csv

def add_oi_columns(input_file, output_file):
    print(f"Reading: {input_file}")
    
    rows = []
    prev_oi = 0
    
    with open(input_file, 'r') as f:
        reader = csv.reader(f)
        header = next(reader)
        
        # Validate input
        if len(header) != 9:
            print(f"❌ ERROR: Expected 9 columns, found {len(header)}")
            print(f"   Columns: {header}")
            return False
        
        print(f"✅ Input has 9 columns: {header}")
        
        # Process data rows
        for row in reader:
            timestamp, datetime, symbol, o, h, l, c, volume, oi = row
            
            # Calculate OI_Fresh (change from previous bar)
            oi_value = float(oi)
            oi_fresh = oi_value - prev_oi if prev_oi > 0 else 0
            prev_oi = oi_value
            
            # OI_Age_Seconds = 0 (assume fresh data)
            oi_age_seconds = 0
            
            # Append new columns
            rows.append(row + [str(int(oi_fresh)), str(oi_age_seconds)])
    
    # Write output
    print(f"Writing: {output_file}")
    with open(output_file, 'w', newline='') as f:
        writer = csv.writer(f)
        
        # Write header with new columns
        new_header = header + ['OI_Fresh', 'OI_Age_Seconds']
        writer.writerow(new_header)
        
        # Write data rows
        writer.writerows(rows)
    
    print(f"✅ SUCCESS: Created 11-column CSV")
    print(f"   Total rows: {len(rows)}")
    print(f"   Columns: {new_header}")
    return True

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print("Usage: python add_oi_columns.py <input_csv> <output_csv>")
        print("Example: python add_oi_columns.py data/LiveTest/live_data-DRYRUN.csv data/LiveTest/live_data-DRYRUN-11col.csv")
        sys.exit(1)
    
    input_file = sys.argv[1]
    output_file = sys.argv[2]
    
    success = add_oi_columns(input_file, output_file)
    sys.exit(0 if success else 1)
