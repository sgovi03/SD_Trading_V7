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
    if (oi_profile.oi_change_percent > 0.3) {
        index += 25.0;
    } else if (oi_profile.oi_change_percent > 0.1) {
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
