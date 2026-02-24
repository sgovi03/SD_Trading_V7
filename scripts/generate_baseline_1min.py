import pandas as pd
import json
from datetime import datetime, timedelta

def generate_volume_baseline_1min(csv_file, lookback_days=20):
    """
    Generate volume baseline for 1-MINUTE bars
    Creates 5-minute time slots with proper averages
    """
    print(f"Loading 1-minute data from: {csv_file}")
    df = pd.read_csv(csv_file)
    df['DateTime'] = pd.to_datetime(df['DateTime'])
    
    # Filter last N days
    cutoff = df['DateTime'].max() - timedelta(days=lookback_days)
    df = df[df['DateTime'] >= cutoff]
    print(f"Using {len(df)} bars from last {lookback_days} days")
    
    # Round time to 5-minute slots
    def round_to_5min(dt):
        hour = dt.hour
        minute = (dt.minute // 5) * 5
        return f"{hour:02d}:{minute:02d}"
    
    df['time_slot'] = df['DateTime'].apply(round_to_5min)
    
    # Calculate AVERAGE volume per 5-minute slot
    baseline = df.groupby('time_slot')['Volume'].mean().to_dict()
    
    # Generate ALL time slots (09:15 to 15:30)
    all_slots = []
    start = datetime.strptime("09:15", "%H:%M")
    end = datetime.strptime("15:30", "%H:%M")
    current = start
    
    while current <= end:
        slot = current.strftime("%H:%M")
        all_slots.append(slot)
        current += timedelta(minutes=5)
    
    # Fill any missing slots with nearby average
    output = {}
    for slot in all_slots:
        if slot in baseline:
            output[slot] = baseline[slot]
        else:
            # Use average of all slots as fallback
            avg = sum(baseline.values()) / len(baseline) if baseline else 50000.0
            output[slot] = avg
            print(f"  Filled missing slot {slot} with average: {avg:.0f}")
    
    # Save to JSON
    with open('data/baselines/volume_baseline.json', 'w') as f:
        json.dump(output, f, indent=2)
    
    print(f"\n✅ Baseline generated successfully!")
    print(f"   Total slots: {len(output)}")
    print(f"   From: {all_slots[0]} to {all_slots[-1]}")
    print(f"   Sample values:")
    for i, slot in enumerate(all_slots[:5]):
        print(f"      {slot}: {output[slot]:,.0f}")
    print(f"   Average volume: {sum(output.values()) / len(output):,.0f}")
    
    return output

# Usage:
if __name__ == "__main__":
    baseline = generate_volume_baseline_1min(
        csv_file='data/LiveTest/live_data-DRYRUN.csv',
        lookback_days=20  # Use last 20 days of data
    )