#ifndef SYSTEM_CONFIG_LOADER_H
#define SYSTEM_CONFIG_LOADER_H

#include <string>
#include "common_types.h"

namespace SDTrading {
namespace Core {

/**
 * @class SystemConfigLoader
 * @brief Loads system-level configuration from JSON file
 * 
 * Parses system_config.json containing:
 * - Directory paths
 * - Broker settings (Fyers)
 * - File locations
 * - Logging configuration
 * - Bridge settings
 * - [V8] Scanner / multi-symbol settings (v8 section)
 * - [V8] SQLite persistence paths
 * - [V8] Compute backend (CPU thread pool / CUDA)
 * - [V8] Order submitter settings
 * 
 * This is a bootstrap configuration loaded before strategy config.
 * 
 * V8 backward-compatibility guarantee:
 *   If the "v8" section is absent from system_config.json, all
 *   V8 fields retain their safe defaults from SystemConfig().
 *   Existing V7 system_config.json files require no changes.
 */
class SystemConfigLoader {
public:
    /**
     * Load system configuration from JSON file
     * @param filepath Path to system_config.json
     * @return SystemConfig structure
     * @throws std::runtime_error if file not found or parsing fails
     */
    static SystemConfig load_from_json(const std::string& filepath);
    
    /**
     * Load with error message
     * @param filepath Path to JSON file
     * @param error_message Output error message if load fails
     * @return SystemConfig structure (or defaults on error)
     */
    static SystemConfig load_from_json(const std::string& filepath, std::string& error_message);
    
    /**
     * Save system configuration to JSON file
     * @param config SystemConfig to save
     * @param filepath Output file path
     * @return true if successful
     */
    static bool save_to_json(const SystemConfig& config, const std::string& filepath);

private:
    /**
     * [V8] Parse the "v8" JSON section into config.
     * Called internally by load_from_json().
     * No-op if the section is absent — V8 defaults apply.
     */
    static void load_v8_section(const std::string& v8_json,
                                SystemConfig& config);

    /**
     * Simple JSON value extraction
     * Extracts value for a key from JSON text
     */
    static std::string extract_json_string(const std::string& json, const std::string& key);
    static int extract_json_int(const std::string& json, const std::string& key);
    static double extract_json_double(const std::string& json, const std::string& key);
    static bool extract_json_bool(const std::string& json, const std::string& key);
    
    /**
     * Extract nested JSON object
     */
    static std::string extract_json_object(const std::string& json, const std::string& key);
    
    /**
     * Trim whitespace
     */
    static std::string trim(const std::string& str);
    
    /**
     * Remove quotes from string value
     */
    static std::string unquote(const std::string& str);
};

} // namespace Core
} // namespace SDTrading

#endif // SYSTEM_CONFIG_LOADER_H