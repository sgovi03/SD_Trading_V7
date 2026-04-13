#!/usr/bin/env python3
# ============================================================
# SD TRADING V8 - VOLUME BASELINE GENERATOR
# scripts/generate_baselines_from_db.py
#
# Reads bars directly from the V8 SQLite DB and generates
# per-symbol volume baseline JSON files used by LiveEngine
# for V6.0 volume scoring.
#
# Usage:
#   python scripts/generate_baselines_from_db.py
#   python scripts/generate_baselines_from_db.py --db data/sd_trading_v8.db
#   python scripts/generate_baselines_from_db.py --symbol NIFTY-FUT --lookback 30
#   python scripts/generate_baselines_from_db.py --list-symbols
# ============================================================

import sqlite3
import json
import os
import sys
import argparse
from datetime import datetime, timedelta
from collections import defaultdict

# ── NSE market session ────────────────────────────────────────
SESSION_START = "09:15"
SESSION_END   = "15:30"
BAR_MINUTES   = 5

def all_session_slots():
    """Return all 5-min time slots for an NSE trading day."""
    slots = []
    h, m = 9, 15
    end_h, end_m = 15, 30
    while (h, m) <= (end_h, end_m):
        slots.append(f"{h:02d}:{m:02d}")
        m += BAR_MINUTES
        if m >= 60:
            m -= 60
            h += 1
    return slots   # 75 slots

def time_slot(timestamp_str):
    """
    Extract HH:MM slot from ISO8601 timestamp.
    Handles: '2025-08-01T09:15:00+05:30' and '2025-08-01 09:15:00'
    Always rounds DOWN to nearest 5-min boundary.
    """
    # Normalise: strip timezone suffix
    ts = timestamp_str.replace("T", " ").split("+")[0].split("-05")[0]
    try:
        dt = datetime.fromisoformat(ts)
    except ValueError:
        dt = datetime.strptime(ts[:19], "%Y-%m-%d %H:%M:%S")
    h = dt.hour
    m = (dt.minute // BAR_MINUTES) * BAR_MINUTES
    return f"{h:02d}:{m:02d}"

def generate_baseline_for_symbol(conn, symbol, lookback_days, out_dir):
    """
    Generate volume baseline JSON for one symbol.
    Returns path to written file, or None on error.
    """
    print(f"\n{'='*55}")
    print(f"  Symbol  : {symbol}")
    print(f"  Lookback: {lookback_days} days")

    cur = conn.cursor()

    # ── 1. Determine date range ───────────────────────────────
    cur.execute(
        "SELECT MIN(time), MAX(time), COUNT(*) FROM bars WHERE symbol = ?",
        (symbol,)
    )
    row = cur.fetchone()
    if not row or row[2] == 0:
        print(f"  ❌ No bars found for {symbol} in DB — skipping.")
        return None

    min_ts, max_ts, total_bars = row
    print(f"  DB range: {min_ts}  →  {max_ts}  ({total_bars:,} bars total)")

    # Parse max date and apply lookback
    max_dt = datetime.fromisoformat(
        max_ts.replace("T", " ").split("+")[0][:19]
    )
    cutoff_dt = max_dt - timedelta(days=lookback_days)
    cutoff_str = cutoff_dt.strftime("%Y-%m-%d")

    # ── 2. Load bars within lookback window ──────────────────
    cur.execute(
        """
        SELECT time, volume
        FROM   bars
        WHERE  symbol = ?
          AND  time   >= ?
        ORDER  BY time
        """,
        (symbol, cutoff_str)
    )
    rows = cur.fetchall()

    if not rows:
        print(f"  ❌ No bars in last {lookback_days} days — using all available data.")
        cur.execute(
            "SELECT time, volume FROM bars WHERE symbol = ? ORDER BY time",
            (symbol,)
        )
        rows = cur.fetchall()

    print(f"  Bars used: {len(rows):,}  (from {cutoff_str} to {max_ts[:10]})")

    # ── 3. Compute average volume per time slot ───────────────
    slot_volumes = defaultdict(list)
    skipped = 0
    for ts, vol in rows:
        try:
            slot = time_slot(ts)
            if slot in all_session_slots_set:
                slot_volumes[slot].append(float(vol))
            else:
                skipped += 1   # pre-market / post-market bar
        except Exception:
            skipped += 1

    if skipped:
        print(f"  Skipped {skipped} bars outside session window.")

    if not slot_volumes:
        print(f"  ❌ No valid session bars found — cannot generate baseline.")
        return None

    # Average per slot
    slot_avg = {slot: sum(vols) / len(vols)
                for slot, vols in slot_volumes.items()}

    # ── 4. Fill missing slots with session-wide average ──────
    session_avg = sum(slot_avg.values()) / len(slot_avg)
    all_slots   = all_session_slots()
    output = {}
    missing = []

    for slot in all_slots:
        if slot in slot_avg:
            output[slot] = round(slot_avg[slot], 2)
        else:
            output[slot] = round(session_avg, 2)
            missing.append(slot)

    if missing:
        print(f"  Filled {len(missing)} missing slots with session avg "
              f"({session_avg:,.0f}): {missing[:5]}{'...' if len(missing)>5 else ''}")

    # ── 5. Write JSON ─────────────────────────────────────────
    # Filename convention matches conf/symbols/<SYMBOL>.config
    # volume_baseline_file = data/baselines/NIFTY_volume_baseline_5min.json
    safe_sym  = symbol.replace("-", "_")          # NIFTY-FUT → NIFTY_FUT
    filename  = f"{safe_sym}_volume_baseline_5min.json"
    out_path  = os.path.join(out_dir, filename)

    os.makedirs(out_dir, exist_ok=True)
    with open(out_path, "w") as f:
        json.dump(output, f, indent=2)

    # ── 6. Summary ────────────────────────────────────────────
    sorted_vols = sorted(output.values())
    p10 = sorted_vols[len(sorted_vols) // 10]
    p90 = sorted_vols[9 * len(sorted_vols) // 10]

    print(f"  ✅ Written: {out_path}")
    print(f"     Slots : {len(output)} / {len(all_slots)}")
    print(f"     Avg   : {session_avg:>12,.0f}")
    print(f"     P10   : {p10:>12,.0f}")
    print(f"     P90   : {p90:>12,.0f}")
    print(f"     Peak  : {sorted_vols[-1]:>12,.0f}  @ "
          f"{max(output, key=output.get)}")
    print(f"     Open  : {output.get('09:15', 0):>12,.0f}  (09:15)")
    print(f"     Close : {output.get('15:25', 0):>12,.0f}  (15:25)")

    return out_path


# ── Pre-compute set for O(1) lookup ──────────────────────────
all_session_slots_set = set(all_session_slots())


def list_symbols(conn):
    cur = conn.cursor()
    cur.execute(
        """
        SELECT b.symbol, COUNT(*) as bars,
               MIN(b.time) as first_bar, MAX(b.time) as last_bar
        FROM   bars b
        GROUP  BY b.symbol
        ORDER  BY b.symbol
        """
    )
    rows = cur.fetchall()
    if not rows:
        print("No bars found in DB.")
        return
    print(f"\n{'Symbol':<20} {'Bars':>8}  {'First':>22}  {'Last':>22}")
    print("-" * 80)
    for sym, cnt, first, last in rows:
        print(f"{sym:<20} {cnt:>8,}  {first:>22}  {last:>22}")


def main():
    parser = argparse.ArgumentParser(
        description="Generate V8 volume baseline files from SQLite DB"
    )
    parser.add_argument(
        "--db",
        default="data/sd_trading_v8.db",
        help="Path to sd_trading_v8.db  (default: data/sd_trading_v8.db)"
    )
    parser.add_argument(
        "--out-dir",
        default="data/baselines",
        help="Output directory for baseline JSON files  (default: data/baselines)"
    )
    parser.add_argument(
        "--symbol",
        default=None,
        help="Generate for one symbol only  (default: all symbols in DB)"
    )
    parser.add_argument(
        "--lookback",
        type=int,
        default=60,
        help="Number of calendar days to include  (default: 60)"
    )
    parser.add_argument(
        "--list-symbols",
        action="store_true",
        help="List symbols and bar counts in DB then exit"
    )
    args = parser.parse_args()

    # ── Connect ───────────────────────────────────────────────
    if not os.path.exists(args.db):
        print(f"❌ DB not found: {args.db}")
        print(f"   Run the engine first to populate bars.")
        sys.exit(1)

    conn = sqlite3.connect(args.db)
    conn.row_factory = sqlite3.Row

    try:
        if args.list_symbols:
            list_symbols(conn)
            return

        # ── Determine which symbols to process ────────────────
        if args.symbol:
            symbols = [args.symbol]
        else:
            cur = conn.cursor()
            cur.execute(
                "SELECT DISTINCT symbol FROM bars ORDER BY symbol"
            )
            symbols = [r[0] for r in cur.fetchall()]

        if not symbols:
            print("❌ No symbols found in bars table.")
            sys.exit(1)

        print(f"\nSD TRADING V8 — Volume Baseline Generator")
        print(f"DB        : {os.path.abspath(args.db)}")
        print(f"Out dir   : {os.path.abspath(args.out_dir)}")
        print(f"Lookback  : {args.lookback} days")
        print(f"Symbols   : {symbols}")

        # ── Generate per symbol ───────────────────────────────
        written = []
        for sym in symbols:
            path = generate_baseline_for_symbol(
                conn, sym, args.lookback, args.out_dir
            )
            if path:
                written.append(path)

        # ── Final summary ─────────────────────────────────────
        print(f"\n{'='*55}")
        print(f"  Done. {len(written)} / {len(symbols)} baseline(s) written.")
        for p in written:
            print(f"    {p}")
        print()

    finally:
        conn.close()


if __name__ == "__main__":
    main()
