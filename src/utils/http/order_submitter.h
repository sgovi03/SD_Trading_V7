// ============================================================
// ORDER SUBMITTER - Java Spring Boot Integration
// Submits trades to multipleOrderSubmit endpoint
// ============================================================

#ifndef ORDER_SUBMITTER_H
#define ORDER_SUBMITTER_H

#include "http_client.h"
#include "common_types.h"
#include <string>
#include <memory>

namespace SDTrading {
namespace Live {

/**
 * @struct OrderSubmitConfig
 * @brief Configuration for Spring Boot order submission
 */
struct OrderSubmitConfig {
    std::string base_url = "http://localhost:8080";  // Spring Boot base URL
    int long_strategy_number = 13;                    // Strategy number for LONG trades
    int short_strategy_number = 14;                   // Strategy number for SHORT trades
    int week_num = 0;                                 // Week number (default: 0)
    int month_num = 0;                                // Month number (default: 0)
    int order_type = 1;                               // 1=MARKET, 2=LIMIT
    int timeout_seconds = 10;                         // HTTP timeout
    bool enable_submission = true;                    // Master switch to enable/disable
    
    // Derived endpoints
    std::string get_order_submit_url() const {
        return base_url + "/multipleOrderSubmit";
    }
    
    std::string get_square_off_url() const {
        return base_url + "/trade/squareOffAllPositions";
    }
};

/**
 * @struct OrderSubmitResult
 * @brief Result of order submission attempt
 */
struct OrderSubmitResult {
    bool success;                   // True if order submitted successfully
    std::string error_message;      // Error description if failed
    int http_status;                // HTTP status code
    std::string response_body;      // Raw response from server
    
    OrderSubmitResult() : success(false), http_status(0) {}
};

/**
 * @class OrderSubmitter
 * @brief Submits trade orders to Java Spring Boot microservice
 * 
 * Architecture:
 * - C++ SD Engine generates trade signals (LONG/SHORT)
 * - OrderSubmitter converts to HTTP POST requests
 * - Spring Boot microservice receives orders
 * - Fyers broker executes trades
 * 
 * Endpoints:
 * - POST /multipleOrderSubmit - Submit new order
 * - POST /squareOffAllPositions - Exit all positions
 * 
 * Form Parameters:
 * - quantity: Number of lots (e.g., "1")
 * - strategyNumber: 13 for LONG, 14 for SHORT
 * - weekNum: Week identifier (default: "0")
 * - monthNum: Month identifier (default: "0")
 * - type: Order type (1=MARKET, 2=LIMIT)
 * 
 * Example Usage:
 *   OrderSubmitter submitter(config);
 *   
 *   // Submit LONG order
 *   OrderSubmitResult result = submitter.submit_order(
 *       "LONG", 1, zone_id, zone_score, entry_price, stop_loss, take_profit
 *   );
 *   
 *   // Exit position
 *   submitter.square_off_all_positions();
 */
class OrderSubmitter {
private:
    OrderSubmitConfig config_;
    std::unique_ptr<Utils::Http::HttpClient> http_client_;
    bool initialized_;
    
    /**
     * Build form parameters for order submission
     */
    std::map<std::string, std::string> build_order_params(
        const std::string& direction,
        int quantity,
        int strategy_number
    );
    
    /**
     * Validate order parameters before submission
     */
    bool validate_order_params(
        const std::string& direction,
        int quantity
    );
    
public:
    /**
     * Constructor
     * @param config Order submission configuration
     */
    explicit OrderSubmitter(const OrderSubmitConfig& config);
    
    /**
     * Destructor
     */
    ~OrderSubmitter();
    
    /**
     * Initialize HTTP client
     * @return true if initialized successfully
     */
    bool initialize();
    
    /**
     * Submit order to Spring Boot microservice
     * @param direction Trade direction ("LONG" or "SHORT")
     * @param quantity Number of lots
     * @param zone_id Zone identifier for logging
     * @param zone_score Zone quality score
     * @param entry_price Entry price
     * @param stop_loss Stop loss price
     * @param take_profit Take profit price
     * @return OrderSubmitResult with success status
     */
    OrderSubmitResult submit_order(
        const std::string& direction,
        int quantity,
        int zone_id = 0,
        double zone_score = 0.0,
        double entry_price = 0.0,
        double stop_loss = 0.0,
        double take_profit = 0.0
    );
    
    /**
     * Square off all open positions
     * @return OrderSubmitResult with success status
     */
    OrderSubmitResult square_off_all_positions();
    
    /**
     * Test connection to Spring Boot service
     * @return true if service is reachable
     */
    bool test_connection();
    
    /**
     * Enable/disable order submission
     * @param enable True to enable, false to disable
     */
    void set_enabled(bool enable) { config_.enable_submission = enable; }
    
    /**
     * Check if order submission is enabled
     * @return true if enabled
     */
    bool is_enabled() const { return config_.enable_submission; }
    
    /**
     * Get current configuration
     */
    const OrderSubmitConfig& get_config() const { return config_; }
};

} // namespace Live
} // namespace SDTrading

#endif // ORDER_SUBMITTER_H
