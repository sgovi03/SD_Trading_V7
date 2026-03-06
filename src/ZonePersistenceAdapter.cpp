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

static MarketPhase parse_market_phase_value(const Json::Value& phase_val) {
    if (phase_val.isInt()) {
        int value = phase_val.asInt();
        if (value >= static_cast<int>(MarketPhase::LONG_BUILDUP) &&
            value <= static_cast<int>(MarketPhase::NEUTRAL)) {
            return static_cast<MarketPhase>(value);
        }
        return MarketPhase::NEUTRAL;
    }

    if (phase_val.isString()) {
        const std::string phase_str = phase_val.asString();
        if (phase_str == "LONG_BUILDUP") return MarketPhase::LONG_BUILDUP;
        if (phase_str == "SHORT_COVERING") return MarketPhase::SHORT_COVERING;
        if (phase_str == "SHORT_BUILDUP") return MarketPhase::SHORT_BUILDUP;
        if (phase_str == "LONG_UNWINDING") return MarketPhase::LONG_UNWINDING;
        if (phase_str == "NEUTRAL") return MarketPhase::NEUTRAL;

        try {
            int value = std::stoi(phase_str);
            if (value >= static_cast<int>(MarketPhase::LONG_BUILDUP) &&
                value <= static_cast<int>(MarketPhase::NEUTRAL)) {
                return static_cast<MarketPhase>(value);
            }
        } catch (...) {
        }
    }

    return MarketPhase::NEUTRAL;
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

static void append_zone_json(std::stringstream& ss, const Zone& zone) {
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
       << "      \"bars_to_retest\": " << zone.bars_to_retest << ",\n"
       << "      \"was_swept\": " << (zone.was_swept ? "true" : "false") << ",\n"
       << "      \"sweep_bar\": " << zone.sweep_bar << ",\n"
       << "      \"bars_inside_after_sweep\": " << zone.bars_inside_after_sweep << ",\n"
       << "      \"reclaim_eligible\": " << (zone.reclaim_eligible ? "true" : "false") << ",\n"
       << "      \"is_flipped_zone\": " << (zone.is_flipped_zone ? "true" : "false") << ",\n"
       << "      \"parent_zone_id\": " << zone.parent_zone_id << ",\n"
       << "      \"entry_retry_count\": " << zone.entry_retry_count << ",\n"
       << "      \"swing_analysis\": {\n"
       << "        \"is_at_swing_high\": " << (zone.swing_analysis.is_at_swing_high ? "true" : "false") << ",\n"
       << "        \"is_at_swing_low\": " << (zone.swing_analysis.is_at_swing_low ? "true" : "false") << ",\n"
       << "        \"swing_percentile\": " << zone.swing_analysis.swing_percentile << ",\n"
       << "        \"bars_to_higher_high\": " << zone.swing_analysis.bars_to_higher_high << ",\n"
       << "        \"bars_to_lower_low\": " << zone.swing_analysis.bars_to_lower_low << ",\n"
       << "        \"swing_score\": " << zone.swing_analysis.swing_score << "\n"
       << "      },\n"
       << "      \"zone_score\": {\n"
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
       << "      },\n"
       << "      \"state_history\": [\n";

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

    ss << "      ],\n"
       << "      \"volume_profile\": {\n"
       << "        \"formation_volume\": " << zone.volume_profile.formation_volume << ",\n"
       << "        \"avg_volume_baseline\": " << zone.volume_profile.avg_volume_baseline << ",\n"
       << "        \"volume_ratio\": " << zone.volume_profile.volume_ratio << ",\n"
       << "        \"peak_volume\": " << zone.volume_profile.peak_volume << ",\n"
       << "        \"high_volume_bar_count\": " << zone.volume_profile.high_volume_bar_count << ",\n"
       << "        \"departure_avg_volume\": " << zone.volume_profile.departure_avg_volume << ",\n"
       << "        \"departure_volume_ratio\": " << zone.volume_profile.departure_volume_ratio << ",\n"
       << "        \"departure_peak_volume\": " << zone.volume_profile.departure_peak_volume << ",\n"
       << "        \"departure_bar_count\": " << zone.volume_profile.departure_bar_count << ",\n"
       << "        \"strong_departure\": " << (zone.volume_profile.strong_departure ? "true" : "false") << ",\n"
       << "        \"is_initiative\": " << (zone.volume_profile.is_initiative ? "true" : "false") << ",\n"
       << "        \"volume_efficiency\": " << zone.volume_profile.volume_efficiency << ",\n"
       << "        \"has_volume_climax\": " << (zone.volume_profile.has_volume_climax ? "true" : "false") << ",\n"
       << "        \"touch_volumes\": [";

    for (size_t k = 0; k < zone.volume_profile.touch_volumes.size(); ++k) {
        ss << zone.volume_profile.touch_volumes[k];
        if (k < zone.volume_profile.touch_volumes.size() - 1) {
            ss << ", ";
        }
    }

    ss << "],\n"
       << "        \"volume_rising_on_retests\": " << (zone.volume_profile.volume_rising_on_retests ? "true" : "false") << ",\n"
       << "        \"volume_score\": " << zone.volume_profile.volume_score << "\n"
       << "      },\n"
       << "      \"oi_profile\": {\n"
       << "        \"formation_oi\": " << zone.oi_profile.formation_oi << ",\n"
       << "        \"oi_change_during_formation\": " << zone.oi_profile.oi_change_during_formation << ",\n"
       << "        \"oi_change_percent\": " << zone.oi_profile.oi_change_percent << ",\n"
       << "        \"price_oi_correlation\": " << zone.oi_profile.price_oi_correlation << ",\n"
       << "        \"oi_data_quality\": " << (zone.oi_profile.oi_data_quality ? "true" : "false") << ",\n"
       << "        \"market_phase\": \"" << market_phase_to_string(zone.oi_profile.market_phase) << "\",\n"
       << "        \"oi_score\": " << zone.oi_profile.oi_score << "\n"
       << "      },\n"
       << "      \"institutional_index\": " << zone.institutional_index << "\n"
       << "    }";
}

static void parse_zone_json(const Json::Value& zone_json, Zone& zone, bool force_active_true = false) {
    zone.zone_id = zone_json.get("zone_id", -1).asInt();
    zone.type = (zone_json.get("type", "DEMAND").asString() == "DEMAND") ?
               ZoneType::DEMAND : ZoneType::SUPPLY;
    zone.base_low = zone_json.get("base_low", 0.0).asDouble();
    zone.base_high = zone_json.get("base_high", 0.0).asDouble();
    zone.distal_line = zone_json.get("distal_line", 0.0).asDouble();
    zone.proximal_line = zone_json.get("proximal_line", 0.0).asDouble();
    zone.formation_bar = zone_json.get("formation_bar", 0).asInt();
    zone.formation_datetime = zone_json.get("formation_datetime", "").asString();
    zone.strength = zone_json.get("strength", 0.0).asDouble();
    zone.state = parse_zone_state(zone_json["state"]);
    zone.touch_count = zone_json.get("touch_count", 0).asInt();
    zone.is_elite = zone_json.get("is_elite", false).asBool();
    zone.is_active = force_active_true ? true : zone_json.get("is_active", true).asBool();

    zone.departure_imbalance = zone_json.get("departure_imbalance", 0.0).asDouble();
    zone.retest_speed = zone_json.get("retest_speed", 0.0).asDouble();
    zone.bars_to_retest = zone_json.get("bars_to_retest", 0).asInt();
    zone.was_swept = zone_json.get("was_swept", false).asBool();
    zone.sweep_bar = zone_json.get("sweep_bar", -1).asInt();
    zone.bars_inside_after_sweep = zone_json.get("bars_inside_after_sweep", 0).asInt();
    zone.reclaim_eligible = zone_json.get("reclaim_eligible", false).asBool();
    zone.is_flipped_zone = zone_json.get("is_flipped_zone", false).asBool();
    zone.parent_zone_id = zone_json.get("parent_zone_id", -1).asInt();
    zone.entry_retry_count = zone_json.get("entry_retry_count", 0).asInt();

    if (zone_json.isMember("swing_analysis")) {
        const Json::Value& sa = zone_json["swing_analysis"];
        zone.swing_analysis.is_at_swing_high = sa.get("is_at_swing_high", false).asBool();
        zone.swing_analysis.is_at_swing_low = sa.get("is_at_swing_low", false).asBool();
        zone.swing_analysis.swing_percentile = sa.get("swing_percentile", 50.0).asDouble();
        zone.swing_analysis.bars_to_higher_high = sa.get("bars_to_higher_high", -1).asInt();
        zone.swing_analysis.bars_to_lower_low = sa.get("bars_to_lower_low", -1).asInt();
        zone.swing_analysis.swing_score = sa.get("swing_score", 0.0).asDouble();
    }

    if (zone_json.isMember("zone_score")) {
        const Json::Value& score_json = zone_json["zone_score"];
        zone.zone_score.base_strength_score = score_json.get("base_strength_score", 0.0).asDouble();
        zone.zone_score.elite_bonus_score = score_json.get("elite_bonus_score", 0.0).asDouble();
        zone.zone_score.swing_position_score = score_json.get("swing_position_score", 0.0).asDouble();
        zone.zone_score.regime_alignment_score = score_json.get("regime_alignment_score", 0.0).asDouble();
        zone.zone_score.state_freshness_score = score_json.get("state_freshness_score", 0.0).asDouble();
        zone.zone_score.rejection_confirmation_score = score_json.get("rejection_confirmation_score", 0.0).asDouble();
        zone.zone_score.total_score = score_json.get("total_score", 0.0).asDouble();
        zone.zone_score.entry_aggressiveness = score_json.get("entry_aggressiveness", 0.0).asDouble();
        zone.zone_score.recommended_rr = score_json.get("recommended_rr", 2.0).asDouble();
        zone.zone_score.score_breakdown = score_json.get("score_breakdown", "").asString();
        zone.zone_score.entry_rationale = score_json.get("entry_rationale", "").asString();
    }

    zone.state_history.clear();
    if (zone_json.isMember("state_history")) {
        const Json::Value& history_json = zone_json["state_history"];
        for (const Json::Value& event_json : history_json) {
            ZoneStateEvent event;
            event.timestamp = event_json.get("timestamp", "").asString();
            event.bar_index = event_json.get("bar_index", -1).asInt();
            event.event = event_json.get("event", "").asString();
            event.old_state = event_json.get("old_state", "").asString();
            event.new_state = event_json.get("new_state", "").asString();
            event.price = event_json.get("price", 0.0).asDouble();
            event.bar_high = event_json.get("bar_high", 0.0).asDouble();
            event.bar_low = event_json.get("bar_low", 0.0).asDouble();
            event.touch_number = event_json.get("touch_number", 0).asInt();
            zone.state_history.push_back(event);
        }
    }

    if (zone_json.isMember("volume_profile")) {
        const Json::Value& vp = zone_json["volume_profile"];
        zone.volume_profile.formation_volume = vp.get("formation_volume", 0.0).asDouble();
        zone.volume_profile.avg_volume_baseline = vp.get("avg_volume_baseline", 0.0).asDouble();
        zone.volume_profile.volume_ratio = vp.get("volume_ratio", 0.0).asDouble();
        zone.volume_profile.peak_volume = vp.get("peak_volume", 0.0).asDouble();
        zone.volume_profile.high_volume_bar_count = vp.get("high_volume_bar_count", 0).asInt();
        zone.volume_profile.departure_avg_volume = vp.get("departure_avg_volume", 0.0).asDouble();
        zone.volume_profile.departure_volume_ratio = vp.get("departure_volume_ratio", 0.0).asDouble();
        zone.volume_profile.departure_peak_volume = vp.get("departure_peak_volume", 0.0).asDouble();
        zone.volume_profile.departure_bar_count = vp.get("departure_bar_count", 0).asInt();
        zone.volume_profile.strong_departure = vp.get("strong_departure", false).asBool();
        zone.volume_profile.is_initiative = vp.get("is_initiative", false).asBool();
        zone.volume_profile.volume_efficiency = vp.get("volume_efficiency", 0.0).asDouble();
        zone.volume_profile.has_volume_climax = vp.get("has_volume_climax", false).asBool();
        zone.volume_profile.volume_rising_on_retests = vp.get("volume_rising_on_retests", false).asBool();
        zone.volume_profile.touch_volumes.clear();
        if (vp.isMember("touch_volumes") && vp["touch_volumes"].isArray()) {
            for (const auto& touch_vol : vp["touch_volumes"]) {
                zone.volume_profile.touch_volumes.push_back(touch_vol.asDouble());
            }
        }
        zone.volume_profile.volume_score = vp.get("volume_score", 0.0).asDouble();
    }

    if (zone_json.isMember("oi_profile")) {
        const Json::Value& op = zone_json["oi_profile"];
        zone.oi_profile.formation_oi = static_cast<long>(op.get("formation_oi", 0).asDouble());
        zone.oi_profile.oi_change_during_formation = static_cast<long>(op.get("oi_change_during_formation", 0).asDouble());
        zone.oi_profile.oi_change_percent = op.get("oi_change_percent", 0.0).asDouble();
        zone.oi_profile.price_oi_correlation = op.get("price_oi_correlation", 0.0).asDouble();
        zone.oi_profile.oi_data_quality = op.get("oi_data_quality", false).asBool();
        if (op.isMember("market_phase")) {
            zone.oi_profile.market_phase = parse_market_phase_value(op["market_phase"]);
        } else {
            zone.oi_profile.market_phase = MarketPhase::NEUTRAL;
        }
        zone.oi_profile.oi_score = op.get("oi_score", 0.0).asDouble();
    }

    zone.institutional_index = zone_json.get("institutional_index", 0.0).asDouble();
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
            append_zone_json(ss, zone);
            if (i < zones.size() - 1) {
                ss << ",\n";
            } else {
                ss << "\n";
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
            
            std::string active_filepath = get_active_zone_file_path();
            if (!save_zones_as(active_zones, next_zone_id, active_filepath)) {
                LOG_WARN("Failed to save active zone cache: " << active_filepath);
            } else {
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
    
    // Live mode: Try to load from files (master first, fallback to active cache)
    try {
        std::string active_filepath = get_active_zone_file_path();
        std::string master_filepath = get_zone_file_path();
        std::string file_to_load;
        
        if (fs::exists(master_filepath)) {
            file_to_load = master_filepath;
            LOG_INFO("Loading zones from master: " << master_filepath);
        } else if (fs::exists(active_filepath)) {
            file_to_load = active_filepath;
            LOG_INFO("Master missing, loading from active cache: " << active_filepath);
        } else {
            LOG_INFO("No existing zone file found (master or active cache)");
            zones.clear();
            next_zone_id = 1;
            return false;
        }
        
        std::ifstream file(file_to_load);
        if (!file.is_open()) {
            LOG_WARN("Could not open zone file: " << file_to_load);
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
            parse_zone_json(zone_json, zone, (file_to_load == active_filepath));
            zones.push_back(zone);
        }
        
        LOG_INFO("Zones loaded: " << zones.size() << " zones from " << file_to_load);
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
            append_zone_json(ss, zone);
            if (i < zones.size() - 1) {
                ss << ",\n";
            } else {
                ss << "\n";
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
            append_zone_json(ss, zone);
            if (i < zones.size() - 1) {
                ss << ",\n";
            } else {
                ss << "\n";
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
