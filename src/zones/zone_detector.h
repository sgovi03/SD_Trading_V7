#ifndef ZONE_DETECTOR_H
#define ZONE_DETECTOR_H

#include <vector>
#include <algorithm>
#include <cmath>
#include "common_types.h"

// Forward declarations for V6.0
namespace SDTrading {
    namespace Utils { class VolumeBaseline; }
    namespace Core { 
        class VolumeScorer; 
        class OIScorer; 
    }
}

namespace SDTrading {
namespace Core {

/**
 * @class ZoneDetector
 * @brief Detects institutional supply and demand zones from price data
 * 
 * This class implements the core zone detection algorithm that identifies
 * consolidation bases followed by impulsive moves. It includes:
 * - Consolidation detection
 * - Impulse validation (before and after base)
 * - Elite zone qualification (departure, speed, patience)
 * - Swing position analysis
 * - Zone strength calculation
 */
class ZoneDetector {
private:
    const Config& config;
    std::vector<Bar> bars;
    mutable std::vector<double> atr_cache;  // Cache ATR values for performance
    mutable bool atr_cache_valid = false;

    struct ZoneReference {
        bool found = false;
        bool in_existing = false;
        size_t index = 0;
    };

    /**
     * Precompute ATR values for all bars
     * This optimization speeds up zone detection by 10-20x
     */
    void build_atr_cache() const;
    
    /**
     * Get cached ATR value for a bar index
     * @param index Bar index
     * @return ATR value at that index
     */
    double get_cached_atr(int index) const;

    /**
     * Check if price range represents consolidation
     * @param start_index Starting bar index
     * @param length Number of bars to check
     * @param high Output: highest price in range
     * @param low Output: lowest price in range
     * @return true if meets consolidation criteria
     */
    bool is_consolidation(int start_index, int length, double& high, double& low) const;

    /**
     * Check for impulsive move before consolidation
     * @param index Bar index to check before
     * @return true if impulse detected
     */
    bool has_impulse_before(int index) const;

    /**
     * Check for impulsive move after consolidation
     * @param index Bar index to check after
     * @return true if impulse detected
     */
    bool has_impulse_after(int index) const;

    /**
     * Calculate zone strength based on consolidation tightness
     * @param zone Zone to evaluate
     * @return Strength score 0-100
     */
    double calculate_zone_strength(const Zone& zone) const;

    /**
     * Analyze elite zone qualities (departure, speed, patience)
     * @param zone Zone to analyze (modified in place)
     */
    void analyze_elite_quality(Zone& zone);

    /**
     * Analyze swing position relative to recent highs/lows
     * @param zone Zone to analyze (modified in place)
     * @param lookback Bars to look back
     * @param lookahead Bars to look ahead
     */
    void analyze_swing_position(Zone& zone, int lookback = 20, int lookahead = 10);

    ZoneReference find_existing_zone_at_level(
        const Zone& candidate_zone,
        const std::vector<Zone>* existing_zones,
        const std::vector<Zone>& new_zones,
        double tolerance_price
    ) const;

    enum class ZoneDecision {
        CREATE_NEW,
        MERGE_WITH_EXISTING,
        SKIP
    };

    ZoneDecision evaluate_zone_candidate(
        const Zone& candidate_zone,
        const std::vector<Zone>* existing_zones,
        const std::vector<Zone>& new_zones,
        ZoneReference& reference
    ) const;

    void merge_into_existing_zone(Zone& existing, const Zone& candidate);

public:
    struct ZoneDetectionSummary {
        int created = 0;
        int merged = 0;
        int skipped = 0;
        int detected = 0;
    };

    /**
     * Constructor
     * @param cfg Configuration settings
     */
    explicit ZoneDetector(const Config& cfg);

    /**
     * Add a new bar to the dataset
     * @param bar Bar data to add
     */
    void add_bar(const Bar& bar);

    /**
     * Update the last bar (for real-time updates within same bar)
     * @param bar Updated bar data
     */
    void update_last_bar(const Bar& bar);

    /**
     * Detect all valid zones in current bar data
     * @return Vector of detected zones
     */
    // std::vector<Zone> detect_zones();
    std::vector<Zone> detect_zones(int start_bar_override = -1);

    /**
     * Detect zones and prevent duplicates using existing zones
     * @param existing_zones Active zones used for duplicate checks
     * @param start_bar_override Optional start index
     * @return Newly detected zones (duplicates skipped/merged)
     */
    std::vector<Zone> detect_zones_no_duplicates(
        std::vector<Zone>& existing_zones,
        int start_bar_override = -1
    );

    /**
     * Detect zones across FULL dataset (for backtesting)
     * Scans entire dataset instead of just trailing window
     * This is the FIX for the V3.x Ghost bug
     * @return Vector of all detected zones
     */
    std::vector<Zone> detect_zones_full();

    /**
     * Get reference to bar data
     * @return Const reference to bars
     */
    const std::vector<Bar>& get_bars() const { return bars; }

    /**
     * Clear all bar data
     */
    void clear() { bars.clear(); }

    /**
     * Get number of bars loaded
     * @return Bar count
     */
    size_t bar_count() const { return bars.size(); }

    /**
     * Zone IDs merged into existing zones during last detection pass
     */
    const std::vector<int>& get_last_merged_zone_ids() const { return last_merged_zone_ids_; }

    /**
     * Last detection summary (created/merged/skipped counts)
     */
    const ZoneDetectionSummary& get_last_detection_summary() const { return last_detection_summary_; }

    // ========================================
    // V6.0: Volume/OI Integration
    // ========================================
    
    /**
     * Set volume baseline for V6.0 volume scoring
     * @param baseline Pointer to loaded volume baseline
     */
    void set_volume_baseline(Utils::VolumeBaseline* baseline);

private:
    std::vector<int> last_merged_zone_ids_;
    ZoneDetectionSummary last_detection_summary_;
    
    // ========================================
    // V6.0: Volume/OI Scoring Components
    // ========================================
    Utils::VolumeBaseline* volume_baseline_;
    Core::VolumeScorer* volume_scorer_;
    Core::OIScorer* oi_scorer_;
    
    /**
     * Calculate V6.0 zone enhancements (volume/OI profiles)
     * @param zone Zone to enhance
     * @param formation_bar Formation bar index
     */
    void calculate_zone_enhancements(Zone& zone, int formation_bar);
    
    /**
     * Calculate initial touch count for a newly detected zone
     * @param zone The zone to calculate touches for
     * @param bars Historical bar data
     * @param formation_index Index where zone was formed
     * @return Number of times price touched the zone after formation
     */
    int calculate_initial_touch_count(const Zone& zone, 
                                      const std::vector<Bar>& bars, 
                                      int formation_index) const;
};

} // namespace Core
} // namespace SDTrading

#endif // ZONE_DETECTOR_H
