#ifndef SDTRADING_COMMON_TYPES_H
#define SDTRADING_COMMON_TYPES_H

#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <iostream>

namespace SDTrading {
namespace Core {

// ============================================================
// ENUMERATIONS
// ============================================================

enum class ZoneType {
    DEMAND,
    SUPPLY
};

enum class ZoneMergeStrategy {
    UPDATE_IF_STRONGER,
    UPDATE_IF_FRESHER,
    EXPAND_BOUNDARIES,
    KEEP_ORIGINAL
};

enum class ZoneState {
    FRESH,
    TESTED,
    VIOLATED,
    RECLAIMED      // Zone swept but reclaimed with acceptance
};

enum class MarketRegime {
    BULL,
    BEAR,
    RANGING
};

// ============================================================
// BAR DATA STRUCTURE
// ============================================================

struct Bar {
    std::string datetime;
    double open;
    double high;
    double low;
    double close;
    double volume;
    double oi;  // Open Interest
    
    // NEW V6.0: OI metadata
    bool oi_fresh;              // Is this a fresh 3-min OI update?
    int oi_age_seconds;         // How old is the OI value?
    
    // NEW V6.0: Volume metadata (calculated in-memory)
    double norm_volume_ratio;   // volume / time-of-day baseline
    
    Bar() 
        : datetime(""), open(0), high(0), low(0), 
          close(0), volume(0), oi(0), oi_fresh(false),
          oi_age_seconds(0), norm_volume_ratio(0) {}
};

// ============================================================
// SWING POSITION ANALYSIS
// ============================================================

struct SwingAnalysis {
    bool is_at_swing_high;      // For SUPPLY zones
    bool is_at_swing_low;       // For DEMAND zones
    double swing_percentile;     // 0-100, where in recent range
    int bars_to_higher_high;     // -1 if none found
    int bars_to_lower_low;       // -1 if none found
    double swing_score;          // 0-30 points
    
    SwingAnalysis() 
        : is_at_swing_high(false), is_at_swing_low(false),
          swing_percentile(50.0), bars_to_higher_high(-1),
          bars_to_lower_low(-1), swing_score(0) {}
};

// ============================================================
// VOLUME PROFILE (NEW V6.0)
// ============================================================


// ============================================================
// UPDATED: VolumeProfile struct for comprehensive scoring
// ============================================================
struct VolumeProfile {
    // Formation/base volume
    double formation_volume;
    double avg_volume_baseline;
    double volume_ratio;
    
    // Peak volume in consolidation
    double peak_volume;
    int high_volume_bar_count;
    
    // ===== NEW: DEPARTURE/IMPULSE TRACKING =====
    double departure_avg_volume;        // Average volume during impulse
    double departure_volume_ratio;      // Avg impulse volume / baseline
    double departure_peak_volume;       // Highest bar during impulse
    int departure_bar_count;            // Number of impulse bars
    bool strong_departure;              // departure_ratio >= 2.0
    
    // ===== NEW: QUALITY INDICATORS =====
    bool is_initiative;                 // vs absorption
    double volume_efficiency;           // Price move / volume
    bool has_volume_climax;             // Spike at last consolidation bar
    
    // ===== NEW: MULTI-TOUCH TRACKING =====
    std::vector<double> touch_volumes;  // Volume at each retest
    bool volume_rising_on_retests;      // Rising trend = bad!
    
    // Score
    double volume_score;                // Total 0-60 points
    
    // Helper method
    std::string to_string() const {
        std::ostringstream oss;
        oss << "VolProfile["
            << "formation=" << formation_volume
            << ", ratio=" << std::fixed << std::setprecision(2) << volume_ratio
            << ", departure=" << departure_volume_ratio
            << ", initiative=" << (is_initiative ? "YES" : "NO")
            << ", score=" << volume_score
            << "]";
        return oss.str();
    }
};

// ===== NEW: Entry Volume Metrics =====
struct EntryVolumeMetrics {
    double pullback_volume_ratio;       // Current bar volume / baseline
    double entry_bar_body_pct;          // Body / range
    bool volume_declining_trend;        // Last 3 bars declining
    bool passes_sweet_spot;             // Volume pattern filter
    int volume_score;                   // 0-60 points
    std::string rejection_reason;       // If failed
};

// ============================================================
// OI PROFILE (NEW V6.0)
// ============================================================

enum class MarketPhase {
    LONG_BUILDUP,      // Price ↑ + OI ↑ (bullish)
    SHORT_COVERING,    // Price ↑ + OI ↓ (temporary bullish)
    SHORT_BUILDUP,     // Price ↓ + OI ↑ (bearish)
    LONG_UNWINDING,    // Price ↓ + OI ↓ (temporary bearish)
    NEUTRAL            // No clear phase
};

inline std::string market_phase_to_string(MarketPhase phase) {
    switch (phase) {
        case MarketPhase::LONG_BUILDUP: return "LONG_BUILDUP";
        case MarketPhase::SHORT_COVERING: return "SHORT_COVERING";
        case MarketPhase::SHORT_BUILDUP: return "SHORT_BUILDUP";
        case MarketPhase::LONG_UNWINDING: return "LONG_UNWINDING";
        case MarketPhase::NEUTRAL: return "NEUTRAL";
        default: return "UNKNOWN";
    }
}

struct OIProfile {
    long formation_oi;                 // OI when zone was created
    long oi_change_during_formation;   // Delta OI (end - start)
    double oi_change_percent;          // Percentage change in OI
    double price_oi_correlation;       // Correlation coefficient (-1 to +1)
    bool oi_data_quality;              // Were OI readings fresh during formation?
    MarketPhase market_phase;          // Detected market phase
    double oi_score;                   // 0-30 points contribution to zone score
    
    OIProfile()
        : formation_oi(0), oi_change_during_formation(0),
          oi_change_percent(0), price_oi_correlation(0),
          oi_data_quality(false), market_phase(MarketPhase::NEUTRAL),
          oi_score(0) {}
    
    std::string to_string() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << "OIProfile[oi=" << formation_oi
            << ", change=" << oi_change_percent << "%"
            << ", corr=" << price_oi_correlation
            << ", phase=" << market_phase_to_string(market_phase)
            << ", score=" << oi_score << "]";
        return oss.str();
    }
};

// ============================================================
// ZONE STATE EVENT STRUCTURE
// ============================================================

struct ZoneStateEvent {
    std::string timestamp;         // Bar datetime when event occurred
    int bar_index;                 // Bar index in data
    std::string event;             // Event type: "ZONE_CREATED", "FIRST_TOUCH", "RETEST", "VIOLATED"
    std::string old_state;         // Previous state (empty for creation)
    std::string new_state;         // New state after event
    double price;                  // Price that triggered the event
    double bar_high;               // High of the bar
    double bar_low;                // Low of the bar
    int touch_number;              // Touch count at time of event
    
    ZoneStateEvent()
        : bar_index(-1), price(0), bar_high(0), bar_low(0), touch_number(0) {}
    
    ZoneStateEvent(const std::string& ts, int idx, const std::string& evt,
                   const std::string& old_st, const std::string& new_st,
                   double p, double h, double l, int touch)
        : timestamp(ts), bar_index(idx), event(evt), old_state(old_st),
          new_state(new_st), price(p), bar_high(h), bar_low(l), touch_number(touch) {}
};

// ============================================================
// ZONE SCORE STRUCTURE (must be defined before Zone)
// ============================================================

struct ZoneScore {
    double base_strength_score;
    double elite_bonus_score;
    double swing_position_score;
    double regime_alignment_score;
    double state_freshness_score;
    double rejection_confirmation_score;
    double total_score;
    double entry_aggressiveness;
    double recommended_rr;
    std::string score_breakdown;
    std::string entry_rationale;
    
    ZoneScore()
        : base_strength_score(0), elite_bonus_score(0),
          swing_position_score(0), regime_alignment_score(0),
          state_freshness_score(0), rejection_confirmation_score(0),
          total_score(0), entry_aggressiveness(0), recommended_rr(2.0) {}
    
    void calculate_composite() {
        total_score = base_strength_score + elite_bonus_score +
                     swing_position_score + regime_alignment_score +
                     state_freshness_score + rejection_confirmation_score;
        
        entry_aggressiveness = total_score / 100.0;
        
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1);
        oss << "Base:" << base_strength_score
            << " Elite:" << elite_bonus_score
            << " Swing:" << swing_position_score
            << " Regime:" << regime_alignment_score
            << " State:" << state_freshness_score
            << " Reject:" << rejection_confirmation_score
            << " = " << total_score;
        score_breakdown = oss.str();
    }
};

// ============================================================
// ZONE STRUCTURE
// ============================================================

struct Zone {
    ZoneType type;
    double base_low;
    double base_high;
    double distal_line;
    double proximal_line;
    int formation_bar;
    std::string formation_datetime;
    double strength;
    ZoneState state;
    int touch_count;
    bool is_elite;
    double departure_imbalance;
    double retest_speed;
    int bars_to_retest;
    SwingAnalysis swing_analysis;
    
    // For persistence
    int zone_id;  // Added for tracking across sessions
    
    // Sweep and reclaim tracking
    bool was_swept;                    // Zone was swept (spiked beyond)
    int sweep_bar;                     // Bar where sweep occurred
    int bars_inside_after_sweep;       // Bars closed inside after sweep
    bool reclaim_eligible;             // Zone eligible for reclaim
    
    // Zone flip tracking
    bool is_flipped_zone;              // Created from opposite zone breakdown
    int parent_zone_id;                // Original zone ID that flipped
    
    // Retry tracking
    int entry_retry_count;             // Times we entered and lost
    
    // State change history
    std::vector<ZoneStateEvent> state_history;
    
    // Scoring engine data - attached when zone is identified
    ZoneScore zone_score;              // Comprehensive scoring data
    
    // Live zone filtering (NEW: dynamic range-based filtering)
    bool is_active;                    // Zone overlaps current price range
    
    // NEW V6.0: Volume & OI Analytics
    VolumeProfile volume_profile;
    OIProfile oi_profile;
    double institutional_index;        // 0-100 composite institutional participation score
    
    Zone() 
        : type(ZoneType::DEMAND), base_low(0), base_high(0), 
          distal_line(0), proximal_line(0), formation_bar(0),
          formation_datetime(""), strength(0), state(ZoneState::FRESH),
          touch_count(0), is_elite(false), departure_imbalance(0),
          retest_speed(0), bars_to_retest(0), zone_id(-1),
          was_swept(false), sweep_bar(-1), bars_inside_after_sweep(0),
          reclaim_eligible(false), is_flipped_zone(false), parent_zone_id(-1),
          entry_retry_count(0), is_active(true), institutional_index(0) {}
};

// ============================================================
// SCORING CONFIGURATION
// ============================================================

struct ScoringConfig {
    // Dimension weights (must sum to 1.0)
    double weight_base_strength;
    double weight_elite_bonus;
    double weight_regime_alignment;
    double weight_state_freshness;
    double weight_rejection_confirmation;
    
    // Entry decision thresholds
    double entry_minimum_score;
    double entry_optimal_score;
    
    // Risk/Reward scaling
    double rr_base_ratio;
    bool rr_scale_with_score;
    double rr_max_ratio;
    
    // Rejection wick thresholds
    double rejection_strong_threshold;
    double rejection_moderate_threshold;
    double rejection_weak_threshold;
    
    // Strength tier thresholds
    double strength_exceptional;
    double strength_good;
    double strength_acceptable;
    
    // Zone exhaustion filter
    int max_zone_touch_count;
    
    ScoringConfig()
        : weight_base_strength(0.40),
          weight_elite_bonus(0.25),
          weight_regime_alignment(0.20),
          weight_state_freshness(0.10),
          weight_rejection_confirmation(0.05),
          entry_minimum_score(30.0),
          entry_optimal_score(70.0),
          rr_base_ratio(2.0),
          rr_scale_with_score(true),
          rr_max_ratio(5.0),
          rejection_strong_threshold(0.6),
          rejection_moderate_threshold(0.4),
          rejection_weak_threshold(0.2),
          strength_exceptional(85.0),
          strength_good(75.0),
          strength_acceptable(65.0),
          max_zone_touch_count(100) {}
    
    bool validate() const {
        double weight_sum = weight_base_strength + weight_elite_bonus +
                          weight_regime_alignment + weight_state_freshness +
                          weight_rejection_confirmation;
        
        if (std::abs(weight_sum - 1.0) > 0.01) {
            std::cerr << "❌ ERROR: Scoring weights must sum to 1.0 (current: "
                     << weight_sum << ")" << std::endl;
            return false;
        }
        
        if (entry_minimum_score < 0.0 || entry_minimum_score > 100.0) {
            std::cerr << "❌ ERROR: entry_minimum_score must be 0-100" << std::endl;
            return false;
        }
        
        if (rr_base_ratio <= 0.0 || rr_max_ratio <= rr_base_ratio) {
            std::cerr << "❌ ERROR: Invalid R:R ratio settings" << std::endl;
            return false;
        }
        
        return true;
    }
};

// ============================================================
// ENTRY DECISION STRUCTURE
// ============================================================

struct EntryDecision {
        bool should_enter;
        double entry_price;
        double stop_loss;
        double take_profit;
        double expected_rr;
        double entry_location_pct;
        std::string rejection_reason;
        ZoneScore score;
        double original_stop_loss;   // Original SL (for position sizing in breakeven mode)
        int lot_size;                // V6.0: Dynamic position sizing (0 = use default from config)
        // --- V6.0: Entry bar volume metrics ---
        double entry_pullback_vol_ratio = 0.0;
        int entry_volume_score = 0;
        std::string entry_volume_pattern = "";

        EntryDecision()
                : should_enter(false), entry_price(0), stop_loss(0),
                    take_profit(0), expected_rr(0), entry_location_pct(0), original_stop_loss(0), lot_size(0),
                    entry_pullback_vol_ratio(0.0), entry_volume_score(0), entry_volume_pattern("") {}
};

// ============================================================
// TRADE STRUCTURE
// ============================================================

struct Trade {
    int trade_num;
    std::string direction;
    std::string entry_date;
    std::string exit_date;
    double entry_price;
    double exit_price;
    std::string exit_reason;
    double stop_loss;
    double take_profit;
    int position_size;          // Position size (contracts/shares)
    int bars_in_trade;
    double pnl;
    double pnl_pct;
    double return_pct;          // Return percentage
    double risk_amount;
    double reward_amount;
    double actual_rr;
    double mae;  // Maximum Adverse Excursion
    double mfe;  // Maximum Favorable Excursion
    double zone_score;
    double entry_aggressiveness;
    std::string regime;
	
    // ⭐ Trailing Stop State
    double original_stop_loss = 0.0;     // Initial SL for R-multiple calculation
    double highest_price = 0.0;          // For LONG: track highest since entry
    double lowest_price = 0.0;           // For SHORT: track lowest since entry
    bool trailing_activated = false;     // Has trail activation threshold been reached?
    double current_trail_stop = 0.0;     // Current trailing stop level

    // Zone score breakdown (for reporting)
    double score_base_strength;
    double score_elite_bonus;
    double score_swing_position;
    double score_regime_alignment;
    double score_state_freshness;
    double score_rejection_confirmation;
    double score_recommended_rr;
    std::string score_rationale;
    
    // Zone information
    int zone_id;                      // NEW: Zone identifier
    std::string zone_formation_time;
    double zone_distal;
    double zone_proximal;
    double swing_percentile;
    bool is_at_swing_extreme;
    
    // ⭐ Technical Indicators at Entry
    double fast_ema;                  // Fast EMA value at entry
    double slow_ema;                  // Slow EMA value at entry
    double rsi;                       // RSI value at entry
    double bb_upper;                  // Bollinger Band Upper
    double bb_middle;                 // Bollinger Band Middle (SMA)
    double bb_lower;                  // Bollinger Band Lower
    double bb_bandwidth;              // Bollinger Band Width
    double adx;                       // ADX value
    double plus_di;                   // +DI value
    double minus_di;                  // -DI value
    double macd_line;                 // MACD Line
    double macd_signal;               // MACD Signal Line
    double macd_histogram;            // MACD Histogram
    
    // ===== V6.0 VOLUME/OI FIELDS FOR TRADE LOG =====
    double entry_volume = 0.0;                // Entry bar volume
    double entry_oi = 0.0;                    // Entry bar open interest
    double entry_volume_ratio = 0.0;          // Entry bar norm_volume_ratio
    double zone_volume_ratio = 0.0;           // Zone volume ratio
    double zone_peak_volume = 0.0;            // Zone peak volume
    double zone_departure_peak_volume = 0.0;  // Departure peak volume
    std::vector<double> zone_touch_volumes;   // Volumes at each retest
    bool zone_volume_rising_on_retests = false; // Rising trend on retests
    std::string zone_oi_phase = "";          // OI phase string
    double zone_oi_score = 0.0;               // OI score
    double exit_volume = 0.0;                 // Exit bar volume
    double exit_oi = 0.0;                     // Exit bar open interest

    // Existing V6.0 volume fields for CSV
    double zone_departure_volume_ratio = 0.0;  // Impulse vol / baseline at zone creation
    bool   zone_is_initiative          = false; // Initiative vs absorption
    bool   zone_has_volume_climax      = false; // Spike at end of consolidation
    double zone_volume_score           = 0.0;  // 0–60 zone volume score
    int    zone_institutional_index    = 0;    // Institutional index at zone

    // Entry-time pullback volume
    double entry_pullback_vol_ratio    = 0.0;  // Pullback bar vol / baseline
    int    entry_volume_score          = 0;    // 0–60 entry volume score
    std::string entry_volume_pattern   = "";   // e.g. "SWEET_SPOT", "ELITE", "LOW_VOL"
    int    entry_position_lots         = 0;    // Final lot size (1, 2, or 3)
    std::string position_size_reason   = "";   // e.g. "SweetSpot 1.5x", "LowVol 0.5x"

    // ===== V6.0 VOLUME DATA AT EXIT =====
    double exit_volume_ratio           = 0.0;  // Exit bar vol / baseline
    bool   exit_was_volume_climax      = false; // Did VOL_CLIMAX trigger exit?

    Trade()
                : trade_num(0), entry_price(0), exit_price(0), stop_loss(0),
                    take_profit(0), position_size(0), bars_in_trade(0), pnl(0), pnl_pct(0),
                    return_pct(0), risk_amount(0), reward_amount(0), actual_rr(0), mae(0), mfe(0),
                    zone_score(0), entry_aggressiveness(0),
                    zone_id(0), zone_formation_time(""), zone_distal(0), zone_proximal(0),
                    swing_percentile(50.0), is_at_swing_extreme(false),
                    score_base_strength(0), score_elite_bonus(0), score_swing_position(0),
                    score_regime_alignment(0), score_state_freshness(0),
                    score_rejection_confirmation(0), score_recommended_rr(0),
                    score_rationale(""),
                    original_stop_loss(0), highest_price(0), lowest_price(0), trailing_activated(false), current_trail_stop(0),
                    fast_ema(0), slow_ema(0), rsi(0), bb_upper(0), bb_middle(0), bb_lower(0), bb_bandwidth(0),
                    adx(0), plus_di(0), minus_di(0), macd_line(0), macd_signal(0), macd_histogram(0),
                    zone_departure_volume_ratio(0), zone_is_initiative(false),
                    zone_has_volume_climax(false), zone_volume_score(0),
                    zone_institutional_index(0), entry_pullback_vol_ratio(0),
                    entry_volume_score(0), entry_position_lots(0),
                    exit_volume_ratio(0), exit_was_volume_climax(false) {}
};

// ============================================================
// CONFIGURATION STRUCTURE
// ============================================================

struct Config {
public:
    // Enable/disable entry time restriction
    bool enable_entry_time_block;
            // Entry Time Restriction (configurable)
            std::string entry_block_start_time; // e.g. "09:15"
            std::string entry_block_end_time;   // e.g. "09:45"
        // Entry Volume Score Filter (V6.0)
        int min_volume_entry_score = -50;  // -50 = off (all pass); raise to 10+ to activate
    // Capital & Risk
    double starting_capital;
    double risk_per_trade_pct;
    
    // Zone Detection
    double base_height_max_atr;
    int consolidation_min_bars;
    int consolidation_max_bars;
    double max_consolidation_range;  // NEW: Max consolidation range in ATR
    double min_zone_width_atr;       // NEW: Minimum zone width filter (0.3 ATR for 1-min)
    double min_impulse_atr;
    int atr_period;
    double min_zone_strength;
    bool only_fresh_zones;
    bool invalidate_on_body_close;
    bool gap_over_invalidation;
    bool skip_retest_after_gap_over;        // Skip zones that gap-over even if price returns

    // Duplicate Prevention (NEW)
    bool duplicate_prevention_enabled;
    double zone_existence_tolerance_atr;    // Tolerance in ATRs for same-level zones
    double atr_buffer_multiplier;           // Multiplier applied to tolerance
    bool check_existing_before_create;
    ZoneMergeStrategy merge_strategy;       // How to merge overlapping candidates
    double strength_improvement_threshold;  // Min strength delta to replace
    int freshness_bars_threshold;           // Min bars newer to replace
    int zone_detection_interval_bars;       // Scan every N bars (>=1)
    
    // Sweep and reclaim detection
    bool enable_sweep_reclaim;              // Allow zones to reclaim after sweep
    int reclaim_acceptance_bars;            // Bars needed inside zone to reclaim (default: 2)
    double reclaim_acceptance_pct;          // % of zone body needed for acceptance (default: 0.5)
    
    // Zone flip detection
    bool enable_zone_flip;                  // Detect opposite zones at breakdown points
    double flip_min_impulse_atr;            // Minimum impulse after breakdown (default: 1.5)
    int flip_lookback_bars;                 // Bars to look back for flip base (default: 10)
    
    // Scoring
    bool use_adaptive_entry;
    ScoringConfig scoring;


    // High score entry logic (V6 config-driven)
    double high_score_position_mult;    // Multiplier for position sizing when score is high (e.g., 1.5)
    double high_score_threshold;        // Score threshold to trigger high multiplier (e.g., 65)
    // Two-stage scoring (optional)

    // V6.0 Volume Pattern Filters (Feb 2026)
    double max_entry_volume_ratio;           // Max volume before noise (e.g., 3.0)
    double max_volume_without_elite;         // Max volume without elite inst (e.g., 2.0)
    double min_inst_for_high_volume;         // Min inst required for high vol (e.g., 60)
    double optimal_volume_min;               // Sweet spot min (e.g., 1.0)
    double optimal_volume_max;               // Sweet spot max (e.g., 2.0)
    double optimal_institutional_min;        // Sweet spot inst min (e.g., 45)
    double allow_low_volume_if_score_above;  // Exception for tight zones (e.g., 65)
    double elite_institutional_threshold;    // Elite inst level (e.g., 70)
    bool enable_two_stage_scoring;

    // Stage 1: Zone quality thresholds (OLD - kept for backward compatibility)
    double zone_quality_minimum_score;
    int zone_quality_age_very_fresh;
    int zone_quality_age_fresh;
    int zone_quality_age_recent;
    int zone_quality_age_aging;
    int zone_quality_age_old;
    double zone_quality_height_optimal_min;
    double zone_quality_height_optimal_max;
    double zone_quality_height_acceptable_min;
    double zone_quality_height_acceptable_max;

    // Stage 1: NEW - Revamped Zone Quality Scoring Parameters
    bool enable_revamped_scoring;                // Toggle for new vs old scoring
    int rejection_analysis_lookback_days;        // Days to analyze rejection rate (default: 30)
    int breakthrough_analysis_lookback_days;     // Days to analyze breakthrough rate (default: 30)
    double rejection_excellent_threshold;        // >= this % = 25 points (default: 60.0)
    double rejection_good_threshold;             // >= this % = scaled 0-25 (default: 40.0)
    double rejection_acceptable_threshold;       // >= this % = scaled 0-10 (default: 20.0)
    double breakthrough_disqualify_threshold;    // > this % = disqualified (default: 40.0)
    double breakthrough_high_penalty_threshold;  // > this % = -15 points (default: 30.0)
    double breakthrough_low_penalty_threshold;   // > this % = -8 points (default: 20.0)
    int touch_count_exhausted_threshold;         // > this = -15 points (default: 100)
    int touch_count_high_threshold;              // > this = -10 points (default: 70)
    int touch_count_moderate_threshold;          // > this = -5 points (default: 50)
    int touch_count_untested_threshold;          // < this = -5 points (default: 5)
    int elite_full_bonus_age_days;               // <= this = 10 points (default: 90)
    int elite_half_bonus_age_days;               // <= this = 5 points (default: 180)
    int age_fresh_threshold_days;                // <= this = full age score (default: 30)
    int age_moderate_threshold_days;             // <= this = decay phase 1 (default: 90)
    int age_old_threshold_days;                  // <= this = decay phase 2 (default: 180)

    // Stage 2: Entry validation thresholds
    double entry_validation_minimum_score;
    int entry_validation_ema_fast_period;
    int entry_validation_ema_slow_period;
    double entry_validation_strong_trend_sep;
    double entry_validation_moderate_trend_sep;
    double entry_validation_weak_trend_sep;
    double entry_validation_ranging_threshold;
    double entry_validation_rsi_deeply_oversold;
    double entry_validation_rsi_oversold;
    double entry_validation_rsi_slightly_oversold;
    double entry_validation_rsi_pullback;
    double entry_validation_rsi_neutral;
    double entry_validation_macd_strong_threshold;
    double entry_validation_macd_moderate_threshold;
    double entry_validation_adx_very_strong;
    double entry_validation_adx_strong;
    double entry_validation_adx_moderate;
    double entry_validation_adx_weak;
    double entry_validation_adx_minimum;
    double entry_validation_optimal_stop_atr_min;
    double entry_validation_optimal_stop_atr_max;
    double entry_validation_acceptable_stop_atr_min;
    double entry_validation_acceptable_stop_atr_max;
    
    // HTF Analysis
    bool use_htf_regime_filter;
    int htf_lookback_bars;
    double htf_trending_threshold;
    
    // Elite Detection
    double min_departure_imbalance;
    double min_departure_distance_atr;  // NEW: Minimum departure distance
    double max_retest_speed_atr;
    int min_bars_to_retest;
    
    // Market Structure
    bool use_bos_trend_filter;
    int swing_detection_bars;
    bool trade_with_trend_only;
    bool allow_ranging_trades;
    
    // EMA Filter
    //bool use_ema_trend_filter;
    //int ema_fast_period;
    //int ema_slow_period;
    //double ema_ranging_threshold_pct;

    // EMA Exit (LIVE)
    bool enable_ema_exit;
    int exit_ema_fast_period;
    int exit_ema_slow_period;
    
    // Entry Logic
    bool entry_at_proximal;
    double rejection_wick_ratio;
    double entry_buffer_pct;

    // ⭐ Live Fill Quality Guards
    // min_zone_penetration_pct: minimum % of zone height price must have entered
    //   before a live market order is placed.
    //   SUPPLY: LTP must be >= proximal + pct × height  (risen into zone)
    //   DEMAND: LTP must be <= proximal - pct × height  (fallen into zone)
    //   Prevents filling at the very edge of the zone on the first tick.
    //   Recommended: 0.15-0.30 (15-30% into zone). Default: 0.20
    double min_zone_penetration_pct;
    // require_price_at_entry: additionally require LTP to have reached
    //   decision.entry_price before firing the market order.
    //   SUPPLY: LTP >= decision.entry_price (already risen to intended short level)
    //   DEMAND: LTP <= decision.entry_price (already fallen to intended long level)
    bool require_price_at_entry_level;
    
    // Risk Management
    //double sl_buffer_zone_pct;
    //double sl_buffer_atr;
    double min_stop_distance_points = 0.0;  // ⭐ NEW
    double risk_reward_ratio;
    bool use_breakeven_stop_loss;           // Move stop to entry price (breakeven) and original SL to next level
    
    // Trailing Stop
    bool use_trailing_stop;
    double trail_activation_rr;
    int trail_ema_period;
    double trail_ema_buffer_pct;
    double trail_atr_multiplier;
    bool trail_use_hybrid;
    double trail_fallback_tp_rr;
    double trailing_stop_activation_r = 0.0;
    
    // Trade Management
    int max_consecutive_losses;
    int pause_bars_after_losses;
    int max_concurrent_trades;
    int max_retries_per_zone;               // Max times to retry same zone after loss (default: 2)
    bool enable_per_zone_retry_limit;       // Enable retry counter per zone
    int lookback_for_zones;
    
    // Session Management
    bool close_eod;
    std::string session_end_time;
    int close_before_session_end_minutes;  // ⭐ NEW: Close positions N minutes before session end (0 = disabled)
    
    // Position Sizing
    int lot_size;
    double commission_per_trade;
    
    // ============================================================
    // V6.0: VOLUME & OI INTEGRATION
    // ============================================================
    
    // Volume Entry Filters
    bool enable_volume_entry_filters;      // Enable volume-based entry filtering
    double min_entry_volume_ratio;         // Minimum normalized volume ratio (e.g., 0.8x)
    double institutional_volume_threshold; // Volume ratio indicating institutional activity (e.g., 2.0x)
    
    // OI Entry Filters
    bool enable_oi_entry_filters;          // Enable OI-based entry filtering
    bool enable_market_phase_detection;    // Enable market phase-based filtering
    
    // Scoring Weights
    double base_score_weight;              // Weight for traditional score (default: 0.50)
    double volume_score_weight;            // Weight for volume score (default: 0.30)
    double oi_score_weight;                // Weight for OI score (default: 0.20)
    
    // Dynamic Position Sizing
    double low_volume_size_multiplier;     // Size multiplier for low volume (default: 0.5)
    double high_institutional_size_mult;   // Size multiplier for high institutional participation (default: 1.5)
    int max_lot_size;                      // Maximum allowed lot size
    
    // Volume Exit Signals
    bool enable_volume_exit_signals;       // Enable volume-based exit signals
    double volume_climax_exit_threshold;   // Volume ratio for climax exit (e.g., 3.0x)
    
    // OI Exit Signals
    bool enable_oi_exit_signals;           // Enable OI-based exit signals
    double oi_unwinding_threshold;         // OI change % for unwinding exit (e.g., -0.01 = -1%)
    double oi_reversal_threshold;          // OI change % for reversal detection (e.g., 0.02 = 2%)
    double oi_stagnation_threshold;        // OI change % for stagnation detection (e.g., 0.005 = 0.5%)
    int oi_stagnation_bar_count;           // Number of bars for OI stagnation
    
    // Volume Analysis Parameters
    int volume_lookback_period;            // Lookback period for volume analysis (default: 20)
    double extreme_volume_threshold;       // Threshold for extreme volume (e.g., 3.0 = 300%)
    std::string volume_normalization_method; // Volume normalization method ("time_of_day", "moving_average", etc.)
    std::string volume_baseline_file;      // Path to volume baseline file
    
    // OI Analysis Parameters
    double min_oi_change_threshold;        // Minimum OI change for significance (e.g., 0.01 = 1%)
    double high_oi_buildup_threshold;      // Threshold for high OI buildup (e.g., 0.05 = 5%)
    int oi_lookback_period;                // Lookback period for OI analysis (default: 10)
    double price_oi_correlation_window;    // Price-OI correlation window (default: 10.0)
    
    // Scoring Bonuses
    double institutional_volume_bonus;     // Bonus for institutional volume (default: 30.0)
    double oi_alignment_bonus;             // Bonus for OI alignment (default: 25.0)
    double low_volume_retest_bonus;        // Bonus for low volume retest (default: 20.0)
    
    // Expiry Day Handling
    bool trade_on_expiry_day;              // Allow trading on expiry day
    bool expiry_day_disable_oi_filters;    // Disable OI filters on expiry day
    double expiry_day_position_multiplier; // Position size multiplier on expiry day (e.g., 0.5)
    double expiry_day_min_zone_score;      // Minimum zone score on expiry day (e.g., 75.0)
    
    // Volume Exit Signals (extended)
    double volume_drying_up_threshold;     // Threshold for volume drying up (e.g., 0.5 = 50%)
    int volume_drying_up_bar_count;        // Number of bars for volume drying up (default: 3)
    bool enable_volume_divergence_exit;    // Enable volume divergence exit
    
    // Feature Flags
    bool v6_fully_enabled;                 // Master V6.0 toggle
    bool v6_log_volume_filters;            // Log volume filter decisions
    bool v6_log_oi_filters;                // Log OI filter decisions
    bool v6_log_institutional_index;       // Log institutional index calculations
    bool v6_validate_baseline_on_startup;  // Validate volume baseline on startup
    
    // ============================================================
    
    // Logging
    bool enable_debug_logging;
    bool enable_score_logging;

    // Live Trade Journal Export
    int live_export_every_n_bars;       // Export CSV after every N bars (0 = only on trade close)

    // State History
    bool enable_state_history;
    int max_state_history_events;
    bool record_zone_creation;
    bool record_first_touch;
    bool record_retests;
    bool record_violations;
    
    // Live Zone Filtering (NEW: dynamic range-based zone filtering)
    bool enable_live_zone_filtering;       // Master toggle for range-based filtering
    double live_zone_range_pct;            // +/- percentage from LTP (e.g., 5.0 for +/-5%)
    int live_zone_refresh_interval_minutes; // Refresh interval for range recalc (e.g., 30 min)
    
    // Entry Filters (NEW: zone distance from current price)
    double max_zone_distance_atr;          // Multiplier for ATR to determine max zone distance from price (e.g., 10 or 15)
    
    // Zone Detection (LIVE: periodic zone detection to maintain fresh pipeline)
    int live_zone_detection_interval_bars; // Run zone detection every N bars (0 = bootstrap only, recommended: 20-30)
    
    // LIVE Zone Detection: Relaxed Thresholds (NEW - FEB 2026)
    bool use_relaxed_live_detection;       // Use more aggressive detection in live mode
    double live_min_zone_width_atr;        // Relaxed width for live (e.g., 0.9 vs 1.3)
    double live_min_zone_strength;         // Relaxed strength for live (e.g., 40 vs 50)

    // LIVE Entry Gating (NEW)
    bool live_entry_require_new_bar;       // Only evaluate entries on new bar
    bool live_skip_when_in_position;       // Skip entry checks if already in position
    int live_zone_entry_cooldown_seconds;  // Per-zone cooldown after an entry attempt

    // Candle Confirmation (LIVE)
    bool enable_candle_confirmation;
    double candle_confirmation_body_pct;

    // Structure Confirmation (LIVE)
    bool enable_structure_confirmation;
    
    // Zone Bootstrap (NEW: startup zone generation with cache management)
    bool zone_bootstrap_enabled;           // Enable bootstrap zone generation at startup
    int zone_bootstrap_ttl_hours;          // TTL for cached zones in hours (e.g., 24)
    std::string zone_bootstrap_refresh_time; // Daily refresh time in HH:MM format (e.g., "08:50")
    bool zone_bootstrap_force_regenerate;  // Force regeneration even if cache valid
    
    // DryRun 2-Phase Testing (NEW: walk-forward testing capability)
    int dryrun_bootstrap_bars;             // Number of bars for bootstrap phase (0 = no bootstrap, -1 = auto)
    int dryrun_test_start_bar;             // Bar index to start testing from (0 = auto after bootstrap)
    bool dryrun_disable_trading_in_bootstrap; // Don't generate trades during bootstrap phase
    bool allow_large_bootstrap;            // Allow bootstrap > 30000 bars (replaces SD_ALLOW_LARGE_BOOTSTRAP env var)
    // ========== Stop Loss Configuration ==========
    double sl_buffer_zone_pct = 0.1;             // SL buffer as % of zone width
    double sl_buffer_atr = 0.1;                  // SL buffer in ATR
    // ⭐ FIX: Max fill slippage — reject trade if fill is too far from intended entry.
    // When bar.open gaps 30-93pts beyond the entry zone, the trade geometry is violated:
    // SL ends up inside the zone buffer, R:R collapses, win rate drops to 38%.
    // These gap-filled trades collectively produce 0.99× PF (breakeven) vs 3.40×
    // for clean fills. Rejecting them costs ₹0 in total P&L but massively
    // improves backtest quality and live predictability.
    // Set to 0 to disable (always tighten, old behaviour). Default: 25pts.
    double max_fill_slippage_pts = 25.0;         // Max gap from intended entry before rejecting
    
    // ⭐ FIX #1: MAX LOSS CAP PER TRADE
    // Root Cause: 81 trades (33%) = ₹268,030 loss (81% of total losses)
    // Impact: Without these 81 trades → +₹241,825 profit!
    double max_loss_per_trade = 1500.0;          // ⭐ NEW: Hard cap at ₹1,500 per trade
    // ========== EMA Trend Filter ========== (around line 422-426)
    bool use_ema_trend_filter = false;           // Use EMA trend filter
    int ema_fast_period = 9;                     // EMA fast period
    int ema_slow_period = 21;                    // EMA slow period
    double ema_ranging_threshold_pct = 1.0;      // EMA ranging threshold %
    
    // ⭐ FIX #3: LONG DIRECTION FILTER (EMA Trend Alignment)
    // Root Cause: 75 of 81 large losses are LONG (92.6%)
    // Impact: LONG: -₹46,151 vs SHORT: +₹19,946
    bool require_ema_alignment_for_longs = true; // ⭐ NEW: Require 20>50 for LONG
    double ema_min_separation_pct_long = 0.1; // Require minimum EMA separation for LONG (percent)
    double ema_min_separation_pct_short = 0.1; // Require minimum EMA separation for SHORT (percent)
    bool require_ema_alignment_for_shorts = false; // Keep SHORT flexible

    // Indicator periods for trade logging
    int rsi_period = 14;
    int bb_period = 20;
    double bb_stddev = 2.0;
    int adx_period = 14;
    int macd_fast_period = 12;
    int macd_slow_period = 26;
    int macd_signal_period = 9;
    // ========== Zone Age Filtering ========== (add after zone detection params)
    // ⭐ FIX #4: BLOCK TOXIC ZONE AGE RANGE (30-60 days)
    // Root Cause: 53 trades in 30-60d zones = -₹32,318 loss
    //   - 30-45d zones: 44.4% WR, -₹442/trade
    //   - 45-60d zones: 25% WR, -₹1,554/trade
    // Fresh zones work: <7d: 66.7% WR, +₹950/trade
    int min_zone_age_days = 0;                   // ⭐ NEW: Minimum zone age (0 = no min)
    int max_zone_age_days = 30;                  // ⭐ NEW: Maximum zone age (30 days)
                                                 // Blocks toxic 30-60d range
    bool exclude_zone_age_range = true;          // ⭐ NEW: Enable age range exclusion
    int exclude_zone_age_start = 30;             // ⭐ NEW: Start of exclusion (30 days)
    int exclude_zone_age_end = 60;               // ⭐ NEW: End of exclusion (60 days)

    int zone_max_age_days = 90;
	int zone_max_touch_count = 50;
	
	// ============================================================
    // VOLUME EXHAUSTION EXIT SETTINGS
    // ============================================================
    bool enable_volume_exhaustion_exit = true;
    double vol_exhaustion_spike_min_ratio = 1.8;
    double vol_exhaustion_spike_min_body_atr = 0.5;
    double vol_exhaustion_absorption_min_ratio = 2.0;
    double vol_exhaustion_absorption_max_body_atr = 0.25;
    double vol_exhaustion_flow_min_ratio = 1.2;
    int vol_exhaustion_flow_min_bars = 3;
    double vol_exhaustion_drift_max_ratio = 0.6;
    double vol_exhaustion_drift_min_loss_atr = 0.5;
    double vol_exhaustion_max_loss_pct = 0.70;
	
	
	
    // Constructor with defaults

    Config()
        : enable_entry_time_block(false),
          starting_capital(100000),
          risk_per_trade_pct(1.0),
          base_height_max_atr(1.5),
          consolidation_min_bars(1),
          consolidation_max_bars(10),
          max_consolidation_range(0.3),
          min_zone_width_atr(0.3),
          min_impulse_atr(1.2),
          atr_period(14),
          min_zone_strength(60.0),
          only_fresh_zones(true),
          invalidate_on_body_close(true),
          gap_over_invalidation(false),
          skip_retest_after_gap_over(true),
          duplicate_prevention_enabled(true),
          zone_existence_tolerance_atr(0.3),
          atr_buffer_multiplier(1.0),
          check_existing_before_create(true),
          merge_strategy(ZoneMergeStrategy::UPDATE_IF_STRONGER),
          strength_improvement_threshold(5.0),
          freshness_bars_threshold(50),
          zone_detection_interval_bars(10),
          enable_sweep_reclaim(false),
          reclaim_acceptance_bars(2),
          reclaim_acceptance_pct(0.5),
          enable_zone_flip(false),
          flip_min_impulse_atr(1.5),
          flip_lookback_bars(10),
          use_adaptive_entry(true),
          enable_two_stage_scoring(false),
          zone_quality_minimum_score(70.0),
          zone_quality_age_very_fresh(2),
          zone_quality_age_fresh(5),
          zone_quality_age_recent(10),
          zone_quality_age_aging(20),
          zone_quality_age_old(30),
          zone_quality_height_optimal_min(0.3),
          zone_quality_height_optimal_max(0.7),
          zone_quality_height_acceptable_min(0.2),
          zone_quality_height_acceptable_max(1.0),
          // NEW: Revamped scoring defaults (from Feb 2026 analysis)
          enable_revamped_scoring(true),
          rejection_analysis_lookback_days(30),
          breakthrough_analysis_lookback_days(30),
          rejection_excellent_threshold(60.0),
          rejection_good_threshold(40.0),
          rejection_acceptable_threshold(20.0),
          breakthrough_disqualify_threshold(40.0),
          breakthrough_high_penalty_threshold(30.0),
          breakthrough_low_penalty_threshold(20.0),
          touch_count_exhausted_threshold(100),
          touch_count_high_threshold(70),
          touch_count_moderate_threshold(50),
          touch_count_untested_threshold(5),
          elite_full_bonus_age_days(90),
          elite_half_bonus_age_days(180),
          age_fresh_threshold_days(30),
          age_moderate_threshold_days(90),
          age_old_threshold_days(180),
          entry_validation_minimum_score(65.0),
          entry_validation_ema_fast_period(50),
          entry_validation_ema_slow_period(200),
          entry_validation_strong_trend_sep(1.0),
          entry_validation_moderate_trend_sep(0.5),
          entry_validation_weak_trend_sep(0.2),
          entry_validation_ranging_threshold(0.3),
          entry_validation_rsi_deeply_oversold(30.0),
          entry_validation_rsi_oversold(35.0),
          entry_validation_rsi_slightly_oversold(40.0),
          entry_validation_rsi_pullback(45.0),
          entry_validation_rsi_neutral(50.0),
          entry_validation_macd_strong_threshold(2.0),
          entry_validation_macd_moderate_threshold(1.0),
          entry_validation_adx_very_strong(50.0),
          entry_validation_adx_strong(40.0),
          entry_validation_adx_moderate(30.0),
          entry_validation_adx_weak(25.0),
          entry_validation_adx_minimum(20.0),
          entry_validation_optimal_stop_atr_min(1.5),
          entry_validation_optimal_stop_atr_max(2.5),
          max_entry_volume_ratio(3.0),
          max_volume_without_elite(2.0),
          min_inst_for_high_volume(60.0),
          optimal_volume_min(1.0),
          optimal_volume_max(2.0),
          optimal_institutional_min(45.0),
          allow_low_volume_if_score_above(65.0),
          elite_institutional_threshold(70.0),
          entry_validation_acceptable_stop_atr_min(1.2),
          entry_validation_acceptable_stop_atr_max(3.0),
          use_htf_regime_filter(true),
          htf_lookback_bars(400),
          htf_trending_threshold(5.0),
          min_departure_imbalance(3.0),
          min_departure_distance_atr(1.5),
          max_retest_speed_atr(0.5),
          min_bars_to_retest(10),
          use_bos_trend_filter(false),
          swing_detection_bars(20),
          trade_with_trend_only(true),
          allow_ranging_trades(true),
          use_ema_trend_filter(true),
          ema_fast_period(20),
          ema_slow_period(50),
          ema_ranging_threshold_pct(0.5),
          rsi_period(14),
          bb_period(20),
          bb_stddev(2.0),
          adx_period(14),
          macd_fast_period(12),
          macd_slow_period(26),
          macd_signal_period(9),
          entry_at_proximal(false),
          rejection_wick_ratio(0.4),
          entry_buffer_pct(0.1),
          min_zone_penetration_pct(0.20),
          require_price_at_entry_level(true),
          sl_buffer_zone_pct(10.0),
          sl_buffer_atr(0.5),
          risk_reward_ratio(2.0),
          use_breakeven_stop_loss(false),
          use_trailing_stop(true),
          trail_activation_rr(1.5),
          trail_ema_period(20),
          trail_ema_buffer_pct(0.2),
          trail_atr_multiplier(2.0),
          trail_use_hybrid(true),
          trail_fallback_tp_rr(5.0),
          max_consecutive_losses(4),
          pause_bars_after_losses(80),
          max_concurrent_trades(1),
          max_retries_per_zone(2),
          enable_per_zone_retry_limit(true),
          lookback_for_zones(100),
          close_eod(true),
          session_end_time("15:25:00"),
          close_before_session_end_minutes(60),  // Default: close 60min before session end
          lot_size(50),
          commission_per_trade(20.0),
          enable_debug_logging(true),
          enable_score_logging(true),
          live_export_every_n_bars(60),
          enable_state_history(true),
          max_state_history_events(100),
          record_zone_creation(true),
          record_first_touch(true),
          record_retests(true),
          record_violations(true),
          enable_live_zone_filtering(true),
          live_zone_range_pct(5.0),
          live_zone_refresh_interval_minutes(30),
          max_zone_distance_atr(10.0),
          live_zone_detection_interval_bars(25),
          use_relaxed_live_detection(false),
          live_min_zone_width_atr(0.9),
          live_min_zone_strength(40.0),
          live_zone_detection_lookback_bars(200),
          live_entry_require_new_bar(true),
          live_skip_when_in_position(true),
          live_zone_entry_cooldown_seconds(60),
          zone_bootstrap_enabled(true),
          zone_bootstrap_ttl_hours(24),
          zone_bootstrap_refresh_time("08:50"),
          zone_bootstrap_force_regenerate(false),
          dryrun_bootstrap_bars(-1),
          dryrun_test_start_bar(0),
          dryrun_disable_trading_in_bootstrap(true),
          allow_large_bootstrap(false),
          // V6.0: Volume & OI Integration defaults
          enable_volume_entry_filters(true),
          min_entry_volume_ratio(0.8),
          institutional_volume_threshold(2.0),
          enable_oi_entry_filters(true),
          enable_market_phase_detection(true),
          base_score_weight(0.50),
          volume_score_weight(0.30),
          oi_score_weight(0.20),
          low_volume_size_multiplier(0.5),
          high_institutional_size_mult(1.5),
          max_lot_size(100),
          enable_volume_exit_signals(false),  // Default OFF — enable explicitly in config file
          volume_climax_exit_threshold(3.0),
          enable_oi_exit_signals(true),
          oi_unwinding_threshold(-0.01),
          oi_reversal_threshold(0.02),
          oi_stagnation_threshold(0.005),
          oi_stagnation_bar_count(10),
          volume_lookback_period(20),
          extreme_volume_threshold(3.0),
          volume_normalization_method("time_of_day"),
          volume_baseline_file("data/baselines/volume_baseline.json"),
          min_oi_change_threshold(0.01),
          high_oi_buildup_threshold(0.05),
          oi_lookback_period(10),
          price_oi_correlation_window(10.0),
          institutional_volume_bonus(30.0),
          oi_alignment_bonus(25.0),
          low_volume_retest_bonus(20.0),
          trade_on_expiry_day(true),
          expiry_day_disable_oi_filters(true),
          expiry_day_position_multiplier(0.5),
          expiry_day_min_zone_score(75.0),
          volume_drying_up_threshold(0.5),
          volume_drying_up_bar_count(3),
          enable_volume_divergence_exit(true),
          v6_fully_enabled(true),
          v6_log_volume_filters(true),
          v6_log_oi_filters(true),
          v6_log_institutional_index(true),
          v6_validate_baseline_on_startup(true) {}
    
    // Validation method
    bool validate() const {
        if (starting_capital <= 0) {
            std::cerr << "❌ ERROR: starting_capital must be > 0" << std::endl;
            return false;
        }
        if (risk_per_trade_pct <= 0 || risk_per_trade_pct > 100) {
            std::cerr << "❌ ERROR: risk_per_trade_pct must be 0-100" << std::endl;
            return false;
        }
        if (atr_period <= 0) {
            std::cerr << "❌ ERROR: atr_period must be > 0" << std::endl;
            return false;
        }
        if (rsi_period <= 0 || bb_period <= 0 || adx_period <= 0) {
            std::cerr << "❌ ERROR: indicator periods must be > 0" << std::endl;
            return false;
        }
        if (bb_stddev <= 0) {
            std::cerr << "❌ ERROR: bb_stddev must be > 0" << std::endl;
            return false;
        }
        if (macd_fast_period <= 0 || macd_slow_period <= 0 || macd_signal_period <= 0) {
            std::cerr << "❌ ERROR: MACD periods must be > 0" << std::endl;
            return false;
        }
        if (macd_fast_period >= macd_slow_period) {
            std::cerr << "❌ ERROR: macd_fast_period must be < macd_slow_period" << std::endl;
            return false;
        }
        if (consolidation_min_bars < 1 || consolidation_min_bars > consolidation_max_bars) {
            std::cerr << "❌ ERROR: Invalid consolidation bars" << std::endl;
            return false;
        }
        if (min_zone_strength < 0 || min_zone_strength > 100) {
            std::cerr << "❌ ERROR: min_zone_strength must be 0-100" << std::endl;
            return false;
        }
        if (lot_size <= 0) {
            std::cerr << "❌ ERROR: lot_size must be > 0" << std::endl;
            return false;
        }
        if (enable_state_history && max_state_history_events <= 0) {
            std::cerr << "❌ ERROR: max_state_history_events must be > 0" << std::endl;
            return false;
        }
        if (use_adaptive_entry && !scoring.validate()) {
            return false;
        }
        return true;
    }
};

// ============================================================
// SYSTEM CONFIGURATION (JSON-based)
// ============================================================

/**
 * @struct SystemConfig
 * @brief System-level configuration for directories, broker, logging
 * 
 * This configuration is loaded from JSON file (system_config.json)
 * and provides bootstrap settings for:
 * - Directory structure
 * - Broker integration (Fyers)
 * - File paths
 * - Logging configuration
 * - Live trading settings
 */
struct SystemConfig {
    // System settings
    std::string root_directory;
    std::string environment;          // "test" or "production"
    std::string log_level;            // "DEBUG", "INFO", "WARN", "ERROR"
    
    // Directories
    std::string conf_dir;
    std::string data_dir;
    std::string logs_dir;
    std::string scripts_dir;
    std::string backtest_results_dir;
    std::string live_trade_logs_dir;
    
    // File paths
    std::string strategy_config_file;
    std::string fyers_token_file;
    std::string fyers_config_file;
    std::string live_data_csv;
    std::string historical_data_cache;
    std::string bridge_status_file;
    std::string python_log_file;
    std::string cpp_log_file;
    std::string trade_log_file;
    
    // Fyers broker settings
    std::string fyers_client_id;
    std::string fyers_secret_key;
    std::string fyers_redirect_uri;
    std::string fyers_environment;    // "test" or "production"
    std::string fyers_api_base;
    std::string fyers_ws_base;
    
    // Trading settings
    std::string trading_symbol;
    std::string trading_resolution;   // "1" for 1-minute bars
    std::string trading_mode;         // "paper" or "live"
    double trading_starting_capital;
    int trading_lot_size;
    
    // Bridge settings
    std::string bridge_mode;          // "file" or "websocket"
    int bridge_update_interval_seconds;
    int bridge_max_rows;
    int bridge_historical_lookback_days;
    
    // Strategy settings (basic)
    int strategy_lookback_bars;
    double strategy_min_zone_strength;
    int strategy_max_concurrent_trades;
    double strategy_risk_per_trade_pct;
    
    // Live Zone Filtering (NEW)
    bool live_zone_filtering_enabled;
    double live_zone_range_pct;
    int live_zone_refresh_interval_minutes;
    
    // Entry Filters (NEW)
    double max_zone_distance_atr;      // Multiplier for ATR to determine max zone distance from price
    int live_zone_detection_interval_bars;  // Zone detection: periodic detection interval (0 = bootstrap only)
    
    // Zone Bootstrap (NEW)
    bool zone_bootstrap_enabled;
    int zone_bootstrap_ttl_hours;
    std::string zone_bootstrap_refresh_time;
    bool zone_bootstrap_force_regenerate;
    
    // Constructor with defaults
    SystemConfig()
        : root_directory("."),
          environment("test"),
          log_level("INFO"),
          conf_dir("./conf"),
          data_dir("./data"),
          logs_dir("./logs"),
          scripts_dir("./scripts"),
          backtest_results_dir("./results/backtest"),
          live_trade_logs_dir("./results/live_trades"),
          strategy_config_file("conf/phase1_enhanced_v3_1_config.txt"),
          fyers_token_file("scripts/fyers_token.txt"),
          fyers_config_file("scripts/fyers_config.json"),
          live_data_csv("data/live_data.csv"),
          historical_data_cache("data/cache/historical"),
          bridge_status_file("data/bridge_status.json"),
          python_log_file("logs/fyers_bridge.log"),
          cpp_log_file("logs/sd_engine.log"),
          trade_log_file("results/live_trades/trades.csv"),
          fyers_client_id(""),
          fyers_secret_key(""),
          fyers_redirect_uri("http://127.0.0.1:5000/callback"),
          fyers_environment("test"),
          fyers_api_base("https://api-t1.fyers.in"),
          fyers_ws_base("wss://api-t1.fyers.in/socket/v3/dataSock"),
          trading_symbol("NSE:NIFTY25DECFUT"),
          trading_resolution("1"),
          trading_mode("paper"),
          trading_starting_capital(300000),
          trading_lot_size(75),
          bridge_mode("file"),
          bridge_update_interval_seconds(5),
          bridge_max_rows(1000),
          bridge_historical_lookback_days(30),
          strategy_lookback_bars(100),
          strategy_min_zone_strength(60.0),
          strategy_max_concurrent_trades(1),
          strategy_risk_per_trade_pct(1.0),
          live_zone_filtering_enabled(true),
          live_zone_range_pct(5.0),
          live_zone_refresh_interval_minutes(30),
          max_zone_distance_atr(10.0),
          live_zone_detection_interval_bars(25),
          zone_bootstrap_enabled(true),
          zone_bootstrap_ttl_hours(24),
          zone_bootstrap_refresh_time("08:50"),
          zone_bootstrap_force_regenerate(false) {}
    
    /**
     * Get full path by resolving relative to root_directory
     */
    std::string get_full_path(const std::string& relative_path) const {
        if (relative_path.empty()) return root_directory;
        if (relative_path[0] == '/' || relative_path[0] == '\\') return relative_path;
        if (relative_path.length() >= 2 && relative_path[1] == ':') return relative_path;  // Windows absolute
        return root_directory + "/" + relative_path;
    }
};

// ============================================================
// VOLUME & OI CONFIGURATION (NEW V6.0)
// ============================================================

struct VolumeOIConfig {
    // Volume Configuration
    bool enable_volume_entry_filters;
    bool enable_volume_exit_signals;
    double min_entry_volume_ratio;           // Default: 0.8
    double institutional_volume_threshold;   // Default: 2.0
    double extreme_volume_threshold;         // Default: 3.0
    int volume_lookback_period;              // Default: 20
    std::string volume_normalization_method; // "time_of_day" or "session_avg"
    std::string volume_baseline_file;        // Path to baseline JSON
    
    // OI Configuration
    bool enable_oi_entry_filters;
    bool enable_oi_exit_signals;
    bool enable_market_phase_detection;
    double min_oi_change_threshold;          // Default: 0.01 (1%)
    double high_oi_buildup_threshold;        // Default: 0.05 (5%)
    int oi_lookback_period;                  // Default: 10 bars
    double price_oi_correlation_window;      // Default: 10 bars
    
    // Scoring Weights
    double base_score_weight;                // Default: 0.50
    double volume_score_weight;              // Default: 0.30
    double oi_score_weight;                  // Default: 0.20
    
    // Institutional Index Bonuses
    double institutional_volume_bonus;       // Default: 30 points
    double oi_alignment_bonus;               // Default: 25 points
    double low_volume_retest_bonus;          // Default: 20 points
    
    // Expiry Day Handling
    bool trade_on_expiry_day;                // Default: true
    bool expiry_day_disable_oi_filters;      // Default: true
    double expiry_day_position_multiplier;   // Default: 0.5
    double expiry_day_min_zone_score;        // Default: 75.0
    
    // Position Sizing
    double low_volume_size_multiplier;       // Default: 0.5
    double high_institutional_size_mult;     // Default: 1.5
    int max_lot_size;                        // Safety cap
    
    // Volume Exit Signals
    double volume_climax_exit_threshold;     // Default: 3.0
    double volume_drying_up_threshold;       // Default: 0.5
    int volume_drying_up_bar_count;          // Default: 3
    bool enable_volume_divergence_exit;      // Default: true
    
    // OI Exit Signals
    double oi_unwinding_threshold;           // Default: -0.01 (-1%)
    double oi_reversal_threshold;            // Default: 0.02 (2%)
    double oi_stagnation_threshold;          // Default: 0.005 (0.5%)
    int oi_stagnation_bar_count;             // Default: 10
    
    VolumeOIConfig()
        : enable_volume_entry_filters(true),
          enable_volume_exit_signals(false),  // Default OFF — enable explicitly in config file
          min_entry_volume_ratio(0.8),
          institutional_volume_threshold(2.0),
          extreme_volume_threshold(3.0),
          volume_lookback_period(20),
          volume_normalization_method("time_of_day"),
          volume_baseline_file("data/baselines/volume_baseline.json"),
          enable_oi_entry_filters(true),
          enable_oi_exit_signals(true),
          enable_market_phase_detection(true),
          min_oi_change_threshold(0.01),
          high_oi_buildup_threshold(0.05),
          oi_lookback_period(10),
          price_oi_correlation_window(10),
          base_score_weight(0.50),
          volume_score_weight(0.30),
          oi_score_weight(0.20),
          institutional_volume_bonus(30.0),
          oi_alignment_bonus(25.0),
          low_volume_retest_bonus(20.0),
          trade_on_expiry_day(true),
          expiry_day_disable_oi_filters(true),
          expiry_day_position_multiplier(0.5),
          expiry_day_min_zone_score(75.0),
          low_volume_size_multiplier(0.5),
          high_institutional_size_mult(1.5),
          max_lot_size(150),
          volume_climax_exit_threshold(3.0),
          volume_drying_up_threshold(0.5),
          volume_drying_up_bar_count(3),
          enable_volume_divergence_exit(true),
          oi_unwinding_threshold(-0.01),
          oi_reversal_threshold(0.02),
          oi_stagnation_threshold(0.005),
          oi_stagnation_bar_count(10) {}
};

} // namespace Core
} // namespace SDTrading

#endif // SDTRADING_COMMON_TYPES_H