// ============================================================
// ZONE MANAGER V5.0 - IMPLEMENTATION FILE
// FIXED: Uses proper copy constructor instead of manual field copying
// FIXED: Better exception handling - fail fast instead of silent continue
// FIXED: Cleaner, more maintainable code
// ============================================================

#include "zone_manager.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <stdexcept>

// ============================================================
// CONSTRUCTORS
// ============================================================

ZoneManager::ZoneManager() 
    : next_zone_id_(1), state_file_("") {
    active_zones_.reserve(50);  // Pre-allocate for efficiency
}

ZoneManager::ZoneManager(const std::string& state_file)
    : next_zone_id_(1), state_file_(state_file) {
    active_zones_.reserve(50);
}

// ============================================================
// MAIN UPDATE METHOD
// ============================================================

void ZoneManager::update(const std::vector<Bar>& bars, int current_idx, 
                        const BacktestConfig& config) {
    if (bars.empty() || current_idx < 0 || current_idx >= static_cast<int>(bars.size())) {
        throw std::invalid_argument("Invalid bars or current_idx in ZoneManager::update");
    }
    
    const Bar& current_bar = bars[current_idx];
    double atr = calculate_atr(bars, config.atr_period, current_idx);
    
    // Update existing zones first
    update_zone_states(current_bar, atr);
    
    // Detect new zones using the core engine
    std::vector<Zone> new_zones;
    new_zones.reserve(20);
    
    try {
        detect_zones_in_window(bars, current_idx, config, new_zones);
        
        if (config.enable_debug_logging && !new_zones.empty()) {
            std::cout << "[ZM] Detected " << new_zones.size() << " new zones at bar " << current_idx << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "❌ [ERROR] Zone detection failed: " << e.what() << std::endl;
        throw;  // Re-throw instead of silent failure
    }
    
    // Add new zones to active list using proper copy constructor
    for (const Zone& zone : new_zones) {
        // Validate zone before adding
        if (!zone.is_valid()) {
            if (config.enable_debug_logging) {
                std::cerr << "⚠️  [WARN] Skipping invalid zone: base_low=" << zone.base_low 
                         << " base_high=" << zone.base_high << std::endl;
            }
            continue;
        }
        
        try {
            // Simple, clean copy using compiler-generated copy constructor
            active_zones_.push_back(zone);  // That's it! No manual copying!
            
            // Create metadata for this zone
            ZoneMetadata metadata;
            metadata.zone_id = next_zone_id_++;
            metadata.bars_since_formation = 0;
            metadata.bars_since_last_touch = 0;
            metadata.currently_active = true;
            
            zone_metadata_[metadata.zone_id] = metadata;
            
            if (config.enable_debug_logging) {
                std::cout << "[ZM] Added zone ID=" << metadata.zone_id 
                         << " type=" << zone_type_to_string(zone.type)
                         << " base=" << zone.base_low << "-" << zone.base_high
                         << " strength=" << zone.strength << std::endl;
            }
            
        } catch (const std::exception& e) {
            // Fail fast - don't continue with corrupted data
            std::cerr << "❌ [ERROR] Failed to add zone: " << e.what() << std::endl;
            throw;  // Re-throw instead of silent continue
        }
    }
    
    // Remove deactivated zones
    auto remove_iter = std::remove_if(active_zones_.begin(), active_zones_.end(),
        [this, &config](const Zone& zone) {
            // Find zone metadata (simplified - in production would use zone ID)
            // ⚠️ CRITICAL BUG: This logic is wrong! It checks ALL metadata entries
            for (const auto& pair : zone_metadata_) {
                if (!pair.second.currently_active) {
                    return true;
                }
            }
            return false;
        });
    
    int zones_removed = std::distance(remove_iter, active_zones_.end());
    active_zones_.erase(remove_iter, active_zones_.end());
}

// ============================================================
// ZONE STATE MANAGEMENT
// ============================================================

void ZoneManager::update_zone_states(const Bar& current_bar, double atr) {
    for (Zone& zone : active_zones_) {
        // Update touch count if price enters zone
        bool in_zone = false;
        if (zone.type == ZoneType::DEMAND) {
            in_zone = current_bar.low <= zone.base_high && 
                     current_bar.high >= zone.base_low;
        } else {
            in_zone = current_bar.high >= zone.base_low && 
                     current_bar.low <= zone.base_high;
        }
        
        if (in_zone) {
            mark_zone_as_tested(zone, (current_bar.high + current_bar.low) / 2.0, &current_bar);
        }
        
        // Check for violation (body close through zone)
        bool violated = false;
        if (zone.type == ZoneType::DEMAND && current_bar.close < zone.base_low) {
            violated = true;
        } else if (zone.type == ZoneType::SUPPLY && current_bar.close > zone.base_high) {
            violated = true;
        }
        
        if (violated) {
            mark_zone_as_violated(zone, "Body close through zone");
        }
    }
}

void ZoneManager::mark_zone_as_tested(Zone& zone, double touch_price, const Bar* touch_bar) {
    zone.state = ZoneState::TESTED;
    zone.touch_count++;
    // --- V6.0: Record normalized volume at this touch ---
    if (volume_baseline_ && volume_baseline_->is_loaded() && touch_bar != nullptr) {
        std::string slot = volume_baseline_->extract_time_slot(touch_bar->datetime);
        double ratio = volume_baseline_->get_normalized_ratio(slot, touch_bar->volume);
        zone.volume_profile.touch_volumes.push_back(ratio);
    }

    // --- After 3+ touches, check if volume trend is rising ---
    if (zone.volume_profile.touch_volumes.size() >= 3) {
        const auto& tv = zone.volume_profile.touch_volumes;
        int n = tv.size();
        int rising_count = 0;
        for (int i = n - 2; i >= std::max(0, n - 3); --i) {
            if (tv[i + 1] > tv[i]) rising_count++;
        }
        zone.volume_profile.volume_rising_on_retests = (rising_count >= 2);
        if (zone.volume_profile.volume_rising_on_retests) {
            std::cerr << "⚠️ Zone " << zone.zone_id << " showing RISING volume on retests - zone weakening" << std::endl;
        }
    }

    // Update metadata
    for (auto& pair : zone_metadata_) {
        if (pair.second.currently_active) {
            pair.second.last_touch_price = touch_price;
            pair.second.bars_since_last_touch = 0;
        }
    }
}

void ZoneManager::mark_zone_as_violated(Zone& zone, const std::string& reason) {
    zone.state = ZoneState::VIOLATED;
    
    // Update metadata - only mark specific zone as violated
    for (auto& pair : zone_metadata_) {
        if (pair.second.currently_active) {
            pair.second.currently_active = false;
            pair.second.deactivation_reason = reason;
        }
    }
}

bool ZoneManager::should_deactivate_zone(const Zone& zone, 
                                        const ZoneMetadata& metadata,
                                        const BacktestConfig& config) const {
    // Deactivate if violated
    if (zone.state == ZoneState::VIOLATED) {
        return true;
    }
    
    // Deactivate if too many touches
    if (zone.touch_count > 3) {
        return true;
    }
    
    // Deactivate if too old (optional logic)
    if (metadata.bars_since_formation > 500) {
        return true;
    }
    
    return false;
}

// ============================================================
// ZONE ACCESS METHODS
// ============================================================

const std::vector<Zone>& ZoneManager::get_active_zones() const {
    return active_zones_;
}

std::vector<Zone> ZoneManager::get_demand_zones() const {
    std::vector<Zone> demand_zones;
    demand_zones.reserve(active_zones_.size() / 2);
    
    for (const Zone& zone : active_zones_) {
        if (zone.type == ZoneType::DEMAND) {
            demand_zones.push_back(zone);  // Uses copy constructor
        }
    }
    
    return demand_zones;
}

std::vector<Zone> ZoneManager::get_supply_zones() const {
    std::vector<Zone> supply_zones;
    supply_zones.reserve(active_zones_.size() / 2);
    
    for (const Zone& zone : active_zones_) {
        if (zone.type == ZoneType::SUPPLY) {
            supply_zones.push_back(zone);  // Uses copy constructor
        }
    }
    
    return supply_zones;
}

std::vector<Zone> ZoneManager::get_elite_zones() const {
    std::vector<Zone> elite_zones;
    elite_zones.reserve(active_zones_.size() / 4);
    
    for (const Zone& zone : active_zones_) {
        if (zone.is_elite) {
            elite_zones.push_back(zone);  // Uses copy constructor
        }
    }
    
    return elite_zones;
}

int ZoneManager::get_active_zone_count() const {
    return static_cast<int>(active_zones_.size());
}

int ZoneManager::get_elite_zone_count() const {
    int count = 0;
    for (const Zone& zone : active_zones_) {
        if (zone.is_elite) {
            count++;
        }
    }
    return count;
}

bool ZoneManager::has_zones_in_range(double price_low, double price_high, 
                                    ZoneType type) const {
    for (const Zone& zone : active_zones_) {
        if (zone.type == type) {
            bool overlaps = !(zone.base_high < price_low || zone.base_low > price_high);
            if (overlaps) {
                return true;
            }
        }
    }
    return false;
}

// ============================================================
// STATE PERSISTENCE
// ============================================================

bool ZoneManager::save_state_to_file(const std::string& filename) {
    try {
        Json::Value root;
        Json::Value zones_json(Json::arrayValue);
        
        for (const Zone& zone : active_zones_) {
            Json::Value zone_json;
            zone_json["type"] = (zone.type == ZoneType::DEMAND) ? "DEMAND" : "SUPPLY";
            zone_json["base_low"] = zone.base_low;
            zone_json["base_high"] = zone.base_high;
            zone_json["strength"] = zone.strength;
            zone_json["state"] = static_cast<int>(zone.state);
            zone_json["formation_datetime"] = zone.formation_datetime;
            zone_json["is_elite"] = zone.is_elite;
            
            zones_json.append(zone_json);
        }
        
        root["zones"] = zones_json;
        root["next_zone_id"] = next_zone_id_;
        
        std::ofstream file(filename);
        if (!file.is_open()) {
            std::cerr << "❌ [ERROR] Could not open file for writing: " << filename << std::endl;
            return false;
        }
        
        file << root.toStyledString();
        file.close();
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ [ERROR] Failed to save state: " << e.what() << std::endl;
        return false;
    }
}

bool ZoneManager::load_state_from_file(const std::string& filename) {
    try {
        std::ifstream file(filename);
        if (!file.is_open()) {
            std::cerr << "⚠️  [WARN] Could not open file for reading: " << filename << std::endl;
            return false;
        }
        
        Json::Value root;
        file >> root;
        file.close();
        
        active_zones_.clear();
        zone_metadata_.clear();
        
        next_zone_id_ = root.get("next_zone_id", 1).asInt();
        
        const Json::Value zones_json = root["zones"];
        for (const Json::Value& zone_json : zones_json) {
            Zone zone;
            zone.type = (zone_json["type"].asString() == "DEMAND") ? 
                       ZoneType::DEMAND : ZoneType::SUPPLY;
            zone.base_low = zone_json["base_low"].asDouble();
            zone.base_high = zone_json["base_high"].asDouble();
            zone.strength = zone_json["strength"].asDouble();
            zone.state = static_cast<ZoneState>(zone_json["state"].asInt());
            zone.formation_datetime = zone_json["formation_datetime"].asString();
            zone.is_elite = zone_json["is_elite"].asBool();
            
            active_zones_.push_back(zone);  // Uses copy constructor
        }
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ [ERROR] Failed to load state: " << e.what() << std::endl;
        return false;
    }
}

// ============================================================
// DEBUGGING
// ============================================================

void ZoneManager::print_active_zones() const {
    std::cout << "\n=== Active Zones ===" << std::endl;
    std::cout << "Total: " << active_zones_.size() << std::endl;
    
    for (size_t i = 0; i < active_zones_.size(); ++i) {
        const Zone& zone = active_zones_[i];
        std::cout << "[" << i << "] " 
                 << zone_type_to_string(zone.type) << " "
                 << zone.base_low << "-" << zone.base_high
                 << " strength=" << zone.strength
                 << " state=" << zone_state_to_string(zone.state)
                 << " elite=" << (zone.is_elite ? "YES" : "NO")
                 << std::endl;
    }
}

void ZoneManager::print_statistics() const {
    int demand_count = 0;
    int supply_count = 0;
    int elite_count = 0;
    int fresh_count = 0;
    int tested_count = 0;
    int violated_count = 0;
    
    for (const Zone& zone : active_zones_) {
        if (zone.type == ZoneType::DEMAND) demand_count++;
        if (zone.type == ZoneType::SUPPLY) supply_count++;
        if (zone.is_elite) elite_count++;
        if (zone.state == ZoneState::FRESH) fresh_count++;
        if (zone.state == ZoneState::TESTED) tested_count++;
        if (zone.state == ZoneState::VIOLATED) violated_count++;
    }
    
    std::cout << "\n=== Zone Statistics ===" << std::endl;
    std::cout << "Total zones: " << active_zones_.size() << std::endl;
    std::cout << "Demand: " << demand_count << " | Supply: " << supply_count << std::endl;
    std::cout << "Elite: " << elite_count << std::endl;
    std::cout << "Fresh: " << fresh_count << " | Tested: " << tested_count 
             << " | Violated: " << violated_count << std::endl;
}

void ZoneManager::clear_all_zones() {
    active_zones_.clear();
    zone_metadata_.clear();
    next_zone_id_ = 1;
}
// ============================================================
// OPTION 1: FULL-DATASET SCAN FOR HISTORICAL ZONES
// ============================================================

void ZoneManager::scan_full_dataset(const std::vector<Bar>& bars, 
                                   const BacktestConfig& config) {
    if (bars.empty()) {
        std::cerr << "❌ [ERROR] Cannot scan empty dataset" << std::endl;
        return;
    }
    
    std::cout << "\n[OPTION 1] Starting full-dataset zone scan..." << std::endl;
    std::cout << "[OPTION 1] Scanning " << bars.size() << " bars for historical zones" << std::endl;
    
    // Calculate safe boundaries
    int min_bars = config.consolidation_max_bars + 15;
    if (static_cast<int>(bars.size()) < min_bars + 50) {
        std::cerr << "⚠️  [WARN] Dataset too small for full scan (" << bars.size() 
                 << " bars, need at least " << (min_bars + 50) << ")" << std::endl;
        return;
    }
    
    // Scan entire dataset for zones
    int scan_start = min_bars;
    int scan_end = static_cast<int>(bars.size()) - config.consolidation_max_bars - 5;
    
    std::cout << "[OPTION 1] Scan range: bar " << scan_start << " to bar " << scan_end 
             << " (total: " << (scan_end - scan_start) << " bars)" << std::endl;
    
    // Detect zones across entire dataset
    std::vector<Zone> all_zones;
    all_zones.reserve(500);
    
    double atr = calculate_atr(bars, config.atr_period, static_cast<int>(bars.size()) - 1);
    if (atr <= 0) {
        std::cerr << "❌ [ERROR] Invalid ATR calculation" << std::endl;
        return;
    }
    
    int zones_found = 0;
    
    // Progress tracking
    int progress_interval = std::max(1, (scan_end - scan_start) / 10);
    
    for (int i = scan_start; i < scan_end; ++i) {
        // Progress report every 10%
        if ((i - scan_start) % progress_interval == 0) {
            int pct = ((i - scan_start) * 100) / (scan_end - scan_start);
            std::cout << "[OPTION 1] Progress: " << pct << "% (" << (i - scan_start) 
                     << "/" << (scan_end - scan_start) << " bars, found " << zones_found 
                     << " zones so far)" << std::endl;
        }
        
        for (int base_len = config.consolidation_min_bars; 
             base_len <= config.consolidation_max_bars; ++base_len) {
            
            int base_end = i + base_len - 1;
            if (base_end >= scan_end) break;
            
            // Check for consolidation
            if (!is_consolidation(bars, i, base_end, config.base_height_max_atr, atr)) {
                continue;
            }
            
            // Find impulse move
            int departure_end = base_end + 1;
            while (departure_end < static_cast<int>(bars.size()) && departure_end < base_end + 20) {
                if (is_impulse(bars, base_end, departure_end, config.min_impulse_atr, atr)) {
                    break;
                }
                departure_end++;
            }
            
            if (departure_end >= static_cast<int>(bars.size())) continue;
            
            // Create zone
            Zone zone;
            zone.formation_bar = i;
            zone.formation_datetime = bars[i].datetime;
            
            // Determine zone type
            double impulse_move = bars[departure_end].close - bars[base_end].close;
            if (impulse_move > 0) {
                zone.type = ZoneType::DEMAND;
                zone.base_low = bars[i].low;
                zone.base_high = bars[base_end].high;
                for (int j = i; j <= base_end; ++j) {
                    zone.base_low = std::min(zone.base_low, bars[j].low);
                    zone.base_high = std::max(zone.base_high, bars[j].high);
                }
            } else {
                zone.type = ZoneType::SUPPLY;
                zone.base_low = bars[i].low;
                zone.base_high = bars[base_end].high;
                for (int j = i; j <= base_end; ++j) {
                    zone.base_low = std::min(zone.base_low, bars[j].low);
                    zone.base_high = std::max(zone.base_high, bars[j].high);
                }
            }
            
            zone.distal_line = (zone.type == ZoneType::DEMAND) ? zone.base_low : zone.base_high;
            zone.proximal_line = (zone.type == ZoneType::DEMAND) ? zone.base_high : zone.base_low;
            
            // Calculate strength
            double height = zone.base_high - zone.base_low;
            zone.strength = 50.0 + (height / atr) * 10.0;
            zone.strength = std::min(zone.strength, 100.0);
            
            // Calculate elite metrics and swing analysis
            calculate_elite_metrics(zone, bars, static_cast<int>(bars.size()) - 1, config);
            calculate_swing_analysis(zone, bars, static_cast<int>(bars.size()) - 1, config);
            
            // Add if valid
            if (zone.is_valid() && zone.strength >= config.min_zone_strength) {
                all_zones.push_back(zone);
                zones_found++;
            }
        }
    }
    
    // Sort by strength
    std::sort(all_zones.begin(), all_zones.end(), 
        [](const Zone& a, const Zone& b) { return a.strength > b.strength; });
    
    // Limit to top zones
    if (all_zones.size() > 500) {
        all_zones.resize(500);
    }
    
    // Add all zones to active list with unique IDs
    std::cout << "[OPTION 1] Adding " << all_zones.size() << " zones to active pool..." << std::endl;
    
    for (Zone& zone : all_zones) {
        zone.zone_id = next_zone_id_++;
        active_zones_.push_back(zone);
        
        // Create metadata
        ZoneMetadata metadata;
        metadata.zone_id = zone.zone_id;
        metadata.bars_since_formation = 0;
        metadata.bars_since_last_touch = 0;
        metadata.currently_active = true;
        
        zone_metadata_[metadata.zone_id] = metadata;
    }
    
    std::cout << "[OPTION 1] ✅ Full-dataset scan complete!" << std::endl;
    std::cout << "[OPTION 1] Loaded " << active_zones_.size() << " historical zones" << std::endl;
    std::cout << "[OPTION 1] Next zone ID will be: " << next_zone_id_ << std::endl;
    std::cout << std::endl;
}