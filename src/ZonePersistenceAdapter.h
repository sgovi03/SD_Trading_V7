// ============================================================
// ZONE PERSISTENCE ADAPTER
// Wrapper around ZoneManager persistence for engines
// ============================================================

#ifndef ZONE_PERSISTENCE_ADAPTER_H
#define ZONE_PERSISTENCE_ADAPTER_H

#include <string>
#include <vector>
#include <map>

// Forward declarations - Zone is in SDTrading::Core namespace
namespace SDTrading {
namespace Core {
    struct Zone;
    enum class ZoneType;
    enum class ZoneState;
}
}

namespace SDTrading {
namespace Core {

/**
 * @class ZonePersistenceAdapter
 * @brief Simplified interface for zone state persistence
 * 
 * Wraps ZoneManager's save/load functionality with engine-specific
 * file naming conventions:
 * - Backtest: zones_backtest_YYYYMMDD.json (date-stamped)
 * - Live: zones_live.json (single persistent file)
 */
class ZonePersistenceAdapter {
private:
    std::string output_dir_;
    std::string mode_;
    bool is_live_mode_;
    bool enable_live_zone_filtering_;  // ⭐ NEW: Control active zones file creation
    std::string symbol_;               // V8: per-symbol zone files (e.g. "NIFTY-FUT")
    
    /**
     * Generate filename based on mode and current date
     * Backtest: zones_backtest_20250101.json
     * Live: zones_live_master.json (all zones source of truth)
     */
    std::string generate_filename() const;
    
    /**
     * ⭐ NEW: Generate active zones cache filename
     * Live: zones_live_active.json (only active zones for fast loading)
     */
    std::string generate_active_filename() const;
    
    /**
     * Get current date in YYYYMMDD format
     */
    std::string get_current_date() const;
    
public:
    /**
     * Constructor
     * @param mode "backtest" or "live"
     * @param output_dir Directory for zone files
     * @param enable_filtering Enable/disable active zones file creation (live mode only)
     */
    ZonePersistenceAdapter(const std::string& mode, 
                          const std::string& output_dir,
                          bool enable_filtering = false,
                          const std::string& symbol = "");
    
    /**
     * Save zones to JSON file
     * @param zones Vector of active zones (SDTrading::Core::Zone)
     * @param next_zone_id Next zone ID counter
     * @return true if successful
     */
    bool save_zones(const std::vector<Zone>& zones, int next_zone_id);
    
    /**
     * Save zones to a specific file path
     * @param zones Vector of zones
     * @param next_zone_id Next zone ID counter
     * @param filepath Custom file path (absolute or relative to output_dir)
     * @return true if successful
     */
    bool save_zones_as(const std::vector<Zone>& zones, int next_zone_id, const std::string& filepath);
    
    /**
     * Save zones with metadata (generated timestamp, data source, symbol, etc.)
     * @param zones Vector of zones
     * @param next_zone_id Next zone ID counter
     * @param filepath Custom file path
     * @param metadata Map of metadata key-value pairs
     * @return true if successful
     */
    bool save_zones_with_metadata(const std::vector<Zone>& zones, int next_zone_id, 
                                   const std::string& filepath,
                                   const std::map<std::string, std::string>& metadata);

    
    /**
     * Load zones from JSON file
     * - Backtest: Always returns false (start fresh)
     * - Live: Loads from zones_live.json if exists
     * @param zones Output: loaded zones (SDTrading::Core::Zone)
     * @param next_zone_id Output: loaded zone ID counter
     * @return true if loaded successfully, false if file not found (OK)
     */
    bool load_zones(std::vector<Zone>& zones, int& next_zone_id);
    
    /**
     * Get full path to current zone file
     * @return Full path to master zone persistence file
     */
    std::string get_zone_file_path() const;
    
    /**
     * Load metadata from zone file
     * @param filepath Path to zone file
     * @return Map of metadata key-value pairs (empty if not found)
     */
    std::map<std::string, std::string> load_metadata(const std::string& filepath);
    
    /**
     * ⭐ NEW: Get full path to active zones cache file
     * @return Full path to active zones cache file (live mode only)
     */
    std::string get_active_zone_file_path() const;
};

} // namespace Core
} // namespace SDTrading

#endif // ZONE_PERSISTENCE_ADAPTER_H
