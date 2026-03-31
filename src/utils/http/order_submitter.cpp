// ============================================================
// ORDER SUBMITTER - IMPLEMENTATION
// ============================================================

#include "order_submitter.h"
#include "../logger.h"
#include "active_order_registry.h"
#include <json/json.h>
#include <iostream>
#include <iomanip>

namespace SDTrading {
namespace Live {

using namespace Utils;

OrderSubmitter::OrderSubmitter(const OrderSubmitConfig& config)
    : config_(config), initialized_(false) {
    
    LOG_INFO("OrderSubmitter created");
    LOG_INFO("  Base URL: " << config_.base_url);
    LOG_INFO("  Order Submit URL: " << config_.get_order_submit_url());
    LOG_INFO("  Square Off URL: " << config_.get_square_off_url());
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

std::string OrderSubmitter::build_order_json(
    int quantity,
    int strategy_number,
    const std::string& order_tag
) {
    Json::Value body;
    body["quantity"] = quantity;
    body["strategyNumber"] = strategy_number;
    body["weekNum"] = config_.week_num;
    body["monthNum"] = config_.month_num;
    body["type"] = config_.order_type;
    if (!order_tag.empty()) {
        body["orderTag"] = order_tag;
    }

    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    return Json::writeString(writer, body);
}

OrderSubmitResult OrderSubmitter::submit_order(
    const std::string& direction,
    int quantity,
    int zone_id,
    double zone_score,
    double entry_price,
    double stop_loss,
    double take_profit,
    const std::string& order_tag
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
    
    // Build JSON request body (includes orderTag if provided)
    auto json_body = build_order_json(quantity, strategy_number, order_tag);
    
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
    std::cout << "  Payload:         " << json_body << "\n";
    std::cout << "+====================================================+\n";
    std::cout.flush();

    LOG_INFO("Submitting " << direction << " order: " << quantity << " lots (strategy: "
             << strategy_number << ") Payload: " << json_body);

    // Submit HTTP request
    try {
        auto http_response = http_client_->post_json(config_.get_order_submit_url(), json_body);
        
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

OrderSubmitResult OrderSubmitter::submit_exit_order(const std::string& order_tag) {
    OrderSubmitResult result;

    if (!config_.enable_submission) {
        result.success = true;
        result.error_message = "Order submission disabled in config";
        LOG_INFO("📝 [DRY-RUN] Exit order would be submitted for tag: " << order_tag);
        get_order_registry().remove(order_tag);
        return result;
    }

    if (!initialized_) {
        result.error_message = "OrderSubmitter not initialized";
        LOG_ERROR(result.error_message);
        return result;
    }

    if (order_tag.empty()) {
        result.error_message = "submit_exit_order called with empty tag";
        LOG_ERROR("❌ " << result.error_message);
        return result;
    }

    // Log a warning if the tag is not in the registry (e.g. entry HTTP failed
    // and was rolled back), but still attempt the exit — the engine recorded
    // the trade and the broker may have filled it.
    if (!get_order_registry().has(order_tag)) {
        LOG_WARN("⚠️  submit_exit_order: tag not in registry (entry may have failed): " << order_tag
                 << " — attempting exit anyway");
    }

    Json::Value body;
    body["orderTag"] = order_tag;
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    std::string json_body = Json::writeString(writer, body);

    std::string url = config_.get_square_off_position_url();

    std::cout << "\n+====================================================+\n";
    std::cout << "| [HTTP EXIT] Submitting exit order                 |\n";
    std::cout << "+====================================================+\n";
    std::cout << "  OrderTag: " << order_tag << "\n";
    std::cout << "  URL:      " << url << "\n";
    std::cout << "  Payload:  " << json_body << "\n";
    std::cout << "+====================================================+\n";
    std::cout.flush();

    LOG_INFO("Submitting exit for OrderTag: " << order_tag << " Payload: " << json_body);

    try {
        auto http_response = http_client_->post_json(url, json_body);

        result.success       = http_response.success;
        result.http_status   = http_response.status_code;
        result.response_body = http_response.body;
        result.error_message = http_response.error_message;

        if (result.success) {
            std::cout << "\n✅ [SUCCESS] Exit order submitted!\n";
            std::cout << "   HTTP Status: " << result.http_status << "\n";
            std::cout << std::endl;
            std::cout.flush();
            LOG_INFO("✅ Exit order submitted - Tag: " << order_tag
                     << " Status: " << result.http_status);
            get_order_registry().remove(order_tag);
        } else {
            std::cout << "\n❌ [ERROR] Exit order failed! Position may still be open.\n";
            std::cout << "   HTTP Status: " << result.http_status << "\n";
            std::cout << "   Error: " << result.error_message << "\n";
            std::cout << std::endl;
            std::cout.flush();
            LOG_ERROR("❌ Exit order failed - Tag: " << order_tag
                      << " Status: " << result.http_status
                      << " Error: " << result.error_message);
            // Do NOT remove from registry — position still open
        }

    } catch (const std::exception& e) {
        result.success = false;
        result.error_message = std::string("Exception: ") + e.what();
        LOG_ERROR("Exception during exit order submission: " << e.what());
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
        // Try to access the configured base URL directly.
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
