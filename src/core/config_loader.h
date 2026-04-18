#ifndef SDTRADING_CONFIG_LOADER_H
#define SDTRADING_CONFIG_LOADER_H

#include "common_types.h"
#include <string>
#include <vector>

namespace SDTrading {
namespace Core {

// ============================================================
// CONFIG LOADER CLASS
// ============================================================

class ConfigLoader {
public:
    // Load configuration from file
    static Config load_from_file(const std::string& filepath);
    
    // Load configuration from file with error reporting
    static Config load_from_file(const std::string& filepath, std::string& error_message);
    
    // Save configuration to file
    static bool save_to_file(const Config& config, const std::string& filepath);
    
private:
    // Helper methods
    static void parse_line(const std::string& line, Config& config);
    static bool parse_section_capital_and_zones(const std::string& key, const std::string& value, Config& config);
    static bool parse_section_scoring(const std::string& key, const std::string& value, Config& config);
    static bool parse_section_indicators_and_entry(const std::string& key, const std::string& value, Config& config);
    static bool parse_section_trade_and_runtime(const std::string& key, const std::string& value, Config& config);
    static bool parse_section_trade_and_runtime_extended(const std::string& key, const std::string& value, Config& config);
    static std::string trim(const std::string& str);
    static bool parse_bool(const std::string& value);
    static std::vector<std::string> get_validation_errors(const Config& config);
};

} // namespace Core
} // namespace SDTrading

#endif // SDTRADING_CONFIG_LOADER_H