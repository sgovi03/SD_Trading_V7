#include "volume_scorer.h"
#include "../utils/logger.h"
#include <algorithm>
#include <cmath>
#include <iomanip>

#include "../analysis/market_analyzer.h"
#include <sstream>

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

    // 1. Formation volume (original)
    profile.formation_volume = bars[formation_bar].volume;

    // 2. Time-of-day baseline
    std::string time_slot = extract_time_slot(bars[formation_bar].datetime);
    profile.avg_volume_baseline = volume_baseline_.get_baseline(time_slot);
    if (profile.avg_volume_baseline <= 0) {
        profile.avg_volume_baseline = 1.0; // Fallback
    }

    // 3. Formation volume ratio
    profile.volume_ratio = profile.formation_volume / profile.avg_volume_baseline;

    // ===== NEW: DEPARTURE/IMPULSE VOLUME TRACKING =====
    int impulse_start = formation_bar;
    int impulse_end = std::min(formation_bar + 7, static_cast<int>(bars.size()) - 1);
    double formation_price = (zone.type == Core::ZoneType::DEMAND) ? zone.distal_line : zone.proximal_line;
    double atr = SDTrading::Core::MarketAnalyzer::calculate_atr(bars, 14, formation_bar);
    for (int i = impulse_start + 1; i <= impulse_end; i++) {
        double price_move = std::abs(bars[i].close - formation_price);
        if (price_move < atr * 0.5) {
            impulse_end = i - 1; // Impulse ended
            break;
        }
    }
    double total_impulse_volume = 0;
    double peak_impulse_volume = 0;
    int impulse_bars = 0;
    for (int i = impulse_start; i <= impulse_end; i++) {
        total_impulse_volume += bars[i].volume;
        peak_impulse_volume = std::max(peak_impulse_volume, bars[i].volume);
        impulse_bars++;
    }
    if (impulse_bars > 0) {
        profile.departure_avg_volume = total_impulse_volume / impulse_bars;
        profile.departure_volume_ratio = profile.departure_avg_volume / profile.avg_volume_baseline;
        profile.departure_peak_volume = peak_impulse_volume;
        profile.departure_bar_count = impulse_bars;
        profile.strong_departure = (profile.departure_volume_ratio >= 2.0);
    }

    // ===== NEW: INITIATIVE vs ABSORPTION CHECK =====
    const Bar& formation = bars[formation_bar];
    double body = std::abs(formation.close - formation.open);
    double range = formation.high - formation.low;
    double body_pct = (range > 0) ? (body / range) : 0.0;
    double price_move = range;
    profile.volume_efficiency = (formation.volume > 0) ? (price_move / formation.volume) : 0.0;
    bool strong_body = (body_pct > 0.6);
    bool high_volume = (profile.volume_ratio > 1.5);
    bool small_body = (body_pct < 0.3);
    double wick_pct = 1.0 - body_pct;
    bool long_wicks = (wick_pct > 0.6);
    if (high_volume && strong_body) {
        profile.is_initiative = true;
    } else if (high_volume && small_body && long_wicks) {
        profile.is_initiative = false; // Absorption
    } else {
        profile.is_initiative = true; // Default to initiative
    }

    // ===== NEW: VOLUME CLIMAX DETECTION =====
    if (formation_bar > 0) {
        int last_consol_bar = formation_bar - 1;
        double last_bar_volume = bars[last_consol_bar].volume;
        std::string last_time_slot = extract_time_slot(bars[last_consol_bar].datetime);
        double last_baseline = volume_baseline_.get_baseline(last_time_slot);
        double last_bar_ratio = (last_baseline > 0) ? (last_bar_volume / last_baseline) : 1.0;
        profile.has_volume_climax = (last_bar_ratio > 2.5);
    }

    // 8. Original peak volume calculation
    int consol_start = std::max(0, formation_bar - 10);
    profile.peak_volume = find_peak_volume(bars, consol_start, formation_bar);
    profile.high_volume_bar_count = count_high_volume_bars(bars, consol_start, formation_bar);

    // 9. Calculate volume score (new logic)
    profile.volume_score = calculate_volume_score(profile, zone);

    LOG_DEBUG("VolumeProfile calculated: " + profile.to_string());
    return profile;
}


double VolumeScorer::calculate_volume_score(const VolumeProfile& profile, const Zone& zone) const {
    double score = 0.0;
    // COMPONENT 1: DEPARTURE VOLUME (0-25 points)
    if (profile.departure_volume_ratio > 3.0) {
        score += 25.0;
        LOG_DEBUG("EXTREME departure volume: " + std::to_string(profile.departure_volume_ratio) + "x baseline (+25pts)");
    } else if (profile.departure_volume_ratio > 2.0) {
        score += 20.0;
        LOG_DEBUG("STRONG departure volume: " + std::to_string(profile.departure_volume_ratio) + "x baseline (+20pts)");
    } else if (profile.departure_volume_ratio > 1.5) {
        score += 12.0;
        LOG_DEBUG("MODERATE departure volume: " + std::to_string(profile.departure_volume_ratio) + "x baseline (+12pts)");
    } else if (profile.departure_volume_ratio >= 1.0) {
        score += 5.0;
        LOG_DEBUG("NORMAL departure volume: " + std::to_string(profile.departure_volume_ratio) + "x baseline (+5pts)");
    } else {
        score -= 15.0;
        LOG_DEBUG("WEAK departure volume: " + std::to_string(profile.departure_volume_ratio) + "x baseline (-15pts penalty!)");
    }

    // COMPONENT 2: INITIATIVE vs ABSORPTION (0-15 points)
    if (profile.is_initiative) {
        score += 15.0;
        LOG_DEBUG("Initiative volume detected - clean institutional move (+15pts)");
    } else {
        score -= 20.0;
        LOG_DEBUG("ABSORPTION detected - high vol but small move (-20pts penalty!)");
    }

    // COMPONENT 3: FORMATION VOLUME (0-10 points)
    if (profile.volume_ratio > 2.5) {
        score += 10.0;
    } else if (profile.volume_ratio > 1.5) {
        score += 5.0;
    }

    // COMPONENT 4: VOLUME CLIMAX AT BASE (0-10 points)
    if (profile.has_volume_climax) {
        score += 10.0;
        LOG_DEBUG("Volume climax detected at base - absorption complete (+10pts)");
    }

    // COMPONENT 5: MULTI-TOUCH VOLUME TREND (penalty only)
    if (profile.volume_rising_on_retests) {
        score -= 30.0;
        LOG_DEBUG("Rising volume on retests detected (-30pts penalty!)");
    }

    // Clamp to valid range [0, 60]
    score = std::max(-30.0, std::min(60.0, score));
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
        std::string time_hhmm = datetime.substr(11, 5);  // Extract "HH:MM"
        
        try {
            // Parse hour and minute
            int hour = std::stoi(time_hhmm.substr(0, 2));
            int min = std::stoi(time_hhmm.substr(3, 2));
            
            // Round down to nearest 5-minute interval
            // 09:32 -> 09:30, 09:33 -> 09:30, 09:34 -> 09:30, 09:35 -> 09:35
            min = (min / 5) * 5;
            
            // Format back to string
            std::ostringstream oss;
            oss << std::setfill('0') << std::setw(2) << hour << ":"
                << std::setfill('0') << std::setw(2) << min;
            
            return oss.str();
        } catch (...) {
            // Fallback if parsing fails
            return "00:00";
        }
    }
    return "00:00"; // Fallback
}

double VolumeScorer::get_normalized_volume(const Bar& bar) const {
    std::string time_slot = extract_time_slot(bar.datetime);
    return volume_baseline_.get_normalized_ratio(time_slot, bar.volume);
}

} // namespace Core
} // namespace SDTrading