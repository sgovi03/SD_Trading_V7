"""
Live data replay driver

Purpose:
- Back up and split data/live_data.csv into historical + live stream slices for a simulated live run.
- Append rows from the live stream slice back into data/live_data.csv at a fixed interval to mimic real-time feed.
- Avoid modifying core engine code; operate purely as a helper script.

Assumptions:
- CSV has a timestamp column that can be parsed with the provided format (default: %Y-%m-%d %H:%M:%S).
- Engine re-reads data/live_data.csv as it grows.
- You will start the live engine separately (in live mode) pointing at data/live_data.csv before starting replay.

Usage examples:
- Prepare split + replay end-to-end (default 5s interval):
    python scripts/live_data_replay.py --historical-end "2025-08-22 23:59:59" --live-start "2025-08-25 09:15:00"

- Split only (no replay):
    python scripts/live_data_replay.py --split-only --historical-end "2025-08-22 23:59:59" --live-start "2025-08-25 09:15:00"

- Replay only (assumes files already split):
    python scripts/live_data_replay.py --replay-only

- Faster-than-real-time (no sleep):
    python scripts/live_data_replay.py --live-start "2025-08-25 09:15:00" --no-sleep
"""

import argparse
import csv
import datetime as dt
import os
import shutil
import sys
import time
from typing import Iterable, Tuple


DEFAULT_SOURCE = os.path.join("data", "live_data.csv")
DEFAULT_HIST = DEFAULT_SOURCE  # historical slice overwrites the original path
DEFAULT_LIVE_SLICE = os.path.join("data", "live_data_stream.csv")
DEFAULT_TS_COL = "timestamp"
DEFAULT_TS_FMT = "%Y-%m-%d %H:%M:%S"
DEFAULT_INTERVAL_SEC = 5


class ReplayError(Exception):
    """Custom exception for replay issues."""


def log(msg: str) -> None:
    ts = dt.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    print(f"[{ts}] {msg}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Live data replay driver")
    parser.add_argument("--source", default=DEFAULT_SOURCE, help="Path to original live_data.csv")
    parser.add_argument("--historical-out", default=DEFAULT_HIST, help="Path to write historical slice (default overwrites source)")
    parser.add_argument("--live-stream-out", default=DEFAULT_LIVE_SLICE, help="Path to write live stream slice")
    parser.add_argument("--backup-dir", default="data", help="Directory for backups")
    parser.add_argument("--timestamp-col", default=DEFAULT_TS_COL, help="Timestamp column name")
    parser.add_argument("--timestamp-format", default=DEFAULT_TS_FMT, help="Timestamp format for parsing")
    parser.add_argument("--historical-end", required=False, default="2025-08-22 23:59:59", help="Inclusive end of historical slice (e.g., 2025-08-22 23:59:59)")
    parser.add_argument("--live-start", required=False, default="2025-08-25 09:15:00", help="Start of live slice (e.g., 2025-08-25 09:15:00)")
    parser.add_argument("--interval-seconds", type=float, default=DEFAULT_INTERVAL_SEC, help="Seconds to wait between rows when replaying")
    parser.add_argument("--no-sleep", action="store_true", help="Do not sleep between rows (fast-forward mode)")
    parser.add_argument("--split-only", action="store_true", help="Only perform split, do not replay")
    parser.add_argument("--replay-only", action="store_true", help="Only replay existing live stream slice (skip split)")
    parser.add_argument("--max-rows", type=int, default=None, help="Limit rows replayed from live stream (for quick smoke runs)")
    return parser.parse_args()


def ensure_paths(args: argparse.Namespace) -> None:
    if not os.path.isfile(args.source):
        raise ReplayError(f"Source file not found: {args.source}")
    os.makedirs(os.path.dirname(args.historical_out) or ".", exist_ok=True)
    os.makedirs(os.path.dirname(args.live_stream_out) or ".", exist_ok=True)
    os.makedirs(args.backup_dir or ".", exist_ok=True)


def backup_file(src: str, backup_dir: str) -> str:
    stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    base = os.path.basename(src)
    dest = os.path.join(backup_dir, f"{base}.bak.{stamp}")
    shutil.copy2(src, dest)
    log(f"Backup created: {dest}")
    return dest


def parse_ts(value: str, fmt: str) -> dt.datetime:
    try:
        return dt.datetime.strptime(value.strip(), fmt)
    except Exception as exc:
        raise ReplayError(f"Failed to parse timestamp '{value}' with format '{fmt}'") from exc


def split_source(
    source: str,
    historical_out: str,
    live_stream_out: str,
    ts_col: str,
    ts_fmt: str,
    historical_end: dt.datetime,
    live_start: dt.datetime,
) -> Tuple[int, int, Iterable[str]]:
    """Split source CSV into historical and live slices.

    Returns (historical_count, live_count, header_fields).
    """
    hist_count = 0
    live_count = 0

    # Read all rows first to avoid truncation when historical_out == source
    with open(source, "r", newline="", encoding="utf-8") as src_file:
        reader = csv.DictReader(src_file)
        fieldnames = reader.fieldnames
        if not fieldnames or ts_col not in fieldnames:
            raise ReplayError(f"Timestamp column '{ts_col}' not found; available columns: {fieldnames}")
        rows = list(reader)

    with open(historical_out, "w", newline="", encoding="utf-8") as hist_file, \
         open(live_stream_out, "w", newline="", encoding="utf-8") as live_file:
        hist_writer = csv.DictWriter(hist_file, fieldnames=fieldnames)
        live_writer = csv.DictWriter(live_file, fieldnames=fieldnames)
        hist_writer.writeheader()
        live_writer.writeheader()

        for row in rows:
            ts_raw = row.get(ts_col, "").strip()
            if not ts_raw:
                # Skip rows without timestamp (defensive for trailing blanks)
                continue

            try:
                ts_val = parse_ts(ts_raw, ts_fmt)
            except ReplayError:
                # Skip unparsable rows but keep going
                continue

            if ts_val <= historical_end:
                hist_writer.writerow(row)
                hist_count += 1
            elif ts_val >= live_start:
                live_writer.writerow(row)
                live_count += 1
            else:
                # Between historical_end and live_start: skip to avoid overlap/ambiguity.
                continue

    log(f"Split complete: {hist_count} historical rows -> {historical_out}; {live_count} live rows -> {live_stream_out}")
    return hist_count, live_count, fieldnames


def replay_stream(
    live_stream_path: str,
    append_target: str,
    fieldnames: Iterable[str],
    interval_sec: float,
    sleep_enabled: bool,
    max_rows: int | None,
) -> int:
    """Append rows from live_stream_path into append_target one-at-a-time."""
    sent = 0
    with open(live_stream_path, "r", newline="", encoding="utf-8") as live_file, \
         open(append_target, "a", newline="", encoding="utf-8") as target_file:
        reader = csv.DictReader(live_file)
        if reader.fieldnames != list(fieldnames):
            raise ReplayError(
                f"Field mismatch between live stream and target. Stream: {reader.fieldnames}, target expected: {fieldnames}"
            )
        writer = csv.DictWriter(target_file, fieldnames=fieldnames)

        for row in reader:
            writer.writerow(row)
            target_file.flush()
            os.fsync(target_file.fileno())
            sent += 1

            if max_rows is not None and sent >= max_rows:
                break

            if sleep_enabled and interval_sec > 0:
                time.sleep(interval_sec)

            if sent % 100 == 0:
                log(f"Replayed {sent} rows so far")

    log(f"Replay complete: appended {sent} rows into {append_target}")
    return sent


def main() -> int:
    args = parse_args()
    try:
        ensure_paths(args)

        if not args.replay_only:
            backup_file(args.source, args.backup_dir)

        hist_end_dt = parse_ts(args.historical_end, args.timestamp_format)
        live_start_dt = parse_ts(args.live_start, args.timestamp_format)

        if live_start_dt <= hist_end_dt:
            raise ReplayError("live-start must be after historical-end")

        if not args.replay_only:
            split_source(
                source=args.source,
                historical_out=args.historical_out,
                live_stream_out=args.live_stream_out,
                ts_col=args.timestamp_col,
                ts_fmt=args.timestamp_format,
                historical_end=hist_end_dt,
                live_start=live_start_dt,
            )
        else:
            log("Replay-only mode: skipping split")

        if args.split_only:
            log("Split-only mode requested; exiting before replay")
            return 0

        sleep_enabled = not args.no_sleep
        fieldnames = None

        # Grab header from append target for validation
        with open(args.historical_out, "r", newline="", encoding="utf-8") as f:
            reader = csv.DictReader(f)
            fieldnames = reader.fieldnames
            if not fieldnames:
                raise ReplayError("Historical file missing header/fieldnames")

        replay_stream(
            live_stream_path=args.live_stream_out,
            append_target=args.historical_out,
            fieldnames=fieldnames,
            interval_sec=args.interval_seconds,
            sleep_enabled=sleep_enabled,
            max_rows=args.max_rows,
        )

        log("All done. Start/monitor the engine separately to consume appended data.")
        return 0

    except ReplayError as exc:
        log(f"ERROR: {exc}")
        return 1
    except KeyboardInterrupt:
        log("Interrupted by user")
        return 130


if __name__ == "__main__":
    sys.exit(main())
