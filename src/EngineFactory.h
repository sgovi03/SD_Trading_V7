// ============================================================
// ENGINE FACTORY - FACTORY PATTERN IMPLEMENTATION
// Creates appropriate engine based on runtime mode selection
// ============================================================

#ifndef ENGINE_FACTORY_H
#define ENGINE_FACTORY_H

#include "ITradingEngine.h"
#include "core/system_config_loader.h"
#include <memory>
#include <string>
#include "backtest/backtest_engine.h"
#include "live/live_engine.h"

namespace SDTrading {
namespace Core {

/**
 * @class EngineFactory
 * @brief Factory for creating trading engines based on mode
 * 
 * Implements Factory Pattern to create either:
 * - BacktestEngine for historical simulation
 * - LiveEngine for real-time trading
 * 
 * Handles all configuration injection and broker setup.
 */
class EngineFactory {
public:
    /**
     * Create trading engine based on mode
     * @param mode "backtest", "dryrun", or "live"
     * @param sys_config System configuration
     * @param strategy_config_path Path to strategy config file
     * @param data_file_path Path to data file (required for backtest, optional for live)
     * @param bootstrap_bars Bootstrap bars for dryrun (-1=auto, 0=disabled)
     * @param test_start_bar Test start bar for dryrun (0=auto)
     * @return Unique pointer to ITradingEngine implementation
     * @throws std::runtime_error if mode invalid or creation fails
     */
    static std::unique_ptr<ITradingEngine> create(
        const std::string& mode,
        const SystemConfig& sys_config,
        const std::string& strategy_config_path,
        const std::string& data_file_path = "",
        int bootstrap_bars = -1,
        int test_start_bar = 0
    );
    
private:
    // Prevent instantiation - static factory only
    EngineFactory() = delete;
};

} // namespace Core
} // namespace SDTrading

#endif // ENGINE_FACTORY_H
