#ifndef ZONE_SCORER_H
#define ZONE_SCORER_H

#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>
#include "common_types.h"

namespace SDTrading {
namespace Core {

/**
 * @class ZoneScorer
 * @brief Multi-dimensional zone scoring system
 * 
 * Evaluates zones based on:
 * - Base strength (consolidation tightness)
 * - Elite bonus (departure, speed, patience)
 * - Swing position (swing high/low context)
 * - Regime alignment (with/against trend)
 * - State freshness (FRESH vs TESTED)
 * - Rejection confirmation (wick analysis)
 */
class ZoneScorer {
private:
    const Config& config;

    /**
     * Calculate base strength component (0-20 points)
     * Based on zone consolidation tightness
     */
    double calculate_base_strength_score(const Zone& zone) const;

    /**
     * Calculate elite bonus component (0-20 points)
     * Based on departure imbalance, retest speed, and patience
     */
    double calculate_elite_bonus_score(const Zone& zone) const;

    /**
     * Calculate regime alignment score (0-20 points)
     * DEMAND aligned with BULL, SUPPLY aligned with BEAR
     */
    double calculate_regime_alignment_score(const Zone& zone, MarketRegime regime) const;

    /**
     * Calculate state freshness score (0-15 points)
     * FRESH zones vs TESTED zones
     */
    double calculate_state_freshness_score(const Zone& zone) const;

    /**
     * Calculate rejection confirmation score (0-15 points)
     * Based on wick-to-body ratio on current bar
     */
    double calculate_rejection_score(const Zone& zone, const Bar& current_bar) const;

public:
    /**
     * Constructor
     * @param cfg Configuration settings
     */
    explicit ZoneScorer(const Config& cfg);

    /**
     * Evaluate zone with comprehensive scoring
     * @param zone Zone to evaluate
     * @param regime Current market regime
     * @param current_bar Current price bar for rejection analysis
     * @return Complete zone score breakdown
     */
    ZoneScore evaluate_zone(const Zone& zone, MarketRegime regime, const Bar& current_bar) const;

    /**
     * Check if zone score meets minimum threshold
     * @param score Zone score to check
     * @return true if score >= minimum entry threshold
     */
    bool meets_entry_threshold(const ZoneScore& score) const;
};

} // namespace Core
} // namespace SDTrading

#endif // ZONE_SCORER_H
