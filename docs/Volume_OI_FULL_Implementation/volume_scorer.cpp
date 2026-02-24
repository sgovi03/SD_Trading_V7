#include "volume_scorer.h"
#include "../utils/logger.h"
#include <algorithm>
#include <cmath>

namespace SDTrading {
namespace Core {

VolumeScorer::VolumeScorer(const Utils::VolumeBaseline& baseline)
    : volume_baseline_(baseline) {}

VolumeProfile VolumeScorer::calculate_volume_profile(
    const Zone& zone,
    const std::vector<Bar>& bars,
    int formation_bar
) const {
    VolumeProfile profile;
    
    if (formation_bar < 0 || formation_bar >= static_cast<int>(bars.size())) {
        LOG_WARN("Invalid formation_bar index: " + std::to_string(formation_bar));
        return profile;
    }
    
    // 1. Calculate formation volume
    profile.formation_volume = bars[formation_bar].volume;
    
    // 2. Get time-of-day baseline
    std::string time_slot = extract_time_slot(bars[formation_bar].datetime);
    profile.avg_volume_baseline = volume_baseline_.get_baseline(time_slot);
    
    // 3. Calculate volume ratio
    if (profile.avg_volume_baseline > 0) {
        profile.volume_ratio = profile.formation_volume / profile.avg_volume_baseline;
    } else {
        profile.volume_ratio = 1.0; // Fallback if baseline not available
    }
    
    // 4. Find consolidation range (estimate 3-10 bars before formation)
    int consolidation_start = std::max(0, formation_bar - 10);
    int consolidation_end = formation_bar;
    
    // 5. Find peak volume in consolidation
    profile.peak_volume = find_peak_volume(bars, consolidation_start, consolidation_end);
    
    // 6. Count high volume bars
    profile.high_volume_bar_count = count_high_volume_bars(
        bars, 
        consolidation_start, 
        consolidation_end
    );
    
    // 7. Calculate volume score
    profile.volume_score = calculate_volume_score(profile);
    
    LOG_DEBUG("VolumeProfile calculated: " + profile.to_string());
    
    return profile;
}

double VolumeScorer::calculate_volume_score(const VolumeProfile& profile) const {
    double score = 0.0;
    
    // Component 1: Formation Volume Ratio (0-20 points)
    // Extreme institutional activity (>3.0x)
    if (profile.volume_ratio > 3.0) {
        score += 20.0;
    }
    // High institutional activity (>2.0x)
    else if (profile.volume_ratio > 2.0) {
        score += 15.0;
    }
    // Moderate institutional activity (>1.5x)
    else if (profile.volume_ratio > 1.5) {
        score += 10.0;
    }
    // Below average (penalty)
    else if (profile.volume_ratio < 0.8) {
        score -= 10.0; // Low liquidity warning
    }
    
    // Component 2: Volume Clustering (0-10 points)
    // Sustained institutional interest
    if (profile.high_volume_bar_count >= 3) {
        score += 10.0;
    } else if (profile.high_volume_bar_count >= 2) {
        score += 5.0;
    }
    
    // Component 3: Peak Volume Amplification (0-10 points)
    // Very high peak volume indicates strong institutional participation
    if (profile.peak_volume > profile.avg_volume_baseline * 3.5) {
        score += 10.0;
    } else if (profile.peak_volume > profile.avg_volume_baseline * 2.5) {
        score += 5.0;
    }
    
    // Clamp to valid range [0, 40]
    score = std::max(0.0, std::min(40.0, score));
    
    return score;
}

double VolumeScorer::calculate_formation_volume_ratio(
    const std::vector<Bar>& bars,
    int formation_bar
) const {
    if (formation_bar < 0 || formation_bar >= static_cast<int>(bars.size())) {
        return 1.0;
    }
    
    std::string time_slot = extract_time_slot(bars[formation_bar].datetime);
    return volume_baseline_.get_normalized_ratio(time_slot, bars[formation_bar].volume);
}

double VolumeScorer::find_peak_volume(
    const std::vector<Bar>& bars,
    int start_index,
    int end_index
) const {
    double peak = 0.0;
    
    for (int i = start_index; i <= end_index && i < static_cast<int>(bars.size()); i++) {
        if (bars[i].volume > peak) {
            peak = bars[i].volume;
        }
    }
    
    return peak;
}

int VolumeScorer::count_high_volume_bars(
    const std::vector<Bar>& bars,
    int start_index,
    int end_index
) const {
    int count = 0;
    
    for (int i = start_index; i <= end_index && i < static_cast<int>(bars.size()); i++) {
        double norm_volume = get_normalized_volume(bars[i]);
        
        // High volume = >1.5x time-of-day average
        if (norm_volume > 1.5) {
            count++;
        }
    }
    
    return count;
}

bool VolumeScorer::has_low_volume_retest(
    const Zone& zone,
    const std::vector<Bar>& bars,
    int current_index
) const {
    // Look for recent touches (within last 20 bars)
    int lookback = std::min(20, current_index);
    
    for (int i = current_index - lookback; i < current_index; i++) {
        if (i < 0) continue;
        
        const Bar& bar = bars[i];
        
        // Check if bar touches zone
        bool touches_zone = false;
        if (zone.type == ZoneType::DEMAND) {
            touches_zone = (bar.low <= zone.proximal_line && bar.low >= zone.distal_line);
        } else {
            touches_zone = (bar.high >= zone.proximal_line && bar.high <= zone.distal_line);
        }
        
        if (touches_zone) {
            // Check if volume is low
            double norm_volume = get_normalized_volume(bar);
            if (norm_volume < 0.7) {
                return true; // Low volume retest found
            }
        }
    }
    
    return false;
}

std::string VolumeScorer::extract_time_slot(const std::string& datetime) const {
    // Expected format: "2024-02-08 09:15:00"
    if (datetime.length() >= 16) {
        return datetime.substr(11, 5); // Extract "09:15"
    }
    return "00:00"; // Fallback
}

double VolumeScorer::get_normalized_volume(const Bar& bar) const {
    std::string time_slot = extract_time_slot(bar.datetime);
    return volume_baseline_.get_normalized_ratio(time_slot, bar.volume);
}

} // namespace Core
} // namespace SDTrading
