#!/usr/bin/env python3
# ============================================================
# SD TRADING V8 - LIVE SIMULATOR
# scripts/live_sim_v8.py
#
# PURPOSE:
#   Reads bars from a CSV file and sends them one by one
#   through the named pipe to V8 engine — simulating live
#   AmiBroker feed exactly as market would deliver bars.
#
# CSV FORMAT (same as AmiBroker export):
#   Timestamp,DateTime,Symbol,Open,High,Low,Close,Volume,OpenInterest
#   OR simple:
#   DateTime,Open,High,Low,Close,Volume,OpenInterest
#
# USAGE:
#   python live_sim_v8.py --csv data\LiveTest\nifty_test.csv --symbol NIFTY
#   python live_sim_v8.py --csv data\LiveTest\nifty_test.csv --symbol NIFTY --interval 1
#   python live_sim_v8.py --csv data\LiveTest\nifty_test.csv --symbol NIFTY --dry-run
#
# REQUIREMENTS:
#   pip install pywin32
# ============================================================

import argparse
import csv
import time
import sys
import os

try:
    import win32pipe
    import win32file
    import pywintypes
    WINDOWS = True
except ImportError:
    WINDOWS = False

TIMEFRAME  = "5min"
SOURCE     = "AMIBROKER"
IST_OFFSET = "+05:30"
PIPE_BASE  = r"\\.\pipe\SD_{symbol}-FUT_5min"


def fmt2(val):
    return f"{float(val):.2f}"


def fmt_int(val):
    try:
        return str(int(float(val)))
    except:
        return "0"


def detect_columns(header):
    """
    Auto-detect CSV column layout from header row.
    Returns dict: {field: col_index}
    """
    h = [c.strip().lower() for c in header]
    mapping = {}

    # DateTime column — try several names
    for name in ['datetime', 'date', 'time', 'timestamp', 'bar_time']:
        if name in h:
            mapping['datetime'] = h.index(name)
            break

    # If both Timestamp(unix) and DateTime exist, prefer DateTime
    if 'datetime' in h and 'timestamp' in h:
        mapping['datetime'] = h.index('datetime')

    for field, candidates in {
        'open':          ['open', 'o'],
        'high':          ['high', 'h'],
        'low':           ['low', 'l'],
        'close':         ['close', 'c'],
        'volume':        ['volume', 'vol', 'v'],
        'open_interest': ['openinterest', 'open_interest', 'oi', 'openint'],
    }.items():
        for c in candidates:
            if c in h:
                mapping[field] = h.index(c)
                break

    return mapping


def parse_datetime(raw):
    """
    Parse datetime string and return ISO8601 with IST offset.
    Handles: YYYY-MM-DD HH:MM:SS, DD-MM-YYYY HH:MM:SS, already ISO8601
    """
    raw = raw.strip()

    # Already has T and offset
    if 'T' in raw and ('+' in raw or 'Z' in raw):
        return raw

    # Replace T with space for parsing
    raw_parse = raw.replace('T', ' ')

    formats = [
        '%Y-%m-%d %H:%M:%S',
        '%d-%m-%Y %H:%M:%S',
        '%Y/%m/%d %H:%M:%S',
        '%d/%m/%Y %H:%M:%S',
        '%Y-%m-%d %H:%M',
        '%d-%m-%Y %H:%M',
    ]
    for fmt in formats:
        try:
            dt = __import__('datetime').datetime.strptime(raw_parse, fmt)
            return dt.strftime('%Y-%m-%dT%H:%M:%S') + IST_OFFSET
        except ValueError:
            continue

    # Return as-is if can't parse
    return raw.replace(' ', 'T') + IST_OFFSET


def build_wire(symbol, dt_str, o, h, l, c, v, oi):
    return (f"{SOURCE}|{symbol}|{TIMEFRAME}|{dt_str}|"
            f"{fmt2(o)}|{fmt2(h)}|{fmt2(l)}|{fmt2(c)}|"
            f"{fmt_int(v)}|{fmt_int(oi)}\n")


def send_to_pipe(pipe_name, wire):
    """Open pipe as client, write one line, close."""
    try:
        handle = win32file.CreateFile(
            pipe_name,
            win32file.GENERIC_WRITE,
            0, None,
            win32file.OPEN_EXISTING,
            0, None
        )
        win32file.WriteFile(handle, wire.encode('utf-8'))
        win32file.CloseHandle(handle)
        return True
    except pywintypes.error as e:
        print(f"  [PIPE ERROR] {e}")
        return False


def main():
    parser = argparse.ArgumentParser(
        description="SD Trading V8 — Live Bar Simulator via Named Pipe"
    )
    parser.add_argument("--csv",      required=True,
                        help="Path to CSV file with bar data")
    parser.add_argument("--symbol",   default="NIFTY",
                        help="Symbol: NIFTY or BANKNIFTY (default: NIFTY)")
    parser.add_argument("--interval", type=float, default=5.0,
                        help="Seconds between bars (default: 5.0)")
    parser.add_argument("--skip",     type=int, default=0,
                        help="Skip first N rows (default: 0)")
    parser.add_argument("--dry-run",  action="store_true",
                        help="Print wire lines without sending to pipe")
    args = parser.parse_args()

    symbol    = args.symbol.upper()
    pipe_name = PIPE_BASE.format(symbol=symbol)
    dry_run   = args.dry_run or not WINDOWS

    if not os.path.exists(args.csv):
        print(f"[ERROR] CSV file not found: {args.csv}")
        sys.exit(1)

    print("=" * 60)
    print("  SD TRADING V8 — LIVE SIMULATOR")
    print("=" * 60)
    print(f"  Symbol:    {symbol}")
    print(f"  CSV:       {args.csv}")
    print(f"  Pipe:      {pipe_name}")
    print(f"  Interval:  {args.interval}s per bar")
    print(f"  Mode:      {'DRY RUN' if dry_run else 'LIVE (writing to pipe)'}")
    if not WINDOWS:
        print("  [WARN] pywin32 not installed — forced dry-run")
        print("         pip install pywin32")
    print("=" * 60)
    print()

    if not dry_run:
        print(f"Waiting for run_v8_live.exe to create pipe server...")
        print(f"Make sure run_v8_live.exe is running first.")
        print(f"Press Ctrl+C to abort.\n")

    # Read CSV — auto-detect if header row exists
    with open(args.csv, 'r', newline='', encoding='utf-8-sig') as f:
        reader = csv.reader(f)
        first_row = next(reader)
        rows_rest = list(reader)

    # Check if first row is a header (contains non-numeric text like 'datetime')
    # If first cell looks like a number it's data, not a header
    def is_header(row):
        for cell in row:
            c = cell.strip().lower()
            if c in ['datetime','date','time','timestamp','open','high','low','close','volume']:
                return True
        return False

    if is_header(first_row):
        header = first_row
        col    = detect_columns(header)
        rows   = rows_rest
        print(f"  Header detected: {header}")
    else:
        # No header — use positional layout from first row
        # Detect layout: Timestamp,DateTime,Symbol,O,H,L,C,V,OI (9 cols)
        # OR: DateTime,O,H,L,C,V,OI (7 cols)
        # OR: DateTime,Symbol,O,H,L,C,V,OI (8 cols)
        rows = [first_row] + rows_rest
        n = len(first_row)

        # Check if col[0] looks like unix timestamp (10 digits)
        if len(first_row[0].strip()) == 10 and first_row[0].strip().isdigit():
            # Format: Timestamp, DateTime, Symbol, O, H, L, C, V, OI
            col = {'datetime':1, 'open':3, 'high':4, 'low':5, 'close':6, 'volume':7, 'open_interest':8}
            print(f"  No header — detected: Timestamp,DateTime,Symbol,O,H,L,C,V,OI")
        elif n >= 8 and not first_row[0].strip().replace('.','').isdigit():
            # Format: DateTime, Symbol, O, H, L, C, V, OI
            col = {'datetime':0, 'open':2, 'high':3, 'low':4, 'close':5, 'volume':6, 'open_interest':7}
            print(f"  No header — detected: DateTime,Symbol,O,H,L,C,V,OI")
        else:
            # Format: DateTime, O, H, L, C, V, OI
            col = {'datetime':0, 'open':1, 'high':2, 'low':3, 'close':4, 'volume':5, 'open_interest':6}
            print(f"  No header — detected: DateTime,O,H,L,C,V,OI")

    if 'datetime' not in col:
        print(f"[ERROR] Cannot detect datetime column.")
        sys.exit(1)

    print(f"  Column mapping: {col}")
    print()

    # Skip rows
    if args.skip > 0:
        rows = rows[args.skip:]
        print(f"  Skipping first {args.skip} rows.")

    total   = len(rows)
    sent    = 0
    failed  = 0
    skipped = 0

    print(f"  Sending {total} bars...\n")

    try:
        for i, row in enumerate(rows):
            if not row or len(row) < 5:
                skipped += 1
                continue

            try:
                dt_raw = row[col['datetime']]
                o      = row[col.get('open',  col['close'])]
                h      = row[col.get('high',  col['close'])]
                l      = row[col.get('low',   col['close'])]
                c      = row[col['close']]
                v      = row[col.get('volume', 0)] if 'volume' in col else '0'
                oi     = row[col.get('open_interest', 0)] if 'open_interest' in col else '0'
            except (IndexError, KeyError) as e:
                print(f"  [ROW {i+1}] Parse error: {e} — skipping")
                skipped += 1
                continue

            dt_str = parse_datetime(dt_raw)
            wire   = build_wire(symbol, dt_str, o, h, l, c, v, oi)

            if dry_run:
                print(f"  [{i+1:>5}/{total}] {wire.strip()}")
                sent += 1
            else:
                ok = send_to_pipe(pipe_name, wire)
                if ok:
                    sent += 1
                    print(f"  [{i+1:>5}/{total}] SENT: {wire.strip()}")
                else:
                    failed += 1
                    print(f"  [{i+1:>5}/{total}] FAIL: {wire.strip()}")

            time.sleep(args.interval)

    except KeyboardInterrupt:
        print("\n\n[STOPPED] Ctrl+C received.")

    print()
    print("=" * 60)
    print(f"  DONE: Sent={sent}  Failed={failed}  Skipped={skipped}")
    print("=" * 60)


if __name__ == "__main__":
    main()