#include "zone_quality_scorer.h"
#include "../analysis/market_analyzer.h"
#include "../utils/logger.h"
#include <ctime>
#include <sstream>
#include <cmath>
#include <algorithm>

namespace SDTrading {
namespace Core {

ZoneQualityScorer::ZoneQualityScorer(const Config& cfg) : config_(cfg) {}

ZoneQualityScore ZoneQualityScorer::calculate(const Zone& zone, const std::vector<Bar>& bars, int current_index) const {
    ZoneQualityScore score;

	std::cout << "\n[DEBUG] Zone #" << zone.zone_id 
          << " touches=" << zone.touch_count
          << " max=" << config_.zone_max_touch_count
          << " max_age=" << config_.zone_max_age_days
          << std::flush;
		  
    if (current_index < 0 || current_index >= static_cast<int>(bars.size())) {
        return score;
    }

std::cout << "\nAfter  if (current_index < 0 || current_index >= static_cast<int>(bars.size())) statement" 
          << " touches=" << zone.touch_count
          << " max=" << config_.zone_max_touch_count
          << " max_age=" << config_.zone_max_age_days
          << std::flush;
    // =================================================================
    // ⭐ CRITICAL FIX #1: AUTO-RETIRE EXHAUSTED ZONES
    // =================================================================
    if (zone.touch_count > config_.zone_max_touch_count) {
        score.total = 0.0;
        LOG_WARN("\nZone #" + std::to_string(zone.zone_id) + " RETIRED: " + 
                std::to_string(zone.touch_count) + " touches (max=" + 
                std::to_string(config_.zone_max_touch_count) + ")");
        return score;
    }
    
    // =================================================================
    // ⭐ CRITICAL FIX #2: AUTO-RETIRE OLD ZONES
    // =================================================================
    int age_days = calculate_days_difference(zone.formation_datetime, bars[current_index].datetime);
    if (age_days > config_.zone_max_age_days) {
        score.total = 0.0;
        LOG_WARN("Zone #" + std::to_string(zone.zone_id) + " RETIRED: " + 
                std::to_string(age_days) + " days old (max=" + 
                std::to_string(config_.zone_max_age_days) + ")");
        return score;
    }

    // =================================================================
    // V4.0 TRADITIONAL METRICS (50% weight in V6.0)
    // =================================================================
    double base_strength = calculate_base_strength_score(zone);      // 0-30
    double age_score = calculate_age_score(zone, bars[current_index]); // 0-25
    double rejection_score = calculate_rejection_score(zone, bars, current_index); // 0-25
    double touch_penalty = calculate_touch_penalty(zone);            // -50 to +5
    double breakthrough_penalty = calculate_breakthrough_penalty(zone, bars, current_index);
    double elite_bonus = calculate_elite_bonus(zone, bars[current_index]);
    
    // Traditional score (max ~65 points)
    double traditional_score = base_strength + age_score + rejection_score + 
                               touch_penalty + breakthrough_penalty + elite_bonus;
    
    // Log detailed traditional scoring breakdown
    LOG_INFO("🔍 Traditional Score Breakdown:");
    LOG_INFO("   Base Strength:       " + std::to_string(base_strength) + " (0-30)");
    LOG_INFO("   Age Score:           " + std::to_string(age_score) + " (0-25)");
    LOG_INFO("   Rejection Score:     " + std::to_string(rejection_score) + " (0-25)");
    LOG_INFO("   Touch Penalty:       " + std::to_string(touch_penalty) + " (-50 to +5)");
    LOG_INFO("   Breakthrough Penalty:" + std::to_string(breakthrough_penalty));
    LOG_INFO("   Elite Bonus:         " + std::to_string(elite_bonus));
    LOG_INFO("   TRADITIONAL TOTAL:   " + std::to_string(traditional_score));
    
    // =================================================================
    // V6.0 VOLUME/OI METRICS (50% weight combined)
    // =================================================================
    double volume_score = zone.volume_profile.volume_score;  // 0-40 points
    double oi_score = zone.oi_profile.oi_score;              // 0-30 points
    
    // =================================================================
    // V6.0 WEIGHTED SCORING FORMULA
    // =================================================================
    // Check if V6 data is available
    bool has_v6_data = (volume_score > 0.0 || oi_score > 0.0 || zone.institutional_index > 0.0);
    
    if (has_v6_data) {
        // V6.0 ENABLED: Use weighted formula
        // Traditional: 50% weight, Volume: 30% weight, OI: 20% weight
        score.total = (0.50 * traditional_score) +
                      (0.30 * volume_score) +
                      (0.20 * oi_score);
        
        LOG_DEBUG("✅ V6 scoring: Traditional=" + std::to_string(traditional_score) +
                 " Volume=" + std::to_string(volume_score) +
                 " OI=" + std::to_string(oi_score));
    } else {
        // V6.0 NOT AVAILABLE: Fallback to traditional only
        score.total = traditional_score;
        LOG_INFO("⚠️ V6 data missing - using traditional scoring only: " + 
                std::to_string(traditional_score));
    }
    
    // =================================================================
    // INSTITUTIONAL INDEX BONUS (only if V6 data available)
    // =================================================================
    if (has_v6_data && zone.institutional_index >= 80.0) {
        score.total += 15.0;  // Elite institutional participation
    } else if (has_v6_data && zone.institutional_index >= 60.0) {
        score.total += 10.0;  // Good institutional participation
    } else if (has_v6_data && zone.institutional_index >= 40.0) {
        score.total += 5.0;   // Moderate institutional participation
    }
    
    // =================================================================
    // POPULATE BREAKDOWN FIELDS (for logging)
    // =================================================================
    score.zone_strength = base_strength;
    score.zone_age = age_score;
    score.touch_count = rejection_score;
    score.zone_height = touch_penalty;
    
    // Clamp to valid range [0, 100]
    score.total = std::max(0.0, std::min(100.0, score.total));
    
    LOG_INFO("Zone #" + std::to_string(zone.zone_id) + 
             " Score: Traditional=" + std::to_string(traditional_score) +
             " Volume=" + std::to_string(volume_score) +
             " OI=" + std::to_string(oi_score) +
             " Touches=" + std::to_string(zone.touch_count) +
             " Age=" + std::to_string(age_days) + "d" +
             " TOTAL=" + std::to_string(score.total));
    
    return score;
}

bool ZoneQualityScorer::meets_threshold(double score) const {
    return score >= config_.zone_quality_minimum_score;
}

// Component 1: Base Strength (0-30 range, normalized from zone strength)
double ZoneQualityScorer::calculate_base_strength_score(const Zone& zone) const {
    // Normalize zone strength (0-100) to base strength score (0-30)
    return (zone.strength / 100.0) * 30.0;
}

// Component 2: AGE DECAY (0-25 range) - ⭐ FIXED: Much more aggressive decay
double ZoneQualityScorer::calculate_age_score(const Zone& zone, const Bar& current_bar) const {
    int age_days = calculate_days_difference(zone.formation_datetime, current_bar.datetime);
    double age_factor;

    // ⭐ FIX: Aggressive age decay - zones expire much faster
    if (age_days <= 7) {
        // Full score for zones < 1 week
        age_factor = 1.0;
    } else if (age_days <= 14) {
        // Linear decay 1.0 -> 0.8 (week 2)
        age_factor = 1.0 - (age_days - 7) / 35.0;  // -0.2 over 7 days
    } else if (age_days <= 30) {
        // Linear decay 0.8 -> 0.5 (weeks 3-4)
        age_factor = 0.8 - (age_days - 14) / 53.33;  // -0.3 over 16 days
    } else if (age_days <= 60) {
        // Linear decay 0.5 -> 0.2 (months 2)
        age_factor = 0.5 - (age_days - 30) / 100.0;  // -0.3 over 30 days
    } else if (age_days <= config_.zone_max_age_days) {
        // Final decay 0.2 -> 0.0 until retirement
        int remaining_days = config_.zone_max_age_days - 60;
        age_factor = 0.2 * (1.0 - static_cast<double>(age_days - 60) / remaining_days);
        age_factor = std::max(0.0, age_factor);
    } else {
        // Zone should be retired (this shouldn't happen due to early return)
        age_factor = 0.0;
    }

    double age_score = 25.0 * age_factor;
    
    LOG_DEBUG("Age scoring: " + std::to_string(age_days) + " days, " +
              "factor=" + std::to_string(age_factor) + ", " +
              "score=" + std::to_string(age_score));
    
    return age_score;
}

// Component 3: REJECTION QUALITY (0-25 range) - Based on 30-day rejection rate OR zone touch count as proxy
double ZoneQualityScorer::calculate_rejection_score(const Zone& zone, const std::vector<Bar>& bars, int current_index) const {
    double rejection_rate = calculate_recent_rejection_rate(zone, bars, current_index, 30);
    double rejection_score;

    // If we have enough historical touches (20+), use actual rejection rate
    if (rejection_rate > 0.0) {
        if (rejection_rate >= 60.0) {
            rejection_score = 25.0;  // Excellent
        } else if (rejection_rate >= 40.0) {
            // Scale from 0-25 for 40-60% range
            rejection_score = 25.0 * (rejection_rate - 40.0) / 20.0;
        } else if (rejection_rate >= 20.0) {
            // Scale from 0-10 for 20-40% range
            rejection_score = 10.0 * (rejection_rate - 20.0) / 20.0;
        } else {
            rejection_score = 0.0;  // Zone is failing
        }
    } else {
        // FALLBACK: Use zone.touch_count as proxy for zone quality
        // ⭐ FIX: Favor FRESH zones (0-3 touches)
        if (zone.touch_count == 0) {
            // Untested fresh zone - give benefit of doubt
            rejection_score = 15.0;
        } else if (zone.touch_count <= 3) {
            // Fresh zone with 1-3 tests - excellent!
            rejection_score = 20.0;
        } else if (zone.touch_count <= 10) {
            // Some validation but still fresh
            rejection_score = 12.0;
        } else {
            // Many touches but no rejection data - partial credit
            rejection_score = 8.0;
        }
    }

    return rejection_score;
}

// Component 4: TOUCH COUNT PENALTY - ⭐ COMPLETELY REWRITTEN
double ZoneQualityScorer::calculate_touch_penalty(const Zone& zone) const {
    double penalty = 0.0;

    // ⭐ FIX: Favor FRESH zones, heavily penalize overtraded zones
    if (zone.touch_count == 0) {
        penalty = +5.0;  // BONUS for untested zones (institutional fresh)
    } else if (zone.touch_count <= 2) {
        penalty = +3.0;  // BONUS for barely-tested zones
    } else if (zone.touch_count <= 5) {
        penalty = 0.0;   // Neutral for lightly-tested zones
    } else if (zone.touch_count <= 10) {
        penalty = -3.0;  // Light penalty
    } else if (zone.touch_count <= 20) {
        penalty = -8.0;  // Medium penalty
    } else if (zone.touch_count <= 30) {
        penalty = -15.0; // Heavy penalty
    } else if (zone.touch_count <= 50) {
        penalty = -30.0; // Very heavy penalty
    } else {
        // Should be retired, but if not, massive penalty
        penalty = -100.0;  // Effectively disables zone
    }

    LOG_DEBUG("Touch penalty: count=" + std::to_string(zone.touch_count) +
              ", penalty=" + std::to_string(penalty));

    return penalty;
}

// Component 5: BREAKTHROUGH PENALTY (-15 to 0)
double ZoneQualityScorer::calculate_breakthrough_penalty(const Zone& zone, const std::vector<Bar>& bars, int current_index) const {
    double breakthrough_rate = calculate_breakthrough_rate(zone, bars, current_index, 30);
    double penalty = 0.0;

    if (breakthrough_rate > 40.0) {
        // Zone is broken - softened penalty for V6 testing
        return -30.0;  // Reduced from -100 to allow V6 volume scoring to help
    } else if (breakthrough_rate > 30.0) {
        penalty = -15.0;
    } else if (breakthrough_rate > 20.0) {
        penalty = -8.0;
    }

    return penalty;
}

// Component 6: ELITE BONUS (0-10 range) - Time-decayed
double ZoneQualityScorer::calculate_elite_bonus(const Zone& zone, const Bar& current_bar) const {
    if (!zone.is_elite) {
        return 0.0;
    }

    int age_days = calculate_days_difference(zone.formation_datetime, current_bar.datetime);
    double elite_bonus;

    // ⭐ FIX: Elite status expires much faster
    if (age_days <= 30) {
        elite_bonus = 10.0;  // Full bonus for elite zones < 1 month
    } else if (age_days <= 60) {
        elite_bonus = 5.0;   // Half bonus for 1-2 months
    } else {
        elite_bonus = 0.0;   // Elite status expires after 2 months (was 6!)
    }

    return elite_bonus;
}

// Helper: Calculate Recent Rejection Rate (last N days)
double ZoneQualityScorer::calculate_recent_rejection_rate(const Zone& zone, const std::vector<Bar>& bars, 
                                                          int current_index, int lookback_days) const {
    if (current_index < 0 || current_index >= static_cast<int>(bars.size())) {
        return 0.0;
    }

    // Calculate lookback start index
    // For 5-min bars: ~78 bars per trading day (6.5 hours × 12 bars/hour)
    int lookback_bars = lookback_days * 78;  // 78 bars ≈ 1 trading day for 5-min
    int start_index = std::max(0, current_index - lookback_bars);

    int touches = 0;
    int rejections = 0;

    for (int i = start_index; i <= current_index; ++i) {
        if (touches_zone(bars[i], zone)) {
            touches++;
            if (is_clean_rejection(bars[i], zone)) {
                rejections++;
            }
        }
    }

    double rejection_rate = touches > 0 ? (rejections * 100.0 / touches) : 0.0;
    return rejection_rate;
}

// Helper: Calculate Breakthrough Rate (last N days)
double ZoneQualityScorer::calculate_breakthrough_rate(const Zone& zone, const std::vector<Bar>& bars, 
                                                       int current_index, int lookback_days) const {
    if (current_index < 0 || current_index >= static_cast<int>(bars.size())) {
        return 0.0;
    }

    // Calculate lookback start index
    // For 5-min bars: ~78 bars per trading day
    int lookback_bars = lookback_days * 78;
    int start_index = std::max(0, current_index - lookback_bars);

    int touches = 0;
    int breakthroughs = 0;

    for (int i = start_index; i <= current_index; ++i) {
        if (touches_zone(bars[i], zone)) {
            touches++;
            if (is_breakthrough(bars[i], zone)) {
                breakthroughs++;
            }
        }
    }

    double breakthrough_rate = touches > 0 ? (breakthroughs * 100.0 / touches) : 0.0;
    return breakthrough_rate;
}

// Helper: Check if bar touches zone
bool ZoneQualityScorer::touches_zone(const Bar& bar, const Zone& zone) const {
    if (zone.type == ZoneType::SUPPLY) {
        // Supply zone: check if bar high touches proximal line
        return bar.high >= zone.proximal_line;
    } else {
        // Demand zone: check if bar low touches proximal line
        return bar.low <= zone.proximal_line;
    }
}

// Helper: Check if bar shows clean rejection
bool ZoneQualityScorer::is_clean_rejection(const Bar& bar, const Zone& zone) const {
    if (zone.type == ZoneType::SUPPLY) {
        // Supply: Price touched zone but closed below it with bearish candle
        return (bar.high >= zone.proximal_line) && 
               (bar.close < zone.distal_line) && 
               (bar.close < bar.open);
    } else {
        // Demand: Price touched zone but closed above it with bullish candle
        return (bar.low <= zone.proximal_line) && 
               (bar.close > zone.distal_line) && 
               (bar.close > bar.open);
    }
}

// Helper: Check if bar breaks through zone
bool ZoneQualityScorer::is_breakthrough(const Bar& bar, const Zone& zone) const {
    if (zone.type == ZoneType::SUPPLY) {
        // Supply: Closed above supply zone (breakthrough to upside)
        return bar.close > zone.distal_line;
    } else {
        // Demand: Closed below demand zone (breakthrough to downside)
        return bar.close < zone.distal_line;
    }
}

int ZoneQualityScorer::calculate_days_difference(const std::string& from_dt, const std::string& to_dt) const {
    auto parse_date = [](const std::string& dt, std::tm& tm_out) -> bool {
        std::istringstream ss(dt);
        ss >> std::get_time(&tm_out, "%Y-%m-%d %H:%M:%S");
        if (!ss.fail()) {
            return true;
        }
        ss.clear();
        ss.str(dt);
        ss >> std::get_time(&tm_out, "%Y-%m-%d");
        return !ss.fail();
    };

    std::tm from_tm{};
    std::tm to_tm{};
    if (!parse_date(from_dt, from_tm) || !parse_date(to_dt, to_tm)) {
        return 0;
    }

    from_tm.tm_isdst = -1;
    to_tm.tm_isdst = -1;

    std::time_t from_time = std::mktime(&from_tm);
    std::time_t to_time = std::mktime(&to_tm);
    if (from_time == static_cast<std::time_t>(-1) || to_time == static_cast<std::time_t>(-1)) {
        return 0;
    }

    double diff_sec = std::difftime(to_time, from_time);
    int days = static_cast<int>(diff_sec / (60.0 * 60.0 * 24.0));
    return std::max(0, days);
}

} // namespace Core
} // namespace SDTrading
