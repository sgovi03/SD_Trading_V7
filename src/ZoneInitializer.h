#pragma once

#include "common_types.h"
#include "zones/zone_detector.h"
#include "utils/logger.h"
#include <vector>

namespace SDTrading {
namespace Core {

/**
 * @brief Shared zone initialization logic for both backtest and live engines
 * Ensures identical zone detection across both engines
 */
class ZoneInitializer {
public:
    /**
     * Detect initial zones from a complete bar history
     * Both backtest and live engines should use this function
     * 
     * @param bars Historical bars to scan
     * @param detector Zone detector instance (must be initialized with bars)
     * @param next_zone_id Output: next zone ID to assign
     * @return Vector of detected zones with assigned IDs
     */
    static std::vector<Zone> detect_initial_zones(
        const std::vector<Bar>& bars,
        ZoneDetector& detector,
        int& next_zone_id
    ) {
        LOG_INFO("=== INITIAL ZONE DETECTION ===");
        LOG_INFO("Scanning " << bars.size() << " bars for zones...");
        
        // Detect zones using full dataset scan
        std::vector<Zone> initial_zones = detector.detect_zones_full();
        
        LOG_INFO("Detected " << initial_zones.size() << " zones");
        
        // Assign IDs to all zones
        for (auto& zone : initial_zones) {
            zone.zone_id = next_zone_id++;
        }
        
        LOG_INFO("Assigned zone IDs (last ID: " << (next_zone_id - 1) << ")");
        LOG_INFO("=== ZONE DETECTION COMPLETE ===");
        
        return initial_zones;
    }
};

} // namespace Core
} // namespace SDTrading
