#include "volume_baseline.h"
#include "logger.h"
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace SDTrading {
namespace Utils {

VolumeBaseline::VolumeBaseline() : loaded_(false) {}

bool VolumeBaseline::load_from_file(const std::string& filepath) {
    LOG_INFO("Loading volume baseline from: " + filepath);
    
    try {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            LOG_ERROR("Failed to open volume baseline file: " + filepath);
            return false;
        }
        
        json j;
        file >> j;
        
        // Parse JSON into map
        baseline_map_.clear();
        for (auto& [key, value] : j.items()) {
            baseline_map_[key] = value.get<double>();
        }
        
        loaded_ = true;
        LOG_INFO("✅ Loaded " + std::to_string(baseline_map_.size()) + " time slots from baseline");
        
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception loading volume baseline: " + std::string(e.what()));
        return false;
    }
}

double VolumeBaseline::get_normalized_ratio(const std::string& time_slot, 
                                             double current_volume) const {
    if (!loaded_) {
        LOG_WARN("Volume baseline not loaded - returning ratio 1.0");
        return 1.0;
    }
    
    auto it = baseline_map_.find(time_slot);
    if (it == baseline_map_.end()) {
        LOG_WARN("Time slot not found in baseline: " << time_slot);
        return 1.0;  // Fallback
    }
    
    if (it->second == 0) {
        LOG_WARN("Zero baseline volume for slot: " << time_slot);
        return 1.0;
    }
    
    return current_volume / it->second;
}

double VolumeBaseline::get_baseline(const std::string& time_slot) const {
    auto it = baseline_map_.find(time_slot);
    if (it != baseline_map_.end()) {
        return it->second;
    }
    return 0;
}

std::string VolumeBaseline::extract_time_slot(const std::string& datetime) const {
    // Extract HH:MM from datetime string
    // Expected format: "2024-02-08 09:15:00"
    if (datetime.length() >= 16) {
        return datetime.substr(11, 5);  // Extract "09:15"
    }
    return "00:00";  // Fallback
}

} // namespace Utils
} // namespace SDTrading
