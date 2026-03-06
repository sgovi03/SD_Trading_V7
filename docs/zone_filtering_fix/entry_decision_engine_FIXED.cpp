#include "entry_decision_engine.h"
#include <vector>
#include <algorithm>
#include "../utils/logger.h"
#include "../analysis/market_analyzer.h"
#include "zone_quality_scorer.h"
#include "entry_validation_scorer.h"
#include "../utils/volume_baseline.h"
#include "oi_scorer.h"


// All method definitions must be outside the namespace block, but use full namespace qualification

namespace SDTrading {
namespace Core {
// Empty block to indicate namespace for clarity, but do not define methods here
//} // namespace Core
//} // namespace SDTrading

using namespace SDTrading::Core;

EntryVolumeMetrics EntryDecisionEngine::calculate_entry_volume_metrics(
    const Zone& zone,
    const Bar& current_bar,
    const std::vector<Bar>& bar_history
) const {
    EntryVolumeMetrics metrics;
    metrics.volume_score = 0;

    if (!volume_baseline_ || !volume_baseline_->is_loaded()) return metrics;

    std::string slot = extract_time_slot(current_bar.datetime);
    double baseline  = volume_baseline_->get_baseline(slot);
    if (baseline <= 0) baseline = 1.0;

    // ── PULLBACK VOLUME: the bar currently hitting the zone ──────────────
    metrics.pullback_volume_ratio = current_bar.volume / baseline;

    // ⭐ FIX 1: Removed hardcoded early-exit (score=-50, return) for high pullback vol.
    //
    // The old block treated pullback_volume_ratio > 1.8 as automatic rejection
    // ("zone under absorption"), bypassing config.pullback_volume_very_high_penalty
    // and config.absorption_penalty entirely regardless of their configured values.
    //
    // EMPIRICAL FINDING (v0_2 CSV, 164 trades):
    //   VOL_CLIMAX wins  (38 trades): 59% had HIGH PULLBACK VOL flagged
    //   TRAIL_SL losses  (45 trades): 27% had HIGH PULLBACK VOL flagged
    //   → High pullback vol = institutional participation AT zone = GOOD signal
    //   → The old -50 penalty was scoring AGAINST the entries that actually win
    //
    // Config v0_6 already sets:
    //   pullback_volume_very_high_penalty = 0    (data-proven inversion fix)
    //   absorption_penalty = 0                   (data-proven inversion fix)
    // But those config values were irrelevant while this early-exit block existed.
    //
    // Now scoring is driven entirely by the tiered thresholds below:
    if      (metrics.pullback_volume_ratio < 0.5) { metrics.volume_score += 30; }  // Excellent — very dry
    else if (metrics.pullback_volume_ratio < 0.8) { metrics.volume_score += 20; }  // Good — light
    else if (metrics.pullback_volume_ratio < 1.2) { metrics.volume_score += 10; }  // Acceptable
    else if (metrics.pullback_volume_ratio < 1.5) { metrics.volume_score -=  0; }  // Neutral (was -10, data-disproven)
    // pullback_volume_ratio >= 1.5: No penalty — institutional absorption is positive

    // ── ENTRY BAR QUALITY ────────────────────────────────────────────────
    double body = std::abs(current_bar.close - current_bar.open);
    double range = current_bar.high - current_bar.low;
    metrics.entry_bar_body_pct = (range > 0) ? body / range : 0.0;

    if      (metrics.entry_bar_body_pct > 0.6) metrics.volume_score += 10;  // Strong directional
    else if (metrics.entry_bar_body_pct < 0.3) metrics.volume_score -= 5;   // Doji/indecision

    // ── RECENT VOLUME TREND (last 3 bars declining = healthy pullback) ───
    if (bar_history.size() >= 3) {
        int n = bar_history.size();
        metrics.volume_declining_trend =
            (bar_history[n-1].volume < bar_history[n-2].volume) &&
            (bar_history[n-2].volume < bar_history[n-3].volume);

        if (metrics.volume_declining_trend) metrics.volume_score += 10;
        else                                metrics.volume_score -= 10;
    }

    metrics.volume_score = std::max(-50, std::min(60, metrics.volume_score));

    LOG_INFO("\xF0\x9F\x93\x8A Entry volume score: " + std::to_string(metrics.volume_score)
             + "/60 (pullback=" + std::to_string(metrics.pullback_volume_ratio) + "x)");

    return metrics;
}

EntryDecisionEngine::EntryDecisionEngine(const Config& cfg) 
    : config(cfg), volume_baseline_(nullptr), oi_scorer_(nullptr) {
    LOG_INFO("EntryDecisionEngine initialized");
}

// ============================================================================
// FIXED calculate_stop_loss() function
// File: src/scoring/entry_decision_engine.cpp
// Lines: 95-116 (REPLACE THIS FUNCTION)
// ============================================================================

double EntryDecisionEngine::calculate_stop_loss(const Zone& zone, double entry_price, double atr) const {
    // Always calculate the ORIGINAL stop loss (not breakeven)
    // The breakeven logic is applied later in calculate_entry()
    (void)entry_price;  // Suppress unused parameter warning
    
    double zone_height = std::abs(zone.proximal_line - zone.distal_line);
    
    // ⭐ STOP LOSS WIDENING FIX (Feb 28, 2026)
    // Based on live results analysis: 8/24 (33%) trades hit stop with 32-55pt distances
    // Problem: Stops too tight for NIFTY 1-min volatility
    // Solution: Enforce minimum 100-point stop distance
    //
    // Calculate buffer as the LARGEST of:
    // 1. Percentage of zone height (structural buffer)
    // 2. ATR multiplier (volatility buffer)
    // 3. Minimum distance (prevents noise stops) ⭐ NEW
    
    double buffer_zone = zone_height * config.sl_buffer_zone_pct / 100.0;
    double buffer_atr = atr * config.sl_buffer_atr;
    double buffer_min = config.min_stop_distance_points;
    
    // Use the LARGEST buffer (most conservative)
    double buffer = std::max<double>({buffer_zone, buffer_atr, buffer_min});
    
    // Debug logging to track buffer selection
    LOG_DEBUG("Stop buffer calculation:"
             << " zone=" << std::fixed << std::setprecision(1) << buffer_zone
             << " pts, atr=" << buffer_atr
             << " pts, min=" << buffer_min
             << " pts → final=" << buffer << " pts");
    
    // Place stop loss beyond zone with calculated buffer
    double stop_loss;
    if (zone.type == ZoneType::DEMAND) {
        stop_loss = zone.distal_line - buffer;  // Below demand zone
    } else {
        stop_loss = zone.distal_line + buffer;  // Above supply zone
    }
    
    return stop_loss;
}


double EntryDecisionEngine::calculate_take_profit(const Zone& zone, double entry_price, 
                                                    double stop_loss, double recommended_rr) const {
    double risk = std::abs(entry_price - stop_loss);
    double reward = risk * recommended_rr;
    
    if (zone.type == ZoneType::DEMAND) {
        return entry_price + reward;  // Target above for longs
    } else {
        return entry_price - reward;  // Target below for shorts
    }
}

EntryDecision EntryDecisionEngine::calculate_entry(const Zone& zone, const ZoneScore& score, double atr, MarketRegime regime, const ZoneQualityScore* zone_quality_score, const Bar* current_bar, const std::vector<Bar>* bar_history) const {
    EntryDecision decision;
    decision.score = score;
    LOG_INFO("=== CALCULATE_ENTRY START ===");
    LOG_INFO("Zone: " + std::to_string(zone.zone_id));
    LOG_INFO("current_bar: " + std::string(current_bar ? "VALID" : "nullptr"));
    LOG_INFO("v6_fully_enabled: " + std::string(config.v6_fully_enabled ? "YES" : "NO"));
    LOG_INFO("Config dump:");
    LOG_INFO("  v6_fully_enabled: " + std::string(config.v6_fully_enabled ? "YES" : "NO"));
    LOG_INFO("  enable_volume_entry_filters: " + std::string(config.enable_volume_entry_filters ? "YES" : "NO"));
    LOG_INFO("  min_entry_volume_ratio: " + std::to_string(config.min_entry_volume_ratio));

    // Force position sizing to always execute (with default if needed)
    decision.lot_size = 1; // Safe default
    if (current_bar != nullptr && volume_baseline_ != nullptr) {
        decision.lot_size = calculate_dynamic_lot_size(zone, *current_bar);
        LOG_INFO("V6 position sizing used: " + std::to_string(decision.lot_size));
    } else {
        LOG_WARN("Using default position size (current_bar or baseline nullptr)");
    }

    // THEN do validations
    if (current_bar != nullptr && config.v6_fully_enabled) {
        // Volume validation
        std::string vol_rejection;
        double zone_score_val = score.total_score;
        bool vol_passed = validate_entry_volume(zone, *current_bar, zone_score_val, vol_rejection);
        if (!vol_passed) {
            decision.should_enter = false;
            decision.rejection_reason = vol_rejection;
            LOG_ERROR("❌ V6 VOLUME FILTER: Entry REJECTED: " + vol_rejection);
            return decision;
        } else if (config.v6_log_volume_filters) {
            LOG_INFO("✅ V6 VOLUME FILTER: Entry PASSED");
        }

        // OI validation
        std::string oi_rejection;
        bool oi_passed = validate_entry_oi(zone, *current_bar, oi_rejection);
        if (!oi_passed) {
            decision.should_enter = false;
            decision.rejection_reason = oi_rejection;
            LOG_ERROR("❌ V6 OI FILTER: Entry REJECTED: " + oi_rejection);
            return decision;
        } else if (config.v6_log_oi_filters) {
            LOG_INFO("✅ V6 OI FILTER: Entry PASSED");
        }
    }
    
    // Check per-zone retry limit first
    if (config.enable_per_zone_retry_limit && 
        zone.entry_retry_count >= config.max_retries_per_zone) {
        decision.should_enter = false;
        std::ostringstream oss;
        oss << "Zone retry limit reached (" << zone.entry_retry_count 
            << "/" << config.max_retries_per_zone << ")";
        decision.rejection_reason = oss.str();
        LOG_DEBUG("Entry rejected: " + decision.rejection_reason);
        return decision;
    }
    
    // Check trend filters based on regime and config
    if (regime == MarketRegime::RANGING) {
        // In ranging market
        if (!config.allow_ranging_trades) {
            decision.should_enter = false;
            decision.rejection_reason = "Ranging market trades disabled (allow_ranging_trades=NO)";
            LOG_DEBUG("Entry rejected: " + decision.rejection_reason);
            return decision;
        }
    } else {
        // In trending market (BULL or BEAR)
        if (config.trade_with_trend_only) {
            bool is_with_trend = (regime == MarketRegime::BULL && zone.type == ZoneType::DEMAND) ||
                                (regime == MarketRegime::BEAR && zone.type == ZoneType::SUPPLY);
            if (!is_with_trend) {
                decision.should_enter = false;
                decision.rejection_reason = "Counter-trend trade blocked (trade_with_trend_only=YES)";
                LOG_DEBUG("Entry rejected: " + decision.rejection_reason);
                return decision;
            }
        }
    }
    
    // Skip gap-over zones if configured
    if (config.skip_retest_after_gap_over && zone.was_swept && !zone.reclaim_eligible) {
        decision.should_enter = false;
        decision.rejection_reason = "Zone gap-over violated, retest skipped";
        LOG_DEBUG("Entry rejected: " + decision.rejection_reason);
        return decision;
    }
    
    // Check if zone is exhausted (too many touches)
    if (config.scoring.max_zone_touch_count > 0 && 
        zone.touch_count > config.scoring.max_zone_touch_count) {
        decision.should_enter = false;
        std::ostringstream oss;
        oss << "Zone exhausted: " << zone.touch_count 
            << " touches exceeds maximum " << config.scoring.max_zone_touch_count;
        decision.rejection_reason = oss.str();
        LOG_DEBUG("Entry rejected: " + decision.rejection_reason);
        return decision;
    }
    
    // Check if score meets minimum threshold
    if (score.total_score < config.scoring.entry_minimum_score) {
        decision.should_enter = false;
        std::ostringstream oss;
        oss << "Score " << std::fixed << std::setprecision(1) << score.total_score
            << " below minimum " << config.scoring.entry_minimum_score;
        decision.rejection_reason = oss.str();
        
        LOG_DEBUG("Entry rejected: " + decision.rejection_reason);
        return decision;
    }
    
    // Calculate aggressiveness based on scoring system in use
    double aggressiveness;
    
    if (config.enable_revamped_scoring && zone_quality_score != nullptr) {
        // NEW REVAMPED SCORING: Score range is 0-75 (typical good zone: 45-60)
        // Normalize to 0.0-1.0 range for aggressiveness
        // Recommended score thresholds from docs:
        //   35+ = tradeable (min)
        //   45-60 = good zones
        //   60+ = excellent zones
        // Map: 35 → 0.40, 50 → 0.65, 60 → 0.80, 75 → 1.0
        double normalized_score = std::max(0.0, std::min(75.0, zone_quality_score->total));
        aggressiveness = 0.40 + (normalized_score - 35.0) / 40.0 * 0.60;  // Maps 35-75 to 0.40-1.0
        aggressiveness = std::max(0.0, std::min(1.0, aggressiveness));
        
        LOG_DEBUG("Aggressiveness from NEW scoring: score=" + std::to_string(zone_quality_score->total) + 
                  " → aggressiveness=" + std::to_string(aggressiveness));
    } else {
        // OLD SCORING: Score range is 0-100, direct normalization
        aggressiveness = score.entry_aggressiveness;
        
        LOG_DEBUG("Aggressiveness from OLD scoring: score=" + std::to_string(score.total_score) + 
                  " → aggressiveness=" + std::to_string(aggressiveness));
    }
    
    // ========================================================
    // ADAPTIVE ENTRY LOGIC: DIFFERENT FOR DEMAND VS SUPPLY
    // ========================================================
    //
    // DEMAND ZONES (Support): CONSERVATIVE ENTRY
    //   - High score → Enter NEAR DISTAL (origin of institutional buying)
    //   - Low score → Enter NEAR PROXIMAL (safer, further from risk)
    //   - Formula: conservative_factor = 1.0 - aggressiveness
    //   - Score 80% → factor 20% → Enter at 20% from distal (deep in zone)
    //
    // SUPPLY ZONES (Resistance): AGGRESSIVE ENTRY  
    //   - High score → Enter NEAR DISTAL (top of zone, early entry)
    //   - Low score → Enter NEAR PROXIMAL (safer, wait for confirmation)
    //   - Formula: Uses aggressiveness directly
    //   - Score 80% → Enter at 80% from distal (aggressive)
    //
    // Rationale: Demand zones show institutional accumulation at origin,
    //            Supply zones may need different strategy
    // ========================================================
    
    // ⭐⭐⭐ EMA FILTER REMOVED FROM HERE - See note below ⭐⭐⭐
    //
    // The EMA filter code that was here caused linker errors because
    // this function doesn't have access to the bars vector.
    //
    // SOLUTION: Implement EMA filtering in the ENGINE layer
    // (backtest_engine.cpp or live_engine.cpp) in check_for_entries()
    // BEFORE calling calculate_entry(). Example:
    //
    //   double ema_20 = MarketAnalyzer::calculate_ema(bars, 20, current_idx);
    //   double ema_50 = MarketAnalyzer::calculate_ema(bars, 50, current_idx);
    //   std::string direction = (zone.type == ZoneType::DEMAND) ? "LONG" : "SHORT";
    //   
    //   if (direction == "LONG" && config.require_ema_alignment_for_longs) {
    //       if (ema_20 <= ema_50) continue; // Skip this zone
    //   }
    //
    // ⭐⭐⭐⭐⭐⭐⭐⭐⭐⭐⭐⭐⭐⭐⭐⭐⭐⭐⭐⭐⭐⭐⭐⭐⭐⭐⭐⭐⭐⭐⭐
    
    // ─────────────────────────────────────────────────────────────────────
    // ENTRY PRICE POSITIONING: identical logic for both zone types
    //
    //   proximal = zone ENTRY edge (where price arrives from)
    //   distal   = zone FAR boundary (where SL is placed beyond)
    //   zone_height = |proximal - distal|
    //
    //   Conservative (low aggressiveness) → enter near PROXIMAL
    //     → wide SL distance, confirms zone is holding before going deeper
    //   Aggressive (high aggressiveness)  → enter near DISTAL
    //     → tight SL distance, best R:R if zone holds perfectly
    //
    //   DEMAND:  proximal = base_high (top),  distal = base_low (bottom)
    //     entry = proximal - aggressiveness × height
    //     Low  aggressiveness (0.2): entry = 25100 - 10 = 25090  (near top, safe)
    //     High aggressiveness (0.8): entry = 25100 - 40 = 25060  (deep, tight SL)
    //
    //   SUPPLY:  proximal = base_low (bottom), distal = base_high (top)
    //     entry = proximal + aggressiveness × height
    //     Low  aggressiveness (0.2): entry = 25200 + 8  = 25208  (near bottom, safe)
    //     High aggressiveness (0.8): entry = 25200 + 32 = 25232  (near top, tight SL)
    // ─────────────────────────────────────────────────────────────────────
    double zone_height = std::abs(zone.proximal_line - zone.distal_line);

    if (zone.type == ZoneType::DEMAND) {
        // LONG: price enters from above through proximal (base_high)
        decision.entry_price = zone.proximal_line - aggressiveness * zone_height;
        decision.entry_location_pct = aggressiveness * 100.0;

        LOG_DEBUG("DEMAND zone: score=" + std::to_string(score.total_score) +
                  ", aggressiveness=" + std::to_string(aggressiveness) +
                  ", entry=" + std::to_string(decision.entry_price) +
                  " (" + std::to_string(decision.entry_location_pct) + "% from proximal toward distal)");
    } else {
        // SHORT: price enters from below through proximal (base_low)
        decision.entry_price = zone.proximal_line + aggressiveness * zone_height;
        decision.entry_location_pct = aggressiveness * 100.0;

        LOG_DEBUG("SUPPLY zone: score=" + std::to_string(score.total_score) +
                  ", aggressiveness=" + std::to_string(aggressiveness) +
                  ", entry=" + std::to_string(decision.entry_price) +
                  " (" + std::to_string(decision.entry_location_pct) + "% from proximal toward distal)");
    }
    
    // Calculate stop loss and take profit
    // ⭐ FIX 2: Structural SL and TP are locked from original entry and zone geometry.
    //
    // The old code had a "BREAKEVEN MODE" block (lines 350-369 in original) that ran
    // when config.use_breakeven_stop_loss=YES.  It did this at CALCULATION time:
    //
    //   decision.entry_price = original_sl;              // shifted entry to SL level
    //   decision.stop_loss   = new_entry - sl_distance;  // doubled the SL buffer
    //   TP = calculate_take_profit(new_entry, new_sl)    // TP from shifted/wrong anchors
    //
    // This is NOT breakeven protection.  Breakeven = moving SL to entry AFTER the
    // trade is in profit.  What the old code did was shift the entire trade frame
    // downward at calculation time, before any fill.  For narrow zones or aggressive
    // entries (entry deep inside zone → entry ≈ distal → sl_distance ≈ 0), this
    // caused:
    //   sl_distance → ~0
    //   decision.stop_loss = entry - 0  → SL == entry
    //   TP = entry + (0 * RR) = entry + ~0    ← TP a few points away
    //
    // OBSERVED IMPACT (12 live trades analysed):
    //   Trades #8261, #9465, #9961: SL exactly == entry → TP 7-20pt from entry
    //   Trades #7016, #7089, #8553, #9487, #9867: TP 7-34pt from entry (RR 0.11–0.51)
    //   Trade #9867: TP ABOVE entry on a SHORT → "TAKE_PROFIT" exit at a LOSS (-₹804)
    //
    // CORRECT BEHAVIOUR:
    //   1. Calculate structural SL from zone geometry + ATR buffer (as before)
    //   2. Calculate TP from (original entry, structural SL) — locked, never shifted
    //   3. Breakeven (SL → entry after profit) is a RUNTIME operation in TradeManager,
    //      NOT a calculation-time entry shift here.
    //
    // The config flag use_breakeven_stop_loss=YES is intentionally left in config
    // so TradeManager can still apply real breakeven protection post-fill.
    // This function must NOT act on it.

    double original_entry = decision.entry_price;   // locked — never modified
    double original_sl    = calculate_stop_loss(zone, original_entry, atr);

    // SL and TP both anchored to original_entry and original_sl.
    // TradeManager may move SL to entry later (runtime breakeven), but TP stays fixed.
    decision.stop_loss          = original_sl;
    decision.original_stop_loss = original_sl;
    
    // ⭐ TP is calculated from original_entry vs original_sl — the structural risk distance.
    // These are the values locked above and are NEVER modified before this call.
    // This ensures TP = entry + (structural_SL_dist × RR) regardless of any post-fill
    // breakeven move that TradeManager may apply later.
    decision.take_profit = calculate_take_profit(zone, original_entry,
                                                  original_sl, score.recommended_rr);
    
    // Calculate actual risk/reward
    double risk = std::abs(decision.entry_price - decision.stop_loss);
    double reward = std::abs(decision.take_profit - decision.entry_price);
    decision.expected_rr = (risk > 0.0001) ? (reward / risk) : score.recommended_rr;
    
    // Decision is valid
    decision.should_enter = true;
    
    LOG_INFO("Entry decision: " + std::string(zone.type == ZoneType::DEMAND ? "LONG" : "SHORT") +
             " @ " + std::to_string(decision.entry_price) +
             ", SL=" + std::to_string(decision.stop_loss) +
             ", TP=" + std::to_string(decision.take_profit) +
             ", RR=" + std::to_string(decision.expected_rr));
    
    // ✅ NEW V6.0: Dynamic position sizing
    if (current_bar != nullptr) {
        decision.lot_size = calculate_dynamic_lot_size(zone, *current_bar);
    }

    // === VOLUME METRICS: Calculate and store pullback/pattern metrics ===
    if (current_bar != nullptr && bar_history != nullptr) {
        EntryVolumeMetrics metrics = calculate_entry_volume_metrics(zone, *current_bar, *bar_history);
        decision.entry_pullback_vol_ratio = metrics.pullback_volume_ratio;
        decision.entry_volume_score = metrics.volume_score;
        decision.entry_volume_pattern = metrics.rejection_reason;
        // V6.0: Entry rejection based on pullback volume score (activate after calibration)
        if (config.min_volume_entry_score > -50 && metrics.volume_score < config.min_volume_entry_score) {
            decision.should_enter = false;
            decision.rejection_reason = metrics.rejection_reason.empty()
                ? "Pullback vol score too low (" + std::to_string(metrics.volume_score) + "/60)"
                : metrics.rejection_reason;
            LOG_INFO("PULLBACK VOL REJECTED: " + decision.rejection_reason);
            return decision;
        }
    }

    return decision;
}

bool EntryDecisionEngine::should_enter_trade_two_stage(
    const Zone& zone,
    const std::vector<Bar>& bars,
    int current_index,
    double entry_price,
    double stop_loss,
    ZoneQualityScore* out_zone_score,
    EntryValidationScore* out_entry_score
) const {
    // ⭐ CRITICAL FIX: Filter VIOLATED zones (Run 2 issue)
    if (config.skip_violated_zones && zone.state == ZoneState::VIOLATED) {
        LOG_INFO("❌ Zone " + std::to_string(zone.zone_id) + " REJECTED: State is VIOLATED");
        return false;
    }
    
    // ⭐ OPTIONAL: Filter TESTED zones if prefer_fresh is enabled
    if (config.skip_tested_zones && zone.state == ZoneState::TESTED) {
        LOG_INFO("❌ Zone " + std::to_string(zone.zone_id) + " REJECTED: State is TESTED (prefer fresh)");
        return false;
    }
    
    // ⭐ CRITICAL FIX: Filter zones by age (Run 2 issue - traded 60-120 day old zones)
    if (current_index >= 0 && current_index < static_cast<int>(bars.size())) {
        const Bar& current_bar = bars[current_index];
        
        // Calculate zone age in days
        std::tm formation_tm = {};
        std::istringstream form_ss(zone.formation_datetime);
        form_ss >> std::get_time(&formation_tm, "%Y-%m-%d %H:%M:%S");
        
        std::tm current_tm = {};
        std::istringstream curr_ss(current_bar.datetime);
        curr_ss >> std::get_time(&current_tm, "%Y-%m-%d %H:%M:%S");
        
        if (!form_ss.fail() && !curr_ss.fail()) {
            auto formation_time = std::chrono::system_clock::from_time_t(std::mktime(&formation_tm));
            auto current_time = std::chrono::system_clock::from_time_t(std::mktime(&current_tm));
            auto age_hours = std::chrono::duration_cast<std::chrono::hours>(current_time - formation_time).count();
            int age_days = static_cast<int>(age_hours / 24);
            
            // Check minimum age
            if (config.min_zone_age_days > 0 && age_days < config.min_zone_age_days) {
                LOG_INFO("❌ Zone " + std::to_string(zone.zone_id) + " REJECTED: Too fresh (" + 
                         std::to_string(age_days) + " days < " + std::to_string(config.min_zone_age_days) + " min)");
                return false;
            }
            
            // Check maximum age (CRITICAL - prevents trading stale zones)
            if (config.max_zone_age_days > 0 && age_days > config.max_zone_age_days) {
                LOG_INFO("❌ Zone " + std::to_string(zone.zone_id) + " REJECTED: Too stale (" + 
                         std::to_string(age_days) + " days > " + std::to_string(config.max_zone_age_days) + " max)");
                return false;
            }
            
            LOG_DEBUG("✅ Zone age check passed: " + std::to_string(age_days) + " days (range: " +
                     std::to_string(config.min_zone_age_days) + "-" + std::to_string(config.max_zone_age_days) + ")");
        }
    }
    
    ZoneQualityScorer zone_scorer(config);
    ZoneQualityScore zone_score = zone_scorer.calculate(zone, bars, current_index);
    if (out_zone_score) {
        *out_zone_score = zone_score;
    }

    if (!zone_scorer.meets_threshold(zone_score.total)) {
        LOG_INFO("❌ Two-stage rejection: Zone quality below threshold (" +
                 std::to_string(zone_score.total) + "/" +
                 std::to_string(config.zone_quality_minimum_score) + ")");
        return false;
    }

    EntryValidationScorer entry_scorer(config);
    EntryValidationScore entry_score = entry_scorer.calculate(
        zone,
        bars,
        current_index,
        entry_price,
        stop_loss
    );
    if (out_entry_score) {
        *out_entry_score = entry_score;
    }

    if (!entry_scorer.meets_threshold(entry_score.total)) {
        LOG_INFO("❌ Two-stage rejection: Entry validation below threshold (" +
                 std::to_string(entry_score.total) + "/" +
                 std::to_string(config.entry_validation_minimum_score) + ")");
        return false;
    }

    double combined_score = (zone_score.total + entry_score.total) / 2.0;
    LOG_INFO("🎯 Two-stage approval: Combined=" + std::to_string(combined_score));
    return true;
}

bool EntryDecisionEngine::validate_decision(const EntryDecision& decision) const {
    if (!decision.should_enter) {
        return false;
    }
    
    // Validate prices are reasonable
    if (decision.entry_price <= 0 || decision.stop_loss <= 0 || decision.take_profit <= 0) {
        LOG_WARN("Invalid decision: negative or zero prices"
                 " entry=" + std::to_string(decision.entry_price) +
                 " sl=" + std::to_string(decision.stop_loss) +
                 " tp=" + std::to_string(decision.take_profit));
        return false;
    }

    // ⭐ FIX 3a: TP direction sanity — TP must be on the profit side of entry.
    // Before Fix 2 this could fire for SHORT trades where a degenerate TP ended up
    // above entry (observed: trade #9867 "TAKE_PROFIT" exit at -₹804).
    bool tp_direction_ok = (decision.stop_loss < decision.entry_price)   // LONG: SL below entry
                           ? (decision.take_profit > decision.entry_price)  // TP must be above
                           : (decision.take_profit < decision.entry_price); // SHORT: TP must be below
    if (!tp_direction_ok) {
        LOG_WARN("Invalid decision: TP on wrong side of entry"
                 " entry=" + std::to_string(decision.entry_price) +
                 " sl=" + std::to_string(decision.stop_loss) +
                 " tp=" + std::to_string(decision.take_profit));
        return false;
    }

    // ⭐ FIX 3b: SL must not equal entry (zero risk distance → degenerate TP).
    double sl_dist = std::abs(decision.entry_price - decision.stop_loss);
    if (sl_dist < 1.0) {
        LOG_WARN("Invalid decision: SL == entry (zero risk distance)"
                 " entry=" + std::to_string(decision.entry_price) +
                 " sl=" + std::to_string(decision.stop_loss));
        return false;
    }
    
    // Validate risk/reward ratio
    double risk   = sl_dist;
    double reward = std::abs(decision.take_profit - decision.entry_price);
    double rr     = reward / risk;
    if (rr < 1.0) {
        LOG_WARN("Invalid decision: RR ratio " + std::to_string(rr) + " < 1.0"
                 " (entry=" + std::to_string(decision.entry_price) +
                 " sl=" + std::to_string(decision.stop_loss) +
                 " tp=" + std::to_string(decision.take_profit) + ")");
        return false;
    }
    
    return true;
}

// ========================================
// V6.0: Volume/OI Integration Methods
// ========================================

void EntryDecisionEngine::set_volume_baseline(const Utils::VolumeBaseline* baseline) {
    volume_baseline_ = baseline;
    LOG_INFO("EntryDecisionEngine: Volume baseline set");
}

void EntryDecisionEngine::set_oi_scorer(const Core::OIScorer* scorer) {
    oi_scorer_ = scorer;
    LOG_INFO("EntryDecisionEngine: OI scorer set");
}

bool EntryDecisionEngine::validate_entry_volume(
    const Zone& zone,
    const Bar& current_bar,
    double zone_score,
    std::string& rejection_reason
) const {
    // Skip if volume baseline not available
    if (volume_baseline_ == nullptr || !volume_baseline_->is_loaded()) {
        LOG_INFO("⚠️ V6 Volume Filter: Volume baseline not available - DEGRADED MODE (entry allowed)");
        return true; // Allow entry (degraded mode)
    }
    // Check if V6.0 volume filters are enabled
    if (!config.enable_volume_entry_filters) {
        LOG_DEBUG("V6 Volume Filter: Disabled in config - entry allowed");
        return true;
    }
    // Use zone's volume profile (deterministic per zone)
    double norm_ratio = zone.volume_profile.volume_ratio;
    // 1. Maximum volume cap (extreme noise)
    if (norm_ratio > config.max_entry_volume_ratio) {
        rejection_reason = "Volume too high (extreme noise: " +
                          std::to_string(norm_ratio) + "x vs max " +
                          std::to_string(config.max_entry_volume_ratio) + "x)";
        return false;
    }
    // 2. High volume trap detection
    if (norm_ratio >= config.max_volume_without_elite &&
        zone.institutional_index < config.min_inst_for_high_volume) {
        rejection_reason = "High volume trap (vol=" +
                          std::to_string(norm_ratio) + "x, inst=" +
                          std::to_string(zone.institutional_index) +
                          ", requires inst>=" +
                          std::to_string(config.min_inst_for_high_volume) + ")";
        LOG_INFO("❌ V6 PATTERN FILTER: " + rejection_reason);
        return false;
    }
    // 3. Minimum volume with exception for tight zones
    if (norm_ratio < config.min_entry_volume_ratio) {
        // Exception: Allow tight zones with high scores
        if (zone_score >= config.allow_low_volume_if_score_above) {
            LOG_INFO(std::string("✅ V6 PATTERN: Low volume allowed (tight institutional zone, vol=") +
                     std::to_string(norm_ratio) + "x, score=" + std::to_string(zone_score) + ")");
            return true;  // ALLOW
        }
        rejection_reason = "Insufficient volume (" +
                          std::to_string(norm_ratio) + "x vs " +
                          std::to_string(config.min_entry_volume_ratio) + "x required)";
        return false;
    }
    // 4. Sweet Spot pattern logging
    if (norm_ratio >= config.optimal_volume_min &&
        norm_ratio <= config.optimal_volume_max &&
        zone.institutional_index >= config.optimal_institutional_min) {
        LOG_INFO("🎯 V6 PATTERN: Sweet Spot detected (vol=" +
                 std::to_string(norm_ratio) + "x, inst=" +
                 std::to_string(zone.institutional_index) + ")");
    }
    LOG_DEBUG("✅ Volume filter PASSED: " + std::to_string(norm_ratio) +
              "x (zone inst=" + std::to_string(zone.institutional_index) + ")");
    return true;
}

bool EntryDecisionEngine::validate_entry_oi(
    const Zone& zone,
    const Bar& current_bar,
    std::string& rejection_reason
) const {
    // Skip if OI scorer not available
    if (oi_scorer_ == nullptr) {
        LOG_INFO("⚠️ V6 OI Filter: OI scorer not available - DEGRADED MODE (entry allowed)");
        return true; // Allow entry (degraded mode)
    }
    
    // Check if V6.0 OI filters are enabled
    if (!config.enable_oi_entry_filters) {
        LOG_DEBUG("V6 OI Filter: Disabled in config - entry allowed");
        return true;
    }
    
    // Only validate if OI data is fresh
    if (!current_bar.oi_fresh) {
        LOG_DEBUG("V6 OI Filter: OI data not fresh - entry allowed");
        return true; // Allow entry
    }
    
    // Get current market phase from zone's OI profile
    MarketPhase phase = zone.oi_profile.market_phase;
    
    // Check phase alignment
    if (zone.type == ZoneType::DEMAND) {
        // DEMAND zones: Favorable phases
        if (phase == MarketPhase::SHORT_BUILDUP || 
            phase == MarketPhase::LONG_UNWINDING) {
            LOG_DEBUG("✅ OI phase FAVORABLE for DEMAND: " + market_phase_to_string(phase));
            return true;
        }
        // Unfavorable phases
        else if (phase == MarketPhase::LONG_BUILDUP ||
                 phase == MarketPhase::SHORT_COVERING) {
            rejection_reason = "Unfavorable OI phase for DEMAND: " + 
                              market_phase_to_string(phase);
            return false;
        }
    } else { // SUPPLY zone
        // SUPPLY zones: Favorable phases
        if (phase == MarketPhase::LONG_BUILDUP ||
            phase == MarketPhase::SHORT_COVERING) {
            LOG_DEBUG("✅ OI phase FAVORABLE for SUPPLY: " + market_phase_to_string(phase));
            return true;
        }
        // Unfavorable phases
        else if (phase == MarketPhase::SHORT_BUILDUP ||
                 phase == MarketPhase::LONG_UNWINDING) {
            rejection_reason = "Unfavorable OI phase for SUPPLY: " + 
                              market_phase_to_string(phase);
            return false;
        }
    }
    
    // Neutral phase - allow entry
    LOG_DEBUG("V6 OI Filter: Phase NEUTRAL - entry allowed");
    return true;
}

int EntryDecisionEngine::calculate_dynamic_lot_size(
    const Zone& zone,
    const Bar& current_bar
) const {
    int base_position = 1;  // Default: 1 contract
    double multiplier = 1.0;

    // ✅ USE ZONE-LEVEL VOLUME (not bar-level!)
    double zone_volume_ratio = zone.volume_profile.volume_ratio;
    int zone_institutional = zone.institutional_index;
    double zone_score = zone.zone_score.total_score;
    // Log zone characteristics for debugging
    LOG_DEBUG("Position sizing for zone " + std::to_string(zone.zone_id) + 
              ": vol=" + std::to_string(zone_volume_ratio) + 
              ", inst=" + std::to_string(zone_institutional));
    
    // Reduce size in low volume ZONES (not bars!)
    if (zone_volume_ratio < 0.8) {
        multiplier = config.low_volume_size_multiplier; // e.g., 0.5
        LOG_INFO("🔻 Position size REDUCED (low volume ZONE): " + 
                 std::to_string(multiplier) + "x (zone vol: " + 
                 std::to_string(zone_volume_ratio) + ")");
    }
    // Sweet spot pattern (moderate vol + medium inst)
    else if (zone_volume_ratio >= config.optimal_volume_min && 
             zone_volume_ratio <= config.optimal_volume_max && 
             zone_institutional >= config.optimal_institutional_min) {
        multiplier = config.high_score_position_mult;
        LOG_INFO("🔺 Position size INCREASED (Sweet Spot): " + 
                 std::to_string(multiplier) + "x (zone vol: " + 
                 std::to_string(zone_volume_ratio) + ", inst: " + 
                 std::to_string(zone_institutional) + ")");
    }
    // Elite pattern (any vol + very high inst)
    else if (zone_institutional >= config.elite_institutional_threshold) {
        multiplier = config.high_score_position_mult;
        LOG_INFO("🔺 Position size INCREASED (Elite): " + 
                 std::to_string(multiplier) + "x (inst: " + 
                 std::to_string(zone_institutional) + ")");
    } // After volume/institutional checks, add:
    else if (zone_score >= config.high_score_threshold) {  // High-quality zones
        multiplier = config.high_score_position_mult;
        LOG_INFO("🔺 Position size INCREASED (High Score): " + 
                 std::to_string(multiplier) + "x (score: " + 
                 std::to_string(zone_score) + ")");
    }
    
    int final_position = static_cast<int>(std::round(base_position * multiplier));
    // Enforce safety limits (min 1, max 3 contracts)
    final_position = std::max(1, std::min(final_position, 3));

    LOG_INFO("V6 Position sizing: Base=" + std::to_string(base_position) +
             " contracts, Multiplier=" + std::to_string(multiplier) +
             ", Final=" + std::to_string(final_position) + " contracts");

    return final_position;
}

std::string EntryDecisionEngine::extract_time_slot(const std::string& datetime) const {
    // Expected format: "2024-02-08 09:15:00"
    if (datetime.length() >= 16) {
        std::string time_hhmm = datetime.substr(11, 5);

        try {
            int hour = std::stoi(time_hhmm.substr(0, 2));
            int min = std::stoi(time_hhmm.substr(3, 2));

            // Round down to nearest 5-minute interval
            min = (min / 5) * 5;

            std::ostringstream oss;
            oss << std::setfill('0') << std::setw(2) << hour << ":"
                << std::setfill('0') << std::setw(2) << min;

            return oss.str();
        } catch (...) {
            return "00:00";
        }
    }
    return "00:00"; // Fallback
}

} // namespace Core
} // namespace SDTrading