#ifndef SYSTEM_INITIALIZER_H
#define SYSTEM_INITIALIZER_H

#include <string>
#include <vector>
#include <filesystem>
#include <mutex>
#include "core/system_config_loader.h"

namespace SDTrading {
namespace Utils {

namespace fs = std::filesystem;

/**
 * @class SystemInitializer
 * @brief Thread-safe Singleton for system-wide configuration management
 * 
 * Provides centralized system initialization and configuration access:
 * - Loads system_config.json once and caches it
 * - Smart path search for config file
 * - Thread-safe access to system configuration
 * - Initialization state tracking
 * 
 * Usage:
 *   // Initialize at application startup
 *   if (!SystemInitializer::getInstance().initialize()) {
 *       // Handle error
 *   }
 *   
 *   // Access config anywhere in code
 *   auto& config = SystemInitializer::getInstance().getConfig();
 *   std::string symbol = config.trading_symbol;
 */
class SystemInitializer {
private:
    // Singleton instance
    static SystemInitializer* instance_;
    static std::mutex mutex_;
    
    // Cached configuration
    Core::SystemConfig config_;
    bool initialized_;
    std::string config_path_;
    std::string last_error_;
    
    // Private constructor (Singleton pattern)
    SystemInitializer() : initialized_(false) {}
    
    // Prevent copying
    SystemInitializer(const SystemInitializer&) = delete;
    SystemInitializer& operator=(const SystemInitializer&) = delete;
    
    /**
     * Find system_config.json by searching multiple locations
     * Handles different execution contexts (root, build/, build/bin/Debug/, etc.)
     */
    bool find_config_file(std::string& path, std::string& error);
    
public:
    /**
     * Get singleton instance (thread-safe)
     * Uses Meyer's Singleton pattern (C++11 guarantees thread safety)
     */
    static SystemInitializer& getInstance() {
        static SystemInitializer instance;
        return instance;
    }
    
    /**
     * Initialize system configuration
     * Loads system_config.json and caches it
     * Call this once at application startup
     * 
     * @return true if successful, false otherwise
     */
    bool initialize();
    
    /**
     * Check if system has been initialized
     * @return true if initialize() was called successfully
     */
    bool isInitialized() const { return initialized_; }
    
    /**
     * Get system configuration (read-only)
     * Must call initialize() first
     * 
     * @return Reference to cached SystemConfig
     * @throws std::runtime_error if not initialized
     */
    const Core::SystemConfig& getConfig() const;
    
    /**
     * Get system configuration (read-only) - non-throwing version
     * Must call initialize() first
     * 
     * @param config Output: SystemConfig if initialized
     * @return true if config is available, false if not initialized
     */
    bool getConfig(Core::SystemConfig& config) const;
    
    /**
     * Get path to loaded config file
     * @return Path to system_config.json (empty if not initialized)
     */
    std::string getConfigPath() const { return config_path_; }
    
    /**
     * Get last error message
     * @return Error message from last failed operation
     */
    std::string getLastError() const { return last_error_; }
    
    /**
     * Print system configuration summary
     * Outputs formatted config info to stdout
     */
    void printConfig() const;
    
    /**
     * Validate system configuration
     * Checks that required fields are set and directories exist
     * 
     * @return true if valid, false otherwise
     */
    bool validateConfig();
    
    /**
     * Reset singleton (mainly for testing)
     * WARNING: Use with caution - only for test scenarios
     */
    void reset() {
        initialized_ = false;
        config_ = Core::SystemConfig();
        config_path_.clear();
        last_error_.clear();
    }
    
    // Destructor
    ~SystemInitializer() = default;
};

} // namespace Utils
} // namespace SDTrading

#endif // SYSTEM_INITIALIZER_H
