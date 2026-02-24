#ifndef BROKER_INTERFACE_H
#define BROKER_INTERFACE_H

#include <string>
#include <vector>
#include "common_types.h"

namespace SDTrading {
namespace Live {

/**
 * @enum OrderStatus
 * @brief Order execution status
 */
enum class OrderStatus {
    PENDING,
    FILLED,
    REJECTED,
    CANCELLED,
    PARTIAL
};

/**
 * @struct OrderResponse
 * @brief Response from broker after order placement
 */
struct OrderResponse {
    std::string order_id;
    OrderStatus status;
    std::string message;
    double filled_price;
    int filled_quantity;
    
    OrderResponse()
        : status(OrderStatus::PENDING), filled_price(0), filled_quantity(0) {}
};

/**
 * @struct Position
 * @brief Current position information
 */
struct Position {
    std::string symbol;
    std::string direction;  // "LONG" or "SHORT"
    int quantity;
    double entry_price;
    double current_price;
    double unrealized_pnl;
    
    Position()
        : quantity(0), entry_price(0), current_price(0), unrealized_pnl(0) {}
};

/**
 * @class BrokerInterface
 * @brief Abstract interface for broker connectivity
 * 
 * Provides standardized interface for:
 * - Market data retrieval
 * - Order placement
 * - Position management
 * - Account information
 */
class BrokerInterface {
public:
    virtual ~BrokerInterface() = default;
    
    /**
     * Connect to broker and authenticate
     * @return true if connection successful
     */
    virtual bool connect() = 0;
    
    /**
     * Disconnect from broker
     */
    virtual void disconnect() = 0;
    
    /**
     * Check if connected
     * @return true if connected
     */
    virtual bool is_connected() const = 0;
    
    /**
     * Get current market price (LTP)
     * @param symbol Instrument symbol
     * @return Current price
     */
    virtual double get_ltp(const std::string& symbol) = 0;
    
    /**
     * Get latest OHLC bar
     * @param symbol Instrument symbol
     * @param interval Bar interval (e.g., "1", "5", "15")
     * @return Latest bar
     */
    virtual Core::Bar get_latest_bar(const std::string& symbol, const std::string& interval) = 0;
    
    /**
     * Get historical bars
     * @param symbol Instrument symbol
     * @param interval Bar interval
     * @param count Number of bars
     * @return Vector of historical bars
     */
    virtual std::vector<Core::Bar> get_historical_bars(
        const std::string& symbol,
        const std::string& interval,
        int count
    ) = 0;
    
    /**
     * Place market order
     * @param symbol Instrument symbol
     * @param direction "BUY" or "SELL"
     * @param quantity Order quantity
     * @return Order response
     */
    virtual OrderResponse place_market_order(
        const std::string& symbol,
        const std::string& direction,
        int quantity
    ) = 0;
    
    /**
     * Place limit order
     * @param symbol Instrument symbol
     * @param direction "BUY" or "SELL"
     * @param quantity Order quantity
     * @param price Limit price
     * @return Order response
     */
    virtual OrderResponse place_limit_order(
        const std::string& symbol,
        const std::string& direction,
        int quantity,
        double price
    ) = 0;
    
    /**
     * Place stop loss order
     * @param symbol Instrument symbol
     * @param direction "BUY" or "SELL"
     * @param quantity Order quantity
     * @param stop_price Stop trigger price
     * @return Order response
     */
    virtual OrderResponse place_stop_order(
        const std::string& symbol,
        const std::string& direction,
        int quantity,
        double stop_price
    ) = 0;
    
    /**
     * Cancel order
     * @param order_id Order ID to cancel
     * @return true if cancelled successfully
     */
    virtual bool cancel_order(const std::string& order_id) = 0;
    
    /**
     * Get current positions
     * @return Vector of open positions
     */
    virtual std::vector<Position> get_positions() = 0;
    
    /**
     * Get account balance
     * @return Available balance
     */
    virtual double get_account_balance() = 0;
    
    /**
     * Check if position exists for symbol
     * @param symbol Instrument symbol
     * @return true if position exists
     */
    virtual bool has_position(const std::string& symbol) = 0;
};

} // namespace Live
} // namespace SDTrading

#endif // BROKER_INTERFACE_H
