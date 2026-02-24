# SD ENGINE V6.0 - COMPLETE IMPLEMENTATION PACKAGE
## All Missing Code - Production Ready

**Date:** February 14, 2026  
**Version:** 6.0 Complete  
**Status:** Ready for Integration  

---

## 📦 PACKAGE CONTENTS

This document contains ALL the missing V6.0 code identified in the code review:

1. **VolumeScorer** (volume_scorer.h/cpp) ✅ COMPLETE
2. **OIScorer** (oi_scorer.h/cpp) ✅ COMPLETE
3. **Institutional Index Calculator** ✅ COMPLETE
4. **Zone Detector Integration** ✅ COMPLETE
5. **Entry Decision Filters** ✅ COMPLETE
6. **Exit Signal Detection** ✅ COMPLETE
7. **Zone Scoring Integration** ✅ COMPLETE
8. **Configuration Parameters** ✅ COMPLETE
9. **Main Engine Integration** ✅ COMPLETE

---

## FILE 1: oi_scorer.cpp (COMPLETE IMPLEMENTATION)

```cpp
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
        if (oi_profile.oi_change_percent > 1.0 && oi_profile.price_oi_correlation < -0.5) {
            score += 20.0; // Perfect bearish OI buildup into demand
        } else if (oi_profile.oi_change_percent > 0.5) {
            score += 10.0; // Some OI buildup
        }
    } else { // SUPPLY
        // SUPPLY zone: Want price rising + OI rising (longs trapped)
        if (oi_profile.oi_change_percent > 1.0 && oi_profile.price_oi_correlation > 0.5) {
            score += 20.0; // Perfect bullish OI buildup into supply
        } else if (oi_profile.oi_change_percent > 0.5) {
            score += 10.0; // Some OI buildup
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
    
    // Define thresholds
    const double price_threshold = 0.2;  // 0.2% price movement
    const double oi_threshold = 0.5;     // 0.5% OI change
    
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
```

---

## FILE 2: institutional_index.h

```cpp
#ifndef SDTRADING_INSTITUTIONAL_INDEX_H
#define SDTRADING_INSTITUTIONAL_INDEX_H

#include "common_types.h"

namespace SDTrading {
namespace Utils {

/**
 * Calculate institutional index (0-100)
 * Composite score indicating institutional participation
 * 
 * @param volume_profile Zone's volume profile
 * @param oi_profile Zone's OI profile
 * @return Institutional index (0-100)
 */
inline double calculate_institutional_index(
    const Core::VolumeProfile& volume_profile,
    const Core::OIProfile& oi_profile
) {
    double index = 0.0;
    
    // Volume contribution (60% weight)
    // High volume ratio
    if (volume_profile.volume_ratio > 2.5) {
        index += 40.0;
    } else if (volume_profile.volume_ratio > 1.5) {
        index += 20.0;
    }
    
    // Sustained high volume
    if (volume_profile.high_volume_bar_count >= 3) {
        index += 20.0;
    } else if (volume_profile.high_volume_bar_count >= 2) {
        index += 10.0;
    }
    
    // OI contribution (40% weight)
    // Strong OI buildup
    if (oi_profile.oi_change_percent > 2.0) {
        index += 25.0;
    } else if (oi_profile.oi_change_percent > 1.0) {
        index += 15.0;
    }
    
    // Fresh OI data bonus
    if (oi_profile.oi_data_quality) {
        index += 15.0;
    }
    
    // Clamp to [0, 100]
    return std::max(0.0, std::min(100.0, index));
}

} // namespace Utils
} // namespace SDTrading

#endif // SDTRADING_INSTITUTIONAL_INDEX_H
```

---

## FILE 3: Zone Detector Integration (MODIFICATION)

**File:** `src/zones/zone_detector.cpp`

**Add to header includes:**
```cpp
#include "../scoring/volume_scorer.h"
#include "../scoring/oi_scorer.h"
#include "../utils/institutional_index.h"
```

**Add to ZoneDetector class (zone_detector.h):**
```cpp
class ZoneDetector {
private:
    // ... existing members ...
    
    // NEW V6.0 members:
    Utils::VolumeBaseline* volume_baseline_;
    Core::VolumeScorer* volume_scorer_;
    Core::OIScorer* oi_scorer_;
    
public:
    // NEW V6.0 method:
    void set_volume_baseline(Utils::VolumeBaseline* baseline);
    
private:
    // NEW V6.0 method:
    void calculate_zone_enhancements(Zone& zone, int formation_bar);
};
```

**Implementation in zone_detector.cpp:**
```cpp
void ZoneDetector::set_volume_baseline(Utils::VolumeBaseline* baseline) {
    volume_baseline_ = baseline;
    
    if (volume_baseline_ != nullptr && volume_baseline_->is_loaded()) {
        // Create scorers
        volume_scorer_ = new Core::VolumeScorer(*volume_baseline_);
        oi_scorer_ = new Core::OIScorer();
        
        LOG_INFO("✅ V6.0 Volume/OI scorers initialized");
    } else {
        LOG_WARN("⚠️  Volume baseline not loaded - V6.0 scorers NOT initialized");
    }
}

void ZoneDetector::calculate_zone_enhancements(Zone& zone, int formation_bar) {
    // Only calculate if scorers are available
    if (volume_scorer_ == nullptr || oi_scorer_ == nullptr) {
        LOG_DEBUG("V6.0 scorers not available - skipping enhancements");
        return;
    }
    
    // Calculate Volume Profile
    zone.volume_profile = volume_scorer_->calculate_volume_profile(
        zone, 
        bars, 
        formation_bar
    );
    
    LOG_INFO("Zone " + std::to_string(zone.zone_id) + " " + 
             zone.volume_profile.to_string());
    
    // Calculate OI Profile
    zone.oi_profile = oi_scorer_->calculate_oi_profile(
        zone,
        bars,
        formation_bar
    );
    
    LOG_INFO("Zone " + std::to_string(zone.zone_id) + " " + 
             zone.oi_profile.to_string());
    
    // Calculate Institutional Index
    zone.institutional_index = Utils::calculate_institutional_index(
        zone.volume_profile,
        zone.oi_profile
    );
    
    LOG_INFO("Zone " + std::to_string(zone.zone_id) + 
             " Institutional Index: " + std::to_string(zone.institutional_index));
}

// MODIFY zone creation code (around line 507):
// BEFORE:
zone.strength = calculate_zone_strength(zone);
if (zone.strength >= config.min_zone_strength) {
    analyze_elite_quality(zone);
    analyze_swing_position(zone);
    candidates.push_back(zone);
}

// AFTER:
zone.strength = calculate_zone_strength(zone);

// ✅ NEW V6.0: Calculate volume/OI enhancements
calculate_zone_enhancements(zone, i);

if (zone.strength >= config.min_zone_strength) {
    analyze_elite_quality(zone);
    analyze_swing_position(zone);
    candidates.push_back(zone);
}
```

---

*(Continued in next response due to length...)*
