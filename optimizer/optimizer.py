"""
SD Trading Engine V6 - Parameter Optimizer
==========================================
Supports: Grid Search | Random Search | Genetic Algorithm | Bayesian (GP-based)

Usage:
    python optimizer.py --config phase_6_config_v0_7_NEW.txt \
                        --backtest-cmd "./backtest {config}" \
                        --output-dir ./optimization_runs \
                        --strategy genetic \
                        --trials 100 \
                        --objective sharpe

Author: Auto-generated for SD Trading Engine V6
"""

import os, sys, re, json, csv, copy, math, time, shutil, random, hashlib
import argparse, subprocess, logging, itertools
from pathlib import Path
from datetime import datetime
from typing import Any, Dict, List, Optional, Tuple
from dataclasses import dataclass, field, asdict
from collections import defaultdict

# ─────────────────────────────────────────────────────────────────────────────
# PARAMETER SEARCH SPACE DEFINITION
# Edit ranges here to control what gets optimized.
# Format: param_name → (type, [values_or_range], group, description)
# ─────────────────────────────────────────────────────────────────────────────
SEARCH_SPACE: Dict[str, dict] = {

    # ── Core Risk / RR ───────────────────────────────────────────────────────
    "stop_loss_atr_multiplier": {
        "type": "float", "min": 1.5, "max": 3.0, "step": 0.1,
        "group": "risk", "desc": "ATR multiplier for stop loss placement"
    },
    "target_rr_ratio": {
        "type": "float", "min": 1.25, "max": 2.5, "step": 0.25,
        "group": "risk", "desc": "Risk-reward target ratio"
    },
    "risk_per_trade_pct": {
        "type": "float", "min": 0.5, "max": 1.5, "step": 0.25,
        "group": "risk", "desc": "Capital risk per trade (%)"
    },
    "max_position_lots": {
        "type": "int", "min": 2, "max": 6, "step": 1,
        "group": "risk", "desc": "Hard cap on position size (lots)"
    },
    "max_consecutive_losses": {
        "type": "int", "min": 2, "max": 5, "step": 1,
        "group": "risk", "desc": "Pause after N consecutive losses"
    },

    # ── Trailing Stop ─────────────────────────────────────────────────────────
    "trail_activation_rr": {
        "type": "float", "min": 0.6, "max": 1.2, "step": 0.1,
        "group": "trailing", "desc": "RR level to activate trailing stop"
    },
    "trail_atr_multiplier": {
        "type": "float", "min": 0.4, "max": 1.2, "step": 0.1,
        "group": "trailing", "desc": "ATR multiplier for trail distance"
    },
    "trail_fallback_tp_rr": {
        "type": "float", "min": 1.25, "max": 2.5, "step": 0.25,
        "group": "trailing", "desc": "Fallback TP when trailing active"
    },

    # ── Entry / Scoring ───────────────────────────────────────────────────────
    "entry_validation_minimum_score": {
        "type": "float", "min": 10.0, "max": 30.0, "step": 5.0,
        "group": "entry", "desc": "Minimum score to validate an entry"
    },
    "entry_minimum_score": {
        "type": "float", "min": 25.0, "max": 35.0, "step": 2.5,
        "group": "entry", "desc": "Adaptive entry minimum score"
    },
    "entry_optimal_score": {
        "type": "float", "min": 55.0, "max": 80.0, "step": 5.0,
        "group": "entry", "desc": "Score for optimal position sizing"
    },
    "entry_score_total_min": {
        "type": "int", "min": 85, "max": 110, "step": 5,
        "group": "entry", "desc": "Min composite entry score"
    },
    "entry_score_zone_quality_min": {
        "type": "int", "min": 45, "max": 70, "step": 5,
        "group": "entry", "desc": "Min zone quality component score"
    },
    "high_score_threshold": {
        "type": "float", "min": 55.0, "max": 80.0, "step": 5.0,
        "group": "entry", "desc": "Score above which position mult applies"
    },
    "high_score_position_mult": {
        "type": "float", "min": 1.0, "max": 2.0, "step": 0.25,
        "group": "entry", "desc": "Position size multiplier for high-score entries"
    },

    # ── Zone Detection ────────────────────────────────────────────────────────
    "min_zone_strength": {
        "type": "int", "min": 35, "max": 50, "step": 5,
        "group": "zone", "desc": "Minimum zone strength to detect"
    },
    "min_impulse_atr": {
        "type": "float", "min": 1.0, "max": 2.0, "step": 0.1,
        "group": "zone", "desc": "Minimum ATR for impulse move"
    },
    "min_zone_width_atr": {
        "type": "float", "min": 0.3, "max": 1.0, "step": 0.1,
        "group": "zone", "desc": "Minimum zone width in ATR"
    },
    "base_height_max_atr": {
        "type": "float", "min": 1.2, "max": 2.8, "step": 0.2,
        "group": "zone", "desc": "Max zone height in ATR"
    },
    "consolidation_min_bars": {
        "type": "int", "min": 4, "max": 12, "step": 2,
        "group": "zone", "desc": "Minimum consolidation bars"
    },
    "consolidation_max_bars": {
        "type": "int", "min": 35, "max": 75, "step": 10,
        "group": "zone", "desc": "Maximum consolidation bars"
    },
    "max_zone_age_days": {
        "type": "int", "min": 60, "max": 150, "step": 30,
        "group": "zone", "desc": "Maximum age of tradable zones"
    },
    "max_zone_distance_atr": {
        "type": "int", "min": 40, "max": 120, "step": 20,
        "group": "zone", "desc": "Max distance from price to zone"
    },

    # ── Volume Filters ────────────────────────────────────────────────────────
    "min_departure_imbalance": {
        "type": "float", "min": 1.2, "max": 3.0, "step": 0.2,
        "group": "volume", "desc": "Min volume imbalance on departure move"
    },
    "pullback_volume_low": {
        "type": "float", "min": 0.5, "max": 1.0, "step": 0.1,
        "group": "volume", "desc": "Low pullback volume threshold"
    },
    "pullback_volume_elevated": {
        "type": "float", "min": 1.2, "max": 2.0, "step": 0.1,
        "group": "volume", "desc": "Elevated pullback volume threshold"
    },
    "optimal_volume_min": {
        "type": "float", "min": 0.7, "max": 1.5, "step": 0.1,
        "group": "volume", "desc": "Optimal entry volume range minimum"
    },
    "optimal_volume_max": {
        "type": "float", "min": 1.8, "max": 3.5, "step": 0.1,
        "group": "volume", "desc": "Optimal entry volume range maximum"
    },
    "optimal_institutional_min": {
        "type": "float", "min": 30.0, "max": 55.0, "step": 5.0,
        "group": "volume", "desc": "Minimum institutional index score"
    },

    # ── ADX Filters ───────────────────────────────────────────────────────────
    "entry_validation_adx_minimum": {
        "type": "float", "min": 15.0, "max": 30.0, "step": 2.5,
        "group": "adx", "desc": "Minimum ADX for entry"
    },
    "adx_transition_min": {
        "type": "float", "min": 30.0, "max": 50.0, "step": 5.0,
        "group": "adx", "desc": "ADX danger band lower bound"
    },
    "adx_transition_max": {
        "type": "float", "min": 50.0, "max": 75.0, "step": 5.0,
        "group": "adx", "desc": "ADX danger band upper bound"
    },
    "adx_transition_size_factor": {
        "type": "float", "min": 0.25, "max": 0.75, "step": 0.25,
        "group": "adx", "desc": "Position size in ADX danger band"
    },

    # ── Zone Exhaustion ───────────────────────────────────────────────────────
    "zone_consecutive_loss_max": {
        "type": "int", "min": 1, "max": 3, "step": 1,
        "group": "exhaustion", "desc": "SL losses before zone marked exhausted"
    },
    "zone_sl_suspend_threshold": {
        "type": "int", "min": 1, "max": 4, "step": 1,
        "group": "exhaustion", "desc": "SL hits per direction before suspension"
    },

    # ── Session Timing ────────────────────────────────────────────────────────
    "close_before_session_end_minutes": {
        "type": "int", "min": 10, "max": 30, "step": 5,
        "group": "timing", "desc": "Minutes before EOD to close positions"
    },
    "pause_bars_after_losses": {
        "type": "int", "min": 15, "max": 60, "step": 15,
        "group": "timing", "desc": "Bars to pause after consecutive losses"
    },

    # ── Scoring Weights ───────────────────────────────────────────────────────
    "base_score_weight": {
        "type": "float", "min": 0.3, "max": 0.7, "step": 0.1,
        "group": "scoring", "desc": "Weight for base zone score"
    },
    "volume_score_weight": {
        "type": "float", "min": 0.2, "max": 0.6, "step": 0.1,
        "group": "scoring", "desc": "Weight for volume score"
    },
    "institutional_volume_bonus": {
        "type": "float", "min": 15.0, "max": 45.0, "step": 5.0,
        "group": "scoring", "desc": "Bonus for institutional volume confirmation"
    },
    "low_volume_retest_bonus": {
        "type": "float", "min": 10.0, "max": 35.0, "step": 5.0,
        "group": "scoring", "desc": "Bonus for low-volume retest entry"
    },

    # ── Rejection / Breakthrough Analysis ─────────────────────────────────────
    "rejection_excellent_threshold": {
        "type": "float", "min": 45.0, "max": 75.0, "step": 5.0,
        "group": "rejbk", "desc": "Rejection % for excellent rating"
    },
    "breakthrough_disqualify_threshold": {
        "type": "float", "min": 25.0, "max": 55.0, "step": 5.0,
        "group": "rejbk", "desc": "Breakthrough % to disqualify zone"
    },
}

# Parameters to NEVER modify (structural / path / boolean flags)
LOCKED_PARAMS = {
    "starting_capital", "lot_size", "commission_per_trade",
    "volume_baseline_file", "volume_normalization_method",
    "session_end_time", "entry_block_start_time", "entry_block_end_time",
    "enable_debug_logging", "enable_score_logging",
    "skip_violated_zones", "v6_fully_enabled",
    "enable_zone_exhaustion", "enable_adx_transition_filter",
    "enable_late_entry_cutoff", "use_trailing_stop",
    "duplicate_prevention_enabled", "check_existing_before_create",
    "close_eod", "merge_strategy", "atr_period", "rsi_period",
    "bb_period", "adx_period", "macd_fast_period", "macd_slow_period",
}


# ─────────────────────────────────────────────────────────────────────────────
# DATA CLASSES
# ─────────────────────────────────────────────────────────────────────────────

@dataclass
class BacktestResult:
    run_id: str
    config_file: str
    result_dir: str
    params: Dict[str, Any]
    # Core metrics
    net_pnl: float = 0.0
    win_rate: float = 0.0
    total_trades: int = 0
    profit_factor: float = 0.0
    sharpe_ratio: float = 0.0
    max_drawdown_pct: float = 0.0
    avg_win: float = 0.0
    avg_loss: float = 0.0
    avg_rr_achieved: float = 0.0
    # Composite objective
    score: float = -999999.0
    status: str = "pending"   # pending | success | failed | timeout
    duration_sec: float = 0.0
    timestamp: str = ""
    raw_output: str = ""

    def to_dict(self) -> dict:
        return asdict(self)


@dataclass
class OptimizationState:
    strategy: str
    objective: str
    total_trials: int
    completed: int = 0
    best_score: float = -999999.0
    best_run_id: str = ""
    history: List[dict] = field(default_factory=list)
    start_time: float = field(default_factory=time.time)


# ─────────────────────────────────────────────────────────────────────────────
# CONFIG PARSER / WRITER
# ─────────────────────────────────────────────────────────────────────────────

class ConfigManager:
    """Parse, modify, and write config files preserving comments and structure."""

    def __init__(self, base_config_path: str):
        self.base_path = Path(base_config_path)
        self.raw_lines: List[str] = []
        self.base_params: Dict[str, str] = {}
        self._parse()

    def _parse(self):
        self.raw_lines = self.base_path.read_text(encoding="utf-8").splitlines(keepends=True)
        for line in self.raw_lines:
            stripped = line.strip()
            if stripped.startswith("#") or "=" not in stripped:
                continue
            # strip inline comment
            code_part = stripped.split("#")[0].strip()
            if "=" in code_part:
                key, _, val = code_part.partition("=")
                self.base_params[key.strip()] = val.strip()

    def generate_config(self, overrides: Dict[str, Any], out_path: Path) -> Path:
        """Write a new config file with overrides applied."""
        new_lines = []
        written = set()

        for line in self.raw_lines:
            stripped = line.strip()
            # comment-only or empty
            if not stripped or stripped.startswith("#"):
                new_lines.append(line)
                continue
            if "=" not in stripped.split("#")[0]:
                new_lines.append(line)
                continue
            code_part = stripped.split("#")[0].strip()
            key, _, _ = code_part.partition("=")
            key = key.strip()
            inline_comment = ""
            if "#" in stripped:
                inline_comment = "  # " + stripped.split("#", 1)[1].strip()
            if key in overrides:
                val = overrides[key]
                if isinstance(val, float):
                    val_str = f"{val:.4f}".rstrip("0").rstrip(".")
                else:
                    val_str = str(val)
                new_lines.append(f"{key} = {val_str}{inline_comment}\n")
                written.add(key)
            else:
                new_lines.append(line)

        out_path.write_text("".join(new_lines), encoding="utf-8")
        return out_path

    def get_base_value(self, param: str) -> Optional[str]:
        return self.base_params.get(param)


# ─────────────────────────────────────────────────────────────────────────────
# RESULT PARSER  (delegates to SDEngineV6Parser in result_parser_adapter.py)
# ─────────────────────────────────────────────────────────────────────────────

try:
    from result_parser_adapter import SDEngineV6Parser as _EngineParser
    _USE_ADAPTER = True
except ImportError:
    _USE_ADAPTER = False

class ResultParser:
    """Thin wrapper - uses SDEngineV6Parser when adapter file is present."""

    def parse(self, stdout: str, result_dir: Path) -> Dict[str, float]:
        if _USE_ADAPTER:
            return _EngineParser().parse_all(stdout, result_dir)
        return self._fallback_parse(stdout)

    def _fallback_parse(self, text: str) -> Dict[str, float]:
        patterns = {
            "net_pnl":          r"Total\s+P&L\s*:\s*\$?\s*([-\d,.]+)",
            "win_rate":         r"Win\s+Rate\s*:\s*([\d.]+)\s*%",
            "total_trades":     r"(?m)^\s*Total\s*:\s*(\d+)",
            "profit_factor":    r"Profit\s+Factor\s*:\s*([\d.]+)",
            "sharpe_ratio":     r"Sharpe\s+Ratio\s*:\s*([-\d.]+)",
            "max_drawdown_pct": r"Max\s+DD\s*%\s*:\s*([\d.]+)",
            "avg_win":          r"Avg\s+Win\s*:\s*\$?\s*([-\d,.]+)",
            "avg_loss":         r"Avg\s+Loss\s*:\s*\$?\s*([-\d,.]+)",
        }
        result = {}
        for metric, pattern in patterns.items():
            m = re.search(pattern, text, re.IGNORECASE | re.MULTILINE)
            if m:
                try:
                    result[metric] = float(m.group(1).replace(",", ""))
                except ValueError:
                    pass
        return result


# ─────────────────────────────────────────────────────────────────────────────
# OBJECTIVE FUNCTION
# ─────────────────────────────────────────────────────────────────────────────

class ObjectiveCalculator:
    """
    Composite scoring. Weights and penalties tuned for NIFTY futures trading.
    """

    def __init__(self, objective: str = "composite"):
        self.objective = objective

    def calculate(self, r: BacktestResult) -> float:
        if r.status != "success":
            return -999999.0
        if r.total_trades < 5:
            return -999999.0

        if self.objective == "net_pnl":
            return r.net_pnl

        elif self.objective == "sharpe":
            return r.sharpe_ratio if r.sharpe_ratio > -999 else -999999.0

        elif self.objective == "profit_factor":
            return r.profit_factor

        elif self.objective == "win_rate":
            return r.win_rate

        elif self.objective == "composite":
            return self._composite(r)

        elif self.objective == "risk_adjusted":
            return self._risk_adjusted(r)

        return r.net_pnl

    def _composite(self, r: BacktestResult) -> float:
        """
        Balanced composite: rewards PnL + consistency, penalizes drawdown + overtrading.
        Tuned for Indian NIFTY futures (INR denominated).
        """
        score = 0.0

        # PnL component (normalized, max ~₹5L for good run)
        pnl_score = min(r.net_pnl / 500000.0, 1.0) * 40.0
        score += pnl_score

        # Win rate (sweet spot: 45–65%)
        wr = r.win_rate / 100.0 if r.win_rate > 1 else r.win_rate
        if wr >= 0.45:
            wr_score = min((wr - 0.45) / 0.25, 1.0) * 20.0
        else:
            wr_score = -(0.45 - wr) * 40.0  # Penalize below 45%
        score += wr_score

        # Profit factor
        pf = r.profit_factor
        if pf > 0:
            pf_score = min((pf - 1.0) / 1.0, 1.0) * 20.0
        else:
            pf_score = -20.0
        score += pf_score

        # Drawdown penalty (>15% is severe)
        dd = r.max_drawdown_pct
        if dd > 0:
            dd_penalty = min(dd / 15.0, 1.0) * 20.0
            score -= dd_penalty

        # Trade count (too few = overfit, too many = noise)
        tc = r.total_trades
        if tc < 30:
            score -= (30 - tc) * 0.8   # stronger penalty for low trade count
        elif tc > 300:
            score -= (tc - 300) * 0.02

        # Sharpe ratio bonus
        if r.sharpe_ratio > 0:
            score += min(r.sharpe_ratio / 2.0, 1.0) * 10.0

        return score

    def _risk_adjusted(self, r: BacktestResult) -> float:
        """Return on max drawdown x win rate - focuses on capital preservation."""
        if r.max_drawdown_pct <= 0 or r.total_trades < 10:
            return -999999.0
        romad = (r.net_pnl / 300000.0) / (r.max_drawdown_pct / 100.0)
        wr = r.win_rate / 100.0 if r.win_rate > 1 else r.win_rate
        pf_bonus = min(r.profit_factor / 2.0, 1.0) * 0.3
        return romad * wr * (1.0 + pf_bonus) * 100.0


# ─────────────────────────────────────────────────────────────────────────────
# SEARCH STRATEGIES
# ─────────────────────────────────────────────────────────────────────────────

def _linspace(lo, hi, step):
    """Generate range including hi if possible."""
    vals = []
    v = lo
    while v <= hi + 1e-9:
        vals.append(round(v, 6))
        v += step
    return vals

def get_param_values(spec: dict) -> list:
    """Return discrete candidate values for a parameter spec."""
    if "values" in spec:
        return spec["values"]
    lo, hi, step = spec["min"], spec["max"], spec["step"]
    vals = _linspace(lo, hi, step)
    if spec["type"] == "int":
        return [int(v) for v in vals]
    return vals


class GridSearch:
    def __init__(self, space: Dict[str, dict], active_params: List[str]):
        self.space = {k: v for k, v in space.items() if k in active_params}
        self._generate_grid()

    def _generate_grid(self):
        self.param_names = list(self.space.keys())
        value_lists = [get_param_values(self.space[p]) for p in self.param_names]
        self.combos = list(itertools.product(*value_lists))
        self.idx = 0

    def total(self) -> int:
        return len(self.combos)

    def next(self) -> Optional[Dict[str, Any]]:
        if self.idx >= len(self.combos):
            return None
        combo = dict(zip(self.param_names, self.combos[self.idx]))
        self.idx += 1
        return combo


class RandomSearch:
    def __init__(self, space: Dict[str, dict], active_params: List[str], seed: int = 42):
        self.space = {k: v for k, v in space.items() if k in active_params}
        random.seed(seed)

    def sample(self) -> Dict[str, Any]:
        params = {}
        for name, spec in self.space.items():
            vals = get_param_values(spec)
            params[name] = random.choice(vals)
        return params


class GeneticOptimizer:
    """
    Real-valued genetic algorithm with tournament selection,
    uniform crossover, and Gaussian mutation.
    """
    def __init__(self, space: Dict[str, dict], active_params: List[str],
                 pop_size: int = 20, elite_pct: float = 0.15,
                 crossover_rate: float = 0.7, mutation_rate: float = 0.15,
                 seed: int = 42):
        self.space = {k: v for k, v in space.items() if k in active_params}
        self.param_names = list(self.space.keys())
        self.pop_size = pop_size
        self.elite_count = max(2, int(pop_size * elite_pct))
        self.crossover_rate = crossover_rate
        self.mutation_rate = mutation_rate
        random.seed(seed)
        self.population: List[Dict[str, Any]] = []
        self.scores: List[float] = []
        self.generation = 0
        self._init_population()

    def _random_individual(self) -> Dict[str, Any]:
        ind = {}
        for name, spec in self.space.items():
            vals = get_param_values(spec)
            ind[name] = random.choice(vals)
        return ind

    def _init_population(self):
        self.population = [self._random_individual() for _ in range(self.pop_size)]
        self.scores = [-999999.0] * self.pop_size

    def get_unevaluated(self) -> List[Tuple[int, Dict]]:
        return [(i, p) for i, (p, s) in enumerate(zip(self.population, self.scores))
                if s == -999999.0]

    def update_score(self, idx: int, score: float):
        self.scores[idx] = score

    def evolve(self):
        """Create next generation from current population."""
        self.generation += 1
        paired = sorted(zip(self.scores, self.population), reverse=True, key=lambda x: x[0])
        elites = [copy.deepcopy(p) for _, p in paired[:self.elite_count]]
        new_pop = list(elites)

        while len(new_pop) < self.pop_size:
            if random.random() < self.crossover_rate:
                p1 = self._tournament_select()
                p2 = self._tournament_select()
                child = self._crossover(p1, p2)
            else:
                child = copy.deepcopy(self._tournament_select())
            child = self._mutate(child)
            new_pop.append(child)

        self.population = new_pop
        self.scores = [-999999.0 if i >= self.elite_count else paired[i][0]
                       for i in range(self.pop_size)]

    def _tournament_select(self, k: int = 3) -> Dict[str, Any]:
        candidates = random.sample(range(self.pop_size), min(k, self.pop_size))
        best_idx = max(candidates, key=lambda i: self.scores[i])
        return copy.deepcopy(self.population[best_idx])

    def _crossover(self, p1: Dict, p2: Dict) -> Dict:
        child = {}
        for name in self.param_names:
            child[name] = p1[name] if random.random() < 0.5 else p2[name]
        return child

    def _mutate(self, ind: Dict) -> Dict:
        for name, spec in self.space.items():
            if random.random() < self.mutation_rate:
                vals = get_param_values(spec)
                # Prefer nearby value (Gaussian-like on discrete grid)
                cur_idx = vals.index(ind[name]) if ind[name] in vals else 0
                delta = int(random.gauss(0, max(1, len(vals) * 0.2)))
                new_idx = max(0, min(len(vals) - 1, cur_idx + delta))
                ind[name] = vals[new_idx]
        return ind


class BayesianOptimizer:
    """
    Lightweight Gaussian Process-based Bayesian optimizer using scipy.
    Falls back to random if no evaluations yet.
    """
    def __init__(self, space: Dict[str, dict], active_params: List[str],
                 n_random_init: int = 10, kappa: float = 2.576, seed: int = 42):
        try:
            from scipy.stats import norm
            from scipy.linalg import cholesky, solve_triangular
            self._norm = norm
            self._chol = cholesky
            self._solve = solve_triangular
            self._scipy_ok = True
        except ImportError:
            self._scipy_ok = False

        self.space = {k: v for k, v in space.items() if k in active_params}
        self.param_names = list(self.space.keys())
        self.all_values = [get_param_values(self.space[p]) for p in self.param_names]
        self.n_random_init = n_random_init
        self.kappa = kappa
        random.seed(seed)

        self.X: List[List[float]] = []   # evaluated parameter vectors (normalized)
        self.y: List[float] = []          # scores
        self._build_candidates()

    def _build_candidates(self, n_candidates: int = 500):
        """Sample candidate points for acquisition function."""
        self._candidates = []
        for _ in range(n_candidates):
            pt = [random.choice(vals) for vals in self.all_values]
            self._candidates.append(pt)

    def _normalize(self, pt: list) -> List[float]:
        norm_pt = []
        for i, (name, vals) in enumerate(zip(self.param_names, self.all_values)):
            lo, hi = vals[0], vals[-1]
            v = pt[i]
            norm_pt.append((v - lo) / (hi - lo + 1e-9))
        return norm_pt

    def _to_params(self, pt: list) -> Dict[str, Any]:
        return {name: pt[i] for i, name in enumerate(self.param_names)}

    def _kernel(self, x1, x2, length_scale=0.5):
        """RBF kernel."""
        dist = sum((a - b) ** 2 for a, b in zip(x1, x2))
        return math.exp(-dist / (2 * length_scale ** 2))

    def _gp_predict(self, x_test):
        """GP posterior mean and variance at x_test."""
        if not self.X or not self._scipy_ok:
            return 0.0, 1.0

        n = len(self.X)
        K = [[self._kernel(self.X[i], self.X[j]) + (1e-6 if i == j else 0)
              for j in range(n)] for i in range(n)]
        k_star = [self._kernel(x_test, self.X[i]) for i in range(n)]
        k_ss = self._kernel(x_test, x_test)

        try:
            import numpy as np
            K_np = np.array(K)
            k_s = np.array(k_star)
            y_np = np.array(self.y)
            # Normalize y
            y_mean = float(np.mean(y_np))
            y_std = float(np.std(y_np)) + 1e-9
            y_norm = (y_np - y_mean) / y_std

            L = self._chol(K_np, lower=True)
            alpha = self._solve(L.T, self._solve(L, y_norm, lower=True))
            mu = float(k_s @ alpha) * y_std + y_mean
            v = self._solve(L, k_s, lower=True)
            var = max(0.0, k_ss - float(v @ v))
            return mu, var
        except Exception:
            return 0.0, 1.0

    def suggest(self) -> Dict[str, Any]:
        if len(self.y) < self.n_random_init or not self._scipy_ok:
            # Random phase
            pt = [random.choice(vals) for vals in self.all_values]
            return self._to_params(pt)

        # Acquisition: Upper Confidence Bound (UCB)
        best_acq = -1e18
        best_pt = self._candidates[0]
        y_max = max(self.y)

        for pt in self._candidates:
            norm_pt = self._normalize(pt)
            mu, var = self._gp_predict(norm_pt)
            acq = mu + self.kappa * math.sqrt(var)
            if acq > best_acq:
                best_acq = acq
                best_pt = pt

        return self._to_params(best_pt)

    def register(self, params: Dict[str, Any], score: float):
        pt = [params.get(name, self.all_values[i][0])
              for i, name in enumerate(self.param_names)]
        self.X.append(self._normalize(pt))
        self.y.append(score)


# ─────────────────────────────────────────────────────────────────────────────
# BACKTEST RUNNER
# ─────────────────────────────────────────────────────────────────────────────
#
# SD Engine V6 reads its config from a FIXED path defined in system_config.json:
#   "strategy_config": "conf/phase_6_config_v0_7_NEW.txt"  (relative to project root)
#
# Strategy:
#   1. Engine runs from project_root  (where system_config.json lives)
#   2. Before each trial: overwrite conf/phase_6_config_v0_7_NEW.txt with trial params
#   3. Run: sd_trading_unified.exe --mode=backtest  (from project_root)
#   4. After each trial: restore original config (even if trial crashes)
# ─────────────────────────────────────────────────────────────────────────────

class BacktestRunner:
    def __init__(self, cmd_template: str, output_dir: Path,
                 timeout_sec: int = 300, dry_run: bool = False,
                 project_root: str = "", fixed_config_rel: str = ""):
        self.cmd_template = cmd_template
        self.output_dir = output_dir
        self.timeout = timeout_sec
        self.dry_run = dry_run
        self.parser = ResultParser()
        output_dir.mkdir(parents=True, exist_ok=True)

        # Fixed-path mode: project root and relative config path from system_config.json
        self.project_root = Path(project_root).resolve() if project_root else None
        self.fixed_config_rel = fixed_config_rel  # e.g. "conf/phase_6_config_v0_7_NEW.txt"
        self._original_config_backup: Optional[str] = None

        if self.project_root and self.fixed_config_rel:
            fixed_path = self.project_root / self.fixed_config_rel
            if fixed_path.exists():
                # Read and store original config at startup
                self._original_config_backup = fixed_path.read_text(encoding="utf-8")
                logging.getLogger("optimizer").info(
                    f"  Fixed-path mode: will overwrite {fixed_path}")
            else:
                logging.getLogger("optimizer").warning(
                    f"  WARNING: Fixed config path not found: {fixed_path}")

    def _get_fixed_config_path(self) -> Optional[Path]:
        if self.project_root and self.fixed_config_rel:
            return self.project_root / self.fixed_config_rel
        return None

    def restore_original_config(self):
        """Restore the original config file. Called on exit/crash."""
        fixed_path = self._get_fixed_config_path()
        if fixed_path and self._original_config_backup is not None:
            fixed_path.write_text(self._original_config_backup, encoding="utf-8")
            logging.getLogger("optimizer").info("  Original config restored.")

    def run(self, params: Dict[str, Any], config_mgr: ConfigManager,
            run_id: str) -> BacktestResult:
        run_dir = self.output_dir / run_id
        run_dir.mkdir(parents=True, exist_ok=True)

        # Always save trial config to run directory for audit trail
        trial_config_path = run_dir / "config.txt"
        config_mgr.generate_config(params, trial_config_path)

        result = BacktestResult(
            run_id=run_id,
            config_file=str(trial_config_path),
            result_dir=str(run_dir),
            params=params,
            timestamp=datetime.now().isoformat(),
        )

        if self.dry_run:
            result.status = "success"
            result.net_pnl = random.uniform(-50000, 300000)
            result.win_rate = random.uniform(30, 70)
            result.total_trades = random.randint(30, 200)
            result.profit_factor = random.uniform(0.5, 2.5)
            result.sharpe_ratio = random.uniform(-1, 3)
            result.max_drawdown_pct = random.uniform(2, 30)
            result.avg_win = random.uniform(3000, 15000)
            result.avg_loss = random.uniform(-8000, -2000)
            return result

        # ── Fixed-path mode: overwrite the engine's config file ──────────────
        fixed_path = self._get_fixed_config_path()
        if fixed_path:
            trial_content = trial_config_path.read_text(encoding="utf-8")
            fixed_path.write_text(trial_content, encoding="utf-8")

        # ── Build command ─────────────────────────────────────────────────────
        # {config} placeholder not used in fixed-path mode, but kept for
        # compatibility if someone passes it in the template anyway
        cmd = self.cmd_template.replace("{config}", str(trial_config_path.resolve()))

        # Run from project root so engine finds system_config.json and
        # all relative paths (data/, logs/, results/) correctly
        cwd = str(self.project_root) if self.project_root else None

        t0 = time.time()
        try:
            proc = subprocess.run(
                cmd, shell=True, capture_output=True,
                timeout=self.timeout,
                encoding="utf-8", errors="replace",
                cwd=cwd
            )
            result.duration_sec = time.time() - t0
            result.raw_output = proc.stdout + proc.stderr

            # Save full output for this trial
            (run_dir / "backtest_output.txt").write_text(
                result.raw_output, encoding="utf-8")

            if proc.returncode != 0:
                result.status = "failed"
                preview = "  ".join(result.raw_output.splitlines()[:5])
                logging.getLogger("optimizer").warning(
                    f"  [FAIL] rc={proc.returncode}  {preview[:300]}")
                return result

            metrics = self.parser.parse(result.raw_output, run_dir)
            for k, v in metrics.items():
                if hasattr(result, k):
                    setattr(result, k, v)
            result.status = "success"

        except subprocess.TimeoutExpired:
            result.status = "timeout"
            result.duration_sec = self.timeout
        except Exception as e:
            result.status = "failed"
            result.raw_output = str(e)

        return result


# ─────────────────────────────────────────────────────────────────────────────
# RESULTS MANAGER
# ─────────────────────────────────────────────────────────────────────────────

class ResultsManager:
    def __init__(self, output_dir: Path):
        self.output_dir = output_dir
        self.results_csv = output_dir / "all_results.csv"
        self.best_dir = output_dir / "BEST"
        self.best_dir.mkdir(parents=True, exist_ok=True)
        self._init_csv()

    def _init_csv(self):
        if not self.results_csv.exists():
            with open(self.results_csv, "w", newline="", encoding="utf-8") as f:
                writer = csv.writer(f)
                writer.writerow([
                    "rank", "run_id", "score", "net_pnl", "win_rate",
                    "total_trades", "profit_factor", "sharpe_ratio",
                    "max_drawdown_pct", "avg_win", "avg_loss",
                    "status", "duration_sec", "timestamp", "params_json"
                ])

    def save(self, result: BacktestResult, rank: int = 0):
        with open(self.results_csv, "a", newline="", encoding="utf-8") as f:
            writer = csv.writer(f)
            writer.writerow([
                rank, result.run_id, f"{result.score:.4f}",
                f"{result.net_pnl:.0f}", f"{result.win_rate:.2f}",
                result.total_trades, f"{result.profit_factor:.3f}",
                f"{result.sharpe_ratio:.3f}", f"{result.max_drawdown_pct:.2f}",
                f"{result.avg_win:.0f}", f"{result.avg_loss:.0f}",
                result.status, f"{result.duration_sec:.1f}",
                result.timestamp, json.dumps(result.params)
            ])

    def update_best(self, result: BacktestResult, config_mgr: ConfigManager):
        """Copy best config and generate a report."""
        shutil.copy(result.config_file, self.best_dir / "best_config.txt")
        # Generate report
        report = self._generate_report(result)
        (self.best_dir / "best_report.txt").write_text(report, encoding="utf-8")
        # JSON summary
        with open(self.best_dir / "best_summary.json", "w", encoding="utf-8") as f:
            json.dump(result.to_dict(), f, indent=2)

    def _generate_report(self, r: BacktestResult) -> str:
        lines = [
            "=" * 70,
            "  SD TRADING ENGINE V6 - OPTIMIZATION BEST RESULT",
            "=" * 70,
            f"  Run ID     : {r.run_id}",
            f"  Timestamp  : {r.timestamp}",
            f"  Score      : {r.score:.4f}",
            "",
            "-- PERFORMANCE METRICS ------------------------------------------",
            f"  Net P&L          : INR {r.net_pnl:,.0f}",
            f"  Win Rate         : {r.win_rate:.1f}%",
            f"  Total Trades     : {r.total_trades}",
            f"  Profit Factor    : {r.profit_factor:.3f}",
            f"  Sharpe Ratio     : {r.sharpe_ratio:.3f}",
            f"  Max Drawdown     : {r.max_drawdown_pct:.1f}%",
            f"  Avg Win          : INR {r.avg_win:,.0f}",
            f"  Avg Loss         : INR {r.avg_loss:,.0f}",
            "",
            "-- OPTIMIZED PARAMETERS -----------------------------------------",
        ]
        for k, v in sorted(r.params.items()):
            lines.append(f"  {k:<45} = {v}")
        lines += [
            "",
            "  Full config: best_config.txt",
            "=" * 70,
        ]
        return "\n".join(lines)

    def get_top_n(self, n: int = 10) -> List[dict]:
        rows = []
        try:
            with open(self.results_csv, newline="", encoding="utf-8") as f:
                reader = csv.DictReader(f)
                for row in reader:
                    if row.get("status") == "success":
                        try:
                            row["score"] = float(row["score"])
                            rows.append(row)
                        except ValueError:
                            pass
        except Exception:
            pass
        return sorted(rows, key=lambda x: x["score"], reverse=True)[:n]


# ─────────────────────────────────────────────────────────────────────────────
# MAIN OPTIMIZER ORCHESTRATOR
# ─────────────────────────────────────────────────────────────────────────────

class Optimizer:
    def __init__(self, args):
        self.args = args
        self.output_dir = Path(args.output_dir)
        self.output_dir.mkdir(parents=True, exist_ok=True)
        self.config_mgr = ConfigManager(args.config)
        self.runner = BacktestRunner(
            args.backtest_cmd, self.output_dir / "runs",
            timeout_sec=args.timeout, dry_run=args.dry_run,
            project_root=args.project_root,
            fixed_config_rel=args.fixed_config_rel,
        )
        self.scorer = ObjectiveCalculator(args.objective)
        self.results_mgr = ResultsManager(self.output_dir)
        self.logger = self._setup_logging()

        # Determine active parameters
        self.active_params = self._resolve_active_params(args.params_groups)

        self.state = OptimizationState(
            strategy=args.strategy,
            objective=args.objective,
            total_trials=args.trials,
        )

    def _setup_logging(self) -> logging.Logger:
        log = logging.getLogger("optimizer")
        log.setLevel(logging.INFO)
        fmt = logging.Formatter("%(asctime)s [%(levelname)s] %(message)s",
                                datefmt="%H:%M:%S")
        # Force UTF-8 on Windows console (fixes cp1252 UnicodeEncodeError)
        import sys, io
        if sys.platform == "win32":
            stream = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8",
                                      errors="replace", line_buffering=True)
        else:
            stream = sys.stdout
        ch = logging.StreamHandler(stream)
        ch.setFormatter(fmt)
        log.addHandler(ch)
        fh = logging.FileHandler(self.output_dir / "optimizer.log", encoding="utf-8")
        fh.setFormatter(fmt)
        log.addHandler(fh)
        return log

    def _resolve_active_params(self, groups: Optional[List[str]]) -> List[str]:
        if not groups or "all" in groups:
            return list(SEARCH_SPACE.keys())
        return [k for k, v in SEARCH_SPACE.items() if v.get("group") in groups]

    def _make_run_id(self, n: int) -> str:
        ts = datetime.now().strftime("%H%M%S")
        return f"run_{n:04d}_{ts}"

    def _run_and_score(self, params: Dict[str, Any], run_n: int) -> BacktestResult:
        """Merge params with base config and execute."""
        # Base params from the config file
        all_params = dict(self.config_mgr.base_params)
        all_params.update(params)

        run_id = self._make_run_id(run_n)
        result = self.runner.run(all_params, self.config_mgr, run_id)
        result.score = self.scorer.calculate(result)

        self.results_mgr.save(result, rank=self.state.completed + 1)

        if result.score > self.state.best_score:
            self.state.best_score = result.score
            self.state.best_run_id = run_id
            self.results_mgr.update_best(result, self.config_mgr)
            self.logger.info(
                f"  ** NEW BEST  score={result.score:.2f}  "
                f"pnl=INR {result.net_pnl:,.0f}  wr={result.win_rate:.1f}%  "
                f"pf={result.profit_factor:.2f}  dd={result.max_drawdown_pct:.1f}%"
            )

        self.state.history.append({
            "run": self.state.completed + 1,
            "score": result.score,
            "best": self.state.best_score,
        })
        self.state.completed += 1
        return result

    def _log_progress(self, result: BacktestResult, elapsed: float):
        pct = 100.0 * self.state.completed / max(self.state.total_trials, 1)
        eta = (elapsed / max(self.state.completed, 1)) * (
            self.state.total_trials - self.state.completed
        )
        self.logger.info(
            f"[{self.state.completed:>4}/{self.state.total_trials}] ({pct:5.1f}%)  "
            f"score={result.score:>8.2f}  best={self.state.best_score:>8.2f}  "
            f"ETA {eta/60:.1f}m  status={result.status}"
        )

    # ── Strategy: Random ──────────────────────────────────────────────────────
    def run_random(self):
        searcher = RandomSearch(SEARCH_SPACE, self.active_params)
        t0 = time.time()
        for i in range(self.args.trials):
            params = searcher.sample()
            result = self._run_and_score(params, i)
            self._log_progress(result, time.time() - t0)

    # ── Strategy: Grid ────────────────────────────────────────────────────────
    def run_grid(self):
        searcher = GridSearch(SEARCH_SPACE, self.active_params)
        total = min(searcher.total(), self.args.trials)
        self.state.total_trials = total
        self.logger.info(f"Grid search: {searcher.total():,} combinations (capped at {total})")
        t0 = time.time()
        for i in range(total):
            params = searcher.next()
            if params is None:
                break
            result = self._run_and_score(params, i)
            self._log_progress(result, time.time() - t0)

    # ── Strategy: Genetic ─────────────────────────────────────────────────────
    def run_genetic(self):
        pop_size = min(self.args.pop_size, self.args.trials)
        ga = GeneticOptimizer(SEARCH_SPACE, self.active_params,
                              pop_size=pop_size, seed=self.args.seed)
        t0 = time.time()
        total_evals = 0
        gen = 0

        while total_evals < self.args.trials:
            gen += 1
            unevaluated = ga.get_unevaluated()
            self.logger.info(f"-- Generation {gen} ({len(unevaluated)} to evaluate) --")

            for idx, params in unevaluated:
                if total_evals >= self.args.trials:
                    break
                result = self._run_and_score(params, total_evals)
                ga.update_score(idx, result.score)
                self._log_progress(result, time.time() - t0)
                total_evals += 1

            ga.evolve()

    # ── Strategy: Bayesian ────────────────────────────────────────────────────
    def run_bayesian(self):
        bo = BayesianOptimizer(SEARCH_SPACE, self.active_params,
                               n_random_init=max(10, self.args.trials // 5),
                               seed=self.args.seed)
        t0 = time.time()
        for i in range(self.args.trials):
            params = bo.suggest()
            result = self._run_and_score(params, i)
            if result.score > -999998:
                bo.register(params, result.score)
            self._log_progress(result, time.time() - t0)

    def run(self):
        self.logger.info("=" * 65)
        self.logger.info("  SD Trading Engine V6 - Parameter Optimizer")
        self.logger.info("=" * 65)
        self.logger.info(f"  Strategy   : {self.args.strategy}")
        self.logger.info(f"  Objective  : {self.args.objective}")
        self.logger.info(f"  Trials     : {self.args.trials}")
        self.logger.info(f"  Params     : {len(self.active_params)} active")
        self.logger.info(f"  Output     : {self.output_dir}")
        self.logger.info(f"  Dry Run    : {self.args.dry_run}")
        self.logger.info("=" * 65)

        strategies = {
            "random": self.run_random,
            "grid": self.run_grid,
            "genetic": self.run_genetic,
            "bayesian": self.run_bayesian,
        }
        strategies[self.args.strategy]()

        self._final_report()

    def _final_report(self):
        top10 = self.results_mgr.get_top_n(10)
        self.logger.info("\n" + "=" * 65)
        self.logger.info("  OPTIMIZATION COMPLETE - TOP 10 RESULTS")
        self.logger.info("=" * 65)
        self.logger.info(f"{'Rank':>4}  {'Score':>9}  {'P&L (INR)':>12}  {'WR%':>6}  {'PF':>5}  {'DD%':>5}  {'Trades':>6}")
        self.logger.info("-" * 65)
        for rank, row in enumerate(top10, 1):
            self.logger.info(
                f"{rank:>4}  {float(row['score']):>9.2f}  "
                f"{float(row.get('net_pnl', 0)):>12,.0f}  "
                f"{float(row.get('win_rate', 0)):>6.1f}  "
                f"{float(row.get('profit_factor', 0)):>5.2f}  "
                f"{float(row.get('max_drawdown_pct', 0)):>5.1f}  "
                f"{row.get('total_trades', 0):>6}"
            )
        self.logger.info("=" * 65)
        self.logger.info(f"  Best config -> {self.output_dir}/BEST/best_config.txt")
        self.logger.info(f"  All results -> {self.output_dir}/all_results.csv")
        self.logger.info(f"  Run log     -> {self.output_dir}/optimizer.log")


# ─────────────────────────────────────────────────────────────────────────────
# CLI
# ─────────────────────────────────────────────────────────────────────────────

def parse_args():
    p = argparse.ArgumentParser(
        description="SD Trading Engine V6 Parameter Optimizer",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Genetic optimization, 100 trials, composite objective
  python optimizer.py --config config.txt --backtest-cmd "./backtest {config}" --strategy genetic --trials 100

  # Bayesian optimization on risk+entry params, sharpe objective
  python optimizer.py --config config.txt --backtest-cmd "./backtest {config}" --strategy bayesian --trials 50 --params-groups risk entry --objective sharpe

  # Dry-run (no actual backtest) to test the framework
  python optimizer.py --config config.txt --backtest-cmd "echo test" --strategy random --trials 20 --dry-run

  # Grid search on specific groups only
  python optimizer.py --config config.txt --backtest-cmd "./backtest {config}" --strategy grid --params-groups risk trailing
        """
    )
    p.add_argument("--config", required=True, help="Path to base config file")
    p.add_argument("--backtest-cmd", required=True,
                   help="Command to run backtest. Use {config} as placeholder for config path.")
    p.add_argument("--output-dir", default="./optimization_runs",
                   help="Output directory for all runs and results")
    p.add_argument("--strategy", choices=["random", "grid", "genetic", "bayesian"],
                   default="genetic", help="Optimization strategy")
    p.add_argument("--objective",
                   choices=["composite", "net_pnl", "sharpe", "profit_factor",
                            "win_rate", "risk_adjusted"],
                   default="composite", help="Optimization objective")
    p.add_argument("--trials", type=int, default=100,
                   help="Number of optimization trials")
    p.add_argument("--params-groups", nargs="*",
                   choices=["all", "risk", "trailing", "entry", "zone", "volume",
                            "adx", "exhaustion", "timing", "scoring", "rejbk"],
                   default=["all"],
                   help="Parameter groups to optimize (default: all)")
    p.add_argument("--pop-size", type=int, default=20,
                   help="Population size for genetic algorithm")
    p.add_argument("--timeout", type=int, default=300,
                   help="Timeout per backtest run (seconds)")
    p.add_argument("--seed", type=int, default=42, help="Random seed")
    p.add_argument("--dry-run", action="store_true",
                   help="Simulate backtest results (for testing the framework)")
    p.add_argument("--list-params", action="store_true",
                   help="List all optimizable parameters and exit")
    # SD Engine V6 fixed-path mode (engine reads config from system_config.json)
    p.add_argument("--project-root", default="",
                   help="Project root directory where system_config.json lives "
                        "(e.g. D:\\SD_System\\SD_Volume_OI\\SD_Trading_V6)")
    p.add_argument("--fixed-config-rel", default="",
                   help="Relative path to strategy config from project root "
                        "(e.g. conf/phase_6_config_v0_7_NEW.txt)")
    return p.parse_args()


def list_params():
    groups = defaultdict(list)
    for name, spec in sorted(SEARCH_SPACE.items()):
        groups[spec["group"]].append((name, spec))
    for grp, params in sorted(groups.items()):
        print(f"\n-- Group: {grp} ------------------------------------------")
        for name, spec in params:
            vals = get_param_values(spec)
            print(f"  {name:<45}  [{vals[0]} ... {vals[-1]}]  ({len(vals)} values)")
    print(f"\nTotal: {len(SEARCH_SPACE)} parameters across {len(groups)} groups")


if __name__ == "__main__":
    args = parse_args()
    if args.list_params:
        list_params()
        sys.exit(0)
    opt = Optimizer(args)
    try:
        opt.run()
    except KeyboardInterrupt:
        print("\nInterrupted by user.")
    finally:
        # Always restore original config even if optimizer crashes mid-run
        opt.runner.restore_original_config()