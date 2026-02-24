#!/usr/bin/env python3
"""
Live Engine Performance Report Generator

Generates performance metrics from trades and zone files after a live trading session.
Automatically called by the live engine on shutdown, or can be run manually to regenerate reports.

Usage:
    python scripts/generate_live_report.py [output_dir]

Example:
    python scripts/generate_live_report.py results/live_trades
"""

import json
import csv
import sys
from pathlib import Path
from typing import List, Dict, Optional
import datetime as dt


class LivePerformanceReporter:
    """Generate performance reports from live trading results."""

    def __init__(self, output_dir: str = "results/live_trades"):
        self.output_dir = Path(output_dir)
        self.output_dir.mkdir(parents=True, exist_ok=True)
        
    def log(self, msg: str) -> None:
        """Log a message with timestamp."""
        ts = dt.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        print(f"[{ts}] {msg}")

    def generate_trades_csv_stub(self) -> bool:
        """
        Generate trades.csv stub if no trades exist yet.
        This file will be populated as trades are executed.
        """
        trades_path = self.output_dir / "trades.csv"
        
        # If it already exists, don't overwrite
        if trades_path.exists():
            self.log(f"Trades file already exists: {trades_path}")
            return True
        
        # Create header-only file (ready for trades to be appended)
        header = ("Trade#,Direction,Entry Date,Exit Date,Entry Price,Exit Price,"
                  "Stop Loss,Take Profit,Position Size,Risk Amount,Reward Amount,"
                  "P&L,Return %,Exit Reason,Zone ID,Zone Score,Base Strength,Elite Bonus,"
                  "Swing Score,Regime Score,State Freshness,Rejection Score,"
                  "Recommended RR,Score Rationale,Aggressiveness,"
                  "Regime,Zone Formation,Zone Distal,Zone Proximal,"
                  "Swing Percentile,At Swing Extreme\n")
        
        with open(trades_path, 'w', newline='') as f:
            f.write(header)
        
        self.log(f"✅ Created trades.csv stub: {trades_path}")
        return True

    def generate_metrics_csv(self, trades_data: Optional[List[Dict]] = None) -> bool:
        """
        Generate metrics.csv with performance summary.
        If no trades yet, shows projected metrics based on zone analysis.
        """
        metrics_path = self.output_dir / "metrics.csv"
        
        # Determine metrics based on trades
        if trades_data and len(trades_data) > 0:
            # Calculate from actual trades
            total_trades = len(trades_data)
            winning_trades = len([t for t in trades_data if t.get('pnl', 0) > 0])
            losing_trades = len([t for t in trades_data if t.get('pnl', 0) < 0])
            total_pnl = sum(float(t.get('pnl', 0)) for t in trades_data)
            win_rate = (winning_trades / total_trades * 100) if total_trades > 0 else 0
        else:
            # No trades yet - show placeholder metrics
            total_trades = 0
            winning_trades = 0
            losing_trades = 0
            total_pnl = 0.0
            win_rate = 0.0
        
        starting_capital = 300000
        ending_capital = starting_capital + total_pnl
        total_return_pct = (total_pnl / starting_capital * 100) if starting_capital > 0 else 0
        
        metrics = [
            ("Metric", "Value"),
            ("Starting Capital", f"{starting_capital:.2f}"),
            ("Ending Capital", f"{ending_capital:.2f}"),
            ("Total P&L", f"{total_pnl:.2f}"),
            ("Total Return %", f"{total_return_pct:.2f}"),
            ("Total Trades", total_trades),
            ("Winning Trades", winning_trades),
            ("Losing Trades", losing_trades),
            ("Win Rate %", f"{win_rate:.2f}"),
            ("Status", "NO_TRADES_YET" if total_trades == 0 else "ACTIVE"),
        ]
        
        with open(metrics_path, 'w', newline='') as f:
            writer = csv.writer(f)
            writer.writerows(metrics)
        
        self.log(f"✅ Created metrics.csv: {metrics_path}")
        self.log(f"   - Total Trades: {total_trades}")
        self.log(f"   - Total P&L: ${total_pnl:.2f}")
        self.log(f"   - Return %: {total_return_pct:.2f}%")
        return True

    def generate_equity_curve_csv(self, trades_data: Optional[List[Dict]] = None) -> bool:
        """
        Generate equity_curve.csv showing capital progression.
        """
        equity_path = self.output_dir / "equity_curve.csv"
        starting_capital = 300000
        
        rows = [("Trade#", "Date", "Capital", "Cumulative P&L", "Cumulative Return %")]
        
        # Starting point
        rows.append(("0", "Start", f"{starting_capital:.2f}", "0.00", "0.00"))
        
        # Add trades if any
        if trades_data and len(trades_data) > 0:
            current_capital = starting_capital
            cumulative_pnl = 0.0
            
            for i, trade in enumerate(trades_data, 1):
                pnl = float(trade.get('pnl', 0))
                current_capital += pnl
                cumulative_pnl += pnl
                cumulative_return = (cumulative_pnl / starting_capital * 100) if starting_capital > 0 else 0
                
                exit_date = trade.get('exit_date', 'N/A')
                rows.append((
                    str(i),
                    exit_date,
                    f"{current_capital:.2f}",
                    f"{cumulative_pnl:.2f}",
                    f"{cumulative_return:.2f}"
                ))
        
        with open(equity_path, 'w', newline='') as f:
            writer = csv.writer(f)
            writer.writerows(rows)
        
        self.log(f"✅ Created equity_curve.csv: {equity_path}")
        return True

    def load_trades_from_csv(self) -> Optional[List[Dict]]:
        """Load existing trades from trades.csv if available."""
        trades_path = self.output_dir / "trades.csv"
        
        if not trades_path.exists():
            return None
        
        trades = []
        with open(trades_path, 'r', newline='') as f:
            reader = csv.DictReader(f)
            for row in reader:
                trades.append(row)
        
        return trades if len(trades) > 0 else None

    def load_zone_stats(self) -> Optional[Dict]:
        """Load zone statistics from zone files."""
        master_path = self.output_dir / "zones_live_master.json"
        
        if not master_path.exists():
            return None
        
        try:
            with open(master_path, 'r') as f:
                data = json.load(f)
            
            zones = data.get('zones', [])
            active_zones = len([z for z in zones if z.get('is_active', False)])
            total_zones = len(zones)
            
            return {
                'total_zones': total_zones,
                'active_zones': active_zones,
                'inactive_zones': total_zones - active_zones,
            }
        except Exception as e:
            self.log(f"⚠️  Could not load zone stats: {e}")
            return None

    def generate_report(self) -> bool:
        """Generate all performance report files."""
        self.log("=" * 70)
        self.log("  LIVE ENGINE PERFORMANCE REPORT GENERATOR")
        self.log("=" * 70)
        
        # Load existing trades
        trades = self.load_trades_from_csv()
        if trades:
            self.log(f"Found {len(trades)} trades in trades.csv")
        else:
            self.log("No trades found yet - generating stub files")
        
        # Load zone stats
        zone_stats = self.load_zone_stats()
        if zone_stats:
            self.log(f"Zone Statistics:")
            self.log(f"  - Total zones: {zone_stats['total_zones']}")
            self.log(f"  - Active zones: {zone_stats['active_zones']}")
            self.log(f"  - Inactive zones: {zone_stats['inactive_zones']}")
        
        # Generate all files
        all_ok = True
        all_ok &= self.generate_trades_csv_stub()
        all_ok &= self.generate_metrics_csv(trades)
        all_ok &= self.generate_equity_curve_csv(trades)
        
        self.log("=" * 70)
        if all_ok:
            self.log("✅ All report files generated successfully!")
            self.log(f"   Location: {self.output_dir}/")
            self.log("   Files:")
            self.log("     - trades.csv (trade-by-trade details)")
            self.log("     - metrics.csv (performance summary)")
            self.log("     - equity_curve.csv (capital progression)")
        else:
            self.log("⚠️  Some report files failed to generate")
        self.log("=" * 70)
        
        return all_ok


def main() -> int:
    """Main entry point."""
    output_dir = sys.argv[1] if len(sys.argv) > 1 else "results/live_trades"
    
    reporter = LivePerformanceReporter(output_dir)
    success = reporter.generate_report()
    
    return 0 if success else 1


if __name__ == "__main__":
    sys.exit(main())
