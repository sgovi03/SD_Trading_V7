"""
Volume Baseline Generator for SD Engine V6.0

Generates time-of-day volume averages for normalization from historical CSV data.
Run this script to generate baseline before market open or when updating baselines.

Usage:
    python build_volume_baseline.py --input data/live_data.csv --lookback 20 --output data/baselines/volume_baseline.json
    python build_volume_baseline.py --help
"""

import argparse
import json
import os
from datetime import datetime, timedelta
from collections import defaultdict
import pandas as pd

def build_volume_baseline_from_csv(input_file='data/live_data.csv', 
                                    lookback_days=20, 
                                    output_file='data/baselines/volume_baseline.json',
                                    resolution_minutes=1):
    """
    Build time-of-day volume baseline from CSV file
    
    Args:
        input_file: Path to CSV file with historical data
        lookback_days: Number of days to average over (default: 20)
        output_file: Where to save the JSON baseline
        resolution_minutes: Time resolution in minutes (1 or 5, default: 1)
    """
    print(f"Building volume baseline from CSV: {input_file}")
    print(f"Lookback period: {lookback_days} days")
    print(f"Resolution: {resolution_minutes} minute(s)")
    
    # Check if input file exists
    if not os.path.exists(input_file):
        print(f"❌ ERROR: Input file not found: {input_file}")
        return None
    
    # Read CSV file
    print("Reading CSV file...")
    try:
        # Try to read with the old format first (9 columns)
        df = pd.read_csv(input_file)
        
        # Check if we have the required columns
        required_cols = ['DateTime', 'Volume']
        if not all(col in df.columns for col in required_cols):
            print(f"❌ ERROR: CSV missing required columns. Found: {df.columns.tolist()}")
            return None
        
        print(f"  Loaded {len(df)} rows from CSV")
        
    except Exception as e:
        print(f"❌ ERROR reading CSV: {e}")
        return None
    
    # Convert DateTime to datetime object
    try:
        df['datetime'] = pd.to_datetime(df['DateTime'])
    except Exception as e:
        print(f"❌ ERROR parsing DateTime column: {e}")
        return None
    
    # Filter by lookback period
    if lookback_days > 0:
        cutoff_date = datetime.now() - timedelta(days=lookback_days)
        df = df[df['datetime'] >= cutoff_date]
        print(f"  Filtered to last {lookback_days} days: {len(df)} rows")
    
    # Extract time slot (HH:MM)
    df['time_slot'] = df['datetime'].dt.strftime('%H:%M')
    
    # Filter market hours only (09:15 - 15:30)
    df = df[df['time_slot'] >= '09:15']
    df = df[df['time_slot'] <= '15:30']
    print(f"  Market hours only: {len(df)} rows")
    
    # Resample to specified resolution and sum volume
    df.set_index('datetime', inplace=True)
    resample_rule = f'{resolution_minutes}T'  # '1T' for 1-min, '5T' for 5-min
    df_resampled = df.resample(resample_rule).agg({
        'Volume': 'sum'
    })
    df_resampled['time_slot'] = df_resampled.index.strftime('%H:%M')
    df_resampled = df_resampled[df_resampled['Volume'] > 0]  # Remove zero-volume bars
    
    print(f"  After {resolution_minutes}-min aggregation: {len(df_resampled)} bars")
    
    # Calculate average volume per time slot
    baseline = {}
    time_slot_groups = df_resampled.groupby('time_slot')
    
    for time_slot, group in time_slot_groups:
        avg_volume = group['Volume'].mean()
        baseline[time_slot] = round(avg_volume, 2)
    
    # Sort by time for readability
    baseline = dict(sorted(baseline.items()))
    
    # Print summary
    print(f"\n✅ Generated baseline for {len(baseline)} time slots")
    print("\nSample baseline values:")
    count = 0
    for time_slot, avg_vol in baseline.items():
        print(f"  {time_slot}: {avg_vol:,.0f} avg volume")
        count += 1
        if count >= 10:
            print(f"  ... ({len(baseline) - 10} more time slots)")
            break
    
    # Create output directory if needed
    output_dir = os.path.dirname(output_file)
    if output_dir and not os.path.exists(output_dir):
        os.makedirs(output_dir, exist_ok=True)
        print(f"\n📁 Created directory: {output_dir}")
    
    # Save to JSON
    try:
        with open(output_file, 'w') as f:
            json.dump(baseline, f, indent=2)
        print(f"\n✅ Volume baseline saved to: {output_file}")
        print(f"   {len(baseline)} time slots with averages")
    except Exception as e:
        print(f"\n❌ ERROR saving baseline: {e}")
        return None
    
    return baseline

def main():
    parser = argparse.ArgumentParser(
        description='Build volume baseline for SD Engine V6.0',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Generate 1-minute baseline (default):
  python build_volume_baseline.py --input data/live_data.csv --lookback 20
  
  # Generate 5-minute baseline:
  python build_volume_baseline.py --input data/live_data.csv --lookback 60 --resolution 5
  
  # Generate with custom output path:
  python build_volume_baseline.py --input data/historical.csv --resolution 1 --output custom_baseline.json
        """
    )
    
    parser.add_argument('--input', type=str, 
                       default='data/live_data.csv',
                       help='Input CSV file with historical data (default: data/live_data.csv)')
    
    parser.add_argument('--lookback', type=int, 
                       default=20, 
                       help='Days to look back for averaging (default: 20, 0 = all data)')
    
    parser.add_argument('--resolution', type=int,
                       choices=[1, 5],
                       default=1,
                       help='Time resolution in minutes: 1 or 5 (default: 1)')
    
    parser.add_argument('--output', type=str, 
                       default='data/baselines/volume_baseline.json',
                       help='Output file path (default: data/baselines/volume_baseline.json)')
    
    args = parser.parse_args()
    
    # Build baseline
    baseline = build_volume_baseline_from_csv(args.input, args.lookback, args.output, args.resolution)
    
    if baseline:
        print("\n" + "="*60)
        print("SUCCESS! Baseline generation complete.")
        print("="*60)
        return 0
    else:
        print("\n" + "="*60)
        print("FAILED! Check errors above.")
        print("="*60)
        return 1

if __name__ == "__main__":
    exit(main())