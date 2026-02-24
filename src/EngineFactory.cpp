// ============================================================
// ENGINE FACTORY - FACTORY PATTERN IMPLEMENTATION
// ============================================================

#include "EngineFactory.h"
#include "backtest/backtest_engine.h"
#include "live/live_engine.h"
#include "dryrun/dryrun_engine.h"
#include "csv_simulator_broker.h"
#include "core/config_loader.h"
#include "utils/logger.h"
#include <iostream>

namespace SDTrading {
namespace Core {

using namespace Utils;

std::unique_ptr<ITradingEngine> EngineFactory::create(
    const std::string& mode,
    const SystemConfig& sys_config,
    const std::string& strategy_config_path,
    const std::string& data_file_path,
    int bootstrap_bars,
    int test_start_bar
) {
    LOG_INFO("Creating " << mode << " engine...");
    
    // Load strategy configuration
    Config config;
    try {
        config = ConfigLoader::load_from_file(strategy_config_path);
        LOG_INFO("Strategy config loaded: " << strategy_config_path);
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to load strategy config: " << e.what());
        throw;
    }
    
    // Apply system config overrides
    config.starting_capital = sys_config.trading_starting_capital;
    // Guard against 0 lot_size from failed JSON parse
    if (sys_config.trading_lot_size > 0) {
        config.lot_size = sys_config.trading_lot_size;
    } else {
        LOG_WARN("sys_config.trading_lot_size=0 — keeping phase_6 config value: " << config.lot_size);
    }

    // Add runtime guard immediately after all overrides
    if (config.lot_size <= 0) {
        throw std::runtime_error(
            "FATAL: config.lot_size=0 after system override. "
            "Check system_config.json 'trading.lot_size'. "
            "Cap check is bypassed when lot_size=0!");
    }
        // Add startup config verification printout
        std::cout << "\n[CONFIG VERIFICATION]\n";
        std::cout << "  lot_size:             " << config.lot_size << "\n";
        std::cout << "  max_loss_per_trade:   Rs" << config.max_loss_per_trade << "\n";
        std::cout << "  starting_capital:     Rs" << config.starting_capital << "\n";
        double max_sl_1lot = config.max_loss_per_trade / config.lot_size;
        std::cout << "  Max SL distance @1lot: " << max_sl_1lot << " pts\n\n";
    config.live_zone_detection_interval_bars = sys_config.live_zone_detection_interval_bars;  // ADD THIS
    
    // Apply dryrun bootstrap parameters if provided
    if (mode == "dryrun") {
        if (bootstrap_bars != -999) {  // -999 = not set
            config.dryrun_bootstrap_bars = bootstrap_bars;
        }
        if (test_start_bar != -999) {
            config.dryrun_test_start_bar = test_start_bar;
        }
        LOG_INFO("DryRun parameters: bootstrap_bars=" << config.dryrun_bootstrap_bars 
                 << ", test_start_bar=" << config.dryrun_test_start_bar);
    }
    
    if (mode == "backtest") {
        if (data_file_path.empty()) {
            throw std::runtime_error("Data file path required for backtest mode");
        }
        
        std::string output_dir = sys_config.get_full_path(sys_config.backtest_results_dir);
        
        return std::make_unique<Backtest::BacktestEngine>(
            config,
            data_file_path,
            output_dir
        );
        
    } else if (mode == "dryrun") {
        if (data_file_path.empty()) {
            throw std::runtime_error(
                "Data file path required for dryrun mode. "
                "Use --data=<path> argument."
            );
        }
        
        std::string output_dir = sys_config.get_full_path(sys_config.backtest_results_dir);
        
        LOG_INFO("Creating DryRunEngine:");
        LOG_INFO("  Data file: " << data_file_path);
        LOG_INFO("  Symbol:    " << sys_config.trading_symbol);
        LOG_INFO("  Interval:  " << sys_config.trading_resolution << " min");
        LOG_INFO("  Output:    " << output_dir);
        
        // Enable tick simulation by default
        bool enable_ticks = true;
        int ticks_per_bar = 4;
        
        return std::make_unique<DryRun::DryRunEngine>(
            config,
            data_file_path,
            sys_config.trading_symbol,
            sys_config.trading_resolution,
            output_dir,
            enable_ticks,
            ticks_per_bar
        );
        
    } else if (mode == "live") {
        std::unique_ptr<Live::BrokerInterface> broker;
        std::string live_data_path = sys_config.get_full_path(sys_config.live_data_csv);
        
        if (sys_config.trading_mode == "live") {
            std::cout << "[WARNING] LIVE TRADING MODE - Real money will be used!" << std::endl;
            // TODO: Create FyersAdapter when available
            throw std::runtime_error("Real live trading not implemented yet - use paper trading mode");
        } else {
            std::cout << "[SIMULATION] PAPER TRADING MODE - Using CSV Simulator" << std::endl;
            
            // Create CSV simulator broker for paper trading
            broker = std::make_unique<Live::CsvSimulatorBroker>(
                live_data_path,
                sys_config.trading_symbol,
                sys_config.trading_resolution,
                false,  // Disable tick simulation for live mode
                4       // Ticks per bar (unused when disabled)
            );
        }
        
        std::string live_results_dir = sys_config.get_full_path(sys_config.live_trade_logs_dir);
        
        return std::make_unique<Live::LiveEngine>(
            config,
            std::move(broker),
            sys_config.trading_symbol,
            sys_config.trading_resolution,
            live_results_dir,
            live_data_path
        );
        
    } else {
        throw std::runtime_error(
            "Invalid engine mode: '" + mode + "'. "
            "Valid modes are 'backtest', 'dryrun', or 'live'."
        );
    }
}

} // namespace Core
} // namespace SDTrading
