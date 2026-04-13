// ============================================================
// SD TRADING V8 - SYSTEM CONFIG LOADER (PATCHED)
// src/core/system_config_loader.cpp
//
// Milestone M0.2 / M1.1
//
// Changes from V7:
//   - Added V8 section parsing (symbol_registry, sqlite, scanner,
//     compute_backend)
//   - All existing sections unchanged — zero regression risk
//   - Added LOG_INFO for v8 fields at startup
//
// INSTRUCTION: Replace src/core/system_config_loader.cpp in
// the V8 codebase with this file content.
// ============================================================

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

SystemConfig SystemConfigLoader::load_from_json(
    const std::string& filepath,
    std::string& error_message)
{
    SystemConfig config;  // Start with defaults

    std::ifstream file(filepath);
    if (!file.is_open()) {
        error_message = "Failed to open system config file: " + filepath;
        LOG_WARN(error_message);
        return config;
    }

    LOG_INFO("Loading system configuration from: " << filepath);

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json = buffer.str();
    file.close();

    try {
        // ---- system ----
        std::string system_obj = extract_json_object(json, "system");
        if (!system_obj.empty()) {
            config.root_directory = extract_json_string(system_obj, "root_directory");
            config.environment    = extract_json_string(system_obj, "environment");
            config.log_level      = extract_json_string(system_obj, "log_level");
        }

        // ---- directories ----
        std::string dirs_obj = extract_json_object(json, "directories");
        if (!dirs_obj.empty()) {
            config.conf_dir              = extract_json_string(dirs_obj, "conf");
            config.data_dir              = extract_json_string(dirs_obj, "data");
            config.logs_dir              = extract_json_string(dirs_obj, "logs");
            config.scripts_dir           = extract_json_string(dirs_obj, "scripts");
            config.backtest_results_dir  = extract_json_string(dirs_obj, "backtest_results");
            config.live_trade_logs_dir   = extract_json_string(dirs_obj, "live_trade_logs");
        }

        // ---- files ----
        std::string files_obj = extract_json_object(json, "files");
        if (!files_obj.empty()) {
            config.strategy_config_file  = extract_json_string(files_obj, "strategy_config");
            config.fyers_token_file      = extract_json_string(files_obj, "fyers_token");
            config.fyers_config_file     = extract_json_string(files_obj, "fyers_config");
            config.live_data_csv         = extract_json_string(files_obj, "live_data_csv");
            config.historical_data_cache = extract_json_string(files_obj, "historical_data_cache");
            config.bridge_status_file    = extract_json_string(files_obj, "bridge_status");
            config.python_log_file       = extract_json_string(files_obj, "python_log");
            config.cpp_log_file          = extract_json_string(files_obj, "cpp_log");
            config.trade_log_file        = extract_json_string(files_obj, "trade_log");
        }

        // ---- fyers ----
        std::string fyers_obj = extract_json_object(json, "fyers");
        if (!fyers_obj.empty()) {
            config.fyers_client_id    = extract_json_string(fyers_obj, "client_id");
            config.fyers_secret_key   = extract_json_string(fyers_obj, "secret_key");
            config.fyers_redirect_uri = extract_json_string(fyers_obj, "redirect_uri");
            config.fyers_environment  = extract_json_string(fyers_obj, "environment");
            config.fyers_api_base     = extract_json_string(fyers_obj, "api_base");

            std::string endpoints_obj = extract_json_object(fyers_obj, "endpoints");
            if (!endpoints_obj.empty()) {
                std::string env_obj = extract_json_object(
                    endpoints_obj, config.fyers_environment);
                if (!env_obj.empty()) {
                    config.fyers_api_base = extract_json_string(env_obj, "api_base");
                    config.fyers_ws_base  = extract_json_string(env_obj, "ws_base");
                }
            }
        }

        // ---- trading ----
        std::string trading_obj = extract_json_object(json, "trading");
        if (!trading_obj.empty()) {
            config.trading_symbol           = extract_json_string(trading_obj, "symbol");
            config.trading_resolution       = extract_json_string(trading_obj, "resolution");
            config.trading_mode             = extract_json_string(trading_obj, "mode");
            config.trading_starting_capital = extract_json_double(trading_obj, "starting_capital");
            config.trading_lot_size         = extract_json_int(trading_obj, "lot_size");
        }

        // ---- bridge ----
        std::string bridge_obj = extract_json_object(json, "bridge");
        if (!bridge_obj.empty()) {
            config.bridge_mode                     = extract_json_string(bridge_obj, "mode");
            config.bridge_update_interval_seconds  = extract_json_int(bridge_obj, "update_interval_seconds");
            config.bridge_max_rows                 = extract_json_int(bridge_obj, "max_rows");
            config.bridge_historical_lookback_days = extract_json_int(bridge_obj, "historical_lookback_days");
        }

        // ---- strategy ----
        std::string strategy_obj = extract_json_object(json, "strategy");
        if (!strategy_obj.empty()) {
            config.strategy_lookback_bars         = extract_json_int(strategy_obj, "lookback_bars");
            config.strategy_min_zone_strength     = extract_json_double(strategy_obj, "min_zone_strength");
            config.strategy_max_concurrent_trades = extract_json_int(strategy_obj, "max_concurrent_trades");
            config.strategy_risk_per_trade_pct    = extract_json_double(strategy_obj, "risk_per_trade_pct");
        }

        // ---- live_zone_filtering ----
        std::string live_zone_obj = extract_json_object(json, "live_zone_filtering");
        if (!live_zone_obj.empty()) {
            config.live_zone_filtering_enabled         = extract_json_bool(live_zone_obj, "enabled");
            config.live_zone_range_pct                 = extract_json_double(live_zone_obj, "range_pct");
            config.live_zone_refresh_interval_minutes  = extract_json_int(live_zone_obj, "refresh_interval_minutes");
        }

        // ---- entry_filters ----
        std::string entry_filters_obj = extract_json_object(json, "entry_filters");
        if (!entry_filters_obj.empty()) {
            config.max_zone_distance_atr = extract_json_double(entry_filters_obj, "max_zone_distance_atr");
        }

        // ---- zone_detection ----
        std::string zone_detection_obj = extract_json_object(json, "zone_detection");
        if (!zone_detection_obj.empty()) {
            config.live_zone_detection_interval_bars =
                extract_json_int(zone_detection_obj, "live_zone_detection_interval_bars");
        }

        // ---- zone_bootstrap ----
        std::string zone_bootstrap_obj = extract_json_object(json, "zone_bootstrap");
        if (!zone_bootstrap_obj.empty()) {
            config.zone_bootstrap_enabled          = extract_json_bool(zone_bootstrap_obj, "enabled");
            config.zone_bootstrap_ttl_hours        = extract_json_int(zone_bootstrap_obj, "ttl_hours");
            config.zone_bootstrap_refresh_time     = extract_json_string(zone_bootstrap_obj, "refresh_time");
            config.zone_bootstrap_force_regenerate = extract_json_bool(zone_bootstrap_obj, "force_regenerate");
        }

        // ---- order_submitter ----
        std::string order_submitter_obj = extract_json_object(json, "order_submitter");
        if (!order_submitter_obj.empty()) {
            config.order_submitter_enabled        = extract_json_bool(order_submitter_obj, "enabled");
            config.order_submitter_base_url       = extract_json_string(order_submitter_obj, "base_url");
            config.order_submitter_long_strategy  = extract_json_int(order_submitter_obj, "long_strategy_number");
            config.order_submitter_short_strategy = extract_json_int(order_submitter_obj, "short_strategy_number");
            config.order_submitter_timeout        = extract_json_int(order_submitter_obj, "timeout_seconds");
        }

        // ============================================================
        // V8 ADDITIONS — parsed from the "v8" section
        // Fully backward-compatible: if section absent, defaults apply
        // ============================================================
        std::string v8_obj = extract_json_object(json, "v8");
        if (!v8_obj.empty()) {

            // Symbol registry replaces single trading.symbol for scanner mode
            std::string sr = extract_json_string(v8_obj, "symbol_registry_file");
            if (!sr.empty()) config.symbol_registry_file = sr;

            // Config root for 3-tier cascade (MASTER/classes/symbols)
            std::string cr = extract_json_string(v8_obj, "config_root_dir");
            if (!cr.empty()) config.config_root_dir = cr;

            // SQLite persistence
            std::string dbp = extract_json_string(v8_obj, "sqlite_db_path");
            if (!dbp.empty()) config.sqlite_db_path = dbp;

            std::string sch = extract_json_string(v8_obj, "sqlite_schema_path");
            if (!sch.empty()) config.sqlite_schema_path = sch;

            // Scanner
            config.scanner_mode_enabled     = extract_json_bool(v8_obj, "scanner_mode_enabled");
            config.scanner_signal_min_score = extract_json_double(v8_obj, "scanner_signal_min_score");
            std::string rank = extract_json_string(v8_obj, "scanner_rank_by");
            if (!rank.empty()) config.scanner_rank_by = rank;

            // Compute backend
            std::string be = extract_json_string(v8_obj, "compute_backend");
            if (!be.empty()) config.compute_backend = be;

            int pool = extract_json_int(v8_obj, "cpu_thread_pool_size");
            if (pool > 0) config.cpu_thread_pool_size = pool;

            config.cuda_device_id = extract_json_int(v8_obj, "cuda_device_id");

            int thresh = extract_json_int(v8_obj, "cuda_min_symbols_threshold");
            if (thresh > 0) config.cuda_min_symbols_threshold = thresh;

            LOG_INFO("[V8] Symbol registry: " << config.symbol_registry_file);
            LOG_INFO("[V8] Config root:     " << config.config_root_dir);
            LOG_INFO("[V8] SQLite DB:       " << config.sqlite_db_path);
            LOG_INFO("[V8] Scanner mode:    " << (config.scanner_mode_enabled ? "YES" : "NO"));
            LOG_INFO("[V8] Compute backend: " << config.compute_backend
                     << " (pool=" << config.cpu_thread_pool_size << ")");
        } else {
            LOG_INFO("[V8] No 'v8' section in system_config.json — using defaults");
        }

        // ---- Summary ----
        LOG_INFO("System configuration loaded successfully");
        LOG_INFO("  Root:        " << config.root_directory);
        LOG_INFO("  Environment: " << config.environment);
        LOG_INFO("  Symbol:      " << config.trading_symbol);
        LOG_INFO("  Fyers env:   " << config.fyers_environment);

    } catch (const std::exception& e) {
        error_message = std::string("Failed to parse system config JSON: ") + e.what();
        LOG_ERROR(error_message);
    }

    return config;
}

// ============================================================
// JSON extraction helpers — unchanged from V7
// ============================================================

std::string SystemConfigLoader::extract_json_object(
    const std::string& json, const std::string& key)
{
    std::string search_key = "\"" + key + "\"";
    size_t key_pos = json.find(search_key);
    if (key_pos == std::string::npos) return "";

    size_t brace_start = json.find('{', key_pos);
    if (brace_start == std::string::npos) return "";

    int depth = 1;
    size_t pos = brace_start + 1;
    while (pos < json.size() && depth > 0) {
        if      (json[pos] == '{') ++depth;
        else if (json[pos] == '}') --depth;
        ++pos;
    }
    if (depth != 0) return "";
    return json.substr(brace_start, pos - brace_start);
}

std::string SystemConfigLoader::extract_json_string(
    const std::string& json, const std::string& key)
{
    std::string search_key = "\"" + key + "\"";
    size_t key_pos = json.find(search_key);
    if (key_pos == std::string::npos) return "";

    size_t colon = json.find(':', key_pos);
    if (colon == std::string::npos) return "";

    size_t q1 = json.find('"', colon);
    if (q1 == std::string::npos) return "";

    size_t q2 = json.find('"', q1 + 1);
    if (q2 == std::string::npos) return "";

    return json.substr(q1 + 1, q2 - q1 - 1);
}

int SystemConfigLoader::extract_json_int(
    const std::string& json, const std::string& key)
{
    std::string search_key = "\"" + key + "\"";
    size_t key_pos = json.find(search_key);
    if (key_pos == std::string::npos) return 0;

    size_t colon = json.find(':', key_pos);
    if (colon == std::string::npos) return 0;

    size_t num_start = colon + 1;
    while (num_start < json.size() && std::isspace(json[num_start])) ++num_start;

    size_t num_end = num_start;
    while (num_end < json.size() &&
           json[num_end] != ',' &&
           json[num_end] != '\n' &&
           json[num_end] != '}') ++num_end;

    std::string s = trim(json.substr(num_start, num_end - num_start));
    if (s.empty()) return 0;
    try { return std::stoi(s); } catch (...) { return 0; }
}

double SystemConfigLoader::extract_json_double(
    const std::string& json, const std::string& key)
{
    std::string search_key = "\"" + key + "\"";
    size_t key_pos = json.find(search_key);
    if (key_pos == std::string::npos) return 0.0;

    size_t colon = json.find(':', key_pos);
    if (colon == std::string::npos) return 0.0;

    size_t num_start = colon + 1;
    while (num_start < json.size() && std::isspace(json[num_start])) ++num_start;

    size_t num_end = num_start;
    while (num_end < json.size() &&
           json[num_end] != ',' &&
           json[num_end] != '\n' &&
           json[num_end] != '}') ++num_end;

    std::string s = trim(json.substr(num_start, num_end - num_start));
    if (s.empty()) return 0.0;
    try { return std::stod(s); } catch (...) { return 0.0; }
}

bool SystemConfigLoader::extract_json_bool(
    const std::string& json, const std::string& key)
{
    std::string search_key = "\"" + key + "\"";
    size_t key_pos = json.find(search_key);
    if (key_pos == std::string::npos) return false;

    size_t colon = json.find(':', key_pos);
    if (colon == std::string::npos) return false;

    size_t vs = colon + 1;
    while (vs < json.size() && std::isspace(json[vs])) ++vs;

    if (vs + 4 <= json.size() && json.substr(vs, 4) == "true")  return true;
    if (vs + 5 <= json.size() && json.substr(vs, 5) == "false") return false;
    return false;
}

std::string SystemConfigLoader::trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, last - first + 1);
}

std::string SystemConfigLoader::unquote(const std::string& str) {
    if (str.size() >= 2 && str.front() == '"' && str.back() == '"')
        return str.substr(1, str.size() - 2);
    return str;
}

bool SystemConfigLoader::save_to_json(
    const SystemConfig& config, const std::string& filepath)
{
    std::ofstream file(filepath);
    if (!file.is_open()) {
        LOG_ERROR("Failed to open file for writing: " << filepath);
        return false;
    }

    file << "{\n";
    file << "  \"_comment\": \"SD Engine V8.0 - System Configuration\",\n";
    file << "  \"_version\": \"8.0\",\n\n";

    file << "  \"system\": {\n";
    file << "    \"root_directory\": \"" << config.root_directory << "\",\n";
    file << "    \"environment\": \""    << config.environment    << "\",\n";
    file << "    \"log_level\": \""      << config.log_level      << "\"\n";
    file << "  },\n\n";

    file << "  \"directories\": {\n";
    file << "    \"conf\": \""              << config.conf_dir             << "\",\n";
    file << "    \"data\": \""              << config.data_dir             << "\",\n";
    file << "    \"logs\": \""              << config.logs_dir             << "\",\n";
    file << "    \"scripts\": \""           << config.scripts_dir          << "\",\n";
    file << "    \"backtest_results\": \""  << config.backtest_results_dir << "\",\n";
    file << "    \"live_trade_logs\": \""   << config.live_trade_logs_dir  << "\"\n";
    file << "  },\n\n";

    file << "  \"files\": {\n";
    file << "    \"strategy_config\": \"" << config.strategy_config_file << "\",\n";
    file << "    \"fyers_token\": \""     << config.fyers_token_file      << "\",\n";
    file << "    \"live_data_csv\": \""   << config.live_data_csv         << "\",\n";
    file << "    \"trade_log\": \""       << config.trade_log_file        << "\"\n";
    file << "  },\n\n";

    file << "  \"trading\": {\n";
    file << "    \"symbol\": \""      << config.trading_symbol           << "\",\n";
    file << "    \"resolution\": \""  << config.trading_resolution       << "\",\n";
    file << "    \"mode\": \""        << config.trading_mode             << "\",\n";
    file << "    \"starting_capital\":" << config.trading_starting_capital << ",\n";
    file << "    \"lot_size\": "      << config.trading_lot_size          << "\n";
    file << "  },\n\n";

    file << "  \"v8\": {\n";
    file << "    \"symbol_registry_file\": \"" << config.symbol_registry_file   << "\",\n";
    file << "    \"config_root_dir\": \""       << config.config_root_dir        << "\",\n";
    file << "    \"sqlite_db_path\": \""        << config.sqlite_db_path         << "\",\n";
    file << "    \"sqlite_schema_path\": \""    << config.sqlite_schema_path     << "\",\n";
    file << "    \"scanner_mode_enabled\": "    << (config.scanner_mode_enabled ? "true" : "false") << ",\n";
    file << "    \"scanner_signal_min_score\": "<< config.scanner_signal_min_score << ",\n";
    file << "    \"scanner_rank_by\": \""       << config.scanner_rank_by        << "\",\n";
    file << "    \"compute_backend\": \""       << config.compute_backend        << "\",\n";
    file << "    \"cpu_thread_pool_size\": "    << config.cpu_thread_pool_size   << ",\n";
    file << "    \"cuda_device_id\": "          << config.cuda_device_id         << ",\n";
    file << "    \"cuda_min_symbols_threshold\":"<< config.cuda_min_symbols_threshold << "\n";
    file << "  }\n";

    file << "}\n";
    file.close();

    LOG_INFO("System configuration saved to: " << filepath);
    return true;
}

} // namespace Core
} // namespace SDTrading
