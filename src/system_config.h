// ============================================================
// SD TRADING SYSTEM - CONFIGURATION LOADER
// Centralized configuration management for all C++ components
// ============================================================

#ifndef SYSTEM_CONFIG_H
#define SYSTEM_CONFIG_H

#include <string>
#include <fstream>
#include <filesystem>
#include <json/json.h>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;

class SystemConfig {
private:
    Json::Value config_;
    fs::path root_dir_;
    fs::path config_file_;
    // DryRun settings
      std::string dryrun_results_dir;
      bool dryrun_enable_tick_simulation;
      int dryrun_ticks_per_bar;
    
public:
    /**
     * Constructor - loads configuration from JSON file
     * 
     * @param config_file Path to system_config.json
     */
    SystemConfig(const std::string& config_file = "system_config.json") 
        : config_file_(config_file) {
        
        load_config();
        setup_root();
        create_directories();
    }
    
    /**
     * Load configuration from JSON file
     */
    void load_config() {
        if (!fs::exists(config_file_)) {
            throw std::runtime_error(
                "Configuration file not found: " + config_file_.string() + "\n"
                "Please ensure system_config.json exists in the root directory"
            );
        }
        
        std::ifstream file(config_file_);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open config file: " + config_file_.string());
        }
        
        try {
            file >> config_;
        } catch (const std::exception& e) {
            throw std::runtime_error(
                "Invalid JSON in config file: " + std::string(e.what())
            );
        }
    }
    
    /**
     * Setup root directory
     */
    void setup_root() {
        if (config_["system"].isMember("root_directory")) {
            root_dir_ = config_["system"]["root_directory"].asString();
        } else {
            root_dir_ = fs::current_path();
        }
    }
    
    /**
     * Create all directories from configuration
     */
    void create_directories() {
        // Create directories
        for (const auto& dir_name : config_["directories"].getMemberNames()) {
            fs::path dir_path = root_dir_ / config_["directories"][dir_name].asString();
            fs::create_directories(dir_path);
        }
        
        // Create parent directories for files
        for (const auto& file_name : config_["files"].getMemberNames()) {
            fs::path file_path = root_dir_ / config_["files"][file_name].asString();
            fs::create_directories(file_path.parent_path());
        }
    }
    
    /**
     * Get file path from configuration
     * 
     * @param key File key from config["files"]
     * @return Full path to file
     */
    fs::path get_path(const std::string& key) {
        if (!config_["files"].isMember(key)) {
            throw std::runtime_error("File path '" + key + "' not found in configuration");
        }
        
        return root_dir_ / config_["files"][key].asString();
    }
    
    /**
     * Get directory path from configuration
     * 
     * @param key Directory key from config["directories"]
     * @return Full path to directory
     */
    fs::path get_directory(const std::string& key) {
        if (!config_["directories"].isMember(key)) {
            throw std::runtime_error("Directory '" + key + "' not found in configuration");
        }
        
        return root_dir_ / config_["directories"][key].asString();
    }
    
    /**
     * Get nested configuration value
     * 
     * @param section Main section (e.g., "fyers")
     * @param key Key within section (e.g., "client_id")
     * @param default_value Default if not found
     * @return Configuration value
     */
    std::string get_value(const std::string& section, const std::string& key, 
                         const std::string& default_value = "") {
        if (config_[section].isMember(key)) {
            return config_[section][key].asString();
        }
        return default_value;
    }
    
    /**
     * Get integer configuration value
     */
    int get_int(const std::string& section, const std::string& key, int default_value = 0) {
        if (config_[section].isMember(key)) {
            return config_[section][key].asInt();
        }
        return default_value;
    }
    
    /**
     * Get double configuration value
     */
    double get_double(const std::string& section, const std::string& key, double default_value = 0.0) {
        if (config_[section].isMember(key)) {
            return config_[section][key].asDouble();
        }
        return default_value;
    }
    
    /**
     * Get bool configuration value
     */
    bool get_bool(const std::string& section, const std::string& key, bool default_value = false) {
        if (config_[section].isMember(key)) {
            return config_[section][key].asBool();
        }
        return default_value;
    }
    
    /**
     * Get Fyers client ID
     */
    std::string get_fyers_client_id() {
        return get_value("fyers", "client_id");
    }
    
    /**
     * Get current environment (test/production)
     */
    std::string get_environment() {
        return get_value("system", "environment", "test");
    }
    
    /**
     * Check if running in test mode
     */
    bool is_test_mode() {
        return get_environment() == "test";
    }
    
    /**
     * Check if running in production mode
     */
    bool is_production_mode() {
        return get_environment() == "production";
    }
    
    /**
     * Get Fyers API endpoint for current environment
     * 
     * @param endpoint_type "api_base" or "ws_base"
     * @return Endpoint URL
     */
    std::string get_fyers_endpoint(const std::string& endpoint_type = "api_base") {
        std::string env = get_environment();
        
        if (config_["fyers"]["endpoints"][env].isMember(endpoint_type)) {
            return config_["fyers"]["endpoints"][env][endpoint_type].asString();
        }
        
        return "";
    }
    
    /**
     * Get trading symbol
     */
    std::string get_trading_symbol() {
        return get_value("trading", "symbol", "NSE:NIFTY25JANFUT");
    }
    
    /**
     * Get trading mode (paper/live)
     */
    std::string get_trading_mode() {
        return get_value("trading", "mode", "paper");
    }
    
    /**
     * Get starting capital
     */
    double get_starting_capital() {
        return get_double("trading", "starting_capital", 100000.0);
    }
    
    /**
     * Get lot size
     */
    int get_lot_size() {
        return get_int("trading", "lot_size", 50);
    }
    
    /**
     * Get strategy lookback bars
     */
    int get_lookback_bars() {
        return get_int("strategy", "lookback_bars", 100);
    }
    
    /**
     * Validate configuration
     * 
     * @return true if valid
     * @throws runtime_error if invalid
     */
    bool validate() {
        // Check required sections
        std::vector<std::string> required_sections = {
            "system", "directories", "files", "fyers", "trading"
        };
        
        for (const auto& section : required_sections) {
            if (!config_.isMember(section)) {
                throw std::runtime_error("Missing required config section: " + section);
            }
        }
        
        // Validate Fyers config
        if (!config_["fyers"].isMember("client_id") || 
            config_["fyers"]["client_id"].asString().empty()) {
            throw std::runtime_error("Missing fyers.client_id in configuration");
        }
        
        return true;
    }
    
    /**
     * Get configuration summary
     */
    std::string summary() {
        std::stringstream ss;
        
        ss << std::string(60, '=') << std::endl;
        ss << "SD TRADING SYSTEM - CONFIGURATION" << std::endl;
        ss << std::string(60, '=') << std::endl;
        ss << "Root Directory: " << root_dir_ << std::endl;
        ss << "Environment: " << get_environment() << std::endl;
        ss << "Config File: " << config_file_ << std::endl;
        ss << std::endl;
        
        ss << "Directories:" << std::endl;
        for (const auto& name : config_["directories"].getMemberNames()) {
            fs::path path = get_directory(name);
            std::string exists = fs::exists(path) ? "✓" : "✗";
            ss << "  [" << exists << "] " << name << " -> " << path << std::endl;
        }
        
        ss << std::endl;
        ss << "Key Files:" << std::endl;
        for (const auto& name : config_["files"].getMemberNames()) {
            fs::path path = get_path(name);
            std::string exists = fs::exists(path) ? "✓" : "✗";
            ss << "  [" << exists << "] " << name << " -> " << path << std::endl;
        }
        
        ss << std::endl;
        ss << "Fyers Configuration:" << std::endl;
        ss << "  Client ID: " << get_fyers_client_id() << std::endl;
        ss << "  Environment: " << get_value("fyers", "environment") << std::endl;
        ss << "  API Endpoint: " << get_fyers_endpoint("api_base") << std::endl;
        
        ss << std::endl;
        ss << "Trading Configuration:" << std::endl;
        ss << "  Symbol: " << get_trading_symbol() << std::endl;
        ss << "  Mode: " << get_trading_mode() << std::endl;
        ss << "  Capital: ₹" << get_starting_capital() << std::endl;
        ss << "  Lot Size: " << get_lot_size() << std::endl;
        
        ss << std::string(60, '=') << std::endl;
        
        return ss.str();
    }
    
    /**
     * Get root directory
     */
    fs::path get_root_directory() const {
        return root_dir_;
    }
    
    /**
     * Get raw config object (for advanced access)
     */
    const Json::Value& get_config() const {
        return config_;
    }
};

#endif // SYSTEM_CONFIG_H
