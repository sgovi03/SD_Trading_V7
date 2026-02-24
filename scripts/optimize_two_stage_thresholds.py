#!/usr/bin/env python
"""Reproducible optimizer for two-stage scoring thresholds.

Usage (PowerShell):
    python scripts\optimize_two_stage_thresholds.py --trials 30 --seed 1337
    python scripts\optimize_two_stage_thresholds.py --optimizer bayes --trials 50
    python scripts\optimize_two_stage_thresholds.py --walk-forward-splits 3

Defaults assume the unified executable is built at build\bin\Release.
"""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import json
import random
import shutil
import subprocess
from pathlib import Path
from typing import Dict, List, Optional, Tuple


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Optimize two-stage scoring thresholds")
    parser.add_argument("--config", default="conf/phase1_enhanced_v3_1_config_FIXED.txt")
    parser.add_argument("--exe", default="build/bin/Release/sd_trading_unified.exe")
    parser.add_argument("--data", default=None, help="CSV data file path (defaults to system_config.json)")
    parser.add_argument("--trials", type=int, default=30)
    parser.add_argument("--seed", type=int, default=1337)
    parser.add_argument("--optimizer", choices=["random", "bayes"], default="random")
    parser.add_argument("--walk-forward-splits", type=int, default=0)
    parser.add_argument("--walk-forward-min-rows", type=int, default=5000)
    parser.add_argument("--results-dir", default="results/optimizer_runs")
    return parser.parse_args()


def read_lines(path: Path) -> List[str]:
    return path.read_text(encoding="utf-8").splitlines(keepends=True)


def write_lines(path: Path, lines: List[str]) -> None:
    path.write_text("".join(lines), encoding="utf-8")


def set_config_value(lines: List[str], key: str, value: str) -> bool:
    """Update key=value line; returns True if found and updated."""
    prefix = key.strip()
    for i, line in enumerate(lines):
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        if stripped.split("#", 1)[0].strip().startswith(prefix + " ") or stripped.split("#", 1)[0].strip().startswith(prefix + "="):
            # Preserve inline comments if present.
            before_comment = line.split("#", 1)[0]
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


def update_config(config_path: Path, updates: Dict[str, str]) -> None:
    lines = read_lines(config_path)
    for key, value in updates.items():
        updated = set_config_value(lines, key, value)
        if not updated:
            lines.append(f"{key} = {value}\n")
    write_lines(config_path, lines)


def run_backtest(exe_path: Path, config_path: Path, data_path: Path) -> None:
    if not exe_path.exists():
        raise FileNotFoundError(f"Executable not found: {exe_path}")
    cmd = [
        str(exe_path),
        "--mode=backtest",
        f"--config={config_path}",
        f"--data={data_path}",
    ]
    subprocess.run(cmd, check=True)


def read_metrics(metrics_path: Path) -> Dict[str, str]:
    metrics: Dict[str, str] = {}
    with metrics_path.open("r", encoding="utf-8") as f:
        reader = csv.reader(f)
        headers = next(reader, None)
        if headers is None:
            return metrics
        for row in reader:
            if len(row) < 2:
                continue
            metrics[row[0].strip()] = row[1].strip()
    return metrics


def parse_float(metrics: Dict[str, str], key: str) -> float:
    try:
        return float(metrics.get(key, "0").replace(",", ""))
    except ValueError:
        return 0.0


def objective_score(metrics: Dict[str, str]) -> float:
    # Simple composite: favor profit factor and expectancy, penalize drawdown.
    pf = parse_float(metrics, "Profit Factor")
    exp = parse_float(metrics, "Expectancy")
    dd = parse_float(metrics, "Max Drawdown")
    return (pf * 1000.0) + exp - (dd * 0.5)


def resolve_data_path(repo_root: Path, data_arg: Optional[str]) -> Path:
    if data_arg:
        data_path = Path(data_arg)
        if not data_path.is_absolute():
            data_path = (repo_root / data_path).resolve()
        return data_path

    sys_config_path = repo_root / "system_config.json"
    data_path = None
    if sys_config_path.exists():
        content = json.loads(sys_config_path.read_text(encoding="utf-8"))
        data_rel = content.get("files", {}).get("live_data_csv")
        if data_rel:
            data_path = Path(data_rel)
            if not data_path.is_absolute():
                data_path = (repo_root / data_path).resolve()

    if data_path is None:
        raise FileNotFoundError("Could not resolve data file path. Provide --data.")

    return data_path


def build_walk_forward_splits(data_path: Path, splits: int, min_rows: int, output_dir: Path) -> List[Path]:
    if splits <= 1:
        return [data_path]

    lines = data_path.read_text(encoding="utf-8").splitlines(keepends=True)
    if not lines:
        raise RuntimeError(f"Empty data file: {data_path}")

    header = lines[0]
    rows = lines[1:]
    total_rows = len(rows)
    if total_rows < min_rows:
        return [data_path]

    max_splits = max(1, total_rows // max(1, min_rows))
    splits = min(splits, max_splits)
    window = max(1, total_rows // splits)

    split_paths: List[Path] = []
    for i in range(splits):
        start = i * window
        end = (i + 1) * window if i < splits - 1 else total_rows
        if start >= total_rows:
            break
        split_rows = rows[start:end]
        split_path = output_dir / f"data_split_{i + 1:02d}.csv"
        split_path.write_text(header + "".join(split_rows), encoding="utf-8")
        split_paths.append(split_path)

    return split_paths


def sample_params(rng: random.Random) -> Dict[str, str]:
    # Keep ranges tight and realistic; adjust as needed.
    zone_quality_min = rng.uniform(60.0, 85.0)
    entry_validation_min = rng.uniform(55.0, 80.0)
    zone_height_opt_min = rng.uniform(0.2, 0.5)
    zone_height_opt_max = rng.uniform(0.6, 1.0)
    adx_min = rng.uniform(15.0, 30.0)
    rsi_oversold = rng.uniform(30.0, 45.0)
    rsi_pullback = rng.uniform(40.0, 55.0)
    macd_moderate = rng.uniform(0.5, 2.0)
    strong_trend_sep = rng.uniform(0.5, 2.0)
    ranging_threshold = rng.uniform(0.2, 0.6)
    opt_stop_min = rng.uniform(1.0, 2.0)
    opt_stop_max = rng.uniform(2.0, 3.5)

    zone_height_opt_min, zone_height_opt_max = min(zone_height_opt_min, zone_height_opt_max), max(zone_height_opt_min, zone_height_opt_max)
    opt_stop_min, opt_stop_max = min(opt_stop_min, opt_stop_max), max(opt_stop_min, opt_stop_max)

    return {
        "zone_quality_minimum_score": f"{zone_quality_min:.2f}",
        "entry_validation_minimum_score": f"{entry_validation_min:.2f}",
        "zone_quality_height_optimal_min": f"{zone_height_opt_min:.2f}",
        "zone_quality_height_optimal_max": f"{zone_height_opt_max:.2f}",
        "entry_validation_adx_minimum": f"{adx_min:.2f}",
        "entry_validation_rsi_oversold": f"{rsi_oversold:.2f}",
        "entry_validation_rsi_pullback": f"{rsi_pullback:.2f}",
        "entry_validation_macd_moderate_threshold": f"{macd_moderate:.2f}",
        "entry_validation_strong_trend_sep": f"{strong_trend_sep:.2f}",
        "entry_validation_ranging_threshold": f"{ranging_threshold:.2f}",
        "entry_validation_optimal_stop_atr_min": f"{opt_stop_min:.2f}",
        "entry_validation_optimal_stop_atr_max": f"{opt_stop_max:.2f}",
    }


def suggest_params_with_optuna(trial) -> Dict[str, str]:
    zone_quality_min = trial.suggest_float("zone_quality_minimum_score", 60.0, 85.0)
    entry_validation_min = trial.suggest_float("entry_validation_minimum_score", 55.0, 80.0)
    zone_height_opt_min = trial.suggest_float("zone_quality_height_optimal_min", 0.2, 0.5)
    zone_height_opt_max = trial.suggest_float("zone_quality_height_optimal_max", 0.6, 1.0)
    adx_min = trial.suggest_float("entry_validation_adx_minimum", 15.0, 30.0)
    rsi_oversold = trial.suggest_float("entry_validation_rsi_oversold", 30.0, 45.0)
    rsi_pullback = trial.suggest_float("entry_validation_rsi_pullback", 40.0, 55.0)
    macd_moderate = trial.suggest_float("entry_validation_macd_moderate_threshold", 0.5, 2.0)
    strong_trend_sep = trial.suggest_float("entry_validation_strong_trend_sep", 0.5, 2.0)
    ranging_threshold = trial.suggest_float("entry_validation_ranging_threshold", 0.2, 0.6)
    opt_stop_min = trial.suggest_float("entry_validation_optimal_stop_atr_min", 1.0, 2.0)
    opt_stop_max = trial.suggest_float("entry_validation_optimal_stop_atr_max", 2.0, 3.5)

    zone_height_opt_min, zone_height_opt_max = min(zone_height_opt_min, zone_height_opt_max), max(zone_height_opt_min, zone_height_opt_max)
    opt_stop_min, opt_stop_max = min(opt_stop_min, opt_stop_max), max(opt_stop_min, opt_stop_max)

    return {
        "zone_quality_minimum_score": f"{zone_quality_min:.2f}",
        "entry_validation_minimum_score": f"{entry_validation_min:.2f}",
        "zone_quality_height_optimal_min": f"{zone_height_opt_min:.2f}",
        "zone_quality_height_optimal_max": f"{zone_height_opt_max:.2f}",
        "entry_validation_adx_minimum": f"{adx_min:.2f}",
        "entry_validation_rsi_oversold": f"{rsi_oversold:.2f}",
        "entry_validation_rsi_pullback": f"{rsi_pullback:.2f}",
        "entry_validation_macd_moderate_threshold": f"{macd_moderate:.2f}",
        "entry_validation_strong_trend_sep": f"{strong_trend_sep:.2f}",
        "entry_validation_ranging_threshold": f"{ranging_threshold:.2f}",
        "entry_validation_optimal_stop_atr_min": f"{opt_stop_min:.2f}",
        "entry_validation_optimal_stop_atr_max": f"{opt_stop_max:.2f}",
    }


def evaluate_params(
    repo_root: Path,
    exe_path: Path,
    config_path: Path,
    params: Dict[str, str],
    data_splits: List[Path],
    run_dir: Path,
    trial: int,
) -> Tuple[float, Dict[str, str]]:
    params = dict(params)
    params["enable_two_stage_scoring"] = "YES"
    update_config(config_path, params)

    scores: List[float] = []
    metric_sums: Dict[str, float] = {
        "profit_factor": 0.0,
        "expectancy": 0.0,
        "max_drawdown": 0.0,
        "total_pnl": 0.0,
        "total_trades": 0.0,
        "win_rate": 0.0,
    }

    for split_index, data_path in enumerate(data_splits, start=1):
        run_backtest(exe_path, config_path, data_path)
        metrics_path = repo_root / "results/backtest/metrics.csv"
        metrics = read_metrics(metrics_path)
        score = objective_score(metrics)
        scores.append(score)

        metric_sums["profit_factor"] += parse_float(metrics, "Profit Factor")
        metric_sums["expectancy"] += parse_float(metrics, "Expectancy")
        metric_sums["max_drawdown"] += parse_float(metrics, "Max Drawdown")
        metric_sums["total_pnl"] += parse_float(metrics, "Total P&L")
        metric_sums["total_trades"] += parse_float(metrics, "Total Trades")
        metric_sums["win_rate"] += parse_float(metrics, "Win Rate %")

        shutil.copy2(metrics_path, run_dir / f"metrics_trial_{trial:03d}_split_{split_index:02d}.csv")

    avg_score = sum(scores) / max(1, len(scores))
    split_count = max(1, len(scores))

    avg_metrics = {
        "profit_factor": f"{metric_sums['profit_factor'] / split_count:.4f}",
        "expectancy": f"{metric_sums['expectancy'] / split_count:.4f}",
        "max_drawdown": f"{metric_sums['max_drawdown'] / split_count:.4f}",
        "total_pnl": f"{metric_sums['total_pnl'] / split_count:.4f}",
        "total_trades": f"{metric_sums['total_trades'] / split_count:.2f}",
        "win_rate": f"{metric_sums['win_rate'] / split_count:.2f}",
        "splits": str(split_count),
    }

    return avg_score, avg_metrics


def main() -> int:
    args = parse_args()
    rng = random.Random(args.seed)

    repo_root = Path(__file__).resolve().parents[1]
    config_path = (repo_root / args.config).resolve()
    exe_path = (repo_root / args.exe).resolve()
    results_dir = (repo_root / args.results_dir).resolve()
    results_dir.mkdir(parents=True, exist_ok=True)

    data_path = resolve_data_path(repo_root, args.data)
    if not data_path.exists():
        raise FileNotFoundError(f"Data file not found: {data_path}")

    timestamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    run_dir = results_dir / f"two_stage_{timestamp}"
    run_dir.mkdir(parents=True, exist_ok=True)

    original_config = read_lines(config_path)
    results_rows: List[Dict[str, str]] = []

    data_splits_dir = run_dir / "data_splits"
    data_splits_dir.mkdir(parents=True, exist_ok=True)
    data_splits = build_walk_forward_splits(
        data_path,
        args.walk_forward_splits,
        args.walk_forward_min_rows,
        data_splits_dir,
    )
    (run_dir / "data_splits.json").write_text(
        json.dumps([str(p) for p in data_splits], indent=2),
        encoding="utf-8",
    )

    try:
        if args.optimizer == "bayes":
            try:
                import optuna
            except ImportError as exc:
                raise RuntimeError("Optuna is required for --optimizer bayes. Install with: pip install optuna") from exc

            sampler = optuna.samplers.TPESampler(seed=args.seed)
            study = optuna.create_study(direction="maximize", sampler=sampler)

            def optuna_objective(trial_obj) -> float:
                trial_index = len(results_rows) + 1
                params = suggest_params_with_optuna(trial_obj)
                score, avg_metrics = evaluate_params(
                    repo_root,
                    exe_path,
                    config_path,
                    params,
                    data_splits,
                    run_dir,
                    trial_index,
                )

                row = {
                    "trial": str(trial_index),
                    "score": f"{score:.4f}",
                    **params,
                    **avg_metrics,
                }
                results_rows.append(row)
                return score

            study.optimize(optuna_objective, n_trials=args.trials)

        else:
            for trial in range(1, args.trials + 1):
                params = sample_params(rng)
                score, avg_metrics = evaluate_params(
                    repo_root,
                    exe_path,
                    config_path,
                    params,
                    data_splits,
                    run_dir,
                    trial,
                )

                row = {
                    "trial": str(trial),
                    "score": f"{score:.4f}",
                    **params,
                    **avg_metrics,
                }
                results_rows.append(row)
        # Write summary
        summary_path = run_dir / "summary.csv"
        with summary_path.open("w", encoding="utf-8", newline="") as f:
            fieldnames = list(results_rows[0].keys()) if results_rows else []
            writer = csv.DictWriter(f, fieldnames=fieldnames)
            writer.writeheader()
            for row in results_rows:
                writer.writerow(row)

        # Best trial
        best = max(results_rows, key=lambda r: float(r["score"])) if results_rows else None
        if best:
            (run_dir / "best.json").write_text(json.dumps(best, indent=2), encoding="utf-8")

    finally:
        # Restore original config
        write_lines(config_path, original_config)

    print(f"Optimization complete. Results in: {run_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
