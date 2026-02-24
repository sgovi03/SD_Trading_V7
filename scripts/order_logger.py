#!/usr/bin/env python3
"""
Order Logging and Persistence Utility

Logs all simulated orders from the live engine to CSV for later simulation.
This enables DRY-run testing without broker connectivity.

CSV Format:
    entry_time,zone_id,zone_score,direction,entry_price,stop_loss,take_profit,
    position_size,lot_size,quantity

Usage:
    This is automatically used by the modified live engine when orders are placed.
    The live engine will call this utility to log orders instead of sending to broker.

Files Generated:
    - results/live_trades/simulated_orders.csv
"""

import csv
import os
from pathlib import Path
from datetime import datetime


class OrderLogger:
    """Log orders to CSV for simulation."""
    
    def __init__(self, output_dir: str = "results/live_trades"):
        self.output_dir = Path(output_dir)
        self.output_dir.mkdir(parents=True, exist_ok=True)
        self.csv_path = self.output_dir / "simulated_orders.csv"
        self._init_csv()
    
    def _init_csv(self) -> None:
        """Initialize CSV file with header if it doesn't exist."""
        if not self.csv_path.exists():
            with open(self.csv_path, 'w', newline='') as f:
                writer = csv.writer(f)
                writer.writerow([
                    'entry_time', 'zone_id', 'zone_score', 'direction',
                    'entry_price', 'stop_loss', 'take_profit',
                    'position_size', 'lot_size', 'quantity'
                ])
    
    def log_order(self, 
                 entry_time: str,
                 zone_id: str,
                 zone_score: float,
                 direction: str,
                 entry_price: float,
                 stop_loss: float,
                 take_profit: float,
                 position_size: int,
                 lot_size: int = 65) -> bool:
        """
        Log a single order to CSV.
        
        Args:
            entry_time: Order entry timestamp (DateTime from bar)
            zone_id: Zone identifier
            zone_score: Zone scoring value
            direction: 'BUY' or 'SELL'
            entry_price: Entry price (order price)
            stop_loss: Stop loss price
            take_profit: Take profit price
            position_size: Number of lots (position_size in TradeManager terms)
            lot_size: Lot size multiplier (default 65)
        
        Returns:
            True if logged successfully
        """
        try:
            quantity = position_size * lot_size
            
            with open(self.csv_path, 'a', newline='') as f:
                writer = csv.writer(f)
                writer.writerow([
                    entry_time,
                    zone_id,
                    f"{zone_score:.4f}",
                    direction,
                    f"{entry_price:.2f}",
                    f"{stop_loss:.2f}",
                    f"{take_profit:.2f}",
                    position_size,
                    lot_size,
                    quantity
                ])
            
            return True
        except Exception as e:
            print(f"Error logging order: {e}", flush=True)
            return False
    
    def get_order_count(self) -> int:
        """Get count of logged orders."""
        try:
            with open(self.csv_path, 'r') as f:
                return sum(1 for line in f) - 1  # Exclude header
        except:
            return 0


def main():
    """Test the order logger."""
    logger = OrderLogger()
    
    # Test log
    success = logger.log_order(
        entry_time='2025-01-06 09:30:00',
        zone_id='zone_3805',
        zone_score=98.5,
        direction='BUY',
        entry_price=3805.00,
        stop_loss=3795.00,
        take_profit=3820.00,
        position_size=1,
        lot_size=65
    )
    
    if success:
        print(f"✅ Order logged successfully")
        print(f"   File: {logger.csv_path}")
        print(f"   Total orders: {logger.get_order_count()}")
    else:
        print("❌ Failed to log order")


if __name__ == "__main__":
    main()
