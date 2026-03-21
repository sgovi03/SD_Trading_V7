"""
SD Engine V6 - Result Parser Adapter
Patterns matched to actual backtest output format.
"""

import re, sys, csv, json
from pathlib import Path
from typing import Dict, Optional


class SDEngineV6Parser:
    """
    Parses SD Trading Engine V6 backtest stdout.
    Matched to the exact output format:

      Total P&L:       $-524684.00
      Win Rate:        38.30%
      Total:           141
      Profit Factor:   0.46
      Sharpe Ratio:    0.348
      Max DD %:        189.11%
      Avg Win:         $8422.36
      Avg Loss:        $11258.53
      Avg RR Ratio:    0.75:1
    """

    STDOUT_PATTERNS: Dict[str, list] = {

        "net_pnl": [
            r"Total\s+P&L\s*:\s*\$\s*([-\d,.]+)",
        ],
        "win_rate": [
            r"Win\s+Rate\s*:\s*([\d.]+)\s*%",
        ],
        "total_trades": [
            r"(?:^|\n)\s*Total\s*:\s*(\d+)",          # under TRADES section
            r"Total\s+Trades\s*:\s*(\d+)",
        ],
        "profit_factor": [
            r"Profit\s+Factor\s*:\s*([\d.]+)",
        ],
        "sharpe_ratio": [
            r"Sharpe\s+Ratio\s*:\s*([-\d.]+)",
        ],
        "max_drawdown_pct": [
            r"Max\s+DD\s*%\s*:\s*([\d.]+)\s*%?",      # "Max DD %: 189.11%"
            r"Max\s+Drawdown\s*%\s*:\s*([\d.]+)",
        ],
        "avg_win": [
            r"Avg\s+Win\s*:\s*\$\s*([-\d,.]+)",
        ],
        "avg_loss": [
            r"Avg\s+Loss\s*:\s*\$\s*([-\d,.]+)",
        ],
        "avg_rr_achieved": [
            r"Avg\s+RR\s+Ratio\s*:\s*([\d.]+)\s*:",   # "0.75:1"
        ],
        # Bonus metrics (used in logging/diagnostics, not scoring)
        "winners": [
            r"Winners\s*:\s*(\d+)",
        ],
        "losers": [
            r"Losers\s*:\s*(\d+)",
        ],
        "largest_win": [
            r"Largest\s+Win\s*:\s*\$\s*([-\d,.]+)",
        ],
        "largest_loss": [
            r"Largest\s+Loss\s*:\s*\$\s*([-\d,.]+)",
        ],
        "expectancy": [
            r"Expectancy\s*:\s*\$\s*([-\d,.]+)",
        ],
        "return_pct": [
            r"Return\s*:\s*([-\d.]+)\s*%",
        ],
    }

    def parse_all(self, stdout: str, result_dir: Path) -> Dict[str, float]:
        metrics: Dict[str, float] = {}
        metrics.update(self._parse_stdout(stdout))

        # Also check for any CSV/JSON files the engine may have written
        for jf in result_dir.glob("*.json"):
            if "summary" in jf.name.lower():
                metrics.update(self._parse_json(jf))
        for cf in result_dir.glob("*.csv"):
            parsed = self._parse_csv(cf)
            if parsed:
                metrics.update(parsed)
                break

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
            headers_norm = {k.strip().lower().replace(" ", "_"): k for k in rows[-1].keys()}
            col_map = {
                "net_pnl":          ["net_pnl", "total_pnl", "pnl"],
                "win_rate":         ["win_rate", "win_pct"],
                "total_trades":     ["total_trades", "trades"],
                "profit_factor":    ["profit_factor", "pf"],
                "sharpe_ratio":     ["sharpe", "sharpe_ratio"],
                "max_drawdown_pct": ["max_dd_pct", "max_drawdown_pct", "max_dd"],
            }
            for metric, candidates in col_map.items():
                for c in candidates:
                    if c in headers_norm:
                        try:
                            result[metric] = float(
                                rows[-1][headers_norm[c]].replace(",", "").replace("$", ""))
                        except (ValueError, AttributeError):
                            pass
                        break
        except Exception:
            pass
        return result

    def _parse_json(self, path: Path) -> Dict[str, float]:
        result = {}
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
            key_map = {
                "net_pnl":          ["net_pnl", "total_pnl"],
                "win_rate":         ["win_rate"],
                "total_trades":     ["total_trades"],
                "profit_factor":    ["profit_factor"],
                "sharpe_ratio":     ["sharpe_ratio"],
                "max_drawdown_pct": ["max_dd_pct", "max_drawdown_pct"],
            }
            summary = data.get("summary", data)
            for metric, keys in key_map.items():
                for k in keys:
                    if k in summary:
                        try:
                            result[metric] = float(summary[k])
                        except (ValueError, TypeError):
                            pass
                        break
        except Exception:
            pass
        return result


# ─────────────────────────────────────────────────────────────────────────────
# STANDALONE TEST — run: python result_parser_adapter.py
# ─────────────────────────────────────────────────────────────────────────────
SAMPLE_OUTPUT = """
==================================================
  BACKTEST PERFORMANCE SUMMARY
==================================================
CAPITAL:
  Starting:        $300000.00
  Ending:          $-224684.00
  Total P&L:       $-524684.00
  Return:          -174.89%
TRADES:
  Total:           141
  Winners:         54
  Losers:          87
  Win Rate:        38.30%
P&L METRICS:
  Avg Win:         $8422.36
  Avg Loss:        $11258.53
  Largest Win:     $31047.16
  Largest Loss:    $-25101.50
  Profit Factor:   0.46
  Expectancy:      $-3721.16
RISK METRICS:
  Max Drawdown:    $567324.51
  Max DD %:        189.11%
  Max Cons Wins:   5
  Max Cons Losses: 8
QUALITY:
  Avg RR Ratio:    0.75:1
  Sharpe Ratio:    0.348
==================================================
"""

if __name__ == "__main__":
    parser = SDEngineV6Parser()

    # Use actual file if passed, else use embedded sample
    if len(sys.argv) > 1:
        p = Path(sys.argv[1])
        text = p.read_text(encoding="utf-8", errors="replace")
        print(f"Parsing: {p}\n")
    else:
        text = SAMPLE_OUTPUT
        print("Parsing embedded sample output\n")

    metrics = parser.parse_all(text, Path("."))

    print("=" * 50)
    print("  PARSED METRICS")
    print("=" * 50)
    if not metrics:
        print("  ERROR: No metrics parsed! Check patterns.")
    else:
        for k, v in sorted(metrics.items()):
            print(f"  {k:<25} = {v}")
    print("=" * 50)

    # Verify the 6 critical fields needed by the optimizer
    critical = ["net_pnl", "win_rate", "total_trades",
                "profit_factor", "sharpe_ratio", "max_drawdown_pct"]
    print("\nCritical fields check:")
    all_ok = True
    for field in critical:
        status = "OK " if field in metrics else "MISSING"
        if field not in metrics:
            all_ok = False
        print(f"  [{status}] {field}")
    print("\nResult:", "READY - optimizer will work correctly" if all_ok
          else "INCOMPLETE - optimizer will show status=failed")