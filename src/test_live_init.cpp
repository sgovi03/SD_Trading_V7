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
    Logger::getInstance().enableFile(true, "live_engine_test.log");
    Logger::getInstance().setLevel(LogLevel::INFO);
    
    cout << "==================================================" << endl;
    cout << "  Live Engine Zone Initialization Test" << endl;
    cout << "==================================================" << endl;
    
    // Load configuration
    string config_file = (argc > 1) ? argv[1] : "conf/phase1_enhanced_v3_1_config.txt";
    
    Config config;
    try {
        ConfigLoader loader;
        config = loader.load_from_file(config_file);
        LOG_INFO("Configuration loaded: " << config_file);
    } catch (const exception& e) {
        LOG_ERROR("Failed to load config: " + string(e.what()));
        config = Config();
    }
    
    // Create broker adapter (simulation mode)
    string csv_file = "data/live_data.csv";
    auto broker = make_unique<FyersAdapter>("KEY", "TOKEN", "CLIENT_ID", csv_file);
    
    // Create live engine
    string symbol = "NSE:NIFTY24JANFUT";
    string interval = "1";
    string output_dir = "results/live_trades";
    string historical_csv = "data/live_data.csv";  // CSV path for historical data
    LiveEngine engine(config, std::move(broker), symbol, interval, output_dir, historical_csv);
    
    // Initialize engine (this will detect zones)
    cout << "\n📊 Initializing live trading engine..." << endl;
    cout << "   Symbol: " << symbol << endl;
    cout << "   Interval: " << interval << " min" << endl;
    
    if (!engine.initialize()) {
        LOG_ERROR("Failed to initialize live engine");
        cout << "\n❌ Initialization failed" << endl;
        return 1;
    }
    
    cout << "\n✅ Engine initialized successfully!" << endl;
    cout << "   Zones detected and saved to results/live_trades/zones_live.json" << endl;
    
    LOG_INFO("TEST COMPLETE - Check zones_live.json");
    
    return 0;
}
