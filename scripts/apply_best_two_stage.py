#!/usr/bin/env python
"""Apply best.json thresholds to strategy config with backup.

Usage (PowerShell):
  python scripts\apply_best_two_stage.py --best results\optimizer_runs\two_stage_YYYYMMDD_HHMMSS\best.json
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import subprocess
from pathlib import Path
from typing import Dict, List


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Apply optimizer best.json to config with backup")
    parser.add_argument("--best", required=True, help="Path to best.json from optimizer")
    parser.add_argument("--config", default="conf/phase1_enhanced_v3_1_config_FIXED.txt")
    parser.add_argument("--exe", default="build/bin/Release/sd_trading_unified.exe")
    parser.add_argument("--data", default=None, help="CSV data file path (defaults to system_config.json)")
    parser.add_argument("--run-backtest", action="store_true", help="Run full backtest after applying")
    return parser.parse_args()


def read_lines(path: Path) -> List[str]:
    return path.read_text(encoding="utf-8").splitlines(keepends=True)


def write_lines(path: Path, lines: List[str]) -> None:
    path.write_text("".join(lines), encoding="utf-8")


def set_config_value(lines: List[str], key: str, value: str) -> bool:
    prefix = key.strip()
    for i, line in enumerate(lines):
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        candidate = stripped.split("#", 1)[0].strip()
        if candidate.startswith(prefix + " ") or candidate.startswith(prefix + "="):
            comment = ""
            if "#" in line:
                comment = "#" + line.split("#", 1)[1].rstrip("\n")
            new_line = f"{prefix} = {value}"
            if comment:
                new_line += " " + comment
            new_line += "\n"
            lines[i] = new_line
            return True
    return False


def apply_updates(config_path: Path, updates: Dict[str, str]) -> None:
    lines = read_lines(config_path)
    for key, value in updates.items():
        updated = set_config_value(lines, key, value)
        if not updated:
            lines.append(f"{key} = {value}\n")
    write_lines(config_path, lines)


def main() -> int:
    args = parse_args()
    repo_root = Path(__file__).resolve().parents[1]
    best_path = Path(args.best)
    if not best_path.is_absolute():
        best_path = (repo_root / best_path).resolve()

    config_path = Path(args.config)
    if not config_path.is_absolute():
        config_path = (repo_root / config_path).resolve()

    exe_path = Path(args.exe)
    if not exe_path.is_absolute():
        exe_path = (repo_root / exe_path).resolve()

    if not best_path.exists():
        raise FileNotFoundError(f"best.json not found: {best_path}")
    if not config_path.exists():
        raise FileNotFoundError(f"Config not found: {config_path}")

    best = json.loads(best_path.read_text(encoding="utf-8"))

    # Keys we allow from best.json (ignore objective columns).
    allowed_prefixes = (
        "zone_quality_",
        "entry_validation_",
    )
    allowed_exact = {"enable_two_stage_scoring"}

    updates: Dict[str, str] = {}
    for key, value in best.items():
        if key in allowed_exact or key.startswith(allowed_prefixes):
            updates[key] = str(value)

    if not updates:
        raise RuntimeError("No applicable threshold keys found in best.json")

    # Backup original config
    timestamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    backup_path = config_path.with_suffix(config_path.suffix + f".bak.{timestamp}")
    backup_path.write_text(config_path.read_text(encoding="utf-8"), encoding="utf-8")

    apply_updates(config_path, updates)

    print(f"Backup created: {backup_path}")
    print(f"Applied {len(updates)} settings to: {config_path}")

    if args.run_backtest:
        if not exe_path.exists():
            raise FileNotFoundError(f"Executable not found: {exe_path}")

        if args.data:
            data_path = Path(args.data)
            if not data_path.is_absolute():
                data_path = (repo_root / data_path).resolve()
        else:
            sys_config_path = repo_root / "system_config.json"
            if not sys_config_path.exists():
                raise FileNotFoundError("system_config.json not found. Provide --data.")
            content = json.loads(sys_config_path.read_text(encoding="utf-8"))
            data_rel = content.get("files", {}).get("live_data_csv")
            if not data_rel:
                raise FileNotFoundError("live_data_csv not set in system_config.json. Provide --data.")
            data_path = Path(data_rel)
            if not data_path.is_absolute():
                data_path = (repo_root / data_path).resolve()

        if not data_path.exists():
            raise FileNotFoundError(f"Data file not found: {data_path}")

        cmd = [
            str(exe_path),
            "--mode=backtest",
            f"--config={config_path}",
            f"--data={data_path}",
        ]
        subprocess.run(cmd, check=True)
        print("Backtest completed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
