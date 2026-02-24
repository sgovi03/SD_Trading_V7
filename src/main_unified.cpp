// ============================================================
// SD TRADING SYSTEM V4.0 - UNIFIED RUNNER
// Single entry point for both Backtest and Live trading
// ============================================================

#include "EngineFactory.h"
#include "utils/system_initializer.h"
#include "utils/logger.h"
#include <iostream>
#include <filesystem>
#include <string>
#include <cstdlib>

using namespace SDTrading;
using namespace SDTrading::Core;
using namespace SDTrading::Utils;

namespace fs = std::filesystem;

void print_usage() {
    std::cout << "\n";
    std::cout << "Usage: sd_trading_unified --mode=<backtest|dryrun|live> [options]\n\n";
    std::cout << "Required:\n";
    std::cout << "  --mode=<backtest|dryrun|live>    Trading mode to run\n\n";
    std::cout << "Optional:\n";
    std::cout << "  --config=<path>           Strategy config file path\n";
    std::cout << "                            (default: from system_config.json)\n";
    std::cout << "  --data=<path>             CSV data file path\n";
    std::cout << "                            (required for backtest/dryrun, optional for live)\n";
    std::cout << "  --duration=<minutes>      Run duration in minutes\n";
    std::cout << "                            (dryrun/live mode only, 0=indefinite, default=0)\n";
    std::cout << "  --bootstrap-bars=<N>      Bootstrap N bars before testing (dryrun only)\n";
    std::cout << "                            (-1=auto[10000], 0=no bootstrap, default=-1)\n";
    std::cout << "  --test-start-bar=<N>      Start testing from bar N (dryrun only)\n";
    std::cout << "                            (0=auto[after bootstrap], default=0)\n";
    std::cout << "  --help, -h                Show this help message\n\n";
    std::cout << "Examples:\n";
    std::cout << "  # Run backtest with specific data file\n";
    std::cout << "  ./sd_trading_unified --mode=backtest --data=nifty_jan2025.csv\n\n";
    std::cout << "  # Run dryrun (live logic with CSV) - auto bootstrap\n";
    std::cout << "  ./sd_trading_unified --mode=dryrun --data=nifty_jan2025.csv\n\n";
    std::cout << "  # Run dryrun with custom bootstrap (5000 bars)\n";
    std::cout << "  ./sd_trading_unified --mode=dryrun --data=data.csv --bootstrap-bars=5000\n\n";
    std::cout << "  # Run live trading for 60 minutes\n";
    std::cout << "  ./sd_trading_unified --mode=live --duration=60\n\n";
}

int main(int argc, char* argv[]) {
    std::cout << "==================================================" << std::endl;
    std::cout << "  SD Trading System V4.0 - Unified Runner" << std::endl;
    std::cout << "  Factory Pattern: Single Entry Point" << std::endl;
    std::cout << "==================================================" << std::endl;
    std::cout << std::endl;
    
    try {
        // ========================================
        // STEP 1: Parse Command Line Arguments
        // ========================================
        std::string mode;
        std::string config_path;
        std::string data_path;
        int duration = 0;
        int bootstrap_bars = -1;  // -1 = auto, 0 = no bootstrap
        int test_start_bar = 0;   // 0 = auto (after bootstrap)
        bool allow_large_bootstrap = false; // optional override
        
        for (int i = 1; i < argc; i++) {
            std::string arg = argv[i];
            
            if (arg.rfind("--mode=", 0) == 0) {
                mode = arg.substr(7);
            } 
            else if (arg.rfind("--config=", 0) == 0) {
                config_path = arg.substr(9);
            } 
            else if (arg.rfind("--data=", 0) == 0) {
                data_path = arg.substr(7);
            } 
            else if (arg.rfind("--duration=", 0) == 0) {
                duration = std::stoi(arg.substr(11));
            }
            else if (arg.rfind("--bootstrap-bars=", 0) == 0) {
                bootstrap_bars = std::stoi(arg.substr(17));
            }
            else if (arg.rfind("--test-start-bar=", 0) == 0) {
                test_start_bar = std::stoi(arg.substr(17));
            }
            else if (arg == "--allow-large-bootstrap") {
                allow_large_bootstrap = true;
                // Env guard so downstream components can opt-in safely
                #ifdef _WIN32
                _putenv_s("SD_ALLOW_LARGE_BOOTSTRAP", "1");
                #else
                setenv("SD_ALLOW_LARGE_BOOTSTRAP", "1", 1);
                #endif
            }
            else if (arg == "--help" || arg == "-h") {
                print_usage();
                return 0;
            }
            else {
                std::cerr << "Unknown argument: " << arg << std::endl;
                print_usage();
                return 1;
            }
        }
        
        // Validate required arguments
        if (mode.empty()) {
            std::cerr << "ERROR: --mode argument is required\n";
            print_usage();
            return 1;
        }
        
        if (mode != "backtest" && mode != "dryrun" && mode != "live") {
            std::cerr << "ERROR: Invalid mode '" << mode << "'\n";
            std::cerr << "       Valid modes: backtest, dryrun, live\n";
            print_usage();
            return 1;
        }
        
        // ========================================
        // STEP 2: Initialize System Configuration
        // ========================================
        auto& sys_init = SystemInitializer::getInstance();
        
        if (!sys_init.initialize()) {
            std::cerr << "ERROR: System initialization failed\n";
            std::cerr << sys_init.getLastError() << std::endl;
            return 1;
        }
        
        const auto& sys_config = sys_init.getConfig();
        
        // Apply log level from system config
        try {
            if (sys_config.log_level == "DEBUG") {
                Logger::getInstance().setLevel(LogLevel::DEBUG);
            } else if (sys_config.log_level == "INFO") {
                Logger::getInstance().setLevel(LogLevel::INFO);
            } else if (sys_config.log_level == "WARN") {
                Logger::getInstance().setLevel(LogLevel::WARN);
            } else if (sys_config.log_level == "ERROR") {
                Logger::getInstance().setLevel(LogLevel::ERROR);
            }
        } catch (const std::exception& e) {
            std::cerr << "Warning: Failed to set log level: " << e.what() << std::endl;
        }
        
        // Print system configuration
        sys_init.printConfig();
        std::cout << std::endl;
        
        // ========================================
        // STEP 3: Resolve Configuration Paths
        // ========================================
        
        // Get strategy config path
        if (config_path.empty()) {
            config_path = sys_config.get_full_path(sys_config.strategy_config_file);
        }
        
        if (!fs::exists(config_path)) {
            std::cerr << "ERROR: Strategy config file not found: " << config_path << std::endl;
            return 1;
        }
        
        // Get data file path (for backtest/dryrun or optional for live)
        if (mode == "backtest" || mode == "dryrun") {
            if (data_path.empty()) {
                data_path = sys_config.get_full_path(sys_config.live_data_csv);
            }
            
            if (!fs::exists(data_path)) {
                std::cerr << "ERROR: Data file not found: " << data_path << std::endl;
                std::cerr << "       Backtest/DryRun mode requires CSV data file" << std::endl;
                return 1;
            }
        }
        
        // Print configuration summary
        std::cout << "Configuration Summary:" << std::endl;
        std::cout << "  Mode:          " << mode << std::endl;
        std::cout << "  Config file:   " << config_path << std::endl;
        if (mode == "backtest" || mode == "dryrun") {
            std::cout << "  Data file:     " << data_path << std::endl;
        }
        if (mode == "dryrun") {
            std::cout << "  Bootstrap:     " << (bootstrap_bars == -1 ? "auto" : 
                                                   bootstrap_bars == 0 ? "disabled" : 
                                                   std::to_string(bootstrap_bars) + " bars") << std::endl;
            std::cout << "  Test start:    " << (test_start_bar == 0 ? "auto" : 
                                                   std::to_string(test_start_bar)) << std::endl;
            if (allow_large_bootstrap) {
                std::cout << "  Large bootstrap: allowed (env guard set)" << std::endl;
            }
        }
        if ((mode == "live" || mode == "dryrun") && duration > 0) {
            std::cout << "  Duration:      " << duration << " minutes" << std::endl;
        }
        std::cout << std::endl;
        
        // ========================================
        // STEP 4: Create Engine via Factory
        // ========================================
        LOG_INFO("Creating trading engine...");
        
        auto engine = EngineFactory::create(
            mode,
            sys_config,
            config_path,
            data_path,
            bootstrap_bars,
            test_start_bar
        );
        
        std::cout << std::endl;
        std::cout << "Engine Type: " << engine->get_engine_type() << std::endl;
        std::cout << std::endl;
        
        // ========================================
        // STEP 5: Initialize Engine
        // ========================================
        LOG_INFO("Initializing engine...");
        
        if (!engine->initialize()) {
            std::cerr << "ERROR: Engine initialization failed" << std::endl;
            return 1;
        }
        
        LOG_INFO("Engine initialized successfully");
        std::cout << std::endl;
        
        // ========================================
        // STEP 6: Run Engine
        // ========================================
        
        if (mode == "live") {
            std::cout << "==================================================" << std::endl;
            std::cout << "  READY TO START LIVE TRADING" << std::endl;
            std::cout << "==================================================" << std::endl;
            std::cout << std::endl;
            std::cout << "Starting automatically (Ctrl+C to stop anytime)..." << std::endl;
            // std::cin.get();  // Disabled for automatic startup
            std::cout << std::endl;
        }
        
        std::cout << "==================================================" << std::endl;
        std::cout << "  ENGINE RUNNING" << std::endl;
        std::cout << "==================================================" << std::endl;
        std::cout << std::endl;
        
        // Run the engine
        engine->run(duration);
        
        // ========================================
        // STEP 7: Cleanup and Stop
        // ========================================
        std::cout << std::endl;
        LOG_INFO("Stopping engine...");
        
        engine->stop();
        
        std::cout << std::endl;
        std::cout << "==================================================" << std::endl;
        std::cout << "  ENGINE STOPPED SUCCESSFULLY" << std::endl;
        std::cout << "==================================================" << std::endl;
        std::cout << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << std::endl;
        std::cerr << "==================================================" << std::endl;
        std::cerr << "  FATAL ERROR" << std::endl;
        std::cerr << "==================================================" << std::endl;
        std::cerr << e.what() << std::endl;
        std::cerr << std::endl;
        return 1;
    }
    
    return 0;
}