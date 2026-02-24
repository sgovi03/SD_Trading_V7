#include "oi_scorer.h"
#include "../utils/logger.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace SDTrading {
namespace Core {

OIProfile OIScorer::calculate_oi_profile(
    const Zone& zone,
    const std::vector<Bar>& bars,
    int formation_bar
) const {
    OIProfile profile;
    
    if (formation_bar < 0 || formation_bar >= static_cast<int>(bars.size())) {
        LOG_WARN("Invalid formation_bar for OI profile: " + std::to_string(formation_bar));
        return profile;
    }
    
    // 1. Get formation OI
    profile.formation_oi = static_cast<long>(bars[formation_bar].oi);
    
    // 2. Estimate consolidation range
    int consolidation_start = std::max(0, formation_bar - 10);
    int consolidation_end = formation_bar;
    
    // 3. Calculate OI change during formation
    if (consolidation_start < static_cast<int>(bars.size())) {
        long start_oi = static_cast<long>(bars[consolidation_start].oi);
        long end_oi = static_cast<long>(bars[consolidation_end].oi);
        
        profile.oi_change_during_formation = end_oi - start_oi;
        
        if (start_oi > 0) {
            profile.oi_change_percent = 
                (static_cast<double>(profile.oi_change_during_formation) / start_oi) * 100.0;
        }
    }
    
    // 4. Calculate price-OI correlation
    profile.price_oi_correlation = calculate_price_oi_correlation(
        bars,
        consolidation_start,
        consolidation_end
    );
    
    // 5. Check OI data quality
    profile.oi_data_quality = check_oi_data_quality(
        bars,
        consolidation_start,
        consolidation_end
    );
    
    // 6. Detect market phase
    profile.market_phase = detect_market_phase(bars, formation_bar, 10);
    
    // 7. Calculate OI score
    profile.oi_score = calculate_oi_score(profile, zone.type);
    
    LOG_DEBUG("OIProfile calculated: " + profile.to_string());
    
    return profile;
}

double OIScorer::calculate_oi_score(
    const OIProfile& oi_profile,
    ZoneType zone_type
) const {
    double score = 0.0;
    
    // Only score if OI data quality is good
    if (!oi_profile.oi_data_quality) {
        LOG_DEBUG("OI data quality poor - returning zero score");
        return 0.0;
    }
    
    // Component 1: OI Alignment (0-20 points)
    // Perfect alignment for zone type
    if (zone_type == ZoneType::DEMAND) {
        // DEMAND zone: Want price falling + OI rising (shorts trapped)
        if (oi_profile.oi_change_percent > 0.3 && oi_profile.price_oi_correlation < -0.5) {
            score += 20.0; // Perfect bearish OI buildup into demand (threshold reduced: 1.0% -> 0.3%)
        } else if (oi_profile.oi_change_percent > 0.1) {
            score += 10.0; // Some OI buildup (threshold reduced: 0.5% -> 0.1%)
        }
    } else { // SUPPLY
        // SUPPLY zone: Want price rising + OI rising (longs trapped)
        if (oi_profile.oi_change_percent > 0.3 && oi_profile.price_oi_correlation > 0.5) {
            score += 20.0; // Perfect bullish OI buildup into supply (threshold reduced: 1.0% -> 0.3%)
        } else if (oi_profile.oi_change_percent > 0.1) {
            score += 10.0; // Some OI buildup (threshold reduced: 0.5% -> 0.1%)
        }
    }
    
    // Component 2: Market Phase Alignment (0-10 points)
    if (zone_type == ZoneType::DEMAND) {
        // Favorable phases for DEMAND zones
        if (oi_profile.market_phase == MarketPhase::SHORT_BUILDUP ||
            oi_profile.market_phase == MarketPhase::LONG_UNWINDING) {
            score += 10.0;
        }
    } else { // SUPPLY
        // Favorable phases for SUPPLY zones
        if (oi_profile.market_phase == MarketPhase::LONG_BUILDUP ||
            oi_profile.market_phase == MarketPhase::SHORT_COVERING) {
            score += 10.0;
        }
    }
    
    // Clamp to valid range [0, 30]
    score = std::max(0.0, std::min(30.0, score));
    
    return score;
}

MarketPhase OIScorer::detect_market_phase(
    const std::vector<Bar>& bars,
    int current_index,
    int lookback
) const {
    if (current_index < lookback || current_index >= static_cast<int>(bars.size())) {
        return MarketPhase::NEUTRAL;
    }
    
    int start_index = current_index - lookback;
    
    // Calculate price and OI changes
    double price_change_pct = calculate_price_change_percent(bars, start_index, current_index);
    double oi_change_pct = calculate_oi_change_percent(bars, start_index, current_index);
    
    // Define thresholds - calibrated for short 10-bar consolidation windows
    const double price_threshold = 0.2;  // 0.2% price movement
    const double oi_threshold = 0.2;     // 0.2% OI change (reduced from 0.5% for short-term sensitivity)
    
    // Detect phase using Price-OI quadrants
    if (price_change_pct > price_threshold && oi_change_pct > oi_threshold) {
        return MarketPhase::LONG_BUILDUP;      // Price ↑ + OI ↑ (bullish)
    } else if (price_change_pct > price_threshold && oi_change_pct < -oi_threshold) {
        return MarketPhase::SHORT_COVERING;    // Price ↑ + OI ↓ (temporary bullish)
    } else if (price_change_pct < -price_threshold && oi_change_pct > oi_threshold) {
        return MarketPhase::SHORT_BUILDUP;     // Price ↓ + OI ↑ (bearish)
    } else if (price_change_pct < -price_threshold && oi_change_pct < -oi_threshold) {
        return MarketPhase::LONG_UNWINDING;    // Price ↓ + OI ↓ (temporary bearish)
    }
    
    return MarketPhase::NEUTRAL;
}

double OIScorer::calculate_price_oi_correlation(
    const std::vector<Bar>& bars,
    int start_index,
    int end_index
) const {
    std::vector<double> prices;
    std::vector<double> ois;
    
    for (int i = start_index; i <= end_index && i < static_cast<int>(bars.size()); i++) {
        prices.push_back(bars[i].close);
        ois.push_back(bars[i].oi);
    }
    
    if (prices.size() < 3) {
        return 0.0; // Not enough data
    }
    
    // Calculate means
    double price_mean = std::accumulate(prices.begin(), prices.end(), 0.0) / prices.size();
    double oi_mean = std::accumulate(ois.begin(), ois.end(), 0.0) / ois.size();
    
    // Calculate covariance and standard deviations
    double covariance = 0.0;
    double price_variance = 0.0;
    double oi_variance = 0.0;
    
    for (size_t i = 0; i < prices.size(); i++) {
        double price_diff = prices[i] - price_mean;
        double oi_diff = ois[i] - oi_mean;
        
        covariance += price_diff * oi_diff;
        price_variance += price_diff * price_diff;
        oi_variance += oi_diff * oi_diff;
    }
    
    // Calculate correlation coefficient
    double denominator = std::sqrt(price_variance * oi_variance);
    if (denominator < 0.0001) {
        return 0.0; // Avoid division by zero
    }
    
    double correlation = covariance / denominator;
    
    // Clamp to [-1, 1]
    return std::max(-1.0, std::min(1.0, correlation));
}

double OIScorer::calculate_oi_change_percent(
    const std::vector<Bar>& bars,
    int start_index,
    int end_index
) const {
    if (start_index < 0 || end_index >= static_cast<int>(bars.size()) || start_index >= end_index) {
        return 0.0;
    }
    
    double start_oi = bars[start_index].oi;
    double end_oi = bars[end_index].oi;
    
    if (start_oi < 1.0) {
        return 0.0; // Avoid division by zero
    }
    
    return ((end_oi - start_oi) / start_oi) * 100.0;
}

bool OIScorer::check_oi_data_quality(
    const std::vector<Bar>& bars,
    int start_index,
    int end_index
) const {
    int fresh_oi_count = 0;
    int total_bars = 0;
    
    for (int i = start_index; i <= end_index && i < static_cast<int>(bars.size()); i++) {
        total_bars++;
        if (bars[i].oi_fresh) {
            fresh_oi_count++;
        }
    }
    
    if (total_bars == 0) {
        return false;
    }
    
    // Require at least 30% fresh OI data
    double fresh_ratio = static_cast<double>(fresh_oi_count) / total_bars;
    return (fresh_ratio >= 0.3);
}

double OIScorer::calculate_price_change_percent(
    const std::vector<Bar>& bars,
    int start_index,
    int end_index
) const {
    if (start_index < 0 || end_index >= static_cast<int>(bars.size()) || start_index >= end_index) {
        return 0.0;
    }
    
    double start_price = bars[start_index].close;
    double end_price = bars[end_index].close;
    
    if (start_price < 0.01) {
        return 0.0; // Avoid division by zero
    }
    
    return ((end_price - start_price) / start_price) * 100.0;
}

} // namespace Core
} // namespace SDTrading
