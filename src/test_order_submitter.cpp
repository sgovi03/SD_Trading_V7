// ============================================================
// ORDER SUBMITTER TEST - Standalone Test Program
// Tests HTTP order submission to Spring Boot microservice
// ============================================================

#include "utils/http/order_submitter.h"
#include "utils/logger.h"
#include <iostream>
#include <iomanip>

using namespace SDTrading::Live;
using namespace SDTrading::Utils;

void print_separator() {
    std::cout << std::string(60, '=') << std::endl;
}

void test_connection(OrderSubmitter& submitter) {
    print_separator();
    std::cout << "TEST 1: Connection Test" << std::endl;
    print_separator();
    
    if (submitter.test_connection()) {
        std::cout << "✅ SUCCESS: Spring Boot service is reachable\n";
    } else {
        std::cout << "❌ FAILURE: Cannot reach Spring Boot service\n";
        std::cout << "   Please ensure service is running at: " 
                  << submitter.get_config().base_url << "\n";
    }
    std::cout << std::endl;
}

void test_long_order(OrderSubmitter& submitter) {
    print_separator();
    std::cout << "TEST 2: LONG Order Submission" << std::endl;
    print_separator();
    
    auto result = submitter.submit_order(
        "LONG",     // direction
        1,          // quantity (1 lot)
        101,        // zone_id
        85.5,       // zone_score
        23450.75,   // entry_price
        23400.00,   // stop_loss
        23550.00    // take_profit
    );
    
    if (result.success) {
        std::cout << "✅ SUCCESS: LONG order submitted\n";
        std::cout << "   HTTP Status: " << result.http_status << "\n";
        std::cout << "   Response: " << result.response_body.substr(0, 200) << "\n";
    } else {
        std::cout << "❌ FAILURE: LONG order submission failed\n";
        std::cout << "   Error: " << result.error_message << "\n";
        std::cout << "   HTTP Status: " << result.http_status << "\n";
    }
    std::cout << std::endl;
}

void test_short_order(OrderSubmitter& submitter) {
    print_separator();
    std::cout << "TEST 3: SHORT Order Submission" << std::endl;
    print_separator();
    
    auto result = submitter.submit_order(
        "SHORT",    // direction
        1,          // quantity (1 lot)
        102,        // zone_id
        78.3,       // zone_score
        23500.25,   // entry_price
        23550.00,   // stop_loss
        23400.00    // take_profit
    );
    
    if (result.success) {
        std::cout << "✅ SUCCESS: SHORT order submitted\n";
        std::cout << "   HTTP Status: " << result.http_status << "\n";
    } else {
        std::cout << "❌ FAILURE: SHORT order submission failed\n";
        std::cout << "   Error: " << result.error_message << "\n";
    }
    std::cout << std::endl;
}

void test_square_off(OrderSubmitter& submitter) {
    print_separator();
    std::cout << "TEST 4: Square Off All Positions" << std::endl;
    print_separator();
    
    auto result = submitter.square_off_all_positions();
    
    if (result.success) {
        std::cout << "✅ SUCCESS: Square off request submitted\n";
        std::cout << "   HTTP Status: " << result.http_status << "\n";
    } else {
        std::cout << "❌ FAILURE: Square off failed\n";
        std::cout << "   Error: " << result.error_message << "\n";
    }
    std::cout << std::endl;
}

void test_disabled_mode() {
    print_separator();
    std::cout << "TEST 5: Disabled Mode (Dry-Run)" << std::endl;
    print_separator();
    
    OrderSubmitConfig config;
    config.enabled = false;  // Disable actual submission
    config.base_url = "http://localhost:8080";
    
    OrderSubmitter submitter(config);
    submitter.initialize();
    
    auto result = submitter.submit_order("LONG", 1);
    
    if (result.success) {
        std::cout << "✅ SUCCESS: Dry-run mode working correctly\n";
        std::cout << "   Message: " << result.error_message << "\n";
    } else {
        std::cout << "❌ UNEXPECTED: Dry-run should always succeed\n";
    }
    std::cout << std::endl;
}

void print_configuration(const OrderSubmitConfig& config) {
    print_separator();
    std::cout << "CONFIGURATION" << std::endl;
    print_separator();
    std::cout << "  Base URL:            " << config.base_url << "\n";
    std::cout << "  LONG Strategy:       " << config.long_strategy_number << "\n";
    std::cout << "  SHORT Strategy:      " << config.short_strategy_number << "\n";
    std::cout << "  Order Type:          " << (config.order_type == 1 ? "MARKET" : "LIMIT") << "\n";
    std::cout << "  Timeout:             " << config.timeout_seconds << " seconds\n";
    std::cout << "  Submission Enabled:  " << (config.enable_submission ? "YES" : "NO") << "\n";
    std::cout << "  Submit URL:          " << config.get_order_submit_url() << "\n";
    std::cout << "  Square Off URL:      " << config.get_square_off_url() << "\n";
    std::cout << std::endl;
}

int main(int argc, char* argv[]) {
    std::cout << std::string(60, '=') << std::endl;
    std::cout << "ORDER SUBMITTER TEST PROGRAM" << std::endl;
    std::cout << "Version 1.0" << std::endl;
    std::cout << std::string(60, '=') << std::endl;
    std::cout << std::endl;
    
    // Parse command line arguments
    bool run_live = false;
    std::string base_url = "http://localhost:8080";
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--live") {
            run_live = true;
        } else if (arg == "--url" && i + 1 < argc) {
            base_url = argv[i + 1];
            i++;
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [OPTIONS]\n";
            std::cout << "\nOptions:\n";
            std::cout << "  --live          Enable live order submission (default: dry-run)\n";
            std::cout << "  --url <url>     Spring Boot base URL (default: http://localhost:8080)\n";
            std::cout << "  --help          Show this help message\n";
            std::cout << "\nExamples:\n";
            std::cout << "  " << argv[0] << "                    # Dry-run mode\n";
            std::cout << "  " << argv[0] << " --live            # Live submission\n";
            std::cout << "  " << argv[0] << " --url http://192.168.1.100:8080\n";
            std::cout << std::endl;
            return 0;
        }
    }
    
    // Setup configuration
    OrderSubmitConfig config;
    config.base_url = base_url;
    config.long_strategy_number = 13;
    config.short_strategy_number = 14;
    config.week_num = 0;
    config.month_num = 0;
    config.order_type = 1;
    config.timeout_seconds = 10;
    config.enable_submission = run_live;
    
    // Print configuration
    print_configuration(config);
    
    if (!run_live) {
        std::cout << "⚠️  RUNNING IN DRY-RUN MODE (no actual orders will be placed)\n";
        std::cout << "   Use --live flag to enable live order submission\n";
        std::cout << std::endl;
    } else {
        std::cout << "⚠️  LIVE MODE ENABLED - ORDERS WILL BE PLACED!\n";
        std::cout << "   Press Ctrl+C within 5 seconds to cancel...\n";
        std::cout << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
    
    // Create order submitter
    OrderSubmitter submitter(config);
    
    // Initialize
    print_separator();
    std::cout << "INITIALIZATION" << std::endl;
    print_separator();
    
    if (!submitter.initialize()) {
        std::cout << "❌ FATAL: Failed to initialize OrderSubmitter\n";
        return 1;
    }
    
    std::cout << "✅ OrderSubmitter initialized successfully\n";
    std::cout << std::endl;
    
    // Run tests
    try {
        test_connection(submitter);
        
        if (run_live) {
            test_long_order(submitter);
            std::this_thread::sleep_for(std::chrono::seconds(2));
            
            test_short_order(submitter);
            std::this_thread::sleep_for(std::chrono::seconds(2));
            
            test_square_off(submitter);
        }
        
        test_disabled_mode();
        
    } catch (const std::exception& e) {
        std::cout << "❌ EXCEPTION: " << e.what() << std::endl;
        return 1;
    }
    
    // Summary
    print_separator();
    std::cout << "TEST SUMMARY" << std::endl;
    print_separator();
    std::cout << "All tests completed successfully!\n";
    
    if (!run_live) {
        std::cout << "\n💡 TIP: Run with --live flag to test actual order submission\n";
        std::cout << "   (Ensure Spring Boot service is running first)\n";
    }
    
    std::cout << std::endl;
    return 0;
}
