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

        // ⭐ CONFLICTING ZONE FIX (bootstrap) — V7 parity.
        //
        // ROOT CAUSE of V8 overtrade vs V7:
        // V7 backtest builds active_zones incrementally bar-by-bar and applies
        // conflicting zone invalidation EACH TIME a zone is added:
        //   "new SUPPLY at price X → mark older DEMAND at X as VIOLATED"
        // This means a price level can only have ONE active zone type at a time.
        //
        // V8 bootstrap detects ALL zones at once (detect_zones_full) and loads
        // them simultaneously — skipping the incremental conflict resolution.
        // Result: zones that V7 would have VIOLATED during detection remain
        // FRESH/TESTED in V8, creating 200 tradeable zones vs V7's ~30.
        // Clone zones at the same price accumulate entries on broken levels.
        //
        // Fix: sort by formation_bar (chronological), then walk the list exactly
        // as V7's detect_and_add_zones loop does — applying the 50% overlap
        // conflict rule as each zone is "added" to the accepted set.
        // Any zone whose price level is superseded by a later opposing zone
        // is marked VIOLATED before being returned, matching V7 output exactly.
        {
            // Sort chronologically — newer zones supersede older ones
            std::sort(initial_zones.begin(), initial_zones.end(),
                [](const Zone& a, const Zone& b) {
                    return a.formation_bar < b.formation_bar;
                });

            const double CONFLICT_THRESHOLD = 0.50;
            int conflicts_resolved = 0;

            // Walk zones in formation order, exactly as V7 does during bar iteration
            for (size_t i = 0; i < initial_zones.size(); ++i) {
                const Zone& new_zone = initial_zones[i];
                // Only live zones can conflict (VIOLATED zones are already dead)
                if (new_zone.state == ZoneState::VIOLATED) continue;

                double new_lo = std::min(new_zone.proximal_line, new_zone.distal_line);
                double new_hi = std::max(new_zone.proximal_line, new_zone.distal_line);

                // Check all earlier-formed zones for price overlap + opposite type
                for (size_t j = 0; j < i; ++j) {
                    Zone& older = initial_zones[j];
                    if (older.state == ZoneState::VIOLATED) continue;
                    if (older.type  == new_zone.type)       continue;  // same type → no conflict

                    double az_lo = std::min(older.proximal_line, older.distal_line);
                    double az_hi = std::max(older.proximal_line, older.distal_line);
                    double overlap = std::max(0.0,
                        std::min(new_hi, az_hi) - std::max(new_lo, az_lo));
                    double smaller = std::min(new_hi - new_lo, az_hi - az_lo);

                    if (smaller > 0.0 && (overlap / smaller) >= CONFLICT_THRESHOLD) {
                        older.state = ZoneState::VIOLATED;
                        conflicts_resolved++;
                        LOG_INFO("⚡ CONFLICTING ZONE (bootstrap): new "
                            << (new_zone.type == ZoneType::DEMAND ? "DEMAND" : "SUPPLY")
                            << " Z" << new_zone.zone_id
                            << " bar=" << new_zone.formation_bar
                            << " @ " << new_zone.distal_line << "-" << new_zone.proximal_line
                            << " supersedes older "
                            << (older.type == ZoneType::DEMAND ? "DEMAND" : "SUPPLY")
                            << " Z" << older.zone_id
                            << " bar=" << older.formation_bar
                            << " @ " << older.distal_line << "-" << older.proximal_line
                            << " (overlap=" << static_cast<int>(overlap / smaller * 100) << "%)");
                    }
                }
            }

            if (conflicts_resolved > 0) {
                LOG_INFO("Bootstrap conflict resolution: " << conflicts_resolved
                         << " older zones marked VIOLATED by newer opposing zones");
                // Count surviving active zones
                int active = 0;
                for (const auto& z : initial_zones)
                    if (z.state != ZoneState::VIOLATED) active++;
                LOG_INFO("Active zones after conflict resolution: " << active
                         << " / " << initial_zones.size() << " total");
            }
        }

        LOG_INFO("=== ZONE DETECTION COMPLETE ===");
        
        return initial_zones;
    }
};

} // namespace Core
} // namespace SDTrading