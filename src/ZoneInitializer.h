#pragma once

#include "common_types.h"
#include "zones/zone_detector.h"
#include "utils/logger.h"
#include <vector>
#include <cmath>

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
        std::vector<Zone> raw_zones = detector.detect_zones_full();
        
        LOG_INFO("Detected " << raw_zones.size() << " zones (before dedup)");

        // ⭐ BUG 3 FIX: Deduplicate zones by formation_bar + boundary before assigning IDs.
        //
        // ROOT CAUSE: detector.detect_zones_full() scans all bars sequentially via
        // add_bar(). The zone detector's internal dedup (detect_zones_no_duplicates)
        // only prevents exact-match returns within a single call. When the detector
        // internally re-examines the same price level across multiple consolidation
        // windows it can emit the same zone (same formation_bar, same proximal/distal)
        // multiple times in the raw output. For fast moves like Mar-09 2026, this
        // produces 17-18 copies of 23676–23705 all with formation_bar=11186.
        //
        // Fix: linear dedup pass — for each raw zone, accept it only if no already-
        // accepted zone shares formation_bar AND has proximal/distal within 1.0 pts.
        // Tolerance of 1.0 pt matches the process_zones() secondary check and the
        // run() periodic detection check, making all three paths consistent.
        std::vector<Zone> initial_zones;
        initial_zones.reserve(raw_zones.size());
        int duplicates_removed = 0;

        for (auto& zone : raw_zones) {
            bool already_present = false;
            for (const auto& accepted : initial_zones) {
                if (zone.formation_bar == accepted.formation_bar &&
                    std::abs(zone.proximal_line - accepted.proximal_line) < 1.0 &&
                    std::abs(zone.distal_line   - accepted.distal_line)   < 1.0) {
                    already_present = true;
                    duplicates_removed++;
                    break;
                }
            }
            if (!already_present) {
                initial_zones.push_back(zone);
            }
        }

        if (duplicates_removed > 0) {
            LOG_INFO("Dedup removed " << duplicates_removed << " duplicate zones ("
                     << raw_zones.size() << " raw → " << initial_zones.size() << " unique)");
        }

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