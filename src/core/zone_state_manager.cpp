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
    
    // ⭐ FIX: Understand zone boundaries correctly
    // For DEMAND: proximal_line is TOP (higher price), distal_line is BOTTOM (lower price)
    // For SUPPLY: proximal_line is BOTTOM (lower price), distal_line is TOP (higher price)
    
    double zone_top = std::max(zone.proximal_line, zone.distal_line);
    double zone_bottom = std::min(zone.proximal_line, zone.distal_line);
    
    // Check if price touched zone (bar overlaps zone body)
    bool touched = (current_bar.low <= zone_top && current_bar.high >= zone_bottom);
    
    // Check if zone was violated (price closed THROUGH the zone)
    bool violated = false;
    if (zone.type == ZoneType::DEMAND) {
        // DEMAND violated if close is BELOW the zone bottom
        violated = (current_bar.close < zone_bottom);
    } else {
        // SUPPLY violated if close is ABOVE the zone top
        violated = (current_bar.close > zone_top);
    }
    
    // State transitions
    if (zone.state == ZoneState::FRESH) {
        if (violated) {
            // FRESH -> VIOLATED (swept through without bounce)
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
    double zone_top = std::max(zone.proximal_line, zone.distal_line);
    double zone_bottom = std::min(zone.proximal_line, zone.distal_line);
    return (bar.low <= zone_top && bar.high >= zone_bottom);
}

bool ZoneStateManager::price_violated_zone(const Zone& zone, const Bar& bar) const {
    double zone_top = std::max(zone.proximal_line, zone.distal_line);
    double zone_bottom = std::min(zone.proximal_line, zone.distal_line);
    
    if (zone.type == ZoneType::DEMAND) {
        // DEMAND violated if close is BELOW zone bottom
        return (bar.close < zone_bottom);
    } else {
        // SUPPLY violated if close is ABOVE zone top
        return (bar.close > zone_top);
    }
}

ZoneStateManager::StateStatistics ZoneStateManager::get_state_statistics() const {
    return stats;
}

} // namespace Core
} // namespace SDTrading