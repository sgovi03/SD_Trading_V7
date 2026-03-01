#include "zone_state_manager.h"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace SDTrading {
namespace Core {

ZoneStateManager::ZoneStateManager(const Config& cfg)
    : config(cfg) {
}

void ZoneStateManager::initialize_zone_states(std::vector<Zone>& zones) {
    // Set all loaded zones to TESTED state initially
    // Fresh zones will be marked as such during detection
    for (auto& zone : zones) {
        if (zone.state == ZoneState::FRESH) {
            // Keep FRESH state from detection
            stats.fresh_count++;
        } else {
            // Default loaded zones to TESTED
            zone.state = ZoneState::TESTED;
            stats.tested_count++;
        }
    }
    stats.total = static_cast<int>(zones.size());
}

void ZoneStateManager::update_zone_states(
    std::vector<Zone>& zones,
    const Bar& current_bar,
    int current_bar_index
) {
    // Reset statistics
    stats = StateStatistics();
    stats.total = static_cast<int>(zones.size());
    
    for (auto& zone : zones) {
        update_single_zone_state(zone, current_bar, current_bar_index);
        
        // Update statistics
        switch (zone.state) {
            case ZoneState::FRESH:
                stats.fresh_count++;
                break;
            case ZoneState::TESTED:
                stats.tested_count++;
                break;
            case ZoneState::VIOLATED:
                stats.violated_count++;
                break;
            case ZoneState::RECLAIMED:
                stats.reclaimed_count++;
                break;
        }
    }
}

void ZoneStateManager::update_single_zone_state(
    Zone& zone,
    const Bar& current_bar,
    int current_bar_index
) {
    (void)current_bar_index; // Unused - kept for future enhancements
    
    // Check if price interacted with zone
    bool touched = price_touched_zone(zone, current_bar);
    bool violated = price_violated_zone(zone, current_bar);
    
    // State transitions
    if (zone.state == ZoneState::FRESH) {
        if (violated) {
            // FRESH -> VIOLATED (swept through)
            zone.state = ZoneState::VIOLATED;
        } else if (touched) {
            // FRESH -> TESTED (first touch/retest)
            zone.state = ZoneState::TESTED;
        }
    } 
    else if (zone.state == ZoneState::TESTED) {
        if (violated) {
            // TESTED -> VIOLATED
            zone.state = ZoneState::VIOLATED;
        }
        // Otherwise stays TESTED (can be tested multiple times)
    }
    else if (zone.state == ZoneState::VIOLATED) {
        // Once violated, zone stays violated
        // (Could add RECLAIMED logic here if needed)
    }
}

bool ZoneStateManager::price_touched_zone(const Zone& zone, const Bar& bar) const {
    if (zone.type == ZoneType::DEMAND) {
        // DEMAND: touched if bar low went into zone
        return (bar.low <= zone.distal_line && bar.low >= zone.proximal_line);
    } else {
        // SUPPLY: touched if bar high went into zone
        return (bar.high >= zone.proximal_line && bar.high <= zone.distal_line);
    }
}

bool ZoneStateManager::price_violated_zone(const Zone& zone, const Bar& bar) const {
    if (zone.type == ZoneType::DEMAND) {
        // DEMAND violated if close is BELOW proximal (broke through support)
        return (bar.close < zone.proximal_line);
    } else {
        // SUPPLY violated if close is ABOVE distal (broke through resistance)
        return (bar.close > zone.distal_line);
    }
}

ZoneStateManager::StateStatistics ZoneStateManager::get_state_statistics() const {
    return stats;
}

} // namespace Core
} // namespace SDTrading
