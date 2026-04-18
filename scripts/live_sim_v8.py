#!/usr/bin/env python3
# ============================================================
# SD TRADING V8 - CSV TO PIPE (PERSISTENT STREAM MODE)
# ============================================================

import argparse
import csv
import time
import sys
import os

try:
    import win32file
    import pywintypes
    WINDOWS = True
except ImportError:
    WINDOWS = False
    print("[WARNING] win32file not found. This script requires Windows for Named Pipes.")

TIMEFRAME  = "5min"
SOURCE     = "AMIBROKER"
IST_OFFSET = "+05:30"
PIPE_BASE  = r"\\.\pipe\SD_{symbol}_5min"


# ============================================================
# PIPE SEND (PERSISTENT STREAM)
# ============================================================
def send_batch_to_pipe(handle, batch):
    """Send batch using an already open, persistent pipe handle"""
    try:
        for wire in batch:
            win32file.WriteFile(handle, wire.encode('utf-8'))
        return True
    except pywintypes.error as e:
        print(f"[PIPE ERROR] {e}")
        return False


# ============================================================
# HELPERS
# ============================================================
def fmt2(val):
    return f"{float(val):.2f}"

def fmt_int(val):
    try:
        return str(int(float(val)))
    except:
        return "0"

def detect_columns(header):
    h = [c.strip().lower() for c in header]
    mapping = {}

    for name in ['datetime', 'date', 'timestamp', 'time']:
        if name in h:
            mapping['datetime'] = h.index(name)
            break

    for field, candidates in {
        'open': ['open','o'],
        'high': ['high','h'],
        'low': ['low','l'],
        'close': ['close','c'],
        'volume': ['volume','vol','v'],
        'open_interest': ['openinterest','oi']
    }.items():
        for c in candidates:
            if c in h:
                mapping[field] = h.index(c)
                break

    return mapping

def parse_datetime(raw):
    raw = raw.strip()

    if 'T' in raw and ('+' in raw or 'Z' in raw):
        return raw

    raw = raw.replace('T', ' ')
    from datetime import datetime

    formats = [
        '%Y-%m-%d %H:%M:%S',
        '%d-%m-%Y %H:%M:%S',
        '%Y-%m-%d %H:%M',
        '%d-%m-%Y %H:%M',
    ]

    for fmt in formats:
        try:
            dt = datetime.strptime(raw, fmt)
            return dt.strftime('%Y-%m-%dT%H:%M:%S') + IST_OFFSET
        except:
            pass

    return raw.replace(' ', 'T') + IST_OFFSET

def build_wire(symbol, dt_str, o, h, l, c, v, oi):
    return (f"{SOURCE}|{symbol}|{TIMEFRAME}|{dt_str}|"
            f"{fmt2(o)}|{fmt2(h)}|{fmt2(l)}|{fmt2(c)}|"
            f"{fmt_int(v)}|{fmt_int(oi)}\n")


# ============================================================
# MAIN
# ============================================================
def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--csv", required=True)
    parser.add_argument("--symbol", default="NIFTY")
    parser.add_argument("--interval", type=float, default=1.0)
    parser.add_argument("--batch-size", type=int, default=50)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    if not WINDOWS and not args.dry_run:
        print("[ERROR] Cannot run live pipe connection without pywin32 on Windows.")
        sys.exit(1)

    symbol = args.symbol.upper()
    pipe_name = PIPE_BASE.format(symbol=symbol)

    if not os.path.exists(args.csv):
        print("[ERROR] CSV file not found")
        sys.exit(1)

    print(f"\n[INFO] Symbol: {symbol}")
    print(f"[INFO] Pipe: {pipe_name}")
    print(f"[INFO] Batch size: {args.batch_size}")
    print(f"[INFO] Mode: {'DRY RUN' if args.dry_run else 'LIVE'}\n")

    # Read CSV
    with open(args.csv, 'r', encoding='utf-8-sig') as f:
        reader = csv.reader(f)
        header = next(reader)
        rows = list(reader)

    col = detect_columns(header)
    print(f"[INFO] Total rows: {len(rows)}\n")

    handle = None
    
    # Establish persistent connection before starting the loop
    if not args.dry_run:
        print(f"[PIPE] Waiting to connect to C++ server at {pipe_name}...")
        while True:
            try:
                handle = win32file.CreateFile(
                    pipe_name,
                    win32file.GENERIC_WRITE,
                    0, None,
                    win32file.OPEN_EXISTING,
                    0, None
                )
                print("[PIPE] Connected successfully!")
                break
            except pywintypes.error as e:
                if e.winerror in (2, 231):  # NOT FOUND / BUSY
                    time.sleep(1)
                    continue
                else:
                    print(f"[PIPE ERROR] Could not connect: {e}")
                    sys.exit(1)

    batch = []
    sent = 0
    failed = 0

    try:
        for i, row in enumerate(rows):
            dt = parse_datetime(row[col['datetime']])
            o  = row[col['open']]
            h  = row[col['high']]
            l  = row[col['low']]
            c  = row[col['close']]
            v  = row[col.get('volume', 0)] if 'volume' in col else 0
            oi = row[col.get('open_interest', 0)] if 'open_interest' in col else 0

            wire = build_wire(symbol, dt, o, h, l, c, v, oi)
            batch.append(wire)

            if len(batch) >= args.batch_size:
                if args.dry_run:
                    for w in batch:
                        print(w.strip())
                    sent += len(batch)
                else:
                    ok = send_batch_to_pipe(handle, batch)
                    if ok:
                        print(f"[BATCH SENT] {len(batch)} rows")
                        sent += len(batch)
                    else:
                        print("[BATCH FAILED] Connection to C++ lost.")
                        failed += len(batch)
                        break  # Stop processing if the pipe breaks

                batch.clear()
                time.sleep(args.interval)

        # Send remaining rows in the final batch
        if batch:
            if args.dry_run:
                for w in batch:
                    print(w.strip())
                sent += len(batch)
            else:
                ok = send_batch_to_pipe(handle, batch)
                if ok:
                    print(f"[FINAL BATCH SENT] {len(batch)} rows")
                    sent += len(batch)
                else:
                    print("[BATCH FAILED] Connection to C++ lost on final batch.")
                    failed += len(batch)

    except KeyboardInterrupt:
        print("\n[STOPPED BY USER]")

    finally:
        # Guarantee the pipe is closed cleanly no matter what happens
        if handle:
            win32file.CloseHandle(handle)
            print("[PIPE] Connection closed safely.")

    print(f"\nDONE: Sent={sent}, Failed={failed}")


if __name__ == "__main__":
    main()