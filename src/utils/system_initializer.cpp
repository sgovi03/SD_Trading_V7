#include "system_initializer.h"
#include <iostream>

namespace SDTrading {
namespace Utils {

// Static member initialization
SystemInitializer* SystemInitializer::instance_ = nullptr;
std::mutex SystemInitializer::mutex_;

bool SystemInitializer::find_config_file(std::string& path, std::string& error) {
    // Search paths in order of preference
    std::vector<std::string> search_paths = {
        "system_config.json",              // Run from root
        "../system_config.json",           // Run from build/
        "../../system_config.json",        // Run from build/bin/
        "../../../system_config.json"      // Run from build/bin/Debug/
    };
    
    for (const auto& search_path : search_paths) {
        if (fs::exists(search_path)) {
            path = search_path;
            return true;
        }
    }
    
    // Not found - build error message
    error = "ERROR: system_config.json not found!\n\nSearched in:\n";
    for (const auto& search_path : search_paths) {
        try {
            error += "  " + fs::absolute(search_path).string() + "\n";
        } catch (...) {
            error += "  " + search_path + "\n";
        }
    }
    error += "\nPlease ensure system_config.json is in project root directory.";
    
    return false;
}

bool SystemInitializer::initialize() {
    // Thread-safe initialization check
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Already initialized - return success
    if (initialized_) {
        return true;
    }
    
    // Find config file
    if (!find_config_file(config_path_, last_error_)) {
        return false;
    }
    
    // Load config
    config_ = Core::SystemConfigLoader::load_from_json(config_path_, last_error_);
    
    if (!last_error_.empty()) {
        last_error_ = "ERROR loading system config: " + last_error_;
        return false;
    }
    
    // Mark as initialized
    initialized_ = true;
    
    return true;
}

const Core::SystemConfig& SystemInitializer::getConfig() const {
    if (!initialized_) {
        throw std::runtime_error(
            "SystemInitializer not initialized! Call initialize() first."
        );
    }
    return config_;
}

bool SystemInitializer::getConfig(Core::SystemConfig& config) const {
    if (!initialized_) {
        return false;
    }
    config = config_;
    return true;
}

void SystemInitializer::printConfig() const {
    if (!initialized_) {
        std::cerr << "SystemInitializer not initialized!" << std::endl;
        return;
    }
    
    std::cout << "System config loaded: ";
    try {
        std::cout << fs::absolute(config_path_).string() << std::endl;
    } catch (...) {
        std::cout << config_path_ << std::endl;
    }
    
    std::cout << std::endl;
    std::cout << "System Configuration:" << std::endl;
    std::cout << "  Root directory: " << config_.root_directory << std::endl;
    std::cout << "  Environment:    " << config_.environment << std::endl;
    std::cout << "  Log level:      " << config_.log_level << std::endl;
}

bool SystemInitializer::validateConfig() {
    if (!initialized_) {
        last_error_ = "Cannot validate - not initialized";
        return false;
    }
    
    // Check required fields
    if (config_.root_directory.empty()) {
        last_error_ = "Validation failed: root_directory is empty";
        return false;
    }
    
    if (config_.environment.empty()) {
        last_error_ = "Validation failed: environment is empty";
        return false;
    }
    
    // Check that root directory exists
    if (!fs::exists(config_.root_directory)) {
        last_error_ = "Validation failed: root_directory does not exist: " + 
                      config_.root_directory;
        return false;
    }
    
    // Validate environment value
    if (config_.environment != "test" && config_.environment != "production") {
        last_error_ = "Validation warning: environment should be 'test' or 'production', got: " + 
                      config_.environment;
        // Don't fail, just warn
    }
    
    return true;
}

} // namespace Utils
} // namespace SDTrading
