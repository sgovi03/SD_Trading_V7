// ============================================================
// ZONE PERSISTENCE ADAPTER - IMPLEMENTATION
// Zone is in SDTrading::Core namespace
// ============================================================

#include "ZonePersistenceAdapter.h"
#include "common_types.h"  // For Zone struct definition in SDTrading::Core
#include "utils/logger.h"
#include "backtest/trade_manager.h" // For exit signal integration
#include <json/json.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <filesystem>
#include <algorithm>  // for std::replace

namespace fs = std::filesystem;

namespace SDTrading {
namespace Core {

// Helper to parse state from either string or int (backward compatible)
static ZoneState parse_zone_state(const Json::Value& state_val) {
    if (state_val.isString()) {
        const auto s = state_val.asString();
        if (s == "FRESH") return ZoneState::FRESH;
        if (s == "TESTED") return ZoneState::TESTED;
        if (s == "VIOLATED") return ZoneState::VIOLATED;
        if (s == "RECLAIMED") return ZoneState::RECLAIMED;
    }
    // Fall back to legacy integer encoding
    return static_cast<ZoneState>(state_val.asInt());
}

// Helper function to convert ZoneState enum to readable string
std::string zone_state_to_string(ZoneState state) {
    switch (state) {
        case ZoneState::FRESH:
            return "FRESH";
        case ZoneState::TESTED:
            return "TESTED";
        case ZoneState::VIOLATED:
            return "VIOLATED";
        case ZoneState::RECLAIMED:
            return "RECLAIMED";
        default:
            return "UNKNOWN";
    }
}

ZonePersistenceAdapter::ZonePersistenceAdapter(
    const std::string& mode,
    const std::string& output_dir,
    bool enable_filtering
) : mode_(mode), 
    output_dir_(output_dir), 
    is_live_mode_(mode == "live"),
    enable_live_zone_filtering_(enable_filtering) {  // ⭐ NEW: Initialize filtering flag
    
    // Create output directory if it doesn't exist
    fs::create_directories(output_dir_);
}

std::string ZonePersistenceAdapter::get_current_date() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    std::tm tm;
#ifdef _WIN32
    localtime_s(&tm, &time_t);  // Windows safe version
#else
    tm = *std::localtime(&time_t);  // POSIX version
#endif
    
    std::stringstream ss;
    ss << std::put_time(&tm, "%Y%m%d");
    return ss.str();
}

std::string ZonePersistenceAdapter::generate_filename() const {
    if (is_live_mode_) {
        // Live mode: Master persistent file (all zones + is_active flag)
        return "zones_live_master.json";
    } else {
        // Backtest mode: Date-stamped file
        std::string date = get_current_date();
        return "zones_backtest_" + date + ".json";
    }
}

std::string ZonePersistenceAdapter::generate_active_filename() const {
    if (is_live_mode_) {
        // Live mode: Active zones cache file (for fast loading)
        return "zones_live_active.json";
    } else {
        // Backtest mode: No separate active file
        return "";
    }
}

std::string ZonePersistenceAdapter::get_zone_file_path() const {
    fs::path full_path = fs::path(output_dir_) / generate_filename();
    return full_path.string();
}

std::string ZonePersistenceAdapter::get_active_zone_file_path() const {
    if (is_live_mode_) {
        fs::path full_path = fs::path(output_dir_) / generate_active_filename();
        return full_path.string();
    }
    return "";
}

bool ZonePersistenceAdapter::save_zones(
    const std::vector<Zone>& zones,
    int next_zone_id
) {
    try {
        // DEBUG: Log what we're about to save
        if (!zones.empty()) {
            const Zone& first_zone = zones[0];
            LOG_ERROR("🔍 [SAVE_ZONES DEBUG] First zone institutional_index: " + std::to_string(first_zone.institutional_index));
            LOG_ERROR("    volume_ratio: " + std::to_string(first_zone.volume_profile.volume_ratio));
            LOG_ERROR("    oi_score: " + std::to_string(first_zone.oi_profile.oi_score));
        }
        
        // Build JSON with proper formatting (beautified)
        std::stringstream ss;
        
        // Root opening brace and zones array opening
        ss << "{\n"
           << "  \"zones\": [\n";
        
        for (size_t i = 0; i < zones.size(); i++) {
            const Zone& zone = zones[i];
            
            // Zone object opening
            ss << "    {\n"
               << "      \"zone_id\": " << zone.zone_id << ",\n"
               << "      \"type\": \"" << (zone.type == ZoneType::DEMAND ? "DEMAND" : "SUPPLY") << "\",\n"
               << "      \"base_low\": " << zone.base_low << ",\n"
               << "      \"base_high\": " << zone.base_high << ",\n"
               << "      \"distal_line\": " << zone.distal_line << ",\n"
               << "      \"proximal_line\": " << zone.proximal_line << ",\n"
               << "      \"formation_bar\": " << zone.formation_bar << ",\n"
               << "      \"formation_datetime\": \"" << zone.formation_datetime << "\",\n"
               << "      \"strength\": " << zone.strength << ",\n"
               << "      \"state\": \"" << zone_state_to_string(zone.state) << "\",\n"
               << "      \"touch_count\": " << zone.touch_count << ",\n"
               << "      \"is_elite\": " << (zone.is_elite ? "true" : "false") << ",\n"
               << "      \"is_active\": " << (zone.is_active ? "true" : "false") << ",\n"
               << "      \"departure_imbalance\": " << zone.departure_imbalance << ",\n"
               << "      \"retest_speed\": " << zone.retest_speed << ",\n"
               << "      \"bars_to_retest\": " << zone.bars_to_retest << ",\n";
            
            // Scoring engine data
            ss << "      \"zone_score\": {\n"
               << "        \"base_strength_score\": " << zone.zone_score.base_strength_score << ",\n"
               << "        \"elite_bonus_score\": " << zone.zone_score.elite_bonus_score << ",\n"
               << "        \"swing_position_score\": " << zone.zone_score.swing_position_score << ",\n"
               << "        \"regime_alignment_score\": " << zone.zone_score.regime_alignment_score << ",\n"
               << "        \"state_freshness_score\": " << zone.zone_score.state_freshness_score << ",\n"
               << "        \"rejection_confirmation_score\": " << zone.zone_score.rejection_confirmation_score << ",\n"
               << "        \"total_score\": " << zone.zone_score.total_score << ",\n"
               << "        \"entry_aggressiveness\": " << zone.zone_score.entry_aggressiveness << ",\n"
               << "        \"recommended_rr\": " << zone.zone_score.recommended_rr << ",\n"
               << "        \"score_breakdown\": \"" << zone.zone_score.score_breakdown << "\",\n"
               << "        \"entry_rationale\": \"" << zone.zone_score.entry_rationale << "\"\n"
               << "      },\n";
            
            // State history array
            ss << "      \"state_history\": [\n";
            for (size_t j = 0; j < zone.state_history.size(); j++) {
                const ZoneStateEvent& event = zone.state_history[j];
                ss << "        {\n"
                   << "          \"timestamp\": \"" << event.timestamp << "\",\n"
                   << "          \"bar_index\": " << event.bar_index << ",\n"
                   << "          \"event\": \"" << event.event << "\",\n"
                   << "          \"old_state\": \"" << event.old_state << "\",\n"
                   << "          \"new_state\": \"" << event.new_state << "\",\n"
                   << "          \"price\": " << event.price << ",\n"
                   << "          \"bar_high\": " << event.bar_high << ",\n"
                   << "          \"bar_low\": " << event.bar_low << ",\n"
                   << "          \"touch_number\": " << event.touch_number << "\n";
                
                if (j < zone.state_history.size() - 1) {
                    ss << "        },\n";
                } else {
                    ss << "        }\n";
                }
            }
            ss << "      ],\n";
            
            // V6.0 Volume Profile ✅ NEW
            LOG_INFO("[V6 PERSIST] Adding volume_profile for zone " + std::to_string(zone.zone_id));
            ss << "      \"volume_profile\": {\n"
               << "        \"formation_volume\": " << zone.volume_profile.formation_volume << ",\n"
               << "        \"avg_volume_baseline\": " << zone.volume_profile.avg_volume_baseline << ",\n"
               << "        \"volume_ratio\": " << zone.volume_profile.volume_ratio << ",\n"
               << "        \"peak_volume\": " << zone.volume_profile.peak_volume << ",\n"
               << "        \"high_volume_bar_count\": " << zone.volume_profile.high_volume_bar_count << ",\n"
               << "        \"volume_score\": " << zone.volume_profile.volume_score << "\n"
               << "      },\n";
            
            // V6.0 OI Profile ✅ NEW
            LOG_INFO("[V6 PERSIST] Adding oi_profile for zone " + std::to_string(zone.zone_id));
            ss << "      \"oi_profile\": {\n"
               << "        \"formation_oi\": " << zone.oi_profile.formation_oi << ",\n"
               << "        \"oi_change_during_formation\": " << zone.oi_profile.oi_change_during_formation << ",\n"
               << "        \"oi_change_percent\": " << zone.oi_profile.oi_change_percent << ",\n"
               << "        \"price_oi_correlation\": " << zone.oi_profile.price_oi_correlation << ",\n"
               << "        \"oi_data_quality\": " << (zone.oi_profile.oi_data_quality ? "true" : "false") << ",\n"
               << "        \"market_phase\": \"" << static_cast<int>(zone.oi_profile.market_phase) << "\",\n"
               << "        \"oi_score\": " << zone.oi_profile.oi_score << "\n"
               << "      },\n";
            
            // V6.0 Institutional Index ✅ NEW
            LOG_INFO("[V6 PERSIST] Adding institutional_index for zone " + std::to_string(zone.zone_id));
            ss << "      \"institutional_index\": " << zone.institutional_index << "\n";
            
            // Zone object closing with comma (except last one)
            if (i < zones.size() - 1) {
                ss << "    },\n";
            } else {
                ss << "    }\n";
            }
        }
        
        // Zones array closing and metadata
        ss << "  ],\n"
           << "  \"next_zone_id\": " << next_zone_id << ",\n"
           << "  \"timestamp\": \"" << get_current_date() << "\",\n"
           << "  \"mode\": \"" << mode_ << "\"\n"
           << "}\n";
        
        std::string json_str = ss.str();
        
        // Normalize path separators
        std::string filepath = get_zone_file_path();
        std::replace(filepath.begin(), filepath.end(), '/', '\\');
        
        // ⭐ NEW: Save MASTER file (all zones + is_active flag)
        {
            std::ofstream file(filepath, std::ios::out | std::ios::binary | std::ios::trunc);
            
            if (!file.is_open()) {
                LOG_ERROR("Failed to open zone file for writing: " << filepath);
                return false;
            }
            
            file.write(json_str.c_str(), json_str.length());
            file.flush();
            file.close();
            
            LOG_INFO("Master zones saved: " << zones.size() << " zones -> " << filepath);
        }
        
        // ⭐ FIXED: Save ACTIVE zones cache ONLY if filtering is enabled
        if (is_live_mode_ && enable_live_zone_filtering_) {
            std::vector<Zone> active_zones;
            for (const auto& zone : zones) {
                if (zone.is_active) {
                    active_zones.push_back(zone);
                }
            }
            
            std::stringstream active_ss;
            active_ss << "{\n"
                      << "  \"zones\": [\n";
            
            for (size_t i = 0; i < active_zones.size(); i++) {
                const Zone& zone = active_zones[i];
                active_ss << "    {\n"
                          << "      \"zone_id\": " << zone.zone_id << ",\n"
                          << "      \"type\": \"" << (zone.type == ZoneType::DEMAND ? "DEMAND" : "SUPPLY") << "\",\n"
                          << "      \"base_low\": " << zone.base_low << ",\n"
                          << "      \"base_high\": " << zone.base_high << ",\n"
                          << "      \"distal_line\": " << zone.distal_line << ",\n"
                          << "      \"proximal_line\": " << zone.proximal_line << ",\n"
                          << "      \"formation_bar\": " << zone.formation_bar << ",\n"
                          << "      \"formation_datetime\": \"" << zone.formation_datetime << "\",\n"
                          << "      \"strength\": " << zone.strength << ",\n"
                          << "      \"state\": \"" << zone_state_to_string(zone.state) << "\",\n"
                          << "      \"touch_count\": " << zone.touch_count << ",\n"
                          << "      \"is_elite\": " << (zone.is_elite ? "true" : "false") << ",\n"
                          << "      \"is_active\": true,\n"
                          // V6.0: Add volume_profile
                          << "      \"volume_profile\": {\n"
                          << "        \"formation_volume\": " << zone.volume_profile.formation_volume << ",\n"
                          << "        \"avg_volume_baseline\": " << zone.volume_profile.avg_volume_baseline << ",\n"
                          << "        \"volume_ratio\": " << zone.volume_profile.volume_ratio << ",\n"
                          << "        \"peak_volume\": " << zone.volume_profile.peak_volume << ",\n"
                          << "        \"high_volume_bar_count\": " << zone.volume_profile.high_volume_bar_count << ",\n"
                          << "        \"volume_score\": " << zone.volume_profile.volume_score << "\n"
                          << "      },\n"
                          // V6.0: Add oi_profile
                          << "      \"oi_profile\": {\n"
                          << "        \"formation_oi\": " << zone.oi_profile.formation_oi << ",\n"
                          << "        \"oi_change_during_formation\": " << zone.oi_profile.oi_change_during_formation << ",\n"
                          << "        \"oi_change_percent\": " << zone.oi_profile.oi_change_percent << ",\n"
                          << "        \"price_oi_correlation\": " << zone.oi_profile.price_oi_correlation << ",\n"
                          << "        \"oi_data_quality\": " << (zone.oi_profile.oi_data_quality ? "true" : "false") << ",\n"
                          << "        \"market_phase\": \"" << market_phase_to_string(zone.oi_profile.market_phase) << "\",\n"
                          << "        \"oi_score\": " << zone.oi_profile.oi_score << "\n"
                          << "      },\n"
                          // V6.0: Add institutional_index
                          << "      \"institutional_index\": " << zone.institutional_index << "\n";
                
                if (i < active_zones.size() - 1) {
                    active_ss << "    },\n";
                } else {
                    active_ss << "    }\n";
                }
            }
            
            active_ss << "  ],\n"
                      << "  \"next_zone_id\": " << next_zone_id << ",\n"
                      << "  \"active_count\": " << active_zones.size() << ",\n"
                      << "  \"timestamp\": \"" << get_current_date() << "\",\n"
                      << "  \"mode\": \"live\"\n"
                      << "}\n";
            
            std::string active_json_str = active_ss.str();
            std::string active_filepath = get_active_zone_file_path();
            std::replace(active_filepath.begin(), active_filepath.end(), '/', '\\');
            
            std::ofstream active_file(active_filepath, std::ios::out | std::ios::binary | std::ios::trunc);
            
            if (!active_file.is_open()) {
                LOG_WARN("Failed to open active zone file for writing: " << active_filepath);
            } else {
                active_file.write(active_json_str.c_str(), active_json_str.length());
                active_file.flush();
                active_file.close();
                
                LOG_INFO("Active zones cache saved: " << active_zones.size() << " active zones -> " << active_filepath);
            }
        } else if (is_live_mode_ && !enable_live_zone_filtering_) {
            // ⭐ NEW: Log that active zones file is NOT being created
            LOG_DEBUG("Zone filtering disabled - skipping zones_live_active.json creation");
        }
        
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception saving zones: " << e.what());
        return false;
    } catch (...) {
        LOG_ERROR("Unknown exception saving zones");
        return false;
    }
}

bool ZonePersistenceAdapter::load_zones(
    std::vector<Zone>& zones,  // Zone is SDTrading::Core::Zone (we're inside that namespace)
    int& next_zone_id
) {
    // Backtest always starts fresh - no loading
    if (!is_live_mode_) {
        zones.clear();
        next_zone_id = 1;
        LOG_INFO("Backtest mode: Starting with fresh zones");
        return false;  // Not an error - expected behavior
    }
    
    // Live mode: Try to load from files (active cache first, fallback to master)
    try {
        std::string active_filepath = get_active_zone_file_path();
        std::string master_filepath = get_zone_file_path();
        
        // ⭐ NEW: Try to load ACTIVE zones cache first (fast path)
        if (fs::exists(active_filepath)) {
            LOG_INFO("Found active zones cache: " << active_filepath);
            
            std::ifstream active_file(active_filepath);
            if (active_file.is_open()) {
                Json::Value active_root;
                active_file >> active_root;
                active_file.close();
                
                zones.clear();
                next_zone_id = active_root.get("next_zone_id", 1).asInt();
                
                const Json::Value zones_json = active_root["zones"];
                int loaded_count = 0;
                
                for (const Json::Value& zone_json : zones_json) {
                    Zone zone;
                    zone.zone_id = zone_json["zone_id"].asInt();
                    zone.type = (zone_json["type"].asString() == "DEMAND") ? 
                               ZoneType::DEMAND : ZoneType::SUPPLY;
                    zone.base_low = zone_json["base_low"].asDouble();
                    zone.base_high = zone_json["base_high"].asDouble();
                    zone.distal_line = zone_json["distal_line"].asDouble();
                    zone.proximal_line = zone_json["proximal_line"].asDouble();
                    zone.formation_bar = zone_json["formation_bar"].asInt();
                    zone.formation_datetime = zone_json["formation_datetime"].asString();
                    zone.strength = zone_json["strength"].asDouble();
                    zone.state = parse_zone_state(zone_json["state"]);
                    zone.touch_count = zone_json["touch_count"].asInt();
                    zone.is_elite = zone_json["is_elite"].asBool();
                    zone.is_active = true;  // All zones in active cache are active
                    
                    zones.push_back(zone);
                    loaded_count++;
                }
                
                LOG_INFO("✅ Loaded " << loaded_count << " active zones from cache (fast path)");
                return true;
            }
        }
        
        // Fallback: Load from MASTER file (all zones + is_active flag)
        LOG_INFO("Active cache not available, loading from master: " << master_filepath);
        
        if (!fs::exists(master_filepath)) {
            LOG_INFO("No existing zone file found: " << master_filepath);
            LOG_INFO("Starting with fresh zones");
            zones.clear();
            next_zone_id = 1;
            return false;  // Not an error - first run
        }
        
        std::ifstream file(master_filepath);
        if (!file.is_open()) {
            LOG_WARN("Could not open zone file: " << master_filepath);
            zones.clear();
            next_zone_id = 1;
            return false;
        }
        
        Json::Value root;
        file >> root;
        file.close();
        
        // Clear existing zones
        zones.clear();
        
        // Load zone ID counter
        next_zone_id = root.get("next_zone_id", 1).asInt();
        
        // Deserialize zones
        const Json::Value zones_json = root["zones"];
        for (const Json::Value& zone_json : zones_json) {
            Zone zone;
            
            // Core properties
            zone.zone_id = zone_json["zone_id"].asInt();
            zone.type = (zone_json["type"].asString() == "DEMAND") ? 
                       ZoneType::DEMAND : ZoneType::SUPPLY;
            zone.base_low = zone_json["base_low"].asDouble();
            zone.base_high = zone_json["base_high"].asDouble();
            zone.distal_line = zone_json["distal_line"].asDouble();
            zone.proximal_line = zone_json["proximal_line"].asDouble();
            
            // Formation info
            zone.formation_bar = zone_json["formation_bar"].asInt();
            zone.formation_datetime = zone_json["formation_datetime"].asString();
            
            // Strength and state
            zone.strength = zone_json["strength"].asDouble();
            zone.state = parse_zone_state(zone_json["state"]);
            zone.touch_count = zone_json["touch_count"].asInt();
            zone.is_elite = zone_json["is_elite"].asBool();
            zone.is_active = zone_json.get("is_active", true).asBool();  // Default to true for backward compatibility
            
            // Additional metrics
            zone.departure_imbalance = zone_json["departure_imbalance"].asDouble();
            zone.retest_speed = zone_json["retest_speed"].asDouble();
            zone.bars_to_retest = zone_json["bars_to_retest"].asInt();
            
            // Load zone score if present (backward compatible)
            if (zone_json.isMember("zone_score")) {
                const Json::Value& score_json = zone_json["zone_score"];
                zone.zone_score.base_strength_score = score_json["base_strength_score"].asDouble();
                zone.zone_score.elite_bonus_score = score_json["elite_bonus_score"].asDouble();
                zone.zone_score.swing_position_score = score_json["swing_position_score"].asDouble();
                zone.zone_score.regime_alignment_score = score_json["regime_alignment_score"].asDouble();
                zone.zone_score.state_freshness_score = score_json["state_freshness_score"].asDouble();
                zone.zone_score.rejection_confirmation_score = score_json["rejection_confirmation_score"].asDouble();
                zone.zone_score.total_score = score_json["total_score"].asDouble();
                zone.zone_score.entry_aggressiveness = score_json["entry_aggressiveness"].asDouble();
                zone.zone_score.recommended_rr = score_json["recommended_rr"].asDouble();
                zone.zone_score.score_breakdown = score_json["score_breakdown"].asString();
                zone.zone_score.entry_rationale = score_json["entry_rationale"].asString();
            }
            
            // State history
            if (zone_json.isMember("state_history")) {
                const Json::Value& history_json = zone_json["state_history"];
                for (const Json::Value& event_json : history_json) {
                    ZoneStateEvent event;
                    event.timestamp = event_json["timestamp"].asString();
                    event.bar_index = event_json["bar_index"].asInt();
                    event.event = event_json["event"].asString();
                    event.old_state = event_json["old_state"].asString();
                    event.new_state = event_json["new_state"].asString();
                    event.price = event_json["price"].asDouble();
                    event.bar_high = event_json["bar_high"].asDouble();
                    event.bar_low = event_json["bar_low"].asDouble();
                    event.touch_number = event_json["touch_number"].asInt();
                    
                    zone.state_history.push_back(event);
                }
            }
            
            zones.push_back(zone);
        }
        
        LOG_INFO("Zones loaded: " << zones.size() << " zones from " << master_filepath);
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception loading zones: " << e.what());
        zones.clear();
        next_zone_id = 1;
        return false;
    }
}

bool ZonePersistenceAdapter::save_zones_as(
    const std::vector<Zone>& zones,
    int next_zone_id,
    const std::string& filepath
) {
    try {
        // Build JSON with proper formatting
        std::stringstream ss;
        
        ss << "{\n"
           << "  \"zones\": [\n";
        
        for (size_t i = 0; i < zones.size(); i++) {
            const Zone& zone = zones[i];
            
            ss << "    {\n"
               << "      \"zone_id\": " << zone.zone_id << ",\n"
               << "      \"type\": \"" << (zone.type == ZoneType::DEMAND ? "DEMAND" : "SUPPLY") << "\",\n"
               << "      \"base_low\": " << zone.base_low << ",\n"
               << "      \"base_high\": " << zone.base_high << ",\n"
               << "      \"distal_line\": " << zone.distal_line << ",\n"
               << "      \"proximal_line\": " << zone.proximal_line << ",\n"
               << "      \"formation_bar\": " << zone.formation_bar << ",\n"
               << "      \"formation_datetime\": \"" << zone.formation_datetime << "\",\n"
               << "      \"strength\": " << zone.strength << ",\n"
               << "      \"state\": \"" << zone_state_to_string(zone.state) << "\",\n"
               << "      \"touch_count\": " << zone.touch_count << ",\n"
               << "      \"is_elite\": " << (zone.is_elite ? "true" : "false") << ",\n"
                    << "      \"is_active\": " << (zone.is_active ? "true" : "false") << ",\n"
                    // V6.0: Add volume_profile
                    << "      \"volume_profile\": {\n"
                    << "        \"formation_volume\": " << zone.volume_profile.formation_volume << ",\n"
                    << "        \"avg_volume_baseline\": " << zone.volume_profile.avg_volume_baseline << ",\n"
                    << "        \"volume_ratio\": " << zone.volume_profile.volume_ratio << ",\n"
                    << "        \"peak_volume\": " << zone.volume_profile.peak_volume << ",\n"
                    << "        \"high_volume_bar_count\": " << zone.volume_profile.high_volume_bar_count << ",\n"
                    << "        \"volume_score\": " << zone.volume_profile.volume_score << "\n"
                    << "      },\n"
                    // V6.0: Add oi_profile
                    << "      \"oi_profile\": {\n"
                    << "        \"formation_oi\": " << zone.oi_profile.formation_oi << ",\n"
                    << "        \"oi_change_during_formation\": " << zone.oi_profile.oi_change_during_formation << ",\n"
                    << "        \"oi_change_percent\": " << zone.oi_profile.oi_change_percent << ",\n"
                    << "        \"price_oi_correlation\": " << zone.oi_profile.price_oi_correlation << ",\n"
                    << "        \"oi_data_quality\": " << (zone.oi_profile.oi_data_quality ? "true" : "false") << ",\n"
                    << "        \"market_phase\": \"" << market_phase_to_string(zone.oi_profile.market_phase) << "\",\n"
                    << "        \"oi_score\": " << zone.oi_profile.oi_score << "\n"
                    << "      },\n"
                    // V6.0: Add institutional_index
                    << "      \"institutional_index\": " << zone.institutional_index << "\n";
            
            if (i < zones.size() - 1) {
                ss << "    },\n";
            } else {
                ss << "    }\n";
            }
        }
        
        ss << "  ],\n"
           << "  \"next_zone_id\": " << next_zone_id << ",\n"
           << "  \"timestamp\": \"" << get_current_date() << "\",\n"
           << "  \"mode\": \"" << mode_ << "\"\n"
           << "}\n";
        
        std::string json_str = ss.str();
        std::string normalized_path = filepath;
        std::replace(normalized_path.begin(), normalized_path.end(), '/', '\\');
        
        std::ofstream file(normalized_path, std::ios::out | std::ios::binary | std::ios::trunc);
        
        if (!file.is_open()) {
            LOG_ERROR("Failed to open zone file for writing: " << normalized_path);
            return false;
        }
        
        file.write(json_str.c_str(), json_str.length());
        file.flush();
        file.close();
        
        LOG_INFO("Zones saved: " << zones.size() << " zones -> " << normalized_path);
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception saving zones: " << e.what());
        return false;
    }
}

bool ZonePersistenceAdapter::save_zones_with_metadata(
    const std::vector<Zone>& zones,
    int next_zone_id,
    const std::string& filepath,
    const std::map<std::string, std::string>& metadata
) {
    try {
        // Build JSON with metadata and zones
        std::stringstream ss;
        
        ss << "{\n";
        
        // Add metadata section
        ss << "  \"metadata\": {\n";
        size_t meta_idx = 0;
        for (const auto& [key, value] : metadata) {
            ss << "    \"" << key << "\": \"" << value << "\"";
            if (++meta_idx < metadata.size()) {
                ss << ",";
            }
            ss << "\n";
        }
        ss << "  },\n";
        
        // Add zones array
        ss << "  \"zones\": [\n";
        
        for (size_t i = 0; i < zones.size(); i++) {
            const Zone& zone = zones[i];
            
            ss << "    {\n"
               << "      \"zone_id\": " << zone.zone_id << ",\n"
               << "      \"type\": \"" << (zone.type == ZoneType::DEMAND ? "DEMAND" : "SUPPLY") << "\",\n"
               << "      \"base_low\": " << zone.base_low << ",\n"
               << "      \"base_high\": " << zone.base_high << ",\n"
               << "      \"distal_line\": " << zone.distal_line << ",\n"
               << "      \"proximal_line\": " << zone.proximal_line << ",\n"
               << "      \"formation_bar\": " << zone.formation_bar << ",\n"
               << "      \"formation_datetime\": \"" << zone.formation_datetime << "\",\n"
               << "      \"strength\": " << zone.strength << ",\n"
               << "      \"state\": \"" << zone_state_to_string(zone.state) << "\",\n"
               << "      \"touch_count\": " << zone.touch_count << ",\n"
               << "      \"is_elite\": " << (zone.is_elite ? "true" : "false") << ",\n"
                    << "      \"is_active\": " << (zone.is_active ? "true" : "false") << ",\n"
                    // V6.0: Add volume_profile
                    << "      \"volume_profile\": {\n"
                    << "        \"formation_volume\": " << zone.volume_profile.formation_volume << ",\n"
                    << "        \"avg_volume_baseline\": " << zone.volume_profile.avg_volume_baseline << ",\n"
                    << "        \"volume_ratio\": " << zone.volume_profile.volume_ratio << ",\n"
                    << "        \"peak_volume\": " << zone.volume_profile.peak_volume << ",\n"
                    << "        \"high_volume_bar_count\": " << zone.volume_profile.high_volume_bar_count << ",\n"
                    << "        \"volume_score\": " << zone.volume_profile.volume_score << "\n"
                    << "      },\n"
                    // V6.0: Add oi_profile
                    << "      \"oi_profile\": {\n"
                    << "        \"formation_oi\": " << zone.oi_profile.formation_oi << ",\n"
                    << "        \"oi_change_during_formation\": " << zone.oi_profile.oi_change_during_formation << ",\n"
                    << "        \"oi_change_percent\": " << zone.oi_profile.oi_change_percent << ",\n"
                    << "        \"price_oi_correlation\": " << zone.oi_profile.price_oi_correlation << ",\n"
                    << "        \"oi_data_quality\": " << (zone.oi_profile.oi_data_quality ? "true" : "false") << ",\n"
                    << "        \"market_phase\": \"" << market_phase_to_string(zone.oi_profile.market_phase) << "\",\n"
                    << "        \"oi_score\": " << zone.oi_profile.oi_score << "\n"
                    << "      },\n"
                    // V6.0: Add institutional_index
                    << "      \"institutional_index\": " << zone.institutional_index << "\n";
            
            if (i < zones.size() - 1) {
                ss << "    },\n";
            } else {
                ss << "    }\n";
            }
        }
        
        ss << "  ],\n"
           << "  \"next_zone_id\": " << next_zone_id << ",\n"
           << "  \"timestamp\": \"" << get_current_date() << "\",\n"
           << "  \"mode\": \"" << mode_ << "\"\n"
           << "}\n";
        
        std::string json_str = ss.str();
        std::string normalized_path = filepath;
        std::replace(normalized_path.begin(), normalized_path.end(), '/', '\\');
        
        std::ofstream file(normalized_path, std::ios::out | std::ios::binary | std::ios::trunc);
        
        if (!file.is_open()) {
            LOG_ERROR("Failed to open zone file for writing: " << normalized_path);
            return false;
        }
        
        file.write(json_str.c_str(), json_str.length());
        file.flush();
        file.close();
        
        LOG_INFO("Zones saved with metadata: " << zones.size() << " zones -> " << normalized_path);
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception saving zones with metadata: " << e.what());
        return false;
    }
}

std::map<std::string, std::string> ZonePersistenceAdapter::load_metadata(const std::string& filepath) {
    std::map<std::string, std::string> metadata;
    
    try {
        if (!fs::exists(filepath)) {
            LOG_WARN("Zone file not found for metadata load: " << filepath);
            return metadata;
        }
        
        std::ifstream file(filepath);
        if (!file.is_open()) {
            LOG_WARN("Could not open zone file for metadata: " << filepath);
            return metadata;
        }
        
        Json::Value root;
        file >> root;
        file.close();
        
        if (root.isMember("metadata")) {
            const Json::Value& meta_json = root["metadata"];
            for (const auto& key : meta_json.getMemberNames()) {
                metadata[key] = meta_json[key].asString();
            }
            LOG_INFO("Loaded metadata from " << filepath << ": " << metadata.size() << " entries");
        } else {
            LOG_WARN("No metadata section found in zone file: " << filepath);
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception loading metadata: " << e.what());
    }
    
    return metadata;
}

} // namespace Core
} // namespace SDTrading
