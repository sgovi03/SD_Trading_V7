#ifndef ENTRY_DECISION_ENGINE_H
#define ENTRY_DECISION_ENGINE_H

#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>
#include "common_types.h"
#include "scoring_types.h"

namespace SDTrading {

// Forward declarations for V6.0
namespace Utils {
    class VolumeBaseline;
}

namespace Core {
    class OIScorer;
}

namespace Core {

/**
 * @class EntryDecisionEngine
 * @brief Calculates optimal entry points based on zone scores
 * 
 * Uses adaptive entry logic:
 * - DEMAND zones: Conservative entry (higher score → enter near origin)
 * - SUPPLY zones: Aggressive entry (higher score → enter near distal)
 * 
 * Also calculates:
 * - Stop loss placement
 * - Take profit targets
 * - Risk/reward ratios
 */
class EntryDecisionEngine {
private:
    const Config& config;
    
    // V6.0: Volume/OI integration
    const Utils::VolumeBaseline* volume_baseline_;
    const Core::OIScorer* oi_scorer_;

    /**
     * Calculate stop loss with buffer
     * @param zone Zone being traded
     * @param entry_price Calculated entry price
     * @param atr Current ATR value
     * @return Stop loss price
     */
    double calculate_stop_loss(const Zone& zone, double entry_price, double atr) const;

    /**
     * Calculate take profit target
     * @param zone Zone being traded
     * @param entry_price Entry price
     * @param stop_loss Stop loss price
     * @param recommended_rr Recommended risk/reward ratio
     * @return Take profit price
     */
    double calculate_take_profit(const Zone& zone, double entry_price, 
                                  double stop_loss, double recommended_rr) const;
    
    // V6.0: Volume entry filter (V6 pattern filters)
    bool validate_entry_volume(
        const Zone& zone,
        const Bar& current_bar,
        double zone_score,
        std::string& rejection_reason
    ) const;

    // V6.0: Real-time pullback volume metrics (Fix #2)
    EntryVolumeMetrics calculate_entry_volume_metrics(
        const Zone& zone,
        const Bar& current_bar,
        const std::vector<Bar>& bar_history
    ) const;
    
    // V6.0: OI entry filter
    bool validate_entry_oi(
        const Zone& zone,
        const Bar& current_bar,
        std::string& rejection_reason
    ) const;
    
    // V6.0: Dynamic position sizing
    int calculate_dynamic_lot_size(
        const Zone& zone,
        const Bar& current_bar,
        double entry_price,
        double stop_loss
    ) const;
    
    // V6.0: Extract time slot from datetime
    std::string extract_time_slot(const std::string& datetime) const;

public:
    /**
     * Constructor
     * @param cfg Configuration settings
     */
    explicit EntryDecisionEngine(const Config& cfg);
    
    // V6.0: Set volume baseline
    void set_volume_baseline(const Utils::VolumeBaseline* baseline);
    
    // V6.0: Set OI scorer
    void set_oi_scorer(const Core::OIScorer* scorer);

    /**
     * Calculate complete entry decision
     * @param zone Zone to trade
     * @param score Zone evaluation score
     * @param atr Current ATR value
     * @param regime Current market regime (for filtering)
     * @param zone_quality_score Optional: New revamped zone quality score (for aggressiveness calculation)
     * @param current_bar Current bar for V6.0 volume/OI validation
     * @return Complete entry decision with prices and ratios
     */
    EntryDecision calculate_entry(const Zone& zone, const ZoneScore& score, double atr, MarketRegime regime = MarketRegime::RANGING, const ZoneQualityScore* zone_quality_score = nullptr, const Bar* current_bar = nullptr, const std::vector<Bar>* bar_history = nullptr) const;

    /**
     * Two-stage scoring gate (optional)
     */
    bool should_enter_trade_two_stage(
        const Zone& zone,
        const std::vector<Bar>& bars,
        int current_index,
        double entry_price,
        double stop_loss,
        ZoneQualityScore* out_zone_score = nullptr,
        EntryValidationScore* out_entry_score = nullptr
    ) const;

    /**
     * Validate entry decision
     * @param decision Entry decision to validate
     * @return true if decision is valid and tradeable
     */
    bool validate_decision(const EntryDecision& decision) const;
};

} // namespace Core
} // namespace SDTrading

#endif // ENTRY_DECISION_ENGINE_H
