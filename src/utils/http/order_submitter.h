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
    std::string base_url = "http://localhost:8080/trade";  // Spring Boot base URL including application path (/trade, /xyz, etc.)
    int long_strategy_number = 13;                    // Strategy number for LONG trades
    int short_strategy_number = 14;                   // Strategy number for SHORT trades
    int week_num = 0;                                 // Week number (default: 0)
    int month_num = 0;                                // Month number (default: 0)
    int order_type = 1;                               // 1=MARKET, 2=LIMIT
    int timeout_seconds = 10;                         // HTTP timeout
    bool enable_submission = true;                    // Master switch to enable/disable
    
    // Normalize base URL by removing any trailing slash.
    std::string normalized_base_url() const {
        if (!base_url.empty() && base_url.back() == '/') {
            return base_url.substr(0, base_url.size() - 1);
        }
        return base_url;
    }

    // Derived endpoints
    std::string get_order_submit_url() const {
        return normalized_base_url() + "/multipleOrderSubmit";
    }

    std::string get_square_off_url() const {
        return normalized_base_url() + "/squareOffPositionsByOrderTag";
    }

    std::string get_square_off_position_url() const {
        return normalized_base_url() + "/squareOffPositionsByOrderTag";
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
 * - OrderSubmitter converts to HTTP POST requests with JSON body
 * - Spring Boot microservice receives orders
 * - Fyers broker executes trades
 *
 * Endpoints:
 * - POST /multipleOrderSubmit - Submit new order
 * - POST /squareOffAllPositions - Exit all positions
 *
 * JSON Body Fields:
 * - quantity: Number of lots (e.g., 1)
 * - strategyNumber: 13 for LONG, 14 for SHORT
 * - weekNum: Week identifier (default: 0)
 * - monthNum: Month identifier (default: 0)
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
     * Build JSON payload for order submission
     */
    std::string build_order_json(
        int quantity,
        int strategy_number,
        const std::string& order_tag = ""
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
        double take_profit = 0.0,
        const std::string& order_tag = ""
    );
    
    /**
     * Submit a tag-specific exit order to /squareOffPosition.
     * Looks up the tag in ActiveOrderRegistry. On success the tag is
     * removed from the registry. On failure the tag is retained (position
     * still open). Logs an error and returns failure if the tag is unknown.
     * @param order_tag 16-char OrderTag from the entry
     * @return OrderSubmitResult with success status
     */
    OrderSubmitResult submit_exit_order(const std::string& order_tag);

    /**
     * Square off ALL open positions (emergency use — not tag-specific).
     * Keep this intact; call it manually when needed.
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
