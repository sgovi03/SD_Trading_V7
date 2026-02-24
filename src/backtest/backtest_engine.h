// ============================================================
// BACKTEST ENGINE - MODIFIED FOR UNIFIED ARCHITECTURE
// Implements ITradingEngine interface with zone persistence
// ============================================================

#ifndef BACKTEST_ENGINE_H
#define BACKTEST_ENGINE_H

#include <vector>
#include <string>
#include <optional>
#include "../ITradingEngine.h"
#include "../ZonePersistenceAdapter.h"
#include "common_types.h"
#include "zones/zone_detector.h"
#include "analysis/market_analyzer.h"
#include "scoring/zone_scorer.h"
#include "scoring/entry_decision_engine.h"
#include "backtest/trade_manager.h"
#include "backtest/performance_tracker.h"
#include "utils/volume_baseline.h"
#include "scoring/volume_scorer.h"
#include "scoring/oi_scorer.h"

namespace SDTrading {
namespace Backtest {

// Import types from Core namespace
using Core::Trade;

/**
 * @class BacktestEngine
 * @brief Main backtest orchestrator - Implements ITradingEngine
 * 
 * MODIFICATIONS FROM ORIGINAL:
 * - Implements ITradingEngine interface
 * - Integrated ZonePersistenceAdapter for zone state saving
 * - CSV loading moved inside class (not external function)
 * - Zone ID tracking for proper persistence
 * 
 * Coordinates all components to run historical backtests:
 * - Loads CSV data
 * - Detects and manages zones
 * - Scores zones and makes entry decisions
 * - Manages open trades
 * - Tracks performance metrics
 * - Persists zones to file (date-stamped)
 */
class BacktestEngine : public Core::ITradingEngine {
private:
    Core::Config config;  // FIX: Store by value, not reference
    
    // Core components (unchanged - The Brain)
    Core::ZoneDetector detector;
    Core::ZoneScorer scorer;
    Core::EntryDecisionEngine entry_engine;
    TradeManager trade_manager;
    PerformanceTracker performance;
    
    // V6.0: Volume & OI Integration
    Utils::VolumeBaseline volume_baseline_;
    Core::VolumeScorer* volume_scorer_;
    Core::OIScorer* oi_scorer_;
    
    // NEW: Zone persistence
    Core::ZonePersistenceAdapter zone_persistence_;
    int next_zone_id_;
    
    // NEW: File paths
    std::string data_file_path_;
    std::string output_dir_;
    
    // State
    std::vector<Core::Bar> bars;
    std::vector<Core::Zone> active_zones;
    int pause_counter;
    int consecutive_losses;

    // Zone detection summary tracking
    Core::ZoneDetector::ZoneDetectionSummary initial_zone_detection_summary_{};
    long long total_zones_created_ = 0;
    long long total_zones_merged_ = 0;
    long long total_zones_skipped_ = 0;
    
    /**
     * NEW: Load CSV data (Fyers broker format)
     * Format: Timestamp,DateTime,Symbol,Open,High,Low,Close,Volume,OpenInterest
     * @return true if loaded successfully
     */
    bool load_csv_data();
    
    /**
     * Process a single bar
     * @param bar Current bar to process
     * @param bar_index Index of current bar
     */
    void process_bar(const Core::Bar& bar, int bar_index);
    
    /**
     * Check for new zone formation
     * @param bar Current bar
     * @param bar_index Current bar index
     */
    void detect_and_add_zones(const Core::Bar& bar, int bar_index);
    
    /**
     * Look for entry opportunities in active zones
     * @param bar Current bar
     * @param bar_index Current bar index
     */
    void check_for_entries(const Core::Bar& bar, int bar_index);
    
    /**
     * Manage open position
     * @param bar Current bar
     */
    void manage_open_position(const Core::Bar& bar, int bar_index);
    
    /**
     * Update zone states based on price action
     * MODIFIED: Now saves zones after state changes
     * @param bar Current bar
     */
    void update_zone_states(const Core::Bar& bar);
    
    /**
     * NEW: Detect zone flip after breakdown (opposite zone creation)
     * @param violated_zone The zone that just got violated
     * @param bar_index Current bar index
     * @return New flipped zone if detected, empty optional otherwise
     */
    std::optional<Core::Zone> detect_zone_flip(const Core::Zone& violated_zone, int bar_index);
    
    /**
     * NEW: Export results to CSV files
     */
    void export_results();
    
    /**
     * Print zone statistics summary
     */
    void print_zone_statistics() const;

    /**
     * Write zone detection summary to a file
     */
    void write_zone_detection_summary() const;

public:
    /**
     * Constructor
     * @param cfg Configuration settings
     * @param data_file Path to CSV data file
     * @param output_directory Path to results output directory
     */
    explicit BacktestEngine(const Core::Config& cfg,
                           const std::string& data_file,
                           const std::string& output_directory);
    
    /**
     * Destructor - cleanup V6.0 resources
     */
    ~BacktestEngine();
    
    // ========================================
    // ITradingEngine Interface Implementation
    // ========================================
    
    /**
     * Initialize backtest engine
     * - Loads CSV data
     * - Starts with fresh zones (no loading for backtest)
     * @return true if initialization successful
     */
    bool initialize() override;
    
    /**
     * Run complete backtest
     * @param duration_minutes Ignored for backtest (processes all data)
     */
    void run(int duration_minutes = 0) override;
    
    /**
     * Stop engine and export results
     */
    void stop() override;
    
    /**
     * Get engine type identifier
     * @return "backtest"
     */
    std::string get_engine_type() const override { return "backtest"; }
    
    // ========================================
    // Public Accessors (unchanged)
    // ========================================
    
    /**
     * Get all completed trades
     * @return Vector of trade records
     */
    const std::vector<Trade>& get_trades() const;
    
    /**
     * Get performance tracker for detailed metrics
     * @return Reference to performance tracker
     */
    const PerformanceTracker& get_performance() const;
};

} // namespace Backtest
} // namespace SDTrading

#endif // BACKTEST_ENGINE_H
