#ifndef SDTRADING_OI_SCORER_H
#define SDTRADING_OI_SCORER_H

#include "common_types.h"
#include <vector>

namespace SDTrading {
namespace Core {

/**
 * @class OIScorer
 * @brief Calculates Open Interest-based metrics and scores for zones
 * 
 * Analyzes OI patterns to identify market commitment, position buildup,
 * and detect smart money activity for index futures (NIFTY/BANKNIFTY).
 */
class OIScorer {
public:
    OIScorer() = default;
    
    /**
     * Calculate complete OI profile for a zone
     * @param zone Zone being analyzed
     * @param bars Price/OI data
     * @param formation_bar Index of zone formation
     * @return Complete OIProfile structure
     */
    OIProfile calculate_oi_profile(
        const Zone& zone,
        const std::vector<Bar>& bars,
        int formation_bar
    ) const;
    
    /**
     * Calculate OI score (0-30 points)
     * @param oi_profile Calculated OI profile
     * @param zone_type Type of zone (DEMAND or SUPPLY)
     * @return OI score contribution
     */
    double calculate_oi_score(
        const OIProfile& oi_profile,
        ZoneType zone_type
    ) const;
    
    /**
     * Detect current market phase using Price-OI correlation
     * @param bars Price/OI data
     * @param current_index Current bar index
     * @param lookback Number of bars to analyze (default: 10)
     * @return Detected MarketPhase
     */
    MarketPhase detect_market_phase(
        const std::vector<Bar>& bars,
        int current_index,
        int lookback = 10
    ) const;

private:
    /**
     * Calculate price-OI correlation coefficient
     * @param bars Price/OI data
     * @param start_index Start of analysis window
     * @param end_index End of analysis window
     * @return Correlation coefficient (-1 to +1)
     */
    double calculate_price_oi_correlation(
        const std::vector<Bar>& bars,
        int start_index,
        int end_index
    ) const;
    
    /**
     * Calculate OI change percentage
     * @param bars Price/OI data
     * @param start_index Start bar
     * @param end_index End bar
     * @return OI change percentage
     */
    double calculate_oi_change_percent(
        const std::vector<Bar>& bars,
        int start_index,
        int end_index
    ) const;
    
    /**
     * Check if OI data quality is good
     * @param bars Price/OI data
     * @param start_index Start of analysis window
     * @param end_index End of analysis window
     * @return true if at least 50% of bars have fresh OI
     */
    bool check_oi_data_quality(
        const std::vector<Bar>& bars,
        int start_index,
        int end_index
    ) const;
    
    /**
     * Calculate price change percentage
     * @param bars Price data
     * @param start_index Start bar
     * @param end_index End bar
     * @return Price change percentage
     */
    double calculate_price_change_percent(
        const std::vector<Bar>& bars,
        int start_index,
        int end_index
    ) const;
};

} // namespace Core
} // namespace SDTrading

#endif // SDTRADING_OI_SCORER_H
