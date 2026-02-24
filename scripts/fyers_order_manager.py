#!/usr/bin/env python3
"""
FYERS ORDER MANAGER
==================
Handles actual order placement to Fyers broker
Called by C++ engine via command-line interface
"""

import sys
import json
import logging
from datetime import datetime
from pathlib import Path

try:
    from fyers_apiv3 import fyersModel
except ImportError:
    print(json.dumps({"status": "error", "message": "fyers-apiv3 not installed"}))
    sys.exit(1)

# Import helpers
from system_config import load_config
import fyers_helper

# ============================================================
# CONFIGURATION
# ============================================================

config = load_config()
logger = config.setup_logging('order_manager')

CLIENT_ID = config.get_value('fyers', 'client_id')
TOKEN_FILE = config.get_path('fyers_token')
LOT_SIZE = int(config.get_value('trading', 'lot_size', default=1))


# ============================================================
# ORDER PLACEMENT
# ============================================================

def place_market_order(symbol: str, side: str, qty: int):
    """
    Place market order to Fyers
    
    Args:
        symbol: Trading symbol (e.g., "NSE:NIFTY26JANFUT")
        side: "BUY" or "SELL"
        qty: Quantity (lot size)
    
    Returns:
        JSON response from Fyers API
    """
    logger.info("=" * 60)
    logger.info(f"PLACING MARKET ORDER: {side} {qty} {symbol}")
    logger.info(f"Lot size (from system_config): {LOT_SIZE}")
    logger.info("=" * 60)

    # Enforce quantity to be a multiple of lot size
    if LOT_SIZE > 1:
        remainder = qty % LOT_SIZE
        if remainder != 0:
            adjusted_qty = qty - remainder
            logger.warning(
                f"Quantity {qty} not a multiple of lot size {LOT_SIZE}; adjusting to {adjusted_qty}"
            )
            qty = adjusted_qty if adjusted_qty > 0 else LOT_SIZE
    
    # Get valid access token (auto-refreshes if needed)
    access_token = fyers_helper.get_valid_access_token()
    
    if not access_token:
        error_msg = "Failed to get valid access token"
        logger.error(error_msg)
        return {
            "s": "error",
            "code": -1,
            "message": error_msg
        }
    
    # Initialize Fyers model
    fyers = fyersModel.FyersModel(
        client_id=CLIENT_ID,
        is_async=False,
        token=access_token,
        log_path=""
    )
    
    # Prepare order payload
    order_data = {
        "symbol": symbol,
        "qty": qty,
        "type": 2,  # Market order
        "side": 1 if side == "BUY" else -1,
        "productType": "INTRADAY",
        "limitPrice": 0,
        "stopPrice": 0,
        "validity": "DAY",
        "disclosedQty": 0,
        "offlineOrder": False
    }
    
    logger.info(f"Order payload: {json.dumps(order_data, indent=2)}")
    
    try:
        # Place order
        response = fyers.place_order(order_data)
        
        logger.info(f"Fyers API Response: {json.dumps(response, indent=2)}")
        
        return response
        
    except Exception as e:
        error_msg = f"Exception placing order: {str(e)}"
        logger.error(error_msg)
        return {
            "s": "error",
            "code": -1,
            "message": error_msg
        }


def cancel_order(order_id: str):
    """
    Cancel an existing order
    
    Args:
        order_id: Order ID returned by Fyers
    
    Returns:
        JSON response from Fyers API
    """
    logger.info(f"CANCELING ORDER: {order_id}")
    
    # Get valid access token
    access_token = fyers_helper.get_valid_access_token()
    
    if not access_token:
        return {
            "s": "error",
            "code": -1,
            "message": "Failed to get valid access token"
        }
    
    # Initialize Fyers model
    fyers = fyersModel.FyersModel(
        client_id=CLIENT_ID,
        is_async=False,
        token=access_token,
        log_path=""
    )
    
    try:
        response = fyers.cancel_order({"id": order_id})
        logger.info(f"Cancel response: {json.dumps(response, indent=2)}")
        return response
        
    except Exception as e:
        error_msg = f"Exception canceling order: {str(e)}"
        logger.error(error_msg)
        return {
            "s": "error",
            "code": -1,
            "message": error_msg
        }


def get_order_status(order_id: str):
    """
    Get order status
    
    Args:
        order_id: Order ID returned by Fyers
    
    Returns:
        JSON response from Fyers API
    """
    access_token = fyers_helper.get_valid_access_token()
    
    if not access_token:
        return {
            "s": "error",
            "code": -1,
            "message": "Failed to get valid access token"
        }
    
    # Initialize Fyers model
    fyers = fyersModel.FyersModel(
        client_id=CLIENT_ID,
        is_async=False,
        token=access_token,
        log_path=""
    )
    
    try:
        response = fyers.orderbook()
        
        # Find specific order
        if response.get('s') == 'ok' and 'orderBook' in response:
            for order in response['orderBook']:
                if order.get('id') == order_id:
                    return {
                        "s": "ok",
                        "order": order
                    }
        
        return {
            "s": "error",
            "message": f"Order {order_id} not found"
        }
        
    except Exception as e:
        return {
            "s": "error",
            "message": str(e)
        }


# ============================================================
# CLI INTERFACE FOR C++
# ============================================================

def main():
    """
    Command-line interface for C++ to call
    
    Usage:
        python fyers_order_manager.py place_market SYMBOL SIDE QTY
        python fyers_order_manager.py cancel ORDER_ID
        python fyers_order_manager.py status ORDER_ID
    """
    if len(sys.argv) < 2:
        print(json.dumps({
            "s": "error",
            "message": "Usage: fyers_order_manager.py <command> [args...]"
        }))
        sys.exit(1)
    
    command = sys.argv[1].lower()
    
    try:
        if command == "place_market":
            if len(sys.argv) != 5:
                print(json.dumps({
                    "s": "error",
                    "message": "Usage: place_market SYMBOL SIDE QTY"
                }))
                sys.exit(1)
            
            symbol = sys.argv[2]
            side = sys.argv[3].upper()
            qty = int(sys.argv[4])
            
            response = place_market_order(symbol, side, qty)
            print(json.dumps(response))
            
        elif command == "cancel":
            if len(sys.argv) != 3:
                print(json.dumps({
                    "s": "error",
                    "message": "Usage: cancel ORDER_ID"
                }))
                sys.exit(1)
            
            order_id = sys.argv[2]
            response = cancel_order(order_id)
            print(json.dumps(response))
            
        elif command == "status":
            if len(sys.argv) != 3:
                print(json.dumps({
                    "s": "error",
                    "message": "Usage: status ORDER_ID"
                }))
                sys.exit(1)
            
            order_id = sys.argv[2]
            response = get_order_status(order_id)
            print(json.dumps(response))
            
        else:
            print(json.dumps({
                "s": "error",
                "message": f"Unknown command: {command}"
            }))
            sys.exit(1)
            
    except Exception as e:
        print(json.dumps({
            "s": "error",
            "message": str(e)
        }))
        sys.exit(1)


if __name__ == "__main__":
    main()
