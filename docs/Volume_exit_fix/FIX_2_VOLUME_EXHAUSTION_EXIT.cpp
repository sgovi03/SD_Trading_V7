// ============================================================================
// FIX #2: VOLUME EXHAUSTION EXIT - REFINED IMPLEMENTATION
// ============================================================================
// This implements volume-based early exits to cut losses BEFORE hitting
// full stop loss, mirroring the successful VOL_CLIMAX logic for profits.
//
// PROBLEM:
//   - All SL exits are losers (100%)
//   - Average SL loss: ₹8,000
//   - Total SL damage: -₹122K to -₹215K
//   - Stops placed on price, not volume/structure
//
// SOLUTION:
//   Detect volume signals that indicate position is failing
//   Exit early at smaller loss (₹3K-5K instead of ₹8K)
//   Expected: Cut SL damage by 40-55%
// ============================================================================

#include "common_types.h"
#include "../utils/volume_baseline.h"
#include <vector>
#include <cmath>

namespace SDTrading {
namespace Core {

/**
 * @struct VolumeExhaustionSignal
 * @brief Signal indicating position should exit due to volume patterns
 */
struct VolumeExhaustionSignal {
    bool should_exit = false;
    std::string reason;
    double confidence = 0.0;  // 0.0 to 1.0
    std::string description;
};

/**
 * @class VolumeExhaustionDetector
 * @brief Detects volume-based signals to exit losing positions early
 * 
 * Uses institutional volume patterns to identify when a trade is failing:
 * 1. Against-trend volume spike (high volume move against us)
 * 2. Absorption pattern (high volume but no price movement)
 * 3. Cumulative volume flow reversal (multiple bars against us)
 * 4. Low volume drift (weak institutional support)
 */
class VolumeExhaustionDetector {
private:
    const Config& config_;
    const Utils::VolumeBaseline& volume_baseline_;
    
    /**
     * Get normalized volume ratio for a bar
     */
    double get_volume_ratio(const Bar& bar) const {
        std::string time_slot = extract_time_slot(bar.datetime);
        double baseline = volume_baseline_.get_baseline_volume(time_slot);
        
        if (baseline < 1.0) baseline = 1.0;  // Avoid division by zero
        
        return bar.volume / baseline;
    }
    
    /**
     * Extract time slot from datetime string
     * Format: "YYYY-MM-DD HH:MM:SS" → "HH:MM"
     */
    std::string extract_time_slot(const std::string& datetime) const {
        if (datetime.length() < 16) return "09:15";
        return datetime.substr(11, 5);  // Extract "HH:MM"
    }
    
    /**
     * Check if bar moved against our position
     */
    bool is_against_position(const Bar& bar, bool is_long) const {
        if (is_long) {
            // Long position: red candle is against us
            return bar.close < bar.open;
        } else {
            // Short position: green candle is against us
            return bar.close > bar.open;
        }
    }
    
    /**
     * Calculate current unrealized loss in points
     */
    double calculate_loss_points(const Trade& trade, const Bar& current_bar) const {
        if (trade.direction == "LONG") {
            return std::max(0.0, trade.entry_price - current_bar.close);
        } else {
            return std::max(0.0, current_bar.close - trade.entry_price);
        }
    }

public:
    VolumeExhaustionDetector(const Config& cfg, const Utils::VolumeBaseline& baseline)
        : config_(cfg), volume_baseline_(baseline) {}
    
    /**
     * Check for volume exhaustion signals
     * Call this on EVERY bar while in a losing position
     * 
     * @param trade Current open trade
     * @param current_bar Current price bar
     * @param atr Current ATR value
     * @param recent_bars Last 5-10 bars for cumulative analysis (optional)
     * @return VolumeExhaustionSignal with exit recommendation
     */
    VolumeExhaustionSignal check_exhaustion(
        const Trade& trade,
        const Bar& current_bar,
        double atr,
        const std::vector<Bar>& recent_bars = {}
    ) const {
        VolumeExhaustionSignal signal;
        
        bool is_long = (trade.direction == "LONG");
        double loss_points = calculate_loss_points(trade, current_bar);
        
        // Only check if position is losing
        if (loss_points <= 0) {
            return signal;  // Position winning, don't check exhaustion
        }
        
        double volume_ratio = get_volume_ratio(current_bar);
        double candle_body = std::abs(current_bar.close - current_bar.open);
        double candle_range = current_bar.high - current_bar.low;
        bool is_against_us = is_against_position(current_bar, is_long);
        
        // ====================================================================
        // SIGNAL #1: AGAINST-TREND VOLUME SPIKE (Highest Priority)
        // ====================================================================
        // Large volume bar moving AGAINST our position
        // Example: LONG position + big red candle with 2x volume
        // This indicates institutional selling pressure
        
        if (volume_ratio >= config_.vol_exhaustion_spike_min_ratio &&  // Default: 1.8
            is_against_us &&
            candle_body > atr * config_.vol_exhaustion_spike_min_body_atr) {  // Default: 0.5
            
            signal.should_exit = true;
            signal.reason = "AGAINST_TREND_SPIKE";
            signal.confidence = std::min(1.0, volume_ratio / 2.5);
            signal.description = "High volume (" + std::to_string(volume_ratio) + 
                               "x) move against position";
            
            LOG_DEBUG("VOL_EXHAUSTION: Against-trend spike detected, vol=" + 
                     std::to_string(volume_ratio) + "x, body=" + 
                     std::to_string(candle_body) + " pts");
            
            return signal;
        }
        
        // ====================================================================
        // SIGNAL #2: ABSORPTION PATTERN
        // ====================================================================
        // High volume but minimal price movement = zone absorbing our direction
        // Indicates zone is broken and can't hold price
        
        if (volume_ratio >= config_.vol_exhaustion_absorption_min_ratio &&  // Default: 2.0
            candle_body < atr * config_.vol_exhaustion_absorption_max_body_atr) {  // Default: 0.25
            
            // Check if close is against us
            if (is_long ? (current_bar.close < trade.entry_price) 
                       : (current_bar.close > trade.entry_price)) {
                
                signal.should_exit = true;
                signal.reason = "ABSORPTION";
                signal.confidence = std::min(1.0, volume_ratio / 2.5);
                signal.description = "High volume (" + std::to_string(volume_ratio) + 
                                   "x) with tiny body - absorption detected";
                
                LOG_DEBUG("VOL_EXHAUSTION: Absorption detected, vol=" + 
                         std::to_string(volume_ratio) + "x, body=" + 
                         std::to_string(candle_body) + " pts (tiny)");
                
                return signal;
            }
        }
        
        // ====================================================================
        // SIGNAL #3: CUMULATIVE VOLUME FLOW REVERSAL
        // ====================================================================
        // If multiple recent bars show high volume against us, flow has reversed
        
        if (!recent_bars.empty() && recent_bars.size() >= 5) {
            int bars_against = 0;
            
            for (const auto& bar : recent_bars) {
                double vol_ratio = get_volume_ratio(bar);
                bool against = is_against_position(bar, is_long);
                
                if (vol_ratio > config_.vol_exhaustion_flow_min_ratio &&  // Default: 1.2
                    against) {
                    bars_against++;
                }
            }
            
            if (bars_against >= config_.vol_exhaustion_flow_min_bars) {  // Default: 3
                signal.should_exit = true;
                signal.reason = "FLOW_REVERSAL";
                signal.confidence = 0.7 + (bars_against - 3) * 0.1;
                signal.description = std::to_string(bars_against) + 
                                   " of last " + std::to_string(recent_bars.size()) + 
                                   " bars with volume against position";
                
                LOG_DEBUG("VOL_EXHAUSTION: Flow reversal detected, " + 
                         std::to_string(bars_against) + " bars against us");
                
                return signal;
            }
        }
        
        // ====================================================================
        // SIGNAL #4: LOW VOLUME DRIFT
        // ====================================================================
        // Price drifting against us on very low volume = no institutional support
        
        if (volume_ratio < config_.vol_exhaustion_drift_max_ratio &&  // Default: 0.6
            loss_points > atr * config_.vol_exhaustion_drift_min_loss_atr) {  // Default: 0.5
            
            signal.should_exit = true;
            signal.reason = "LOW_VOLUME_DRIFT";
            signal.confidence = 0.5;
            signal.description = "Low volume (" + std::to_string(volume_ratio) + 
                               "x) drift with loss=" + std::to_string(loss_points) + " pts";
            
            LOG_DEBUG("VOL_EXHAUSTION: Low volume drift, vol=" + 
                     std::to_string(volume_ratio) + "x, loss=" + 
                     std::to_string(loss_points) + " pts");
            
            return signal;
        }
        
        return signal;  // No exhaustion detected
    }
    
    /**
     * Determine if should exit based on exhaustion signal and loss ratio
     * Only exit early if loss is less than a threshold % of full SL
     * 
     * @param signal Volume exhaustion signal
     * @param trade Current trade
     * @param current_bar Current bar
     * @return true if should exit now
     */
    bool should_exit_now(
        const VolumeExhaustionSignal& signal,
        const Trade& trade,
        const Bar& current_bar
    ) const {
        if (!signal.should_exit) {
            return false;
        }
        
        // Calculate current loss and full SL distance
        double current_loss_points = calculate_loss_points(trade, current_bar);
        double sl_distance_points = std::abs(trade.entry_price - trade.stop_loss);
        
        // Calculate loss ratio (what % of full SL are we at?)
        double loss_ratio = current_loss_points / sl_distance_points;
        
        // Only exit early if loss is less than threshold
        // Default: 70% - if we're already >70% to SL, just let it hit
        if (loss_ratio < config_.vol_exhaustion_max_loss_pct) {
            LOG_INFO("VOL_EXHAUSTION: Exiting at " + 
                    std::to_string(loss_ratio * 100) + "% of SL distance (" +
                    std::to_string(current_loss_points) + "/" + 
                    std::to_string(sl_distance_points) + " pts)");
            return true;
        } else {
            LOG_DEBUG("VOL_EXHAUSTION: Signal detected but loss already " +
                     std::to_string(loss_ratio * 100) + "% of SL, using price SL");
            return false;
        }
    }
};

} // namespace Core
} // namespace SDTrading

// ============================================================================
// CONFIGURATION PARAMETERS
// ============================================================================

/*
Add these to your config file (phase_6_config_v0_6_ABSORPTION.txt):

# ============================================================
# VOLUME EXHAUSTION EXIT SETTINGS
# ============================================================
# Enable/disable volume exhaustion exits
enable_volume_exhaustion_exit = YES

# Against-trend volume spike detection
vol_exhaustion_spike_min_ratio = 1.8       # Minimum volume spike (vs baseline)
vol_exhaustion_spike_min_body_atr = 0.5    # Minimum candle body size (ATR multiple)

# Absorption pattern detection
vol_exhaustion_absorption_min_ratio = 2.0   # High volume for absorption
vol_exhaustion_absorption_max_body_atr = 0.25  # Max body for absorption

# Flow reversal detection
vol_exhaustion_flow_min_ratio = 1.2        # Elevated volume threshold
vol_exhaustion_flow_min_bars = 3           # Min bars against position

# Low volume drift detection
vol_exhaustion_drift_max_ratio = 0.6       # Low volume threshold
vol_exhaustion_drift_min_loss_atr = 0.5    # Min loss before triggering

# Exit threshold
vol_exhaustion_max_loss_pct = 0.70         # Only exit if loss < 70% of full SL
*/

// ============================================================================
// INTEGRATION INTO POSITION MANAGEMENT
// ============================================================================

/*
// In your manage_position() or trade management code:

#include "volume_exhaustion_detector.h"

ExitSignal manage_position_with_vol_exhaustion(
    Trade& trade,
    const Bar& current_bar,
    const VolumeBaseline& baseline,
    const std::vector<Bar>& recent_bars,
    const Config& config
) {
    ExitSignal exit_signal;
    exit_signal.should_exit = false;
    
    double atr = calculate_atr(current_bar, 14);
    
    // Create detector
    VolumeExhaustionDetector detector(config, baseline);
    
    // ========================================================================
    // STEP 1: Check VOL_CLIMAX (profit exit) - EXISTING
    // ========================================================================
    if (check_volume_climax(trade, current_bar, baseline)) {
        exit_signal.should_exit = true;
        exit_signal.reason = "VOL_CLIMAX";
        exit_signal.price = current_bar.close;
        return exit_signal;
    }
    
    // ========================================================================
    // STEP 2: ⭐ NEW - Check VOLUME EXHAUSTION (early loss exit)
    // ========================================================================
    if (config.enable_volume_exhaustion_exit) {
        VolumeExhaustionSignal vol_ex = detector.check_exhaustion(
            trade, current_bar, atr, recent_bars
        );
        
        if (detector.should_exit_now(vol_ex, trade, current_bar)) {
            exit_signal.should_exit = true;
            exit_signal.reason = "VOL_EXHAUSTION_" + vol_ex.reason;
            exit_signal.price = current_bar.close;
            
            LOG_INFO("VOL_EXHAUSTION exit: " + vol_ex.description +
                    " (confidence=" + std::to_string(vol_ex.confidence) + ")");
            
            return exit_signal;
        }
    }
    
    // ========================================================================
    // STEP 3: Check trailing stop - EXISTING
    // ========================================================================
    if (check_trailing_stop(trade, current_bar)) {
        exit_signal.should_exit = true;
        exit_signal.reason = "TRAIL_SL";
        exit_signal.price = trade.trailing_stop;
        return exit_signal;
    }
    
    // ========================================================================
    // STEP 4: Check price-based stop loss - EXISTING
    // ========================================================================
    if (check_stop_loss(trade, current_bar)) {
        exit_signal.should_exit = true;
        exit_signal.reason = "SL";
        exit_signal.price = trade.stop_loss;
        return exit_signal;
    }
    
    // ... rest of exit checks ...
    
    return exit_signal;
}
*/

// ============================================================================
// EXPECTED RESULTS AFTER IMPLEMENTATION
// ============================================================================

/*
BEFORE (Current):
  SL exits: 15 trades
  All losers: 15/15 (100%)
  Total damage: -₹122,223
  Avg loss: ₹8,148
  
AFTER (With Volume Exhaustion):
  SL exits: 6-8 trades (remaining)
  VOL_EXHAUSTION exits: 7-9 trades
  Avg VOL_EXHAUSTION loss: ₹3,500-5,000 (vs ₹8,148)
  Total damage: -₹50,000 to -₹70,000
  
  SAVINGS: ₹50,000 to ₹72,000
  
IMPACT ON P&L:
  Current: -₹38,628
  After: +₹10,000 to +₹35,000 (swings positive!)
  
Combined with VOL_CLIMAX disable:
  Target: +₹60,000 to +₹100,000 (20-33% returns)
*/
