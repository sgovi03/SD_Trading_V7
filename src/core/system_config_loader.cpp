#include "system_config_loader.h"
#include "../utils/logger.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>

namespace SDTrading {
namespace Core {

using namespace Utils;

SystemConfig SystemConfigLoader::load_from_json(const std::string& filepath) {
    std::string error_message;
    return load_from_json(filepath, error_message);
}

SystemConfig SystemConfigLoader::load_from_json(const std::string& filepath, std::string& error_message) {
    SystemConfig config;  // Start with defaults
    
    std::ifstream file(filepath);
    if (!file.is_open()) {
        error_message = "Failed to open system config file: " + filepath;
        LOG_WARN(error_message);
        return config;  // Return defaults
    }
    
    LOG_INFO("Loading system configuration from: " << filepath);
    
    // Read entire file into string
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json = buffer.str();
    file.close();
    
    try {
        // Parse system section
        std::string system_obj = extract_json_object(json, "system");
        if (!system_obj.empty()) {
            config.root_directory = extract_json_string(system_obj, "root_directory");
            config.environment = extract_json_string(system_obj, "environment");
            config.log_level = extract_json_string(system_obj, "log_level");
        }
        
        // Parse directories section
        std::string dirs_obj = extract_json_object(json, "directories");
        if (!dirs_obj.empty()) {
            config.conf_dir = extract_json_string(dirs_obj, "conf");
            config.data_dir = extract_json_string(dirs_obj, "data");
            config.logs_dir = extract_json_string(dirs_obj, "logs");
            config.scripts_dir = extract_json_string(dirs_obj, "scripts");
            config.backtest_results_dir = extract_json_string(dirs_obj, "backtest_results");
            config.live_trade_logs_dir = extract_json_string(dirs_obj, "live_trade_logs");
        }
        
        // Parse files section
        std::string files_obj = extract_json_object(json, "files");
        if (!files_obj.empty()) {
            config.strategy_config_file = extract_json_string(files_obj, "strategy_config");
            config.fyers_token_file = extract_json_string(files_obj, "fyers_token");
            config.fyers_config_file = extract_json_string(files_obj, "fyers_config");
            config.live_data_csv = extract_json_string(files_obj, "live_data_csv");
            config.historical_data_cache = extract_json_string(files_obj, "historical_data_cache");
            config.bridge_status_file = extract_json_string(files_obj, "bridge_status");
            config.python_log_file = extract_json_string(files_obj, "python_log");
            config.cpp_log_file = extract_json_string(files_obj, "cpp_log");
            config.trade_log_file = extract_json_string(files_obj, "trade_log");
        }
        
        // Parse fyers section
        std::string fyers_obj = extract_json_object(json, "fyers");
        if (!fyers_obj.empty()) {
            config.fyers_client_id = extract_json_string(fyers_obj, "client_id");
            config.fyers_secret_key = extract_json_string(fyers_obj, "secret_key");
            config.fyers_redirect_uri = extract_json_string(fyers_obj, "redirect_uri");
            config.fyers_environment = extract_json_string(fyers_obj, "environment");
            
            // Parse endpoints based on environment
            std::string endpoints_obj = extract_json_object(fyers_obj, "endpoints");
            if (!endpoints_obj.empty()) {
                std::string env_obj = extract_json_object(endpoints_obj, config.fyers_environment);
                if (!env_obj.empty()) {
                    config.fyers_api_base = extract_json_string(env_obj, "api_base");
                    config.fyers_ws_base = extract_json_string(env_obj, "ws_base");
                }
            }
        }
        
        // Parse trading section
        std::string trading_obj = extract_json_object(json, "trading");
        if (!trading_obj.empty()) {
            config.trading_symbol = extract_json_string(trading_obj, "symbol");
            config.trading_resolution = extract_json_string(trading_obj, "resolution");
            config.trading_mode = extract_json_string(trading_obj, "mode");
            config.trading_starting_capital = extract_json_double(trading_obj, "starting_capital");
            config.trading_lot_size = extract_json_int(trading_obj, "lot_size");
        }
        
        // Parse bridge section
        std::string bridge_obj = extract_json_object(json, "bridge");
        if (!bridge_obj.empty()) {
            config.bridge_mode = extract_json_string(bridge_obj, "mode");
            config.bridge_update_interval_seconds = extract_json_int(bridge_obj, "update_interval_seconds");
            config.bridge_max_rows = extract_json_int(bridge_obj, "max_rows");
            config.bridge_historical_lookback_days = extract_json_int(bridge_obj, "historical_lookback_days");
        }
        
        // Parse strategy section (basic settings)
        std::string strategy_obj = extract_json_object(json, "strategy");
        if (!strategy_obj.empty()) {
            config.strategy_lookback_bars = extract_json_int(strategy_obj, "lookback_bars");
            config.strategy_min_zone_strength = extract_json_double(strategy_obj, "min_zone_strength");
            config.strategy_max_concurrent_trades = extract_json_int(strategy_obj, "max_concurrent_trades");
            config.strategy_risk_per_trade_pct = extract_json_double(strategy_obj, "risk_per_trade_pct");
        }
        
        // Parse live zone filtering section (NEW)
        std::string live_zone_obj = extract_json_object(json, "live_zone_filtering");
        if (!live_zone_obj.empty()) {
            config.live_zone_filtering_enabled = extract_json_bool(live_zone_obj, "enabled");
            config.live_zone_range_pct = extract_json_double(live_zone_obj, "range_pct");
            config.live_zone_refresh_interval_minutes = extract_json_int(live_zone_obj, "refresh_interval_minutes");
        }
        
        // Parse entry filters section (NEW)
        std::string entry_filters_obj = extract_json_object(json, "entry_filters");
        if (!entry_filters_obj.empty()) {
            config.max_zone_distance_atr = extract_json_double(entry_filters_obj, "max_zone_distance_atr");
        }
        
        // Parse zone detection section (NEW)
        std::string zone_detection_obj = extract_json_object(json, "zone_detection");
        if (!zone_detection_obj.empty()) {
            config.live_zone_detection_interval_bars = extract_json_int(zone_detection_obj, "live_zone_detection_interval_bars");
        }
        
        // Parse zone bootstrap section (NEW)
        std::string zone_bootstrap_obj = extract_json_object(json, "zone_bootstrap");
        if (!zone_bootstrap_obj.empty()) {
            config.zone_bootstrap_enabled = extract_json_bool(zone_bootstrap_obj, "enabled");
            config.zone_bootstrap_ttl_hours = extract_json_int(zone_bootstrap_obj, "ttl_hours");
            config.zone_bootstrap_refresh_time = extract_json_string(zone_bootstrap_obj, "refresh_time");
            config.zone_bootstrap_force_regenerate = extract_json_bool(zone_bootstrap_obj, "force_regenerate");
        }
        
        LOG_INFO("System configuration loaded successfully");
        LOG_INFO("  Root directory: " << config.root_directory);
        LOG_INFO("  Environment: " << config.environment);
        LOG_INFO("  Trading symbol: " << config.trading_symbol);
        LOG_INFO("  Fyers environment: " << config.fyers_environment);
        
    } catch (const std::exception& e) {
        error_message = std::string("Failed to parse JSON: ") + e.what();
        LOG_ERROR(error_message);
        // Return partial config with defaults
    }
    
    return config;
}

std::string SystemConfigLoader::extract_json_object(const std::string& json, const std::string& key) {
    // Find key in JSON
    std::string search_key = "\"" + key + "\"";
    size_t key_pos = json.find(search_key);
    if (key_pos == std::string::npos) {
        return "";  // Key not found
    }
    
    // Find opening brace after key
    size_t brace_start = json.find('{', key_pos);
    if (brace_start == std::string::npos) {
        return "";
    }
    
    // Find matching closing brace
    int brace_count = 1;
    size_t pos = brace_start + 1;
    while (pos < json.length() && brace_count > 0) {
        if (json[pos] == '{') brace_count++;
        else if (json[pos] == '}') brace_count--;
        pos++;
    }
    
    if (brace_count != 0) {
        return "";  // Unmatched braces
    }
    
    return json.substr(brace_start, pos - brace_start);
}

std::string SystemConfigLoader::extract_json_string(const std::string& json, const std::string& key) {
    // Find key in JSON
    std::string search_key = "\"" + key + "\"";
    size_t key_pos = json.find(search_key);
    if (key_pos == std::string::npos) {
        return "";  // Key not found
    }
    
    // Find colon after key
    size_t colon_pos = json.find(':', key_pos);
    if (colon_pos == std::string::npos) {
        return "";
    }
    
    // Find opening quote
    size_t quote_start = json.find('"', colon_pos);
    if (quote_start == std::string::npos) {
        return "";
    }
    
    // Find closing quote
    size_t quote_end = json.find('"', quote_start + 1);
    if (quote_end == std::string::npos) {
        return "";
    }
    
    return json.substr(quote_start + 1, quote_end - quote_start - 1);
}

int SystemConfigLoader::extract_json_int(const std::string& json, const std::string& key) {
    // Find key in JSON
    std::string search_key = "\"" + key + "\"";
    size_t key_pos = json.find(search_key);
    if (key_pos == std::string::npos) {
        return 0;  // Key not found
    }
    
    // Find colon after key
    size_t colon_pos = json.find(':', key_pos);
    if (colon_pos == std::string::npos) {
        return 0;
    }
    
    // Skip whitespace
    size_t num_start = colon_pos + 1;
    while (num_start < json.length() && std::isspace(json[num_start])) {
        num_start++;
    }
    
    // Find end of number (comma, newline, or closing brace)
    size_t num_end = num_start;
    while (num_end < json.length() && 
           json[num_end] != ',' && 
           json[num_end] != '\n' && 
           json[num_end] != '}') {
        num_end++;
    }
    
    std::string num_str = trim(json.substr(num_start, num_end - num_start));
    if (num_str.empty()) {
        return 0;
    }
    
    return std::stoi(num_str);
}

double SystemConfigLoader::extract_json_double(const std::string& json, const std::string& key) {
    // Find key in JSON
    std::string search_key = "\"" + key + "\"";
    size_t key_pos = json.find(search_key);
    if (key_pos == std::string::npos) {
        return 0.0;  // Key not found
    }
    
    // Find colon after key
    size_t colon_pos = json.find(':', key_pos);
    if (colon_pos == std::string::npos) {
        return 0.0;
    }
    
    // Skip whitespace
    size_t num_start = colon_pos + 1;
    while (num_start < json.length() && std::isspace(json[num_start])) {
        num_start++;
    }
    
    // Find end of number
    size_t num_end = num_start;
    while (num_end < json.length() && 
           json[num_end] != ',' && 
           json[num_end] != '\n' && 
           json[num_end] != '}') {
        num_end++;
    }
    
    std::string num_str = trim(json.substr(num_start, num_end - num_start));
    if (num_str.empty()) {
        return 0.0;
    }
    
    return std::stod(num_str);
}

bool SystemConfigLoader::extract_json_bool(const std::string& json, const std::string& key) {
    // Find key in JSON
    std::string search_key = "\"" + key + "\"";
    size_t key_pos = json.find(search_key);
    if (key_pos == std::string::npos) {
        return false;  // Key not found
    }
    
    // Find colon after key
    size_t colon_pos = json.find(':', key_pos);
    if (colon_pos == std::string::npos) {
        return false;
    }
    
    // Skip whitespace after colon
    size_t val_start = colon_pos + 1;
    while (val_start < json.length() && std::isspace(json[val_start])) {
        val_start++;
    }
    
    // Check for true or false
    if (val_start + 4 <= json.length() && json.substr(val_start, 4) == "true") {
        return true;
    }
    if (val_start + 5 <= json.length() && json.substr(val_start, 5) == "false") {
        return false;
    }
    
    return false;
}

std::string SystemConfigLoader::trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

std::string SystemConfigLoader::unquote(const std::string& str) {
    if (str.length() >= 2 && str.front() == '"' && str.back() == '"') {
        return str.substr(1, str.length() - 2);
    }
    return str;
}

bool SystemConfigLoader::save_to_json(const SystemConfig& config, const std::string& filepath) {
    std::ofstream file(filepath);
    if (!file.is_open()) {
        LOG_ERROR("Failed to open file for writing: " << filepath);
        return false;
    }
    
    file << "{\n";
    file << "  \"_comment\": \"SD Engine V4.0 - System Configuration\",\n";
    file << "  \"_version\": \"1.0\",\n\n";
    
    // System section
    file << "  \"system\": {\n";
    file << "    \"root_directory\": \"" << config.root_directory << "\",\n";
    file << "    \"environment\": \"" << config.environment << "\",\n";
    file << "    \"log_level\": \"" << config.log_level << "\"\n";
    file << "  },\n\n";
    
    // Directories section
    file << "  \"directories\": {\n";
    file << "    \"conf\": \"" << config.conf_dir << "\",\n";
    file << "    \"data\": \"" << config.data_dir << "\",\n";
    file << "    \"logs\": \"" << config.logs_dir << "\",\n";
    file << "    \"scripts\": \"" << config.scripts_dir << "\",\n";
    file << "    \"backtest_results\": \"" << config.backtest_results_dir << "\",\n";
    file << "    \"live_trade_logs\": \"" << config.live_trade_logs_dir << "\"\n";
    file << "  },\n\n";
    
    // Files section
    file << "  \"files\": {\n";
    file << "    \"strategy_config\": \"" << config.strategy_config_file << "\",\n";
    file << "    \"fyers_token\": \"" << config.fyers_token_file << "\",\n";
    file << "    \"live_data_csv\": \"" << config.live_data_csv << "\",\n";
    file << "    \"trade_log\": \"" << config.trade_log_file << "\"\n";
    file << "  },\n\n";
    
    // Trading section
    file << "  \"trading\": {\n";
    file << "    \"symbol\": \"" << config.trading_symbol << "\",\n";
    file << "    \"resolution\": \"" << config.trading_resolution << "\",\n";
    file << "    \"mode\": \"" << config.trading_mode << "\",\n";
    file << "    \"starting_capital\": " << config.trading_starting_capital << ",\n";
    file << "    \"lot_size\": " << config.trading_lot_size << "\n";
    file << "  }\n";
    
    file << "}\n";
    file.close();
    
    LOG_INFO("System configuration saved to: " << filepath);
    return true;
}

} // namespace Core
} // namespace SDTrading
