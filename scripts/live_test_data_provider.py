import time
import argparse
import csv
import os

def append_rows(source_file, target_file, interval):
    print(f"Starting to append rows from {source_file} to {target_file} every {interval} seconds.")
    row_count = 0
    with open(source_file, 'r', newline='') as src, open(target_file, 'a', newline='') as tgt:
        reader = csv.reader(src)
        writer = csv.writer(tgt)
        for row in reader:
            writer.writerow(row)
            tgt.flush()
            row_count += 1
            print(f"Appended row {row_count}: {row}")
            time.sleep(interval)
    print(f"Completed appending {row_count} rows from {source_file} to {target_file}.")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Append rows from test_data to live_data-DRYRUN at a given speed.")
    parser.add_argument(
           "--source_file",
           default="D:/SD_System/SD_Volume_OI/data/LiveTest/test_data_5Mins.csv",
           help="Path to the test_data CSV file. Default: d:/SD_System/DEV/data/LiveTest/test_data.csv"
    )
    parser.add_argument(
           "--target_file",
           default="D:/SD_System/SD_Volume_OI/data/LiveTest/Live_Data_5Mins_20Feb2026_test.csv",
           help="Path to the live_data-DRYRUN CSV file. Default: d:/SD_System/DEV/data/LiveTest/live_data-DRYRUN.csv"
    )
    parser.add_argument("--interval", type=float, default=1.0, help="Interval between appends in seconds (default: 1.0)")
    args = parser.parse_args()

    if not os.path.exists(args.source_file):
        print(f"Source file {args.source_file} does not exist.")
        exit(1)
    if not os.path.exists(args.target_file):
        print(f"Target file {args.target_file} does not exist.")
        exit(1)

    append_rows(args.source_file, args.target_file, args.interval)