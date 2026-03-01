#ifndef ZONE_STATE_MANAGER_H
#define ZONE_STATE_MANAGER_H

#include "common_types.h"
#include <vector>

namespace SDTrading {
namespace Core {

/**
 * Zone State Manager - Handles zone state lifecycle transitions
 * States: FRESH -> TESTED -> VIOLATED
 */
class ZoneStateManager {
public:
    struct StateStatistics {
        int total = 0;
        int fresh_count = 0;
        int tested_count = 0;
        int violated_count = 0;
        int reclaimed_count = 0;
    };

    explicit ZoneStateManager(const Config& cfg);
    
    // Initialize states for newly loaded/created zones
    void initialize_zone_states(std::vector<Zone>& zones);
    
    // Update zone states based on current bar
    void update_zone_states(
        std::vector<Zone>& zones,
        const Bar& current_bar,
        int current_bar_index
    );
    
    // Get statistics
    StateStatistics get_state_statistics() const;

private:
    const Config& config;
    StateStatistics stats;
    
    // Update a single zone's state
    void update_single_zone_state(
        Zone& zone,
        const Bar& current_bar,
        int current_bar_index
    );
    
    // Check if price touched zone
    bool price_touched_zone(const Zone& zone, const Bar& bar) const;
    
    // Check if price violated zone (closed through it)
    bool price_violated_zone(const Zone& zone, const Bar& bar) const;
};

} // namespace Core
} // namespace SDTrading

#endif // ZONE_STATE_MANAGER_H