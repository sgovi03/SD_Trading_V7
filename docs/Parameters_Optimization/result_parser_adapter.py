"""
SD Engine V6 — Result Parser Adapter
=====================================
Parses your engine's specific backtest output format.
Edit SECTION BELOW to match your engine's actual output.

Drop-in replacement for ResultParser in optimizer.py, or use standalone:
    python result_parser_adapter.py backtest_output.txt
"""

import re, sys, csv, json
from pathlib import Path
from typing import Dict, Optional


# ─────────────────────────────────────────────────────────────────────────────
# ⚠️  EDIT THIS SECTION to match your engine's exact output format
# ─────────────────────────────────────────────────────────────────────────────
class SDEngineV6Parser:
    """
    Customize the patterns below to match your backtest engine's output.
    
    Your engine likely outputs lines like:
      Total Trades    : 87
      Win Rate        : 54.02%
      Net P&L         : ₹1,23,456
      Profit Factor   : 1.834
      Max Drawdown    : 12.4%
      Sharpe Ratio    : 1.23
    
    Add/modify patterns in STDOUT_PATTERNS to match exactly.
    """

    # ── Pattern 1: Key-Value pairs in stdout/log ──────────────────────────────
    # Each entry: metric_name → list of regex patterns (tried in order)
    STDOUT_PATTERNS: Dict[str, list] = {

        "total_trades": [
            r"Total\s+Trades\s*[=:]\s*(\d+)",
            r"Trades\s*[=:]\s*(\d+)",
            r"Trade\s+Count\s*[=:]\s*(\d+)",
        ],
        "win_rate": [
            r"Win\s+Rate\s*[=:]\s*([\d.]+)\s*%?",
            r"Winners\s*[=:]\s*([\d.]+)\s*%",
            r"Winning\s+%\s*[=:]\s*([\d.]+)",
        ],
        "net_pnl": [
            r"Net\s+P(?:&|and)L\s*[=:]\s*[₹]?\s*([-\d,]+)",
            r"Total\s+P(?:&|and)L\s*[=:]\s*[₹]?\s*([-\d,]+)",
            r"Net\s+Profit\s*[=:]\s*[₹]?\s*([-\d,]+)",
            r"Total\s+Return\s*[=:]\s*[₹]?\s*([-\d,]+)",
        ],
        "profit_factor": [
            r"Profit\s+Factor\s*[=:]\s*([\d.]+)",
            r"PF\s*[=:]\s*([\d.]+)",
        ],
        "sharpe_ratio": [
            r"Sharpe\s+(?:Ratio)?\s*[=:]\s*([-\d.]+)",
            r"Sharpe\s*[=:]\s*([-\d.]+)",
        ],
        "max_drawdown_pct": [
            r"Max(?:imum)?\s+Draw(?:down)?\s*[=:]\s*([\d.]+)\s*%?",
            r"MaxDD\s*[=:]\s*([\d.]+)",
        ],
        "avg_win": [
            r"Avg(?:erage)?\s+Win\s*[=:]\s*[₹]?\s*([-\d,]+)",
            r"Mean\s+Win\s*[=:]\s*[₹]?\s*([-\d,]+)",
        ],
        "avg_loss": [
            r"Avg(?:erage)?\s+Loss\s*[=:]\s*[₹]?\s*([-\d,]+)",
            r"Mean\s+Loss\s*[=:]\s*[₹]?\s*([-\d,]+)",
        ],
        "avg_rr_achieved": [
            r"Avg(?:erage)?\s+R(?:isk)?R(?:eward)?\s*[=:]\s*([-\d.]+)",
            r"Avg\s+RR\s*[=:]\s*([-\d.]+)",
        ],
    }

    # ── Pattern 2: CSV summary file ───────────────────────────────────────────
    # Map from metric_name → list of possible column header names
    CSV_COLUMNS: Dict[str, list] = {
        "total_trades":     ["total_trades", "trades", "trade_count", "num_trades"],
        "win_rate":         ["win_rate", "win_pct", "winners_pct", "win%"],
        "net_pnl":          ["net_pnl", "total_pnl", "net_profit", "pnl", "profit"],
        "profit_factor":    ["profit_factor", "pf"],
        "sharpe_ratio":     ["sharpe", "sharpe_ratio"],
        "max_drawdown_pct": ["max_drawdown", "drawdown_pct", "max_dd", "drawdown%"],
        "avg_win":          ["avg_win", "average_win", "mean_win"],
        "avg_loss":         ["avg_loss", "average_loss", "mean_loss"],
    }

    # ── Pattern 3: JSON output file ───────────────────────────────────────────
    # Map from metric_name → JSON key path (dot-separated)
    JSON_KEYS: Dict[str, str] = {
        "total_trades":     "summary.total_trades",
        "win_rate":         "summary.win_rate",
        "net_pnl":          "summary.net_pnl",
        "profit_factor":    "summary.profit_factor",
        "sharpe_ratio":     "summary.sharpe_ratio",
        "max_drawdown_pct": "summary.max_drawdown_pct",
        "avg_win":          "summary.avg_win",
        "avg_loss":         "summary.avg_loss",
    }

    def parse_all(self, stdout: str, result_dir: Path) -> Dict[str, float]:
        """
        Try all parsers, merge results (later parsers override earlier ones).
        Order: stdout → JSON → CSV (CSV is most structured, wins on conflict).
        """
        metrics: Dict[str, float] = {}

        # 1. Stdout patterns
        metrics.update(self._parse_stdout(stdout))

        # 2. JSON file
        for jf in result_dir.glob("*.json"):
            if "config" not in jf.name and "summary" in jf.name.lower():
                metrics.update(self._parse_json(jf))

        # 3. CSV summary file
        for cf in result_dir.glob("*.csv"):
            parsed = self._parse_csv(cf)
            if parsed:
                metrics.update(parsed)
                break  # use first valid CSV

        return metrics

    def _parse_stdout(self, text: str) -> Dict[str, float]:
        result = {}
        for metric, patterns in self.STDOUT_PATTERNS.items():
            for pattern in patterns:
                m = re.search(pattern, text, re.IGNORECASE | re.MULTILINE)
                if m:
                    try:
                        val = float(m.group(1).replace(",", ""))
                        result[metric] = val
                    except ValueError:
                        pass
                    break
        return result

    def _parse_csv(self, path: Path) -> Dict[str, float]:
        result = {}
        try:
            with open(path, newline="", encoding="utf-8-sig") as f:
                reader = csv.DictReader(f)
                rows = list(reader)

            if not rows:
                return result

            # Try summary row (last row) or first row
            for candidate_row in [rows[-1], rows[0]]:
                headers_norm = {k.strip().lower().replace(" ", "_"): k
                                for k in candidate_row.keys()}
                found = 0
                temp = {}
                for metric, candidates in self.CSV_COLUMNS.items():
                    for c in candidates:
                        if c.lower() in headers_norm:
                            try:
                                raw = candidate_row[headers_norm[c.lower()]]
                                val = float(str(raw).replace(",", "").replace("₹", "").strip())
                                temp[metric] = val
                                found += 1
                            except (ValueError, TypeError):
                                pass
                            break
                if found >= 3:  # minimum fields to trust this row
                    result.update(temp)
                    break
        except Exception:
            pass
        return result

    def _parse_json(self, path: Path) -> Dict[str, float]:
        result = {}
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
            for metric, key_path in self.JSON_KEYS.items():
                parts = key_path.split(".")
                val = data
                for p in parts:
                    if isinstance(val, dict):
                        val = val.get(p)
                    else:
                        val = None
                        break
                if val is not None:
                    try:
                        result[metric] = float(val)
                    except (ValueError, TypeError):
                        pass
        except Exception:
            pass
        return result


# ─────────────────────────────────────────────────────────────────────────────
# STANDALONE TEST
# ─────────────────────────────────────────────────────────────────────────────
if __name__ == "__main__":
    # Test with a sample output string
    sample_output = """
    ============================================================
    SD Trading Engine V6 — Backtest Summary
    ============================================================
    Period          : 2024-01-01 to 2025-03-31
    Symbol          : NIFTY
    Total Trades    : 142
    Winners         : 79
    Losers          : 63
    Win Rate        : 55.63%
    Net P&L         : ₹1,84,320
    Gross Profit    : ₹4,23,850
    Gross Loss      : ₹2,39,530
    Profit Factor   : 1.770
    Avg Win         : ₹5,365
    Avg Loss        : ₹3,801
    Max Drawdown    : 14.2%
    Sharpe Ratio    : 1.42
    Avg RR          : 1.65
    ============================================================
    """

    parser = SDEngineV6Parser()
    metrics = parser.parse_all(sample_output, Path("."))

    print("\n── Parsed Metrics ──────────────────────────────────────")
    for k, v in sorted(metrics.items()):
        print(f"  {k:<25} = {v}")

    if len(sys.argv) > 1:
        p = Path(sys.argv[1])
        if p.exists():
            print(f"\n── Parsing file: {p} ──────────────────────────────────")
            text = p.read_text(encoding="utf-8", errors="replace")
            metrics = parser.parse_all(text, p.parent)
            for k, v in sorted(metrics.items()):
                print(f"  {k:<25} = {v}")
