#!/usr/bin/env python3
"""
DRY-Run Workflow Orchestrator

Simplifies the entire DRY-run testing process:
1. Verifies prerequisites
2. Checks for logged orders
3. Runs trade simulator
4. Displays results
5. Suggests next steps

Usage:
    python scripts/run_dry_run_workflow.py
    
    Optional arguments:
    --orders-csv PATH       Path to simulated_orders.csv (default: results/live_trades/simulated_orders.csv)
    --data-csv PATH         Path to live_data_stream.csv (default: data/live_data_stream.csv)
    --output-dir PATH       Path to output directory (default: results/live_trades)
"""

import sys
import subprocess
import os
from pathlib import Path
from typing import Optional, Tuple
import datetime as dt
import argparse


class DRYRunOrchestrator:
    """Orchestrates the entire DRY-run workflow."""
    
    def __init__(self, 
                 orders_csv: str = "results/live_trades/simulated_orders.csv",
                 data_csv: str = "data/live_data_stream.csv",
                 output_dir: str = "results/live_trades"):
        self.orders_csv = Path(orders_csv)
        self.data_csv = Path(data_csv)
        self.output_dir = Path(output_dir)
        self.results = {
            'orders_checked': False,
            'simulator_ran': False,
            'reports_generated': False,
            'errors': []
        }
    
    def log(self, msg: str, level: str = "INFO") -> None:
        """Log with timestamp and level."""
        ts = dt.datetime.now().strftime("%H:%M:%S")
        icons = {
            "INFO": "ℹ️",
            "SUCCESS": "✅",
            "WARNING": "⚠️",
            "ERROR": "❌",
            "STEP": "📋",
            "RESULT": "📊"
        }
        icon = icons.get(level, "")
        print(f"[{ts}] {icon} {msg}")
    
    def check_prerequisites(self) -> bool:
        """Verify all files and directories exist."""
        self.log("Checking prerequisites...", "STEP")
        
        checks_passed = True
        
        # Check orders CSV
        if self.orders_csv.exists():
            order_count = sum(1 for _ in open(self.orders_csv)) - 1  # -1 for header
            self.log(f"✓ Orders file found: {self.orders_csv} ({order_count} orders)", "SUCCESS")
        else:
            self.log(f"✗ Orders file NOT found: {self.orders_csv}", "ERROR")
            self.log("  Run live engine first to generate orders", "INFO")
            checks_passed = False
        
        # Check price data CSV
        if self.data_csv.exists():
            bar_count = sum(1 for _ in open(self.data_csv)) - 1  # -1 for header
            self.log(f"✓ Price data found: {self.data_csv} ({bar_count} bars)", "SUCCESS")
        else:
            self.log(f"✗ Price data NOT found: {self.data_csv}", "ERROR")
            checks_passed = False
        
        # Check output directory
        if self.output_dir.exists():
            self.log(f"✓ Output directory exists: {self.output_dir}", "SUCCESS")
        else:
            self.log(f"⚠ Creating output directory: {self.output_dir}", "WARNING")
            self.output_dir.mkdir(parents=True, exist_ok=True)
        
        return checks_passed
    
    def count_orders(self) -> int:
        """Count orders in CSV."""
        if not self.orders_csv.exists():
            return 0
        try:
            with open(self.orders_csv, 'r') as f:
                return sum(1 for _ in f) - 1  # -1 for header
        except:
            return 0
    
    def run_simulator(self) -> bool:
        """Run the trade simulator."""
        self.log("Running trade simulator...", "STEP")
        
        order_count = self.count_orders()
        if order_count == 0:
            self.log("No orders to simulate", "ERROR")
            return False
        
        try:
            # Build command
            cmd = [
                sys.executable,
                "scripts/simulate_trades.py",
                str(self.orders_csv),
                str(self.data_csv),
                str(self.output_dir)
            ]
            
            # Run simulator
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
            
            if result.returncode == 0:
                self.log("✓ Trade simulator completed successfully", "SUCCESS")
                
                # Display simulator output
                if result.stdout:
                    for line in result.stdout.split('\n'):
                        if line.strip():
                            self.log(f"  {line}", "RESULT")
                
                return True
            else:
                self.log("✗ Trade simulator failed", "ERROR")
                if result.stderr:
                    self.log(f"Error: {result.stderr}", "ERROR")
                self.results['errors'].append(f"Simulator error: {result.stderr}")
                return False
                
        except subprocess.TimeoutExpired:
            self.log("✗ Trade simulator timed out", "ERROR")
            return False
        except Exception as e:
            self.log(f"✗ Failed to run simulator: {e}", "ERROR")
            self.results['errors'].append(str(e))
            return False
    
    def check_reports_generated(self) -> bool:
        """Verify reports were generated."""
        self.log("Checking generated reports...", "STEP")
        
        reports = {
            'trades.csv': 'Simulated trades',
            'metrics.csv': 'Performance metrics',
            'equity_curve.csv': 'Equity curve'
        }
        
        all_present = True
        for report_name, description in reports.items():
            report_path = self.output_dir / report_name
            if report_path.exists():
                file_size = report_path.stat().st_size
                self.log(f"✓ {report_name} generated ({file_size} bytes) - {description}", "SUCCESS")
            else:
                self.log(f"✗ {report_name} NOT found", "ERROR")
                all_present = False
        
        return all_present
    
    def display_metrics(self) -> Optional[dict]:
        """Display metrics from metrics.csv."""
        self.log("Extracting metrics...", "STEP")
        
        metrics_path = self.output_dir / "metrics.csv"
        if not metrics_path.exists():
            self.log("Metrics file not found", "ERROR")
            return None
        
        try:
            metrics = {}
            with open(metrics_path, 'r') as f:
                f.readline()  # Skip header
                for line in f:
                    parts = line.strip().split(',')
                    if len(parts) >= 2:
                        key = parts[0].strip()
                        value = parts[1].strip()
                        metrics[key] = value
            
            # Display key metrics
            self.log("Performance Summary:", "RESULT")
            key_metrics = [
                'Total Trades',
                'Winning Trades',
                'Losing Trades',
                'Win Rate %',
                'Total P&L',
                'Total Return %',
                'Starting Capital',
                'Ending Capital',
                'Profit Factor'
            ]
            
            for metric in key_metrics:
                if metric in metrics:
                    self.log(f"  {metric:.<30} {metrics[metric]}", "RESULT")
            
            return metrics
        except Exception as e:
            self.log(f"Failed to parse metrics: {e}", "ERROR")
            return None
    
    def suggest_next_steps(self, metrics: Optional[dict]) -> None:
        """Suggest next steps based on results."""
        self.log("Next Steps:", "STEP")
        
        print("\n" + "="*70)
        print("NEXT STEPS")
        print("="*70)
        
        print("\n1. 📊 REVIEW RESULTS")
        print("   View individual trades:")
        print(f"      python -c \"import pandas as pd; print(pd.read_csv('{self.output_dir}/trades.csv'))\"")
        print("\n   View equity curve:")
        print(f"      python -c \"import pandas as pd; df=pd.read_csv('{self.output_dir}/equity_curve.csv'); print(df)\"")
        
        print("\n2. 🔍 ANALYZE PERFORMANCE")
        if metrics:
            try:
                win_rate = float(metrics.get('Win Rate %', '0'))
                total_pl = float(metrics.get('Total P&L', '0'))
                
                print(f"   Current Win Rate: {win_rate:.1f}%")
                print(f"   Current P&L: ${total_pl:.2f}")
                
                if win_rate >= 50:
                    print("   ✓ Win rate is acceptable (>= 50%)")
                else:
                    print("   ⚠ Win rate is low (< 50%) - review entry/exit logic")
                    
                if total_pl > 0:
                    print("   ✓ System is profitable in simulation")
                else:
                    print("   ⚠ System is not profitable in simulation")
            except:
                pass
        
        print("\n3. 🎯 VALIDATE REALISM")
        print("   - Do the simulated P&L amounts seem reasonable?")
        print("   - Are TP/SL hits matching expected market behavior?")
        print("   - Are trade timing and prices realistic?")
        
        print("\n4. ✅ WHEN READY FOR LIVE TRADING")
        print("   1. Uncomment broker.place_market_order() calls in:")
        print("      src/backtest/trade_manager.cpp (lines ~154, ~180)")
        print("   2. Rebuild:")
        print("      Build-Project.ps1 -Config Release")
        print("   3. Run live engine again")
        print("      ./build/Release/run_live.exe")
        print("   4. Orders will now be sent to the broker")
        
        print("\n5. 📝 DOCUMENTATION")
        print("   Full guide: DRY_RUN_IMPLEMENTATION.md")
        print("="*70 + "\n")
    
    def run_workflow(self) -> bool:
        """Execute entire DRY-run workflow."""
        print("\n" + "="*70)
        print("  DRY-RUN WORKFLOW ORCHESTRATOR")
        print("="*70 + "\n")
        
        # Step 1: Check prerequisites
        if not self.check_prerequisites():
            print("\n" + "="*70)
            print("PREREQUISITES NOT MET")
            print("="*70)
            print("\nPlease:")
            print("1. Run the live engine first (./build/Release/run_live.exe)")
            print("2. Wait for orders to be logged to simulated_orders.csv")
            print("3. Run this script again")
            print("="*70 + "\n")
            return False
        
        print()
        
        # Step 2: Run simulator
        if not self.run_simulator():
            print("\n" + "="*70)
            print("SIMULATION FAILED")
            print("="*70)
            print(f"\nErrors encountered:")
            for error in self.results['errors']:
                print(f"  - {error}")
            print("="*70 + "\n")
            return False
        
        print()
        
        # Step 3: Check reports
        if not self.check_reports_generated():
            self.log("Some reports were not generated", "WARNING")
        
        print()
        
        # Step 4: Display metrics
        metrics = self.display_metrics()
        
        print()
        
        # Step 5: Suggest next steps
        self.suggest_next_steps(metrics)
        
        return True


def main() -> int:
    """Main entry point."""
    parser = argparse.ArgumentParser(
        description="DRY-Run Workflow Orchestrator",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python scripts/run_dry_run_workflow.py
  python scripts/run_dry_run_workflow.py --orders-csv custom_orders.csv
  python scripts/run_dry_run_workflow.py --data-csv data/price_data.csv --output-dir my_results
        """
    )
    
    parser.add_argument('--orders-csv', 
                       default='results/live_trades/simulated_orders.csv',
                       help='Path to simulated_orders.csv')
    parser.add_argument('--data-csv',
                       default='data/live_data_stream.csv',
                       help='Path to live_data_stream.csv')
    parser.add_argument('--output-dir',
                       default='results/live_trades',
                       help='Output directory for reports')
    
    args = parser.parse_args()
    
    orchestrator = DRYRunOrchestrator(
        orders_csv=args.orders_csv,
        data_csv=args.data_csv,
        output_dir=args.output_dir
    )
    
    success = orchestrator.run_workflow()
    return 0 if success else 1


if __name__ == "__main__":
    sys.exit(main())
