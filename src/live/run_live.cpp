#include "live_engine.h"
#include "fyers_adapter.h"
#include "../core/config_loader.h"
#include "../utils/logger.h"
#include "../utils/system_initializer.h"
#include <iostream>
#include <memory>
#include <filesystem>

using namespace SDTrading;
using namespace SDTrading::Core;
using namespace SDTrading::Live;
using namespace SDTrading::Utils;

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    std::cout << "==================================================" << std::endl;
    std::cout << "  SD Trading System V4.0 - Live Trading Runner" << std::endl;
    std::cout << "  Milestone 5: Live Trading Engine" << std::endl;
    std::cout << "==================================================" << std::endl;
    std::cout << std::endl;
    
    try {
        // STEP 1: Initialize system configuration (Singleton)
        auto& sys_init = SystemInitializer::getInstance();
        
        if (!sys_init.initialize()) {
            std::cerr << sys_init.getLastError() << std::endl;
            std::cerr << "Live trading requires system configuration." << std::endl;
            return 1;
        }
        
        // Get cached system config
        const auto& sys_config = sys_init.getConfig();
        
        // Print system config
        sys_init.printConfig();
        
        // Print trading-specific info
        std::cout << "  Trading mode:   " << sys_config.trading_mode << std::endl;
        std::cout << "  Symbol:         " << sys_config.trading_symbol << std::endl;
        std::cout << "  Resolution:     " << sys_config.trading_resolution << " min" << std::endl;
        std::cout << std::endl;
        
        // Get strategy config path from system config
        std::string strategy_config_path = sys_config.get_full_path(sys_config.strategy_config_file);
        
        // Allow override from command line
        if (argc >= 2) {
            strategy_config_path = argv[1];
        }
        
        std::cout << "Loading strategy config: " << strategy_config_path << std::endl;
        
        // Load strategy configuration
        Config config;
        try {
            config = ConfigLoader::load_from_file(strategy_config_path);
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to load config: " << e.what());
            LOG_INFO("Using default configuration");
            config = Config();
        }
        
        // Override with system config values
        config.starting_capital = sys_config.trading_starting_capital;
        config.lot_size = sys_config.trading_lot_size;
        config.risk_per_trade_pct = sys_config.strategy_risk_per_trade_pct;
        config.lookback_for_zones = sys_config.strategy_lookback_bars;
        
        std::cout << std::endl;
        std::cout << "Trading Parameters:" << std::endl;
        std::cout << "  Starting capital: $" << config.starting_capital << std::endl;
        std::cout << "  Risk per trade:   " << config.risk_per_trade_pct << "%" << std::endl;
        std::cout << "  Lot size:         " << config.lot_size << std::endl;
        std::cout << std::endl;
        
        // Get data file path from system config (for historical data loading)
        std::string live_data_path = sys_config.get_full_path(sys_config.live_data_csv);
        
        // Create broker adapter with correct constructor arguments
        std::unique_ptr<BrokerInterface> broker;
        
        if (sys_config.trading_mode == "live") {
            std::cout << "[WARNING] LIVE TRADING MODE" << std::endl;
            std::cout << "[WARNING] Real money will be used!" << std::endl;
            std::cout << std::endl;
            
            // Create real Fyers adapter
            std::string access_token = "";  // TODO: Implement OAuth flow
            
            broker = std::make_unique<FyersAdapter>(
                sys_config.fyers_secret_key,    // API key/secret
                access_token,                    // Access token
                sys_config.fyers_client_id,     // Client ID
                live_data_path                   // Data file path from config
            );
        } else {
            std::cout << "[SIMULATION] PAPER TRADING MODE (Simulation)" << std::endl;
            std::cout << "   No real trades will be executed" << std::endl;
            std::cout << std::endl;
            
            // Create simulated broker
            broker = std::make_unique<FyersAdapter>(
                "SIMULATION_KEY",
                "SIMULATION_TOKEN",
                "SIMULATION_CLIENT",
                live_data_path  // Data file path from config
            );
        }
        
        // Create live engine
        std::string live_results_dir = sys_config.get_full_path(sys_config.live_trade_logs_dir);
        
        LiveEngine engine(
            config,
            std::move(broker),
            sys_config.trading_symbol,
            sys_config.trading_resolution,
            live_results_dir,  // Pass configured output directory
            sys_config.get_full_path(sys_config.live_data_csv)  // Pass CSV path for historical data
        );
        
        // Initialize engine
        if (!engine.initialize()) {
            LOG_ERROR("Failed to initialize live engine");
            return 1;
        }
        
        std::cout << std::endl;
        std::cout << "==================================================" << std::endl;
        std::cout << "  Live Engine Ready" << std::endl;
        std::cout << "==================================================" << std::endl;
        std::cout << std::endl;
        std::cout << "Press Enter to start trading (Ctrl+C to stop)..." << std::endl;
        std::cin.get();
        
        // Run live trading
        int duration_minutes = 0;
        if (argc >= 3) {
            duration_minutes = std::stoi(argv[2]);
        }
        
        engine.run(duration_minutes);
        
        // Cleanup
        engine.stop();
        
        std::cout << std::endl;
        std::cout << "==================================================" << std::endl;
        std::cout << "  Live Trading Stopped" << std::endl;
        std::cout << "==================================================" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
