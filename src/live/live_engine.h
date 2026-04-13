// ============================================================
// LIVE ENGINE - MODIFIED FOR UNIFIED ARCHITECTURE
// Implements ITradingEngine interface with zone persistence
// ============================================================

#ifndef LIVE_ENGINE_H
#define LIVE_ENGINE_H

#include <memory>
#include <vector>
#include <string>
#include <functional>
#include "../events/event_types.h"  // V8: Events::SignalEvent, TradeOpenEvent, TradeCloseEvent
#include <optional>
#include <map>
#include <unordered_map>
#include <chrono>
#include "../ITradingEngine.h"
#include "../ZonePersistenceAdapter.h"
#include "common_types.h"
#include "broker_interface.h"
#include "zones/zone_detector.h"
#include "analysis/market_analyzer.h"
#include "scoring/zone_scorer.h"
#include "scoring/entry_decision_engine.h"
#include "backtest/trade_manager.h"  // ⭐ SHARED MODULE
#include "backtest/performance_tracker.h"  // ⭐ NEW: Performance tracking
#include "../utils/http/order_submitter.h"
#include "utils/volume_baseline.h"  // V6.0: Volume baseline
#include "scoring/volume_scorer.h"  // V6.0: Volume scorer
#include "scoring/oi_scorer.h"  // V6.0: OI scorer
#include "core/zone_state_manager.h"

namespace SDTrading {
namespace Live {

// Import types from Core namespace
using Core::Bar;
using Core::Zone;
using Core::Config;
using Core::ZoneScorer;
using Core::EntryDecisionEngine;
using Core::ZoneDetector;
using Core::MarketRegime;
using Core::EntryDecision;
using Core::Trade;

/**
 * @class LiveEngine
 * @brief Real-time trading engine - Implements ITradingEngine
 * 
 * MODIFICATIONS FROM ORIGINAL:
 * - Implements ITradingEngine interface
 * - Integrated ZonePersistenceAdapter for zone state persistence
 * - Loads zones from file on initialize (zones_live.json)
 * - Saves zones after detection and state changes
 * - Zone ID tracking for proper persistence
 * 
 * ⭐ USES SHARED TRADEMANAGER ⭐
 * This ensures IDENTICAL trade management logic as backtest:
 * - Same stop loss monitoring
 * - Same take profit monitoring
 * - Same P&L calculation
 * - Only execution differs (real vs simulated)
 * 
 * Manages live trading workflow:
 * - Monitors market in real-time
 * - Detects zones from live data
 * - Scores zones and makes entry decisions
 * - Places orders through broker
 * - Manages open positions using TradeManager
 * - Persists zones to single file (zones_live.json)
 */
class LiveEngine : public Core::ITradingEngine {
protected:
    Config config;  // Store by value to avoid dangling reference
    std::unique_ptr<BrokerInterface> broker;
    
	// ⭐ FIX #2: Zone State Lifecycle Manager
    std::unique_ptr<Core::ZoneStateManager> zone_state_manager_;
	
    // Core components (same as backtest - The Brain)
    ZoneDetector detector;
    ZoneScorer scorer;
    EntryDecisionEngine entry_engine;
    
    // ⭐ SHARED TRADE MANAGEMENT ⭐
    Backtest::TradeManager trade_mgr;  // Same module as backtest!
    
    // ⭐ NEW: Performance tracking and CSV export ⭐
    Backtest::PerformanceTracker performance;  // Track all trades and metrics
    int last_csv_export_bar_;  // Track last bar when CSV was exported
    
    // ✅ V6.0: Volume & OI Integration
    Utils::VolumeBaseline volume_baseline_;                   // Volume time-of-day baseline
    std::unique_ptr<Core::VolumeScorer> volume_scorer_;      // Volume analyzer
    std::unique_ptr<Core::OIScorer> oi_scorer_;              // OI analyzer
    bool v6_enabled_;                                         // V6.0 activation flag
    
    // NEW: Zone persistence
    Core::ZonePersistenceAdapter zone_persistence_;
    int next_zone_id_;
    std::string output_dir_;
    std::string historical_csv_path_;  // ⭐ NEW: Path to historical CSV data
    
	std::unique_ptr<OrderSubmitter> order_submitter_;
    // State
    std::vector<Bar> bar_history;
    std::vector<Zone> active_zones;
    std::vector<Zone> inactive_zones;      // NEW: Zones outside current price range
    int bar_index = 0;
    int last_zone_scan_bar_;  // Track last bar index where zones were scanned
    int bars_since_last_refresh_ = 0;      // NEW: Counter for periodic refresh
    double last_refresh_ltp_ = 0.0;        // NEW: Track LTP at last refresh
    std::string last_saved_bar_time_;  // Avoid duplicate zone saves per bar
    // Console print tracking
    std::string last_printed_bar_time_;
    bool has_printed_first_tick_ = false;
    // LIVE gating state
    std::string last_entry_check_bar_time_;
    std::unordered_map<int, std::chrono::steady_clock::time_point> last_entry_attempt_by_zone_;

    // === LOSS PROTECTION STATE ===
    int consecutive_losses = 0;
    int pause_counter = 0;

    // === SAME-SESSION RE-ENTRY GUARD ===
    // Maps zone_id → session date (YYYY-MM-DD) of last STOP_LOSS exit.
    // Any zone present here is blocked from re-entry for the remainder of that session.
    // Cleared automatically at the start of each new trading day.
    std::map<int, std::string> zone_sl_session_;

    // ⭐ NEW: DryRun bootstrap phase tracking
    int dryrun_bootstrap_end_bar_;  // Bar index where bootstrap ends (only used in dryrun mode)
    bool skip_trading_until_bar_;   // Flag to skip trading until reaching bootstrap end bar

    // V8: Two-phase operation gate.
    // false = HISTORICAL mode → build zones, no trading
    // true  = LIVE mode → full trading enabled
    // Flips true on first bar whose source != "HISTORICAL"
    bool historical_mode_;

    // Dedup cache for process_zones() — rebuilt only when active_zones changes.
    // Avoids copying active_zones on every bar (was O(n) allocation per tick).
    std::vector<Zone> dedup_cache_;
    size_t            dedup_cache_zone_count_ = 0;
    int               dedup_cache_cutoff_     = -1;

    // V8: trade event publish callbacks (wired by SymbolContext → EventBus → DbWriter)
    std::function<void(const Events::SignalEvent&)>     signal_pub_cb_;
    std::function<void(const Events::TradeOpenEvent&)>  trade_open_pub_cb_;
    std::function<void(const Events::TradeCloseEvent&)> trade_close_pub_cb_;

    // V8: zone callbacks
    std::function<void(const std::string&, int,
                       const std::string&, int, int)> zone_update_cb_;
    std::function<void(const std::vector<Zone>&,
                       const std::vector<Zone>&)>     zone_snapshot_cb_;
    
    // ⭐ NEW: Track exported trade count to prevent duplicate exports
    size_t last_exported_trade_count_ = 0;
    
    /**
     * ⭐ NEW: Load historical bars from CSV file
     * @param csv_path Path to CSV file
     * @return Vector of bars loaded from CSV
     */
    std::vector<Bar> load_csv_data(const std::string& csv_path);
    
    /**
     * Update bar history with latest bar from broker
     */
    void update_bar_history();
    void fire_zone_update(const Zone& zone);
    void publish_trade_events(const Trade& trade, const Zone& zone, bool is_close);
    
    /**
     * Detect and manage zones
     * MODIFIED: Now saves zones after detection
     */
    void process_zones();
    
    /**
     * Check for entry opportunities
     */
    void check_for_entries();
    
    /**
     * Manage open position using TradeManager
     */
    void manage_position();
    
    /**
     * NEW: Update zone states based on current price
     * Saves zones after state changes
     */
    void update_zone_states(const Bar& current_bar);

    /**
     * Replay historical bars to align zone states with backtest
     * (no per-bar persistence during replay)
     */
    void replay_historical_zone_states();

    /**
     * Load zones from persistent file (zones_live.json)
     * @return true if zones were loaded, false if no file or load failed
     */
    bool try_load_backtest_zones();

    /**
     * NEW: Detect zone flip after breakdown (opposite zone creation)
     * @param violated_zone The zone that just got violated
     * @param bar_index Current bar index
     * @return New flipped zone if detected, empty optional otherwise
     */
    std::optional<Core::Zone> detect_zone_flip(const Core::Zone& violated_zone, int bar_index);

    /**
     * ⭐ NEW: Calculate price range for zone filtering
     * Range = [LTP * (1 - range_pct/100), LTP * (1 + range_pct/100)]
     * @param ltp Current LTP
     * @param range_pct Percentage (e.g., 5.0 for +/-5%)
     * @return Pair of (lower_bound, upper_bound)
     */
    std::pair<double, double> calculate_price_range(double ltp, double range_pct) const;
    
    /**
     * ⭐ NEW: Check if zone overlaps with price range
     * Zone overlaps if any part is within [lower, upper]
     * @param zone Zone to check
     * @param lower_bound Lower price boundary
     * @param upper_bound Upper price boundary
     * @return true if zone overlaps the range
     */
    bool is_zone_in_range(const Zone& zone, double lower_bound, double upper_bound) const;
    
    /**
     * ⭐ NEW: Filter all zones into active/inactive based on price range
     * Called after loading or detecting zones
     * @param ltp Current LTP for range calculation
     */
    void filter_zones_by_range(double ltp);
    
    /**
     * ⭐ NEW: Refresh active/inactive zones based on current price
     * Called periodically (every N minutes)
     * @param ltp Current LTP
     */
    void refresh_active_zones(double ltp);

    /**
     * ⭐ NEW: Export trade journal (trades.csv, metrics.csv, equity_curve.csv)
     * Called after each trade close and periodically
     */
    void export_trade_journal();

    /**
     * ✅ V6.0: Initialize Volume/OI infrastructure
     * Loads volume baseline and creates scorers
     * @return true if V6.0 successfully initialized
     */
    bool initialize_v6_components();
    
    /**
     * ✅ V6.0: Wire V6.0 dependencies to shared components
     * Passes volume_baseline and scorers to detector, entry_engine, trade_mgr
     */
    void wire_v6_dependencies();
    
    /**
     * ✅ V6.0: Validate V6.0 activation status
     * Logs component status for debugging
     */
    void validate_v6_startup();
    
    /**
     * ✅ V6.0: Check if today is monthly expiry
     * @param current_datetime Current bar datetime
     * @return true if expiry day
     */
    bool is_expiry_day(const std::string& current_datetime) const;

    /**
     * ⭐ NEW: Bootstrap zones on startup using full historical data
     * Generates fresh zone snapshot identical to backtest process
     * @return true if bootstrap successful (fresh or reused), false on error
     */
    bool bootstrap_zones_on_startup();

    /**
     * ⭐ NEW: Check if cached zones are still valid (TTL check)
     * @param metadata Metadata from zones file
     * @return true if zones can be reused, false if regeneration needed
     */
    bool check_bootstrap_validity(const std::map<std::string, std::string>& metadata);
// In private: section of class LiveEngine
std::string last_exit_date_ ="";        // replaces static local in manage_position()
std::string last_guard_date_ = "";       // replaces static local in check_for_entries()
std::string guard_last_exit_date_ = "";  // replaces static local in process_tick()

    // ⭐ ISSUE-3: Overnight continuation trade snapshot
    // Populated when a profitable SESSION_END trade closes.
    // Consumed (and cleared) at the first bar of the next session.
    Core::PendingContinuation pending_continuation_;
    /**
     * 📝 LOG ORDER TO CSV FOR SIMULATION
     * Writes order details to simulated_orders.csv for DRY-run simulation
     * @param entry_time Order entry timestamp
     * @param zone_id Zone identifier
     * @param zone_score Zone scoring value
     * @param direction "BUY" or "SELL"
     * @param entry_price Entry price
     * @param stop_loss Stop loss price
     * @param take_profit Take profit price
     * @param position_size Number of lots
     */
    void log_order_to_csv(const std::string& entry_time,
                         const std::string& zone_id,
                         double zone_score,
                         const std::string& direction,
                         double entry_price,
                         double stop_loss,
                         double take_profit,
                         int position_size);
public:
    // Public trading configuration
    std::string trading_symbol;
    std::string bar_interval;
    
    /**
     * Constructor
     * @param cfg Configuration
     * @param broker_interface Broker implementation
     * @param symbol Trading symbol
     * @param interval Bar interval (e.g., "1", "5", "15")
     * @param output_dir Output directory for zone persistence (from system config)
     * @param historical_csv_path Path to CSV file for historical data loading
     */
    LiveEngine(const Config& cfg,
               std::unique_ptr<BrokerInterface> broker_interface,
               const std::string& symbol,
               const std::string& interval,
               const std::string& output_dir,
               const std::string& historical_csv_path);
    
    /**
     * Destructor - Cleanup V6.0 resources
     */
    ~LiveEngine();
    
    // ========================================
    // ITradingEngine Interface Implementation
    // ========================================
    
    /**Loads historical bars from CSV (NOT from broker)
     * - Initializes zone detector with historical data
     * - Connects to broker for live tick monitoring onlyonnect to broker
     * - Connects to broker
     * - Loads historical bars
     * - Loads persisted zones from zones_live.json (if exists)
     * @return true if initialized successfully
     */
    bool initialize() override;
    
    /**
     * Start live trading loop
     * @param duration_minutes How long to run (0 = indefinite)
     */
    void run(int duration_minutes = 0) override;
    
    /**
     * Stop trading and cleanup
     * - Saves zones to file
     * - Closes open positions
     * - Disconnects from broker
     */
    void stop() override;
    
    /**
     * Get engine type identifier
     * @return "live"
     */
    std::string get_engine_type() const override { return "live"; }

    /**
     * V8: Preload bar history from external source (e.g. SQLite).
     * Must be called BEFORE initialize(). Allows zone bootstrap
     * on restart without requiring AFL to replay historical data.
     */
    void preload_bar_history(const std::vector<Bar>& bars) {
        bar_history = bars;
    }

    // V8: Read-only access to active zones after initialize().
    // Used by SymbolContext to publish ZoneSnapshotEvents to EventBus.
    const std::vector<Zone>& get_active_zones() const { return active_zones; }
    const std::vector<Zone>& get_inactive_zones() const { return inactive_zones; }

    // V8: Trade event publish callbacks.
    // SymbolContext wires these to bus_->publish() so signals and trades
    // reach the DB via DbWriter without LiveEngine depending on EventBus.
    using SignalPubCallback    = std::function<void(const Events::SignalEvent&)>;
    using TradeOpenPubCallback = std::function<void(const Events::TradeOpenEvent&)>;
    using TradeClosePubCallback= std::function<void(const Events::TradeCloseEvent&)>;

    void set_signal_pub_callback(SignalPubCallback cb)     { signal_pub_cb_     = std::move(cb); }
    void set_trade_open_callback(TradeOpenPubCallback cb)  { trade_open_pub_cb_ = std::move(cb); }
    void set_trade_close_callback(TradeClosePubCallback cb){ trade_close_pub_cb_= std::move(cb); }

    // V8: zone callbacks
    using ZoneUpdateCallback   = std::function<void(const std::string&, int,
                                                    const std::string&, int, int)>;
    using ZoneSnapshotCallback = std::function<void(const std::vector<Zone>&,
                                                    const std::vector<Zone>&)>;
    void set_zone_update_callback(ZoneUpdateCallback cb)   { zone_update_cb_    = std::move(cb); }
    void set_zone_snapshot_callback(ZoneSnapshotCallback cb){ zone_snapshot_cb_ = std::move(cb); }

    // ========================================
    // Public Accessors (unchanged)
    // ========================================
    
    /**
     * Process one trading cycle
     * Called periodically (e.g., every minute, every bar close)
     */
    void process_tick();
    
    /**
     * Check if currently in position
     * @return true if position open
     */
    bool is_in_position() const { return trade_mgr.is_in_position(); }
    
    /**
     * Get current trade info
     * @return Current live trade
     */
    const Trade& get_current_trade() const { return trade_mgr.get_current_trade(); }
};

} // namespace Live
} // namespace SDTrading

#endif // LIVE_ENGINE_H