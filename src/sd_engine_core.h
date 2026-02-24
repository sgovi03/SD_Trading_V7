// ============================================================
// SD ENGINE CORE V5.0 - HEADER FILE
// Multi-Dimensional Zone Scoring with Swing Position Analysis
// FIXED: Simplified Zone struct using Rule of Zero
// FIXED: Removed custom copy/move constructors
// FIXED: Better exception handling
// ============================================================

#ifndef SD_ENGINE_CORE_H
#define SD_ENGINE_CORE_H

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <map>
#include <iomanip>
#include <stdexcept>
#include <memory>

// ============================================================
// ENUMS
// ============================================================

enum ZoneType { DEMAND, SUPPLY };
enum ZoneState { FRESH, TESTED, VIOLATED, RECLAIMED };
enum MarketRegime { BULL, BEAR, RANGING };

// ============================================================
// FORWARD DECLARATIONS
// ============================================================

struct Bar;
struct Zone;
struct Trade;
struct BacktestConfig;
struct ScoringConfig;
struct ZoneScore;
struct EntryDecision;
struct SwingAnalysis;

// ============================================================
// STRUCTURES
// ============================================================

struct ScoringConfig {
    // Dimension weights (must sum to 1.0)
    double weight_base_strength = 0.40;
    double weight_elite_bonus = 0.25;
    double weight_regime_alignment = 0.20;
    double weight_state_freshness = 0.10;
    double weight_rejection_confirmation = 0.05;

    // Entry decision thresholds
    double entry_minimum_score = 30.0;
    double entry_optimal_score = 70.0;

    // Risk/Reward scaling
    double rr_base_ratio = 2.0;
    bool rr_scale_with_score = true;
    double rr_max_ratio = 5.0;

    // Rejection wick thresholds
    double rejection_strong_threshold = 0.6;
    double rejection_moderate_threshold = 0.4;
    double rejection_weak_threshold = 0.2;

    // Strength tier thresholds
    double strength_exceptional = 85.0;
    double strength_good = 75.0;
    double strength_acceptable = 65.0;

    bool validate() const;
};

struct ZoneScore {
    double base_strength_score = 0.0;
    double elite_bonus_score = 0.0;
    double swing_position_score = 0.0;
    double regime_alignment_score = 0.0;
    double state_freshness_score = 0.0;
    double rejection_confirmation_score = 0.0;
    double total_score = 0.0;
    double entry_aggressiveness = 0.0;
    double recommended_rr = 0.0;
    std::string score_breakdown;
    std::string entry_rationale;

    void calculate_composite();
};

struct EntryDecision {
    bool should_enter = false;
    double entry_price = 0.0;
    double stop_loss = 0.0;
    double take_profit = 0.0;
    double expected_rr = 0.0;
    double entry_location_pct = 0.0;
    std::string rejection_reason;
    ZoneScore score;
};

struct SwingAnalysis {
    bool is_at_swing_high = false;
    bool is_at_swing_low = false;
    double swing_percentile = 0.0;
    int bars_to_higher_high = 0;
    int bars_to_lower_low = 0;
    double swing_score = 0.0;
};

struct Bar {
    std::string datetime;
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double close = 0.0;
    double volume = 0.0;
    double oi = 0.0;
};

// ============================================================
// ZONE STRUCT - V5.0 FIXED VERSION
// Using Rule of Zero: Let compiler generate copy/move operations
// Removed all custom copy/move constructors
// PHASE 1 FIX: Added zone_id for proper metadata linking
// ============================================================
struct Zone {
    // PHASE 1 FIX: Unique zone identifier for metadata tracking
    int zone_id = 0;
    
    ZoneType type = ZoneType::DEMAND;
    double base_low = 0.0;
    double base_high = 0.0;
    double distal_line = 0.0;
    double proximal_line = 0.0;
    int formation_bar = 0;
    std::string formation_datetime;
    double strength = 0.0;
    ZoneState state = ZoneState::FRESH;
    int touch_count = 0;
    bool is_elite = false;
    double departure_imbalance = 0.0;
    double retest_speed = 0.0;
    int bars_to_retest = 0;
    SwingAnalysis swing_analysis;
    bool departure_confirmed = false;
    double min_departure_distance_atr = 1.5;

    // Default constructor with member initializers
    // Compiler generates copy constructor, move constructor,
    // copy assignment, and move assignment automatically
    
    // Helper method to validate zone data
    bool is_valid() const {
        return base_low > 0.0 && 
               base_high > base_low && 
               strength >= 0.0 &&
               !formation_datetime.empty();
    }
};

struct Trade {
    int trade_num = 0;
    std::string direction;
    std::string entry_date;
    std::string exit_date;
    double entry_price = 0.0;
    double exit_price = 0.0;
    std::string exit_reason;
    double stop_loss = 0.0;
    double take_profit = 0.0;
    int bars_in_trade = 0;
    double pnl = 0.0;
    double pnl_pct = 0.0;
    double mae = 0.0;
    double mfe = 0.0;
    double entry_score = 0.0;
    int zone_touch_count = 0;
    std::string zone_state;
    bool was_elite = false;
    double risk_reward_ratio = 0.0;
};

struct BacktestConfig {
    // Capital Management
    double starting_capital = 300000.0;
    double risk_per_trade_pct = 1.0;
    int lot_size = 75;
    double commission_per_trade = 20.0;

    // Zone Detection
    double base_height_max_atr = 1.9;
    int consolidation_min_bars = 2;
    int consolidation_max_bars = 10;
    double min_impulse_atr = 1.2;
    int atr_period = 14;

    // Zone Quality
    double min_zone_strength = 50.0;
    bool only_fresh_zones = false;
    bool invalidate_on_body_close = false;
    bool gap_over_invalidation = false;

    // V4.0 Adaptive Scoring
    bool use_adaptive_entry = true;
    ScoringConfig scoring_config;

    // HTF Market Regime
    bool use_htf_regime_filter = true;
    int htf_lookback_bars = 700;
    double htf_trending_threshold = 5.0;

    // Elite Zone Criteria
    double min_departure_imbalance = 2.0;
    double max_retest_speed_atr = 1.5;
    int min_bars_to_retest = 10;

    // Swing Detection
    int swing_detection_bars = 20;

    // Trend Filters
    bool use_bos_trend_filter = false;
    bool trade_with_trend_only = true;
    bool allow_ranging_trades = true;
    bool use_ema_trend_filter = true;
    int ema_fast_period = 20;
    int ema_slow_period = 50;
    double ema_ranging_threshold_pct = 0.5;

    // EMA Exit (live)
    bool enable_ema_exit = true;
    int exit_ema_fast_period = 9;
    int exit_ema_slow_period = 20;

    // Candle Confirmation (live)
    bool enable_candle_confirmation = false;
    double candle_confirmation_body_pct = 35.0;

    // Structure Confirmation (live)
    bool enable_structure_confirmation = false;

    // Stop Loss & Position Sizing
    double rejection_wick_ratio = 0.4;
    double entry_buffer_pct = 0.1;
    double sl_buffer_zone_pct = 10.0;
    double sl_buffer_atr = 0.5;

    // Trailing Stop
    bool use_trailing_stop = true;
    double trail_activation_rr = 1.5;
    int trail_ema_period = 20;
    double trail_ema_buffer_pct = 0.2;
    double trail_atr_multiplier = 2.0;
    bool trail_use_hybrid = true;
    double trail_fallback_tp_rr = 5.0;

    // ⭐ ADD THIS:
    double max_loss_per_trade = 10000.0;   // Hard cap: max closed loss per trade in ₹

    // Loss Protection
    int max_consecutive_losses = 4;
    int pause_bars_after_losses = 80;

    // Position Management
    int max_concurrent_trades = 1;

    // Backtesting
    int lookback_for_zones = 100;
    bool close_eod = true;
    std::string session_end_time = "15:30:00";

    // Debug & Logging
    bool enable_debug_logging = false;
    bool enable_score_logging = false;

    bool validate() const;
    void print_summary() const;
};

// ============================================================
// FUNCTION DECLARATIONS
// ============================================================

// Configuration
BacktestConfig load_config(const std::string& config_file);

// Data Loading
std::vector<Bar> load_csv_data(const std::string& filename);

// Technical Indicators
double calculate_atr(const std::vector<SDTrading::Core::Bar>& bars, int period, int end_idx);
double calculate_ema(const std::vector<SDTrading::Core::Bar>& bars, int period, int end_idx);

// Zone Detection - Broken into smaller functions
bool is_consolidation(const std::vector<Bar>& bars, int start_idx, int end_idx, 
                      double max_height_atr, double atr);
bool is_impulse(const std::vector<Bar>& bars, int base_end, int departure_end,
                double min_atr, double atr);
void detect_zones_in_window(const std::vector<Bar>& bars, int current_idx,
                            const BacktestConfig& config,
                            std::vector<Zone>& result);

// Elite Metrics - Separated functions
void calculate_elite_metrics(Zone& zone, const std::vector<Bar>& bars, 
                             int current_idx, const BacktestConfig& config);
void calculate_swing_analysis(Zone& zone, const std::vector<Bar>& bars, 
                              int current_idx, const BacktestConfig& config);

// Market Regime
MarketRegime determine_htf_regime(const std::vector<Bar>& bars, int current_idx, 
                                  const BacktestConfig& config);

// Market Context (dynamic scoring inputs)
struct MarketContext {
    MarketRegime regime;
    double trend_strength;      // 0-1 absolute EMA slope strength
    double volatility_ratio;    // ATR(14)/ATR(50) ratio
    double fib_position;        // 0-1 position within recent swing
    bool rsi_bull_div;          // bullish divergence detected
    bool rsi_bear_div;          // bearish divergence detected
    int wave_hint;              // +1 bullish impulse, -1 bearish impulse, 0 neutral
    bool no_trade_zone;         // true when conditions are choppy / conflicting
};

MarketContext build_market_context(const std::vector<Bar>& bars, int current_idx,
                                   const BacktestConfig& config, MarketRegime regime);

// Zone Scoring (dynamic, market-aware)
ZoneScore calculate_zone_score(const Zone& zone, const MarketContext& context,
                               const std::vector<Bar>& bars, int current_idx,
                               const BacktestConfig& config);

// Entry Decision
EntryDecision evaluate_entry(const Zone& zone, const std::vector<Bar>& bars,
                             int current_idx, MarketRegime regime,
                             const BacktestConfig& config);

// Position Sizing
int calculate_position_size(double entry_price, double stop_loss,
                           const BacktestConfig& config, double current_capital);

// Trade Management
double calculate_trailing_stop(const std::vector<Bar>& bars, int entry_idx,
                               int current_idx, double entry_price,
                               const std::string& direction,
                               const BacktestConfig& config);

// Utility
std::string zone_type_to_string(ZoneType type);
std::string zone_state_to_string(ZoneState state);
std::string market_regime_to_string(MarketRegime regime);

#endif // SD_ENGINE_CORE_H
