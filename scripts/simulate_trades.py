#!/usr/bin/env python3
"""
Live Engine Trade Simulator

Simulates trade outcomes from logged orders based on actual market price data.
Reads orders that WOULD have been placed and simulates their execution against
the live_data_stream.csv price feed.

Purpose:
- Simulate trade fills and exits without broker connectivity
- Generate realistic P&L analysis for DRY runs
- Produce performance reports (trades.csv, metrics.csv, equity_curve.csv)

Usage:
    python scripts/simulate_trades.py [simulated_orders_csv] [live_data_csv]

Example:
    python scripts/simulate_trades.py \
      results/live_trades/simulated_orders.csv \
      data/live_data_stream.csv
"""

import csv
import json
import sys
from pathlib import Path
from typing import List, Dict, Optional, Tuple
import datetime as dt


class TradeSimulator:
    """Simulate trade outcomes from logged orders and price data."""

    def __init__(self, output_dir: str = "results/live_trades"):
        self.output_dir = Path(output_dir)
        self.output_dir.mkdir(parents=True, exist_ok=True)
        self.starting_capital = 300000.0
        self.trades = []
        
    def log(self, msg: str) -> None:
        """Log with timestamp."""
        ts = dt.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        try:
            print(f"[{ts}] {msg}")
        except UnicodeEncodeError:
            # Fallback for Windows consoles that cannot render emojis
            safe_msg = msg.encode('ascii', 'ignore').decode('ascii')
            print(f"[{ts}] {safe_msg}")

    def load_orders(self, orders_csv: str) -> List[Dict]:
        """Load simulated orders from CSV."""
        if not Path(orders_csv).exists():
            self.log(f"❌ Orders file not found: {orders_csv}")
            return []
        
        orders = []
        with open(orders_csv, 'r', newline='') as f:
            reader = csv.DictReader(f)
            for row in reader:
                orders.append(row)
        
        self.log(f"✅ Loaded {len(orders)} simulated orders")
        return orders

    def load_price_data(self, data_csv: str) -> List[Dict]:
        """Load price data from CSV."""
        if not Path(data_csv).exists():
            self.log(f"❌ Price data file not found: {data_csv}")
            return []
        
        data = []
        with open(data_csv, 'r', newline='') as f:
            reader = csv.DictReader(f)
            for row in reader:
                data.append(row)
        
        self.log(f"✅ Loaded {len(data)} price bars")
        return data

    def find_next_prices(self, 
                        entry_time: str, 
                        price_data: List[Dict]
                       ) -> Tuple[List[Dict], int]:
        """Find price bars after entry time."""
        start_idx = 0
        for i, bar in enumerate(price_data):
            if bar.get('DateTime', '') >= entry_time:
                start_idx = i + 1
                break
        
        return price_data[start_idx:], start_idx

    def simulate_trade(self, 
                      order: Dict, 
                      price_data: List[Dict]
                     ) -> Optional[Dict]:
        """
        Simulate a single trade.
        Returns completed trade with exit price, P&L, etc.
        """
        try:
            entry_time = order.get('entry_time', '')
            entry_price = float(order.get('entry_price', 0))
            stop_loss = float(order.get('stop_loss', 0))
            take_profit = float(order.get('take_profit', 0))
            direction = order.get('direction', 'BUY')
            position_size = int(order.get('position_size', 1))
            zone_id = order.get('zone_id', '0')
            zone_score = float(order.get('zone_score', 0))
            lot_size = int(order.get('lot_size', 65))
            
            total_quantity = position_size * lot_size
            
            # Find price data after entry
            future_prices, start_idx = self.find_next_prices(entry_time, price_data)
            
            if not future_prices:
                self.log(f"⚠️  No price data after {entry_time} for trade {zone_id}")
                return None
            
            # Simulate exit: which is hit first, SL or TP?
            exit_price = None
            exit_time = None
            exit_reason = None
            
            for bar in future_prices:
                current_time = bar.get('DateTime', '')
                high = float(bar.get('High', entry_price))
                low = float(bar.get('Low', entry_price))
                close = float(bar.get('Close', entry_price))
                
                # Check if TP or SL is hit
                if direction == "BUY":
                    # For long: check if high >= TP or low <= SL
                    if high >= take_profit:
                        exit_price = take_profit
                        exit_time = current_time
                        exit_reason = "TAKE_PROFIT_HIT"
                        break
                    elif low <= stop_loss:
                        exit_price = stop_loss
                        exit_time = current_time
                        exit_reason = "STOP_LOSS_HIT"
                        break
                else:  # SELL
                    # For short: check if low <= TP or high >= SL
                    if low <= take_profit:
                        exit_price = take_profit
                        exit_time = current_time
                        exit_reason = "TAKE_PROFIT_HIT"
                        break
                    elif high >= stop_loss:
                        exit_price = stop_loss
                        exit_time = current_time
                        exit_reason = "STOP_LOSS_HIT"
                        break
            
            # If no exit hit, use last close as exit
            if exit_price is None:
                last_bar = future_prices[-1] if future_prices else price_data[-1]
                exit_price = float(last_bar.get('Close', entry_price))
                exit_time = last_bar.get('DateTime', entry_time)
                exit_reason = "SESSION_END"
            
            # Calculate P&L
            if direction == "BUY":
                pnl = (exit_price - entry_price) * total_quantity
            else:
                pnl = (entry_price - exit_price) * total_quantity
            
            return_pct = (pnl / (entry_price * total_quantity)) * 100 if entry_price > 0 else 0
            
            return {
                'entry_time': entry_time,
                'exit_time': exit_time,
                'entry_price': entry_price,
                'exit_price': exit_price,
                'stop_loss': stop_loss,
                'take_profit': take_profit,
                'direction': direction,
                'position_size': position_size,
                'quantity': total_quantity,
                'pnl': pnl,
                'return_pct': return_pct,
                'exit_reason': exit_reason,
                'zone_id': zone_id,
                'zone_score': zone_score,
            }
            
        except Exception as e:
            self.log(f"❌ Error simulating trade: {e}")
            return None

    def simulate_all_trades(self, orders: List[Dict], price_data: List[Dict]) -> List[Dict]:
        """Simulate all orders into completed trades."""
        simulated_trades = []
        
        for i, order in enumerate(orders, 1):
            trade = self.simulate_trade(order, price_data)
            if trade:
                trade['trade_num'] = i
                simulated_trades.append(trade)
                
                if i % 50 == 0:
                    self.log(f"Simulated {i}/{len(orders)} trades...")
        
        self.log(f"✅ Simulated {len(simulated_trades)} trades")
        return simulated_trades

    def export_trades_csv(self, trades: List[Dict]) -> bool:
        """Export trades to trades.csv (same format as live engine)."""
        output_path = self.output_dir / "trades.csv"
        
        # Simplified version of backtest format
        header = ("Trade#,Direction,Entry Date,Exit Date,Entry Price,Exit Price,"
                  "Stop Loss,Take Profit,Position Size,P&L,Return %,Exit Reason,Zone ID,Zone Score\n")
        
        with open(output_path, 'w', newline='') as f:
            f.write(header)
            for trade in trades:
                row = (f"{trade['trade_num']},"
                       f"{trade['direction']},"
                       f"{trade['entry_time']},"
                       f"{trade['exit_time']},"
                       f"{trade['entry_price']:.2f},"
                       f"{trade['exit_price']:.2f},"
                       f"{trade['stop_loss']:.2f},"
                       f"{trade['take_profit']:.2f},"
                       f"{trade['position_size']},"
                       f"{trade['pnl']:.2f},"
                       f"{trade['return_pct']:.2f},"
                       f"{trade['exit_reason']},"
                       f"{trade['zone_id']},"
                       f"{trade['zone_score']:.2f}\n")
                f.write(row)
        
        self.log(f"✅ Exported {len(trades)} trades to: {output_path}")
        return True

    def calculate_metrics(self, trades: List[Dict]) -> Dict:
        """Calculate performance metrics."""
        if not trades:
            return {
                'total_trades': 0,
                'winning_trades': 0,
                'losing_trades': 0,
                'total_pnl': 0,
                'win_rate': 0,
                'starting_capital': self.starting_capital,
                'ending_capital': self.starting_capital,
            }
        
        total_trades = len(trades)
        winning_trades = len([t for t in trades if t['pnl'] > 0])
        losing_trades = len([t for t in trades if t['pnl'] < 0])
        total_pnl = sum(t['pnl'] for t in trades)
        win_rate = (winning_trades / total_trades * 100) if total_trades > 0 else 0
        
        ending_capital = self.starting_capital + total_pnl
        total_return_pct = (total_pnl / self.starting_capital * 100) if self.starting_capital > 0 else 0
        
        # Calculate additional metrics
        avg_win = sum(t['pnl'] for t in trades if t['pnl'] > 0) / winning_trades if winning_trades > 0 else 0
        avg_loss = abs(sum(t['pnl'] for t in trades if t['pnl'] < 0) / losing_trades) if losing_trades > 0 else 0
        
        largest_win = max([t['pnl'] for t in trades], default=0)
        largest_loss = min([t['pnl'] for t in trades], default=0)
        
        profit_factor = (sum(t['pnl'] for t in trades if t['pnl'] > 0) / 
                        abs(sum(t['pnl'] for t in trades if t['pnl'] < 0))) \
                        if sum(t['pnl'] for t in trades if t['pnl'] < 0) != 0 else 0
        
        expectancy = (winning_trades * avg_win - losing_trades * avg_loss) / total_trades if total_trades > 0 else 0
        
        return {
            'total_trades': total_trades,
            'winning_trades': winning_trades,
            'losing_trades': losing_trades,
            'win_rate': win_rate,
            'total_pnl': total_pnl,
            'starting_capital': self.starting_capital,
            'ending_capital': ending_capital,
            'total_return_pct': total_return_pct,
            'avg_win': avg_win,
            'avg_loss': avg_loss,
            'largest_win': largest_win,
            'largest_loss': largest_loss,
            'profit_factor': profit_factor,
            'expectancy': expectancy,
        }

    def export_metrics_csv(self, metrics: Dict) -> bool:
        """Export metrics to metrics.csv."""
        output_path = self.output_dir / "metrics.csv"
        
        rows = [
            ("Metric", "Value"),
            ("Total Trades", metrics['total_trades']),
            ("Winning Trades", metrics['winning_trades']),
            ("Losing Trades", metrics['losing_trades']),
            ("Win Rate %", f"{metrics['win_rate']:.2f}"),
            ("Starting Capital", f"{metrics['starting_capital']:.2f}"),
            ("Ending Capital", f"{metrics['ending_capital']:.2f}"),
            ("Total P&L", f"{metrics['total_pnl']:.2f}"),
            ("Total Return %", f"{metrics['total_return_pct']:.2f}"),
            ("Average Win", f"{metrics['avg_win']:.2f}"),
            ("Average Loss", f"{metrics['avg_loss']:.2f}"),
            ("Largest Win", f"{metrics['largest_win']:.2f}"),
            ("Largest Loss", f"{metrics['largest_loss']:.2f}"),
            ("Profit Factor", f"{metrics['profit_factor']:.2f}"),
            ("Expectancy", f"{metrics['expectancy']:.2f}"),
        ]
        
        with open(output_path, 'w', newline='') as f:
            writer = csv.writer(f)
            writer.writerows(rows)
        
        self.log(f"✅ Exported metrics to: {output_path}")
        return True

    def export_equity_curve(self, trades: List[Dict]) -> bool:
        """Export equity curve to equity_curve.csv."""
        output_path = self.output_dir / "equity_curve.csv"
        
        rows = [("Trade#", "Date", "Capital", "Cumulative P&L", "Cumulative Return %")]
        
        # Starting point
        rows.append(("0", "Start", f"{self.starting_capital:.2f}", "0.00", "0.00"))
        
        current_capital = self.starting_capital
        cumulative_pnl = 0.0
        
        for trade in trades:
            current_capital += trade['pnl']
            cumulative_pnl += trade['pnl']
            cumulative_return = (cumulative_pnl / self.starting_capital * 100) if self.starting_capital > 0 else 0
            
            rows.append((
                str(trade['trade_num']),
                trade['exit_time'],
                f"{current_capital:.2f}",
                f"{cumulative_pnl:.2f}",
                f"{cumulative_return:.2f}"
            ))
        
        with open(output_path, 'w', newline='') as f:
            writer = csv.writer(f)
            writer.writerows(rows)
        
        self.log(f"✅ Exported equity curve to: {output_path}")
        return True

    def generate_reports(self, orders_csv: str, data_csv: str) -> bool:
        """Generate all performance reports from orders and price data."""
        self.log("=" * 70)
        self.log("  TRADE SIMULATOR - GENERATING REALISTIC PERFORMANCE REPORTS")
        self.log("=" * 70)
        
        # Load data
        orders = self.load_orders(orders_csv)
        if not orders:
            self.log("❌ No orders to simulate")
            return False
        
        price_data = self.load_price_data(data_csv)
        if not price_data:
            self.log("❌ No price data available")
            return False
        
        # Simulate trades
        trades = self.simulate_all_trades(orders, price_data)
        if not trades:
            self.log("❌ No trades could be simulated")
            return False
        
        # Calculate metrics
        metrics = self.calculate_metrics(trades)
        
        # Export reports
        all_ok = True
        all_ok &= self.export_trades_csv(trades)
        all_ok &= self.export_metrics_csv(metrics)
        all_ok &= self.export_equity_curve(trades)
        
        # Display summary
        self.log("=" * 70)
        self.log("  SIMULATION RESULTS")
        self.log("=" * 70)
        self.log(f"Total Trades Simulated:  {metrics['total_trades']}")
        self.log(f"Winning Trades:          {metrics['winning_trades']}")
        self.log(f"Losing Trades:           {metrics['losing_trades']}")
        self.log(f"Win Rate:                {metrics['win_rate']:.2f}%")
        self.log(f"Total P&L:               ${metrics['total_pnl']:.2f}")
        self.log(f"Return %:                {metrics['total_return_pct']:.2f}%")
        self.log(f"Starting Capital:        ${metrics['starting_capital']:.2f}")
        self.log(f"Ending Capital:          ${metrics['ending_capital']:.2f}")
        self.log(f"Profit Factor:           {metrics['profit_factor']:.2f}")
        self.log("=" * 70)
        
        return all_ok


def main() -> int:
    """Main entry point."""
    if len(sys.argv) < 3:
        print("Usage: python simulate_trades.py <orders_csv> <price_data_csv>")
        print("Example: python simulate_trades.py results/live_trades/simulated_orders.csv data/live_data_stream.csv")
        return 1
    
    orders_csv = sys.argv[1]
    data_csv = sys.argv[2]
    output_dir = sys.argv[3] if len(sys.argv) > 3 else "results/live_trades"
    
    simulator = TradeSimulator(output_dir)
    success = simulator.generate_reports(orders_csv, data_csv)
    
    return 0 if success else 1


if __name__ == "__main__":
    sys.exit(main())
