#ifndef SDTRADING_VOLUME_SCORER_H
#define SDTRADING_VOLUME_SCORER_H

#include "common_types.h"
#include "../utils/volume_baseline.h"
#include <vector>

namespace SDTrading {
namespace Core {

/**
 * @class VolumeScorer
 * @brief Calculates volume-based metrics and scores for zones
 * 
 * Analyzes volume patterns during zone formation to identify
 * institutional participation and trading quality.
 */
class VolumeScorer {
public:
    /**
     * Constructor
     * @param baseline Reference to loaded volume baseline (time-of-day averages)
     */
    explicit VolumeScorer(const Utils::VolumeBaseline& baseline);
    
    /**
     * Calculate complete volume profile for a zone
     * @param zone Zone being analyzed
     * @param bars Price/volume data
     * @param formation_bar Index of zone formation
     * @return Complete VolumeProfile structure
     */
    VolumeProfile calculate_volume_profile(
        const Zone& zone,
        const std::vector<Bar>& bars,
        int formation_bar
    ) const;
    
    /**
     * Calculate volume score (0-40 points)
     * @param volume_profile Calculated volume profile
     * @return Volume score contribution
     */
    double calculate_volume_score(const VolumeProfile& volume_profile) const;

private:
    const Utils::VolumeBaseline& volume_baseline_;
    
    /**
     * Calculate formation volume ratio
     * @param bars Price/volume data
     * @param formation_bar Zone formation index
     * @return Volume ratio vs time-of-day baseline
     */
    double calculate_formation_volume_ratio(
        const std::vector<Bar>& bars,
        int formation_bar
    ) const;
    
    /**
     * Find peak volume during zone formation
     * @param bars Price/volume data
     * @param start_index Start of consolidation
     * @param end_index End of consolidation
     * @return Peak volume value
     */
    double find_peak_volume(
        const std::vector<Bar>& bars,
        int start_index,
        int end_index
    ) const;
    
    /**
     * Count high volume bars during formation
     * @param bars Price/volume data
     * @param start_index Start of consolidation
     * @param end_index End of consolidation
     * @return Count of bars with >1.5x normalized volume
     */
    int count_high_volume_bars(
        const std::vector<Bar>& bars,
        int start_index,
        int end_index
    ) const;
    
    /**
     * Check if zone has low volume retest pattern
     * @param zone Zone being checked
     * @param bars Price/volume data
     * @param current_index Current bar index
     * @return true if low volume retest detected
     */
    bool has_low_volume_retest(
        const Zone& zone,
        const std::vector<Bar>& bars,
        int current_index
    ) const;
    
    /**
     * Extract time slot from datetime string
     * @param datetime DateTime string (format: "YYYY-MM-DD HH:MM:SS")
     * @return Time slot string (format: "HH:MM")
     */
    std::string extract_time_slot(const std::string& datetime) const;
    
    /**
     * Get normalized volume ratio for a bar
     * @param bar Bar to analyze
     * @return Normalized volume ratio
     */
    double get_normalized_volume(const Bar& bar) const;
};

} // namespace Core
} // namespace SDTrading

#endif // SDTRADING_VOLUME_SCORER_H
