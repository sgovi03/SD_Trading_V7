// ============================================================
// DRY RUN ENGINE - LIVE ENGINE WITH CSV SIMULATOR
// ============================================================

#ifndef DRYRUN_ENGINE_H
#define DRYRUN_ENGINE_H

#include "live/live_engine.h"
#include "csv_simulator_broker.h"
#include "ZoneInitializer.h"
#include <memory>
#include <string>

namespace SDTrading {
namespace DryRun {

/**
 * @class DryRunEngine
 * @brief Live engine with CSV simulator broker for deterministic testing
 * 
 * DESIGN PHILOSOPHY:
 * - Inherits ALL logic from LiveEngine (100% code reuse)
 * - Only difference: Uses CsvSimulatorBroker instead of FyersAdapter
 * - Processes CSV sequentially (bar-by-bar) like live trading
 * - Applies ALL live features (filtering, gating, bootstrap)
 * - Deterministic and repeatable (same CSV → same results)
 * 
 * KEY BENEFITS:
 * 1. Tests live engine logic without real broker connection
 * 2. Validates filtering, gating, and bootstrap mechanisms
 * 3. Provides baseline for live engine comparison
 * 4. Enables debugging of live-specific issues
 * 
 * COMPARISON MATRIX:
 * 
 * Feature               | Backtest | DryRun | Live
 * =====================================================
 * Data Source           | CSV      | CSV    | Broker
 * Processing Model      | Batch    | Event  | Event
 * Zone Filtering        | No       | Yes    | Yes
 * Entry Gating          | No       | Yes    | Yes
 * Zone Bootstrap        | No       | Yes    | Yes
 * Tick Simulation       | No       | Yes    | N/A
 * Deterministic         | Yes      | Yes    | No
 * Real Orders           | No       | No     | Yes
 * 
 * USAGE:
 * ```cpp
 * // Run dry run mode
 * ./sd_trading_unified --mode=dryrun --data=nifty_jan2025.csv
 * 
 * // Compare results
 * diff results/backtest/trades.csv results/dryrun/trades.csv
 * ```
 * 
 * TESTING WORKFLOW:
 * 1. Run backtest: Establish baseline P&L
 * 2. Run dryrun:   Test live logic with same CSV
 * 3. Compare:      Identify divergence sources
 * 4. Fix:          Adjust config to align results
 * 5. Run live:     Deploy with confidence
 */
class DryRunEngine : public Live::LiveEngine {
private:
    std::string csv_file_path_;
    std::string symbol_;
    std::string interval_;
    bool enable_tick_simulation_;
    int ticks_per_bar_;
    
    // Simulator reference (for progress tracking)
    Live::CsvSimulatorBroker* simulator_ptr_;
    
    // ⭐ NEW: Bootstrap phase tracking for dryrun mode
    int bootstrap_end_bar_;  // Bar index where bootstrap phase ends
    bool is_dryrun_bootstrap_active_;  // Flag to skip trading during bootstrap

public:
    /**
     * Constructor
     * @param cfg Configuration settings
     * @param csv_path Path to CSV data file
     * @param symbol Trading symbol
     * @param interval Bar interval
     * @param output_dir Output directory for results
     * @param enable_ticks Enable intra-bar tick simulation
     * @param ticks_per_bar Number of ticks per bar (default: 4)
     */
    DryRunEngine(
        const Core::Config& cfg,
        const std::string& csv_path,
        const std::string& symbol,
        const std::string& interval,
        const std::string& output_dir,
        bool enable_ticks = false,
        int ticks_per_bar = 4
    );
    
    // ========================================
    // ITradingEngine Interface (inherited)
    // ========================================
    
    /**
     * Initialize engine
     * - Creates CSV simulator broker
     * - Calls LiveEngine::initialize() (inherits all logic)
     * @return true if initialized successfully
     */
    bool initialize() override;
    
    /**
     * Run dry run
     * - Calls LiveEngine::run() (inherits all logic)
     * - Processes CSV bars sequentially
     * - Applies all live features
     * @param duration_minutes Run duration (0 = until CSV exhausted)
     */
    void run(int duration_minutes = 0) override;
    
    /**
     * Stop engine
     * - Calls LiveEngine::stop() (inherits all logic)
     * - Exports simulator order log
     */
    void stop() override;
    
    /**
     * Get engine type identifier
     * @return "dryrun"
     */
    std::string get_engine_type() const override { return "dryrun"; }
    
    // ========================================
    // DryRun-Specific Methods
    // ========================================
    
    /**
     * Get simulator progress percentage
     * @return 0.0 to 100.0
     */
    double get_progress_pct() const;
    
    /**
     * Check if more bars available in CSV
     * @return true if not at end
     */
    bool has_more_bars() const;
    
    /**
     * Get total bars in CSV
     * @return Bar count
     */
    size_t get_total_bars() const;
    
    /**
     * Get current position in CSV
     * @return Current bar index
     */
    size_t get_current_position() const;
};

} // namespace DryRun
} // namespace SDTrading

#endif // DRYRUN_ENGINE_H
