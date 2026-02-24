#include "zone_scorer.h"
#include "../utils/logger.h"

namespace SDTrading {
namespace Core {

ZoneScorer::ZoneScorer(const Config& cfg) : config(cfg) {
    LOG_INFO("ZoneScorer initialized");
}

double ZoneScorer::calculate_base_strength_score(const Zone& zone) const {
    // Max 20 points for base strength
    double max_score = config.scoring.weight_base_strength * 100.0;
    return (zone.strength / 100.0) * max_score;
}

double ZoneScorer::calculate_elite_bonus_score(const Zone& zone) const {
    if (!zone.is_elite) return 0.0;
    
    // Max 20 points for elite zones
    double max_score = config.scoring.weight_elite_bonus * 100.0;
    double score = 0.0;
    
    // Departure imbalance (40% of elite score)
    // Higher departure = stronger institutional move
    double departure_score = std::min<double>(zone.departure_imbalance / 5.0, 1.0);
    score += departure_score * (max_score * 0.40);
    
    // Retest speed (30% of elite score)
    // Slower retest = more patience = stronger zone
    double speed_score = 1.0 - std::min<double>(zone.retest_speed / 1.0, 1.0);
    score += speed_score * (max_score * 0.30);
    
    // Bars to retest - patience (30% of elite score)
    // More bars = institutional patience
    double patience_score = std::min<double>(zone.bars_to_retest / 15.0, 1.0);
    score += patience_score * (max_score * 0.30);
    
    return score;
}

double ZoneScorer::calculate_regime_alignment_score(const Zone& zone, MarketRegime regime) const {
    // Max 20 points for regime alignment
    double max_score = config.scoring.weight_regime_alignment * 100.0;
    
    // Perfect alignment: DEMAND in BULL, SUPPLY in BEAR
    if ((regime == MarketRegime::BULL && zone.type == ZoneType::DEMAND) ||
        (regime == MarketRegime::BEAR && zone.type == ZoneType::SUPPLY)) {
        return max_score;  // 100% score
    }
    // Ranging market: partial credit
    else if (regime == MarketRegime::RANGING) {
        return max_score * 0.40;  // 40% score
    }
    // Counter-trend: no credit
    else {
        return 0.0;
    }
}

double ZoneScorer::calculate_state_freshness_score(const Zone& zone) const {
    // Max 15 points for state freshness
    double max_score = config.scoring.weight_state_freshness * 100.0;
    double base_score = 0.0;
    
    switch (zone.state) {
        case ZoneState::FRESH:
            base_score = max_score * 1.00;  // 100% - untested zone gets full credit
            break;
        case ZoneState::TESTED:
            base_score = max_score * 0.80;  // 80% - tested and held (reduced from 100%)
            break;
        case ZoneState::VIOLATED:
            return 0.0;  // 0% - zone broken
        default:
            return 0.0;
    }
    
    // Apply touch count penalty (exponential decay after 10 touches)
    // This prevents trading exhausted zones like Zone 212 (613 touches)
    double touch_penalty = 1.0;
    if (zone.touch_count > 10) {
        // Linear decay: 10 touches = 100%, 50 touches = 20%, 100+ touches = 10%
        touch_penalty = 10.0 / static_cast<double>(zone.touch_count);
        touch_penalty = std::max(touch_penalty, 0.1);  // Floor at 10%
    }
    
    return base_score * touch_penalty;
}

double ZoneScorer::calculate_rejection_score(const Zone& zone, const Bar& current_bar) const {
    // Max 15 points for rejection confirmation
    double max_score = config.scoring.weight_rejection_confirmation * 100.0;
    
    double total_range = current_bar.high - current_bar.low;
    if (total_range < 0.0001) return 0.0;
    
    // Calculate wick length based on zone type
    double wick_length;
    if (zone.type == ZoneType::DEMAND) {
        // For demand zones, look for bullish rejection (long lower wick)
        wick_length = current_bar.close - current_bar.low;
    } else {
        // For supply zones, look for bearish rejection (long upper wick)
        wick_length = current_bar.high - current_bar.close;
    }
    
    double wick_ratio = wick_length / total_range;
    
    // Score based on wick strength
    if (wick_ratio >= config.scoring.rejection_strong_threshold) {
        return max_score * 1.0;  // Strong rejection (e.g., wick > 60%)
    } else if (wick_ratio >= config.scoring.rejection_moderate_threshold) {
        return max_score * 0.6;  // Moderate rejection (e.g., wick > 40%)
    } else if (wick_ratio >= config.scoring.rejection_weak_threshold) {
        return max_score * 0.2;  // Weak rejection (e.g., wick > 25%)
    } else {
        return 0.0;  // No significant rejection
    }
}

ZoneScore ZoneScorer::evaluate_zone(const Zone& zone, MarketRegime regime, const Bar& current_bar) const {
    ZoneScore score;
    
    // Calculate individual score components
    score.base_strength_score = calculate_base_strength_score(zone);
    score.elite_bonus_score = calculate_elite_bonus_score(zone);
    score.swing_position_score = zone.swing_analysis.swing_score;  // Already calculated (0-30)
    score.regime_alignment_score = calculate_regime_alignment_score(zone, regime);
    score.state_freshness_score = calculate_state_freshness_score(zone);
    score.rejection_confirmation_score = calculate_rejection_score(zone, current_bar);
    
    // Calculate composite score (calls method defined in common_types.h)
    score.calculate_composite();
    
    // Calculate recommended risk/reward ratio
    if (config.scoring.rr_scale_with_score) {
        double score_factor = score.total_score / 100.0;
        score.recommended_rr = config.scoring.rr_base_ratio +
            (score_factor * (config.scoring.rr_max_ratio - config.scoring.rr_base_ratio));
        score.recommended_rr = std::min<double>(score.recommended_rr, config.scoring.rr_max_ratio);
    } else {
        score.recommended_rr = config.scoring.rr_base_ratio;
    }
    
    // Generate entry rationale based on aggressiveness
    std::ostringstream rationale;
    if (score.entry_aggressiveness >= 0.80) {
        rationale << "VERY AGGRESSIVE";
    } else if (score.entry_aggressiveness >= 0.60) {
        rationale << "AGGRESSIVE";
    } else if (score.entry_aggressiveness >= 0.40) {
        rationale << "BALANCED";
    } else if (score.entry_aggressiveness >= 0.20) {
        rationale << "CONSERVATIVE";
    } else {
        rationale << "VERY CONSERVATIVE";
    }
    score.entry_rationale = rationale.str();
    
    LOG_DEBUG("Zone scored: total=" + std::to_string(score.total_score) + 
              ", aggressiveness=" + std::to_string(score.entry_aggressiveness) +
              ", rationale=" + score.entry_rationale);
    
    return score;
}

bool ZoneScorer::meets_entry_threshold(const ZoneScore& score) const {
    return score.total_score >= config.scoring.entry_minimum_score;
}

} // namespace Core
} // namespace SDTrading
