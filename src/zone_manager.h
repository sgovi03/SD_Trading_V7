#include "utils/volume_baseline.h"
    // Volume baseline for normalized volume tracking
    SDTrading::Utils::VolumeBaseline* volume_baseline_ = nullptr;
    // Set the volume baseline pointer
    void set_volume_baseline(SDTrading::Utils::VolumeBaseline* baseline) { volume_baseline_ = baseline; }
// ============================================================
// ZONE MANAGER V5.0 - HEADER FILE (Interface Only)
// FIXED: Moved implementation to .cpp file
// FIXED: Uses proper copy constructor instead of manual copying
// FIXED: Better exception handling - fail fast
// ============================================================

#ifndef ZONE_MANAGER_H
#define ZONE_MANAGER_H

#include "sd_engine_core.h"
#include <json/json.h>
#include <vector>
#include <map>
#include <string>
#include <memory>

// ============================================================
// ZONE METADATA - Tracking zone history
// ============================================================
struct ZoneMetadata {
    int zone_id = 0;
    int bars_since_formation = 0;
    int bars_since_last_touch = 0;
    double last_touch_price = 0.0;
    bool currently_active = true;
    std::string deactivation_reason;
    
    ZoneMetadata() = default;
};

// ============================================================
// ZONE MANAGER CLASS - Interface Only
// ============================================================
class ZoneManager {
private:
    std::vector<Zone> active_zones_;
    std::map<int, ZoneMetadata> zone_metadata_;
    int next_zone_id_;
    std::string state_file_;
    
    // Private helper methods - implementation in .cpp
    void update_zone_states(const Bar& current_bar, double atr);
    void mark_zone_as_tested(Zone& zone, double touch_price, const Bar* touch_bar = nullptr);
    void mark_zone_as_violated(Zone& zone, const std::string& reason);
    bool should_deactivate_zone(const Zone& zone, const ZoneMetadata& metadata,
                                const BacktestConfig& config) const;
    
public:
    // Constructor
    ZoneManager();
    explicit ZoneManager(const std::string& state_file);
    
    // Destructor
    ~ZoneManager() = default;
    
    // Copy and move operations
    ZoneManager(const ZoneManager&) = default;
    ZoneManager& operator=(const ZoneManager&) = default;
    ZoneManager(ZoneManager&&) noexcept = default;
    ZoneManager& operator=(ZoneManager&&) noexcept = default;
    
    // Main update method
    void update(const std::vector<Bar>& bars, int current_idx, 
                const BacktestConfig& config);
    
    // OPTION 1 FIX: Full-dataset scan for historical zones
    void scan_full_dataset(const std::vector<Bar>& bars, 
                          const BacktestConfig& config);
    
    // Zone access methods
    const std::vector<Zone>& get_active_zones() const;
    std::vector<Zone> get_demand_zones() const;
    std::vector<Zone> get_supply_zones() const;
    std::vector<Zone> get_elite_zones() const;
    
    // Zone info methods
    int get_active_zone_count() const;
    int get_elite_zone_count() const;
    bool has_zones_in_range(double price_low, double price_high, 
                           ZoneType type) const;
    
    // State persistence
    bool save_state_to_file(const std::string& filename);
    bool load_state_from_file(const std::string& filename);
    
    // Debugging
    void print_active_zones() const;
    void print_statistics() const;
    
    // Reset
    void clear_all_zones();
};

#endif // ZONE_MANAGER_H
