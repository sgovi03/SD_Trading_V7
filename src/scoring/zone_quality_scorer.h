#ifndef ZONE_QUALITY_SCORER_H
#define ZONE_QUALITY_SCORER_H

#include "scoring_types.h"
#include "common_types.h"

namespace SDTrading {
namespace Core {

class ZoneQualityScorer {
public:
    explicit ZoneQualityScorer(const Config& cfg);

    ZoneQualityScore calculate(const Zone& zone, const std::vector<Bar>& bars, int current_index) const;
    bool meets_threshold(double score) const;

private:
    const Config& config_;

    // NEW: Revamped scoring components
    double calculate_base_strength_score(const Zone& zone) const;
    double calculate_age_score(const Zone& zone, const Bar& current_bar) const;
    double calculate_rejection_score(const Zone& zone, const std::vector<Bar>& bars, int current_index) const;
    double calculate_touch_penalty(const Zone& zone) const;
    double calculate_breakthrough_penalty(const Zone& zone, const std::vector<Bar>& bars, int current_index) const;
    double calculate_elite_bonus(const Zone& zone, const Bar& current_bar) const;

    // Helper functions for rejection/breakthrough analysis
    double calculate_recent_rejection_rate(const Zone& zone, const std::vector<Bar>& bars, int current_index, int lookback_days) const;
    double calculate_breakthrough_rate(const Zone& zone, const std::vector<Bar>& bars, int current_index, int lookback_days) const;
    bool touches_zone(const Bar& bar, const Zone& zone) const;
    bool is_clean_rejection(const Bar& bar, const Zone& zone) const;
    bool is_breakthrough(const Bar& bar, const Zone& zone) const;

    int calculate_days_difference(const std::string& from_dt, const std::string& to_dt) const;
};

} // namespace Core
} // namespace SDTrading

#endif // ZONE_QUALITY_SCORER_H
