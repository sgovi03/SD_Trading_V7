#include <iostream>
#include <memory>
#include "common_types.h"
#include "core/config_loader.h"
#include "live/live_engine.h"
#include "live/fyers_adapter.h"
#include "utils/logger.h"

using namespace SDTrading;
using namespace SDTrading::Core;
using namespace SDTrading::Live;
using namespace SDTrading::Utils;
using namespace std;

int main(int argc, char* argv[]) {
    // Initialize logger
    Logger::getInstance().enableConsole(true);
    Logger::getInstance().enableFile(true, "live_trading.log");
    Logger::getInstance().setLevel(LogLevel::INFO);
    
    cout << "==================================================" << endl;
    cout << "  SD Trading System V4.0 - Live Trading Runner" << endl;
    cout << "  Milestone 5: Real-Time Trading Engine" << endl;
    cout << "==================================================" << endl;
    
    // File paths
    string config_file = (argc > 1) ? argv[1] : "../examples/sample_config.txt";
    
    cout << "\nConfiguration: " << config_file << endl;
    
    // Load configuration
    Config config;
    try {
        ConfigLoader loader;
        config = loader.load_from_file(config_file);
        LOG_INFO("Configuration loaded");
    } catch (const exception& e) {
        LOG_ERROR("Failed to load config: " + string(e.what()));
        LOG_INFO("Using default configuration");
        config = Config();
    }
    
    // Fyers API credentials (in production, load from secure config)
    // SKELETON: Replace with actual credentials
    string fyers_api_key = "YOUR_API_KEY_HERE";
    string fyers_access_token = "YOUR_ACCESS_TOKEN_HERE";
    string fyers_client_id = "YOUR_CLIENT_ID_HERE";
    
    cout << "\n⚠️  SIMULATION MODE ACTIVE" << endl;
    cout << "No real orders will be placed!" << endl;
    cout << "This is a skeleton implementation for demonstration." << endl;
    
    // Trading parameters
    string symbol = "NSE:NIFTY24JANFUT";  // Example: NIFTY Future
    string interval = "1";  // 1-minute bars
    int duration_minutes = 60;  // Run for 1 hour
    
    cout << "\nTrading Parameters:" << endl;
    cout << "  Symbol:      " << symbol << endl;
    cout << "  Interval:    " << interval << " min" << endl;
    cout << "  Duration:    " << duration_minutes << " min" << endl;
    
    // Create broker adapter
    auto broker = make_unique<FyersAdapter>(
        fyers_api_key,
        fyers_access_token,
        fyers_client_id
    );
    
    // Create live engine
    LiveEngine engine(config, std::move(broker), symbol, interval);
    
    // Initialize engine
    cout << "\nInitializing live trading engine..." << endl;
    
    if (!engine.initialize()) {
        LOG_ERROR("Failed to initialize live engine");
        cout << "\n❌ Initialization failed - check logs" << endl;
        return 1;
    }
    
    cout << "\n✅ Engine initialized successfully!" << endl;
    
    // Display warning
    cout << "\n==================================================" << endl;
    cout << "  READY TO START LIVE TRADING" << endl;
    cout << "==================================================" << endl;
    cout << "\n⚠️  WARNING: This will monitor live markets and" << endl;
    cout << "   generate trade signals in SIMULATION mode." << endl;
    cout << "\nPress Enter to start, or Ctrl+C to cancel..." << endl;
    cin.get();
    
    // Start live trading
    try {
        engine.run(duration_minutes);
    } catch (const exception& e) {
        LOG_ERROR("Fatal error in live engine: " + string(e.what()));
        cout << "\n❌ Live engine crashed: " << e.what() << endl;
        return 1;
    }
    
    cout << "\n==================================================" << endl;
    cout << "  Live Trading Session Complete" << endl;
    cout << "==================================================" << endl;
    cout << "\nCheck live_trading.log for detailed logs" << endl;
    
    return 0;
}
