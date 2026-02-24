#include "backtest_engine.h"
#include "csv_reporter.h"
#include "../core/config_loader.h"
#include "../utils/logger.h"
#include "../utils/system_initializer.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>

using namespace SDTrading;
using namespace SDTrading::Core;
using namespace SDTrading::Backtest;
using namespace SDTrading::Utils;

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    std::cout << "==================================================" << std::endl;
    std::cout << "  SD Trading System V4.0 - Backtest Runner" << std::endl;
    std::cout << "==================================================" << std::endl;
    std::cout << std::endl;
    
    try {
        // STEP 1: Initialize system configuration
        auto& sys_init = SystemInitializer::getInstance();
        
        if (!sys_init.initialize()) {
            std::cerr << "ERROR: System initialization failed" << std::endl;
            std::cerr << sys_init.getLastError() << std::endl;
            return 1;
        }
        
        const auto& sys_config = sys_init.getConfig();
        
        // Print system configuration
        sys_init.printConfig();
        std::cout << std::endl;
        
        // STEP 2: Get strategy config path
        std::string strategy_config_path;
        if (argc > 1) {
            strategy_config_path = argv[1];
        } else {
            strategy_config_path = sys_config.get_full_path(sys_config.strategy_config_file);
        }
        
        if (!fs::exists(strategy_config_path)) {
            std::cerr << "ERROR: Strategy config file not found: " << strategy_config_path << std::endl;
            return 1;
        }
        
        // STEP 3: Get data file path
        std::string data_file_path;
        if (argc > 2) {
            data_file_path = argv[2];
        } else {
            data_file_path = sys_config.get_full_path(sys_config.live_data_csv);
        }
        
        if (!fs::exists(data_file_path)) {
            std::cerr << "ERROR: Data file not found: " << data_file_path << std::endl;
            return 1;
        }
        
        // STEP 4: Setup output directory
        std::string output_dir = sys_config.get_full_path(sys_config.backtest_results_dir);
        fs::create_directories(output_dir);
        
        // Print configuration summary
        std::cout << "Configuration Summary:" << std::endl;
        std::cout << "  Strategy Config: " << strategy_config_path << std::endl;
        std::cout << "  Data File:       " << data_file_path << std::endl;
        std::cout << "  Output Dir:      " << output_dir << std::endl;
        std::cout << std::endl;
        
        // STEP 5: Load strategy configuration
        LOG_INFO("Loading strategy configuration...");
        Config config = ConfigLoader::load_from_file(strategy_config_path);
        
        // Override with system config values
        config.starting_capital = sys_config.trading_starting_capital;
        config.lot_size = sys_config.trading_lot_size;
        config.risk_per_trade_pct = sys_config.strategy_risk_per_trade_pct;
        config.lookback_for_zones = sys_config.strategy_lookback_bars;
        
        // STEP 6: Create backtest engine with NEW API
        LOG_INFO("Creating backtest engine...");
        BacktestEngine engine(config, data_file_path, output_dir);
        
        // STEP 7: Initialize engine (loads data internally)
        LOG_INFO("Initializing engine...");
        if (!engine.initialize()) {
            std::cerr << "ERROR: Engine initialization failed" << std::endl;
            return 1;
        }
        
        LOG_INFO("Engine initialized successfully");
        std::cout << std::endl;
        
        // STEP 8: Run backtest
        LOG_INFO("Starting backtest...");
        std::cout << std::endl;
        
        engine.run();  // Now returns void
        
        // STEP 9: Stop engine and export results
        std::cout << std::endl;
        LOG_INFO("Stopping engine and exporting results...");
        
        engine.stop();  // Exports all results
        
        // STEP 10: Display completion message
        std::cout << std::endl;
        std::cout << "==================================================" << std::endl;
        std::cout << "  Backtest Complete!" << std::endl;
        std::cout << "  Results: " << output_dir << std::endl;
        std::cout << "==================================================" << std::endl;
        std::cout << std::endl;
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << std::endl;
        std::cerr << "==================================================" << std::endl;
        std::cerr << "  FATAL ERROR" << std::endl;
        std::cerr << "==================================================" << std::endl;
        std::cerr << e.what() << std::endl;
        std::cerr << std::endl;
        return 1;
    }
}
