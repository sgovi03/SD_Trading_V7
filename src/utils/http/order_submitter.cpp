// ============================================================
// ORDER SUBMITTER - IMPLEMENTATION
// ============================================================

#include "order_submitter.h"
#include "../logger.h"
#include <iostream>
#include <iomanip>

namespace SDTrading {
namespace Live {

using namespace Utils;

OrderSubmitter::OrderSubmitter(const OrderSubmitConfig& config)
    : config_(config), initialized_(false) {
    
    LOG_INFO("OrderSubmitter created");
    LOG_INFO("  Base URL: " << config_.base_url);
    LOG_INFO("  LONG Strategy: " << config_.long_strategy_number);
    LOG_INFO("  SHORT Strategy: " << config_.short_strategy_number);
    LOG_INFO("  Enabled: " << (config_.enable_submission ? "YES" : "NO"));
}

OrderSubmitter::~OrderSubmitter() {
    LOG_INFO("OrderSubmitter destroyed");
}

bool OrderSubmitter::initialize() {
    if (initialized_) {
        LOG_WARN("OrderSubmitter already initialized");
        return true;
    }
    
    try {
        // Create HTTP client
        http_client_ = std::make_unique<Utils::Http::HttpClient>(
            config_.timeout_seconds,
            false  // verbose=false by default
        );
        
        initialized_ = true;
        LOG_INFO("OrderSubmitter initialized successfully");
        
        // Test connection if enabled
        if (config_.enable_submission) {
            if (!test_connection()) {
                LOG_WARN("⚠️  Spring Boot service not reachable at: " << config_.base_url);
                LOG_WARN("    Order submission will fail until service is available");
            }
        }
        
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to initialize OrderSubmitter: " << e.what());
        return false;
    }
}

bool OrderSubmitter::validate_order_params(
    const std::string& direction,
    int quantity
) {
    if (direction != "LONG" && direction != "SHORT") {
        LOG_ERROR("Invalid direction: " << direction << " (must be LONG or SHORT)");
        return false;
    }
    
    if (quantity <= 0) {
        LOG_ERROR("Invalid quantity: " << quantity << " (must be > 0)");
        return false;
    }
    
    return true;
}

std::map<std::string, std::string> OrderSubmitter::build_order_params(
    const std::string& direction,
    int quantity,
    int strategy_number
) {
    std::map<std::string, std::string> params;
    
    params["quantity"] = std::to_string(quantity);
    params["strategyNumber"] = std::to_string(strategy_number);
    params["weekNum"] = std::to_string(config_.week_num);
    params["monthNum"] = std::to_string(config_.month_num);
    params["type"] = std::to_string(config_.order_type);
    
    return params;
}

OrderSubmitResult OrderSubmitter::submit_order(
    const std::string& direction,
    int quantity,
    int zone_id,
    double zone_score,
    double entry_price,
    double stop_loss,
    double take_profit
) {
    OrderSubmitResult result;
    
    // Check if enabled
    if (!config_.enable_submission) {
        result.success = true;  // Consider it success since submission is disabled
        result.error_message = "Order submission disabled in config";
        LOG_INFO("📝 [DRY-RUN] Order would be submitted: " << direction << " " << quantity << " lots");
        return result;
    }
    
    // Check initialization
    if (!initialized_) {
        result.error_message = "OrderSubmitter not initialized";
        LOG_ERROR(result.error_message);
        return result;
    }
    
    // Validate parameters
    if (!validate_order_params(direction, quantity)) {
        result.error_message = "Invalid order parameters";
        return result;
    }
    
    // Determine strategy number based on direction
    int strategy_number = (direction == "LONG") ? 
        config_.long_strategy_number : config_.short_strategy_number;
    
    // Build form parameters
    auto params = build_order_params(direction, quantity, strategy_number);
    
    // Log order details
    std::cout << "\n+====================================================+\n";
    std::cout << "| [HTTP ORDER] Submitting to Spring Boot            |\n";
    std::cout << "+====================================================+\n";
    std::cout << "  Direction:       " << direction << "\n";
    std::cout << "  Quantity:        " << quantity << "\n";
    std::cout << "  Strategy Number: " << strategy_number << "\n";
    std::cout << "  Zone ID:         " << zone_id << "\n";
    std::cout << "  Zone Score:      " << std::fixed << std::setprecision(2) << zone_score << "\n";
    std::cout << "  Entry Price:     " << entry_price << "\n";
    std::cout << "  Stop Loss:       " << stop_loss << "\n";
    std::cout << "  Take Profit:     " << take_profit << "\n";
    std::cout << "  URL:             " << config_.get_order_submit_url() << "\n";
    std::cout << "+====================================================+\n";
    std::cout.flush();
    
    LOG_INFO("Submitting " << direction << " order: " << quantity << " lots (strategy: " 
             << strategy_number << ")");
    
    // Submit HTTP request
    try {
        auto http_response = http_client_->post(config_.get_order_submit_url(), params);
        
        result.success = http_response.success;
        result.http_status = http_response.status_code;
        result.response_body = http_response.body;
        result.error_message = http_response.error_message;
        
        if (result.success) {
            std::cout << "\n✅ [SUCCESS] Order submitted successfully!\n";
            std::cout << "   HTTP Status: " << result.http_status << "\n";
            std::cout << "   Response: " << result.response_body.substr(0, 200) 
                      << (result.response_body.length() > 200 ? "..." : "") << "\n";
            std::cout << std::endl;
            std::cout.flush();
            
            LOG_INFO("✅ Order submitted successfully - Status: " << result.http_status);
        } else {
            std::cout << "\n❌ [ERROR] Order submission failed!\n";
            std::cout << "   HTTP Status: " << result.http_status << "\n";
            std::cout << "   Error: " << result.error_message << "\n";
            std::cout << "   Response: " << result.response_body << "\n";
            std::cout << std::endl;
            std::cout.flush();
            
            LOG_ERROR("❌ Order submission failed - Status: " << result.http_status 
                     << " Error: " << result.error_message);
        }
        
    } catch (const std::exception& e) {
        result.success = false;
        result.error_message = std::string("Exception: ") + e.what();
        LOG_ERROR("Exception during order submission: " << e.what());
    }
    
    return result;
}

OrderSubmitResult OrderSubmitter::square_off_all_positions() {
    OrderSubmitResult result;
    
    // Check if enabled
    if (!config_.enable_submission) {
        result.success = true;
        result.error_message = "Order submission disabled in config";
        LOG_INFO("📝 [DRY-RUN] Square off would be executed");
        return result;
    }
    
    // Check initialization
    if (!initialized_) {
        result.error_message = "OrderSubmitter not initialized";
        LOG_ERROR(result.error_message);
        return result;
    }
    
    std::cout << "\n+====================================================+\n";
    std::cout << "| [HTTP SQUARE OFF] Closing all positions           |\n";
    std::cout << "+====================================================+\n";
    std::cout << "  URL: " << config_.get_square_off_url() << "\n";
    std::cout << "+====================================================+\n";
    std::cout.flush();
    
    LOG_INFO("Squaring off all positions");
    
    try {
        // Try GET method for square off endpoint
        auto http_response = http_client_->get(config_.get_square_off_url());
        
        result.success = http_response.success;
        result.http_status = http_response.status_code;
        result.response_body = http_response.body;
        result.error_message = http_response.error_message;
        
        if (result.success) {
            std::cout << "\n✅ [SUCCESS] All positions squared off!\n";
            std::cout << "   HTTP Status: " << result.http_status << "\n";
            std::cout << std::endl;
            std::cout.flush();
            
            LOG_INFO("✅ Square off successful - Status: " << result.http_status);
        } else {
            std::cout << "\n❌ [ERROR] Square off failed!\n";
            std::cout << "   HTTP Status: " << result.http_status << "\n";
            std::cout << "   Error: " << result.error_message << "\n";
            std::cout << std::endl;
            std::cout.flush();
            
            LOG_ERROR("❌ Square off failed - Status: " << result.http_status 
                     << " Error: " << result.error_message);
        }
        
    } catch (const std::exception& e) {
        result.success = false;
        result.error_message = std::string("Exception: ") + e.what();
        LOG_ERROR("Exception during square off: " << e.what());
    }
    
    return result;
}

bool OrderSubmitter::test_connection() {
    if (!initialized_) {
        LOG_WARN("Cannot test connection - OrderSubmitter not initialized");
        return false;
    }
    
    LOG_INFO("Testing connection to Spring Boot service...");
    
    try {
        // Try to access base URL (might return 404 but proves server is up)
        auto response = http_client_->get(config_.base_url);
        
        // Any response (even 404) means server is reachable
        bool reachable = (response.status_code > 0);
        
        if (reachable) {
            LOG_INFO("✅ Spring Boot service is reachable (Status: " 
                    << response.status_code << ")");
        } else {
            LOG_WARN("⚠️  Spring Boot service not reachable");
        }
        
        return reachable;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Connection test failed: " << e.what());
        return false;
    }
}

} // namespace Live
} // namespace SDTrading
