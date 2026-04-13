#!/usr/bin/env python3
"""
NIFTY Historical Data Transformer
Converts NIFTY 1-minute CSV data to SD Backtest Engine format
"""

import csv
from datetime import datetime
import sys
import os

def convert_datetime_to_unix(date_str):
    """
    Convert DD-MM-YYYY HH:MM:SS to Unix timestamp
    Example: 08-02-2024 09:15:00 -> 1707375900
    """
    try:
        # Parse the date string
        dt = datetime.strptime(date_str, '%d-%m-%Y %H:%M:%S')
        
        # Convert to Unix timestamp
        unix_timestamp = int(dt.timestamp())
        
        return unix_timestamp
    except Exception as e:
        print(f"Error converting date: {date_str} - {e}")
        return 0

def convert_datetime_format(date_str):
    """
    Convert DD-MM-YYYY HH:MM:SS to YYYY-MM-DD HH:MM:SS
    Example: 08-02-2024 09:15:00 -> 2024-02-08 09:15:00
    """
    try:
        dt = datetime.strptime(date_str, '%d-%m-%Y %H:%M:%S')
        return dt.strftime('%Y-%m-%d %H:%M:%S')
    except Exception as e:
        print(f"Error formatting date: {date_str} - {e}")
        return date_str

def transform_data(input_file, output_file):
    """
    Transform NIFTY CSV to Backtest Engine format
    
    Input format:
    Ticker,Date/Time,Open,High,Low,Close,Volume,Open Interest
    
    Output format:
    Timestamp,DateTime,Symbol,Open,High,Low,Close,Volume,OpenInterest
    """
    
    print("\n" + "="*60)
    print("NIFTY DATA TRANSFORMER")
    print("="*60 + "\n")
    
    # Check if input file exists
    if not os.path.exists(input_file):
        print(f"❌ Error: Input file not found: {input_file}")
        return False
    
    print(f"📥 Reading: {input_file}")
    
    rows_read = 0
    rows_written = 0
    errors = 0
    
    try:
        with open(input_file, 'r') as infile, open(output_file, 'w', newline='') as outfile:
            reader = csv.reader(infile)
            writer = csv.writer(outfile)
            
            # Read header
            header = next(reader)
            print(f"   Input columns: {', '.join(header)}")
            
            # Write new header
            new_header = ['Timestamp', 'DateTime', 'Symbol', 'Open', 'High', 'Low', 'Close', 'Volume', 'OpenInterest']
            writer.writerow(new_header)
            print(f"   Output columns: {', '.join(new_header)}")
            print()
            
            # Process data rows
            print("🔄 Transforming data...")
            
            for row in reader:
                rows_read += 1
                
                try:
                    # Parse input row
                    ticker = row[0].strip()
                    datetime_str = row[1].strip()
                    open_price = float(row[2])
                    high_price = float(row[3])
                    low_price = float(row[4])
                    close_price = float(row[5])
                    volume = int(float(row[6])) if row[6] else 0
                    open_interest = int(float(row[7])) if row[7] else 0
                    
                    # Convert datetime
                    unix_timestamp = convert_datetime_to_unix(datetime_str)
                    formatted_datetime = convert_datetime_format(datetime_str)
                    
                    # Use NIFTY-FUT as symbol for consistency
                    symbol = "NIFTY-FUT"
                    
                    # Write transformed row
                    output_row = [
                        unix_timestamp,
                        formatted_datetime,
                        symbol,
                        f"{open_price:.2f}",
                        f"{high_price:.2f}",
                        f"{low_price:.2f}",
                        f"{close_price:.2f}",
                        volume,
                        open_interest
                    ]
                    
                    writer.writerow(output_row)
                    rows_written += 1
                    
                    # Progress indicator
                    if rows_written % 10000 == 0:
                        print(f"   Processed: {rows_written:,} rows...")
                    
                except Exception as e:
                    errors += 1
                    if errors <= 10:  # Only show first 10 errors
                        print(f"   ⚠️  Error on row {rows_read}: {e}")
        
        print()
        print("="*60)
        print("✅ TRANSFORMATION COMPLETE!")
        print("="*60)
        print(f"📊 Statistics:")
        print(f"   Rows read:    {rows_read:,}")
        print(f"   Rows written: {rows_written:,}")
        print(f"   Errors:       {errors:,}")
        print(f"   Success rate: {(rows_written/rows_read*100):.2f}%")
        print()
        print(f"📁 Output file: {output_file}")
        print(f"   File size: {os.path.getsize(output_file) / (1024*1024):.2f} MB")
        print()
        
        # Show sample data
        print("📋 Sample output (first 3 rows):")
        with open(output_file, 'r') as f:
            for i, line in enumerate(f):
                if i < 4:  # Header + 3 rows
                    print(f"   {line.strip()}")
        
        print()
        print("🎯 Ready for backtesting!")
        print(f"   Command: sd_backtest.exe {output_file}")
        print()
        
        return True
        
    except Exception as e:
        print(f"\n❌ Fatal error: {e}")
        return False

def analyze_date_range(input_file):
    """
    Analyze the date range in the input file
    """
    print("\n📅 Analyzing date range...")
    
    try:
        with open(input_file, 'r') as f:
            reader = csv.reader(f)
            next(reader)  # Skip header
            
            rows = list(reader)
            if not rows:
                print("   No data found")
                return
            
            first_date = rows[0][1]
            last_date = rows[-1][1]
            total_rows = len(rows)
            
            print(f"   First bar: {first_date}")
            print(f"   Last bar:  {last_date}")
            print(f"   Total bars: {total_rows:,}")
            
            # Calculate trading days
            first_dt = datetime.strptime(first_date, '%d-%m-%Y %H:%M:%S')
            last_dt = datetime.strptime(last_date, '%d-%m-%Y %H:%M:%S')
            days = (last_dt - first_dt).days
            
            print(f"   Calendar days: {days:,}")
            print(f"   Avg bars/day: {total_rows/days:.0f}")
            print()
            
    except Exception as e:
        print(f"   Error analyzing: {e}")

def main():
    """Main function"""
    
    # Default file names
    input_file = "NIFTY_1min.csv"
    output_file = "nifty_backtest_data.csv"
    
    # Check command line arguments
    if len(sys.argv) > 1:
        input_file = sys.argv[1]
    if len(sys.argv) > 2:
        output_file = sys.argv[2]
    
    print()
    print("Usage: python transform_nifty_data.py [input_file] [output_file]")
    print(f"Using input:  {input_file}")
    print(f"Using output: {output_file}")
    
    # Analyze date range
    if os.path.exists(input_file):
        analyze_date_range(input_file)
    
    # Transform data
    success = transform_data(input_file, output_file)
    
    if success:
        print("✅ Transformation successful!")
        print()
        print("Next steps:")
        print("1. Copy nifty_backtest_data.csv to D:\\ClaudeAi\\StandaloneCpp\\")
        print("2. Run: sd_backtest.exe nifty_backtest_data.csv")
        print("3. Analyze the results!")
        print()
    else:
        print("❌ Transformation failed!")
        sys.exit(1)

if __name__ == "__main__":
    main()
