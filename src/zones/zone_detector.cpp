#include "zone_detector.h"
#include "../utils/logger.h"
#include "../analysis/market_analyzer.h"
#include "../scoring/volume_scorer.h"
#include "../scoring/oi_scorer.h"
#include "../utils/institutional_index.h"
#include <sstream>
#include <iomanip>
#include <chrono>

namespace SDTrading {
namespace Core {

ZoneDetector::ZoneDetector(const Config& cfg) 
    : config(cfg), volume_baseline_(nullptr), volume_scorer_(nullptr), oi_scorer_(nullptr) {
    LOG_INFO("ZoneDetector initialized");
}

void ZoneDetector::build_atr_cache() const {
    if (atr_cache_valid && atr_cache.size() == bars.size()) {
        return;  // Cache already built
    }
    
    atr_cache.clear();
    atr_cache.resize(bars.size(), 0.0);
    
    // Precompute ATR for all bars
    for (size_t i = config.atr_period; i < bars.size(); i++) {
        atr_cache[i] = MarketAnalyzer::calculate_atr(bars, config.atr_period, i);
    }
    
    atr_cache_valid = true;
    LOG_INFO("ATR cache built for " + std::to_string(bars.size()) + " bars");
}

double ZoneDetector::get_cached_atr(int index) const {
    if (!atr_cache_valid || index < 0 || index >= (int)atr_cache.size()) {
        // Fallback to direct calculation if cache not available
        return MarketAnalyzer::calculate_atr(bars, config.atr_period, index);
    }
    return atr_cache[index];
}

void ZoneDetector::add_bar(const Bar& bar) {
    bars.push_back(bar);
    atr_cache_valid = false;  // Invalidate cache when bars change
}

void ZoneDetector::update_last_bar(const Bar& bar) {
    if (!bars.empty()) {
        bars.back() = bar;
        atr_cache_valid = false;  // Invalidate cache when bars change
    }
}

bool ZoneDetector::is_consolidation(int start_index, int length, double& high, double& low) const {
    if (start_index + length > (int)bars.size()) return false;
    
    high = bars[start_index].high;
    low = bars[start_index].low;
    
    // Find range
    for (int i = start_index; i < start_index + length; i++) {
        if (bars[i].high > high) high = bars[i].high;
        if (bars[i].low < low) low = bars[i].low;
    }
    
    double range = high - low;
    double atr = get_cached_atr(start_index + length - 1);  // Use cached ATR
    
    // Check if range is tight enough
    return (range <= config.max_consolidation_range * atr);
}

bool ZoneDetector::has_impulse_before(int index) const {
    if (index < 6) return false;
    
    double price_start = bars[index - 5].close;
    double price_end = bars[index].close;
    double move = std::abs(price_end - price_start);
    double atr = get_cached_atr(index);  // Use cached ATR
    
    return (move >= config.min_impulse_atr * atr);
}

bool ZoneDetector::has_impulse_after(int index) const {
    if (index + 6 >= (int)bars.size()) return false;
    
    double price_start = bars[index].close;
    double price_end = bars[index + 5].close;
    double move = std::abs(price_end - price_start);
    double atr = get_cached_atr(index);  // Use cached ATR
    
    return (move >= config.min_impulse_atr * atr);
}

double ZoneDetector::calculate_zone_strength(const Zone& zone) const {
    double range = zone.base_high - zone.base_low;
    double atr = get_cached_atr(zone.formation_bar);  // Use cached ATR
    
    if (atr < 0.0001) return 0.0;
    
    double ratio = range / atr;
    double max_range = config.max_consolidation_range;
    double strength = 100.0 * (1.0 - std::min<double>(ratio / max_range, 1.0));
    
    return std::max<double>(0.0, std::min<double>(100.0, strength));
}

void ZoneDetector::analyze_elite_quality(Zone& zone) {
    if (zone.formation_bar + 10 >= (int)bars.size()) {
        zone.is_elite = false;
        return;
    }
    
    // Calculate ATR at formation
    double atr = get_cached_atr(zone.formation_bar);  // Use cached ATR
    
    // Find departure point (first significant move away from zone)
    int departure_bar = -1;
    double zone_mid = (zone.proximal_line + zone.distal_line) / 2.0;
    
    for (int i = zone.formation_bar + 1; i < std::min(zone.formation_bar + 20, (int)bars.size()); i++) {
        double distance = std::abs(bars[i].close - zone_mid);
        if (distance >= config.min_departure_distance_atr * atr) {
            departure_bar = i;
            break;
        }
    }
    
    if (departure_bar < 0) {
        zone.is_elite = false;
        return;
    }
    
    // Calculate departure imbalance
    double departure_distance = std::abs(bars[departure_bar].close - zone_mid);
    zone.departure_imbalance = departure_distance / atr;
    
    // Find first retest
    int retest_bar = -1;
    for (int i = zone.formation_bar + 5; i < std::min(zone.formation_bar + 100, (int)bars.size()); i++) {
        bool touches = (bars[i].low <= zone.proximal_line && bars[i].high >= zone.distal_line);
        if (touches) {
            retest_bar = i;
            break;
        }
    }
    
    if (retest_bar > 0) {
        zone.bars_to_retest = retest_bar - zone.formation_bar;
        zone.retest_speed = (zone.bars_to_retest > 0) ? 
            (departure_distance / zone.bars_to_retest) : 999.0;
        
        // Check elite criteria
        zone.is_elite = (
            zone.departure_imbalance >= config.min_departure_imbalance &&
            zone.retest_speed < config.max_retest_speed_atr * atr &&
            zone.bars_to_retest >= config.min_bars_to_retest
        );
    } else {
        zone.is_elite = false;
    }
}

void ZoneDetector::analyze_swing_position(Zone& zone, int lookback, int lookahead) {
    SwingAnalysis& swing = zone.swing_analysis;
    int formation_bar = zone.formation_bar;
    
    // Need enough bars for analysis
    if (formation_bar < lookback) {
        return;  // Keep defaults
    }
    
    // Find highest/lowest in lookback window
    double highest = -999999.0;
    double lowest = 999999.0;
    
    int start = std::max(0, formation_bar - lookback);
    int end = std::min((int)bars.size(), formation_bar + lookahead);
    
    for (int i = start; i < end; i++) {
        highest = std::max(highest, bars[i].high);
        lowest = std::min(lowest, bars[i].low);
    }
    
    double range = highest - lowest;
    if (range < 0.0001) return;  // Avoid division by zero
    
    double zone_mid = (zone.proximal_line + zone.distal_line) / 2.0;
    
    // Calculate percentile (0 = bottom, 100 = top)
    swing.swing_percentile = ((zone_mid - lowest) / range) * 100.0;
    
    if (zone.type == ZoneType::SUPPLY) {
        // Check if zone is at swing high
        double zone_top = std::max(zone.proximal_line, zone.distal_line);
        swing.is_at_swing_high = (zone_top >= highest - 0.1 * range);
        
        // Count bars to next higher high
        swing.bars_to_higher_high = -1;
        for (int i = formation_bar + 1; i < (int)bars.size(); i++) {
            if (bars[i].high > zone_top) {
                swing.bars_to_higher_high = i - formation_bar;
                break;
            }
        }
        
        // Calculate swing score for SUPPLY
        if (swing.is_at_swing_high) {
            swing.swing_score = 20.0;  // Major bonus for swing high
        } else if (swing.swing_percentile >= 90) {
            swing.swing_score = 15.0;  // Top 10% of range
        } else if (swing.swing_percentile >= 75) {
            swing.swing_score = 10.0;  // Top 25% of range
        } else if (swing.swing_percentile >= 60) {
            swing.swing_score = 5.0;   // Upper 40%
        } else {
            swing.swing_score = 0.0;   // Mid-range, no bonus
        }
        
        // Bonus if held for many bars (strong resistance)
        if (swing.bars_to_higher_high > 50) {
            swing.swing_score += 10.0;
        } else if (swing.bars_to_higher_high > 20) {
            swing.swing_score += 5.0;
        }
        
    } else {  // DEMAND
        // Check if zone is at swing low
        double zone_bottom = std::min(zone.proximal_line, zone.distal_line);
        swing.is_at_swing_low = (zone_bottom <= lowest + 0.1 * range);
        
        // Count bars to next lower low
        swing.bars_to_lower_low = -1;
        for (int i = formation_bar + 1; i < (int)bars.size(); i++) {
            if (bars[i].low < zone_bottom) {
                swing.bars_to_lower_low = i - formation_bar;
                break;
            }
        }
        
        // Calculate swing score for DEMAND
        if (swing.is_at_swing_low) {
            swing.swing_score = 20.0;  // Major bonus for swing low
        } else if (swing.swing_percentile <= 10) {
            swing.swing_score = 15.0;  // Bottom 10% of range
        } else if (swing.swing_percentile <= 25) {
            swing.swing_score = 10.0;  // Bottom 25% of range
        } else if (swing.swing_percentile <= 40) {
            swing.swing_score = 5.0;   // Lower 40%
        } else {
            swing.swing_score = 0.0;   // Mid-range, no bonus
        }
        
        // Bonus if held for many bars (strong support)
        if (swing.bars_to_lower_low > 50) {
            swing.swing_score += 10.0;
        } else if (swing.bars_to_lower_low > 20) {
            swing.swing_score += 5.0;
        }
    }
    
    // Cap swing score at 30 points maximum
    swing.swing_score = std::min(swing.swing_score, 30.0);
}

ZoneDetector::ZoneReference ZoneDetector::find_existing_zone_at_level(
    const Zone& candidate_zone,
    const std::vector<Zone>* existing_zones,
    const std::vector<Zone>& new_zones,
    double tolerance_price
) const {
    ZoneReference reference;

    auto matches = [&](const Zone& existing) {
        if (existing.type != candidate_zone.type) {
            return false;
        }

        double candidate_min = std::min(candidate_zone.proximal_line, candidate_zone.distal_line);
        double candidate_max = std::max(candidate_zone.proximal_line, candidate_zone.distal_line);
        double candidate_mid = (candidate_zone.proximal_line + candidate_zone.distal_line) / 2.0;

        double existing_min = std::min(existing.proximal_line, existing.distal_line);
        double existing_max = std::max(existing.proximal_line, existing.distal_line);
        double existing_mid = (existing.proximal_line + existing.distal_line) / 2.0;

        if (candidate_max >= existing_min && candidate_min <= existing_max) {
            return true;
        }

        double distance = std::min(
            std::abs(candidate_min - existing_max),
            std::abs(existing_min - candidate_max)
        );

        if (distance <= tolerance_price) {
            return true;
        }

        return (std::abs(candidate_mid - existing_mid) <= tolerance_price);
    };

    if (existing_zones != nullptr) {
        for (size_t i = 0; i < existing_zones->size(); ++i) {
            if (matches((*existing_zones)[i])) {
                reference.found = true;
                reference.in_existing = true;
                reference.index = i;
                return reference;
            }
        }
    }

    for (size_t i = 0; i < new_zones.size(); ++i) {
        if (matches(new_zones[i])) {
            reference.found = true;
            reference.in_existing = false;
            reference.index = i;
            return reference;
        }
    }

    return reference;
}

ZoneDetector::ZoneDecision ZoneDetector::evaluate_zone_candidate(
    const Zone& candidate_zone,
    const std::vector<Zone>* existing_zones,
    const std::vector<Zone>& new_zones,
    ZoneReference& reference
) const {
    double atr = get_cached_atr(candidate_zone.formation_bar);
    double tolerance_price = config.zone_existence_tolerance_atr * atr * config.atr_buffer_multiplier;

    reference = find_existing_zone_at_level(candidate_zone, existing_zones, new_zones, tolerance_price);
    if (!reference.found) {
        return ZoneDecision::CREATE_NEW;
    }

    const Zone& existing = reference.in_existing
        ? (*existing_zones)[reference.index]
        : new_zones[reference.index];

    if (candidate_zone.strength > existing.strength + config.strength_improvement_threshold) {
        return ZoneDecision::MERGE_WITH_EXISTING;
    }

    if (candidate_zone.formation_bar > existing.formation_bar + config.freshness_bars_threshold) {
        return ZoneDecision::MERGE_WITH_EXISTING;
    }

    if (candidate_zone.state != existing.state) {
        return ZoneDecision::MERGE_WITH_EXISTING;
    }

    return ZoneDecision::SKIP;
}

void ZoneDetector::merge_into_existing_zone(Zone& existing, const Zone& candidate) {
    switch (config.merge_strategy) {
        case ZoneMergeStrategy::UPDATE_IF_STRONGER:
            if (candidate.strength > existing.strength) {
                existing.strength = candidate.strength;
                existing.base_low = candidate.base_low;
                existing.base_high = candidate.base_high;
                existing.distal_line = candidate.distal_line;
                existing.proximal_line = candidate.proximal_line;
                existing.formation_bar = candidate.formation_bar;
                existing.formation_datetime = candidate.formation_datetime;
            }
            break;

        case ZoneMergeStrategy::UPDATE_IF_FRESHER:
            if (candidate.formation_bar > existing.formation_bar) {
                existing.formation_bar = candidate.formation_bar;
                existing.formation_datetime = candidate.formation_datetime;
                existing.state = candidate.state;
            }
            break;

        case ZoneMergeStrategy::EXPAND_BOUNDARIES: {
            double existing_min = std::min(existing.proximal_line, existing.distal_line);
            double existing_max = std::max(existing.proximal_line, existing.distal_line);
            double candidate_min = std::min(candidate.proximal_line, candidate.distal_line);
            double candidate_max = std::max(candidate.proximal_line, candidate.distal_line);

            double merged_min = std::min(existing_min, candidate_min);
            double merged_max = std::max(existing_max, candidate_max);

            if (existing.type == ZoneType::SUPPLY) {
                existing.proximal_line = merged_max;
                existing.distal_line = merged_min;
            } else {
                existing.distal_line = merged_min;
                existing.proximal_line = merged_max;
            }

            existing.base_low = std::min(existing.base_low, candidate.base_low);
            existing.base_high = std::max(existing.base_high, candidate.base_high);
            existing.strength = (existing.strength + candidate.strength) / 2.0;
            break;
        }

        case ZoneMergeStrategy::KEEP_ORIGINAL:
            break;
    }

    if (candidate.touch_count > 0) {
        existing.touch_count += candidate.touch_count;
    }

    existing.is_elite = existing.is_elite || candidate.is_elite;
}

std::vector<Zone> ZoneDetector::detect_zones(int start_bar_override) {
    std::vector<Zone> empty_existing;
    return detect_zones_no_duplicates(empty_existing, start_bar_override);
}

std::vector<Zone> ZoneDetector::detect_zones_no_duplicates(
    std::vector<Zone>& existing_zones,
    int start_bar_override
) {
    std::vector<Zone> zones;
    last_merged_zone_ids_.clear();
    int created_count = 0;
    int merged_count = 0;
    int skipped_count = 0;

    int search_start;
    if (start_bar_override >= 0) {
        search_start = start_bar_override;
    } else {
        int lookback = std::min((int)config.lookback_for_zones, (int)bars.size());
        search_start = std::max(0, (int)bars.size() - lookback);
    }

    int detection_interval = config.zone_detection_interval_bars > 0
        ? config.zone_detection_interval_bars
        : 1;

    LOG_DEBUG("Detecting zones from bar " + std::to_string(search_start));

    for (int i = search_start; i < (int)bars.size() - 5; i += detection_interval) {
        std::vector<Zone> candidates;

        for (int len = config.consolidation_min_bars; len <= config.consolidation_max_bars; len++) {
            double high, low;
            if (!is_consolidation(i, len, high, low)) continue;
            if (!has_impulse_before(i)) continue;
            if (!has_impulse_after(i + len - 1)) continue;

            double price_before = bars[i - 5].close;
            double price_at_base = bars[i].close;
            double price_after = bars[i + len - 1 + 5].close;

            bool rally_before = (price_at_base > price_before);
            bool drop_before = (price_at_base < price_before);
            bool rally_after = (price_after > price_at_base);
            bool drop_after = (price_after < price_at_base);

            ZoneType zone_type;
            if (rally_before && drop_after) {
                zone_type = ZoneType::SUPPLY;
            } else if (drop_before && rally_after) {
                zone_type = ZoneType::DEMAND;
            } else {
                continue;
            }

            if (zone_type == ZoneType::SUPPLY) {
                double atr = get_cached_atr(i);
                double rejection_move = std::abs(price_after - price_at_base);
                if (rejection_move < 1.5 * config.min_impulse_atr * atr) {
                    continue;
                }
            }

            Zone zone;
            zone.type = zone_type;
            zone.base_low = low;
            zone.base_high = high;
            zone.distal_line = (zone.type == ZoneType::DEMAND) ? low : high;
            zone.proximal_line = (zone.type == ZoneType::DEMAND) ? high : low;
            zone.formation_bar = i;
            zone.formation_datetime = bars[i].datetime;
            zone.state = ZoneState::FRESH;
            zone.touch_count = calculate_initial_touch_count(zone, bars, i);

            double zone_width = (zone.type == ZoneType::DEMAND) ?
                                (zone.proximal_line - zone.distal_line) :
                                (zone.distal_line - zone.proximal_line);
            double atr_for_width = get_cached_atr(i);

            double min_width_atr_based = config.min_zone_width_atr * atr_for_width;
            double min_width_absolute = 6.0;
            double min_width = std::max(min_width_atr_based, min_width_absolute);

            if (zone_width < min_width) {
                LOG_DEBUG("Zone rejected: width " + std::to_string(zone_width) +
                         " below max(ATR: " + std::to_string(min_width_atr_based) +
                         ", Absolute: " + std::to_string(min_width_absolute) + ")");
                continue;
            }

            zone.strength = calculate_zone_strength(zone);
            
            // V6.0: Calculate Volume/OI enhancements
            calculate_zone_enhancements(zone, i);
            
            if (zone.strength >= config.min_zone_strength) {
                analyze_elite_quality(zone);
                analyze_swing_position(zone);
                candidates.push_back(zone);
            }
        }

        if (candidates.empty()) {
            continue;
        }

        auto strongest_it = std::max_element(
            candidates.begin(),
            candidates.end(),
            [](const Zone& a, const Zone& b) { return a.strength < b.strength; }
        );

        Zone candidate = *strongest_it;

        if (config.duplicate_prevention_enabled && config.check_existing_before_create) {
            ZoneReference reference;
            ZoneDecision decision = evaluate_zone_candidate(
                candidate,
                &existing_zones,
                zones,
                reference
            );

            if (decision == ZoneDecision::CREATE_NEW) {
                zones.push_back(candidate);
                created_count++;
            } else if (decision == ZoneDecision::MERGE_WITH_EXISTING) {
                if (reference.in_existing) {
                    Zone& existing = existing_zones[reference.index];
                    merge_into_existing_zone(existing, candidate);
                    if (existing.zone_id > 0) {
                        last_merged_zone_ids_.push_back(existing.zone_id);
                    }
                } else {
                    merge_into_existing_zone(zones[reference.index], candidate);
                }
                merged_count++;
            } else {
                skipped_count++;
            }
        } else {
            zones.push_back(candidate);
            created_count++;
        }
    }
    if(std::to_string(zones.size()) == "1") {
        LOG_DEBUG("Detected " + std::to_string(zones.size()) + " zones");
        LOG_DEBUG("Zone detection summary: created=" + std::to_string(created_count) +
             ", merged=" + std::to_string(merged_count) +
             ", skipped=" + std::to_string(skipped_count));
    } else {       
    
        LOG_DEBUG("Detected " + std::to_string(zones.size()) + " zones");
        LOG_DEBUG("Zone detection summary: created=" + std::to_string(created_count) +
                ", merged=" + std::to_string(merged_count) +
                ", skipped=" + std::to_string(skipped_count));
    }


    last_detection_summary_.created = created_count;
    last_detection_summary_.merged = merged_count;
    last_detection_summary_.skipped = skipped_count;
    last_detection_summary_.detected = static_cast<int>(zones.size());
    return zones;
}

std::vector<Zone> ZoneDetector::detect_zones_full() {
    std::vector<Zone> zones;
    
    // ✅ V6.0: Check scorer status before detection
    LOG_INFO("🔍 ZoneDetector::detect_zones_full starting");
    LOG_INFO("   V6 Scorers status:");
    LOG_INFO("      volume_scorer: " + std::string(volume_scorer_ ? "AVAILABLE" : "NULL"));
    LOG_INFO("      oi_scorer: " + std::string(oi_scorer_ ? "AVAILABLE" : "NULL"));
    LOG_INFO("      volume_baseline: " + std::string(volume_baseline_ ? "SET" : "NULL"));
    if (volume_baseline_) {
        LOG_INFO("      baseline loaded: " + std::string(volume_baseline_->is_loaded() ? "YES" : "NO"));
    }
    
    // ⚡ PERFORMANCE OPTIMIZATION: Precompute all ATR values once
    // This speeds up detection by 10-20x by avoiding redundant calculations
    auto atr_cache_start = std::chrono::high_resolution_clock::now();
    build_atr_cache();
    auto atr_cache_end = std::chrono::high_resolution_clock::now();
    auto atr_cache_ms = std::chrono::duration_cast<std::chrono::milliseconds>(atr_cache_end - atr_cache_start).count();
    LOG_INFO("ATR cache built in " + std::to_string(atr_cache_ms) + "ms");
    
    // Calculate search range - SCAN ENTIRE DATASET (FIX for V3.x Ghost bug)
    int min_bar = config.atr_period + 10;  // Need ATR + some history
    
    // ✅ CRITICAL FIX: Account for consolidation_max_bars AND impulse check needs
    int max_bar = (int)bars.size() - config.consolidation_max_bars - 10;
    
    if (max_bar <= min_bar) {
        LOG_WARN("Not enough bars for zone detection (need at least " + 
                 std::to_string(min_bar + config.consolidation_max_bars + 10) + " bars)");
        return zones;
    }
    
    LOG_INFO("Full dataset scan: searching bars " + std::to_string(min_bar) + 
             " to " + std::to_string(max_bar) + " (scanning " + std::to_string(max_bar - min_bar) + " bars)");
    
    // 🔍 DEBUG COUNTERS - Find out what's failing!
    int total_checked = 0;
    int fail_consolidation = 0;
    int fail_impulse_before = 0;
    int fail_impulse_after = 0;
    int fail_direction = 0;
    int fail_supply_rejection = 0;
    int fail_strength = 0;
    int created_count = 0;
    int merged_count = 0;
    int skipped_count = 0;
    int pass_all_checks = 0;
    
    // Sample a few positions for detailed logging
    std::vector<int> sample_positions = {100, 500, 1000, 5000, 10000};
    
    // ⏱️ PROGRESS TRACKING
    auto detection_start = std::chrono::high_resolution_clock::now();
    int total_bars = max_bar - min_bar;
    int last_progress_pct = 0;
    
    int detection_interval = config.zone_detection_interval_bars > 0
        ? config.zone_detection_interval_bars
        : 1;

    // ✅ FIX: Scan ALL bars in dataset, not just trailing window
    for (int i = min_bar; i < max_bar; i += detection_interval) {
        // Progress reporting every 5% (more frequent updates)
        int current_progress_pct = ((i - min_bar) * 100) / total_bars;
        if (current_progress_pct >= last_progress_pct + 5 && current_progress_pct > last_progress_pct) {
            auto now = std::chrono::high_resolution_clock::now();
            auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - detection_start).count();
            int bars_processed = i - min_bar;
            double bars_per_sec = (elapsed_ms > 0) ? (bars_processed * 1000.0 / elapsed_ms) : 0.0;
            int remaining_bars = total_bars - bars_processed;
            int eta_sec = (bars_per_sec > 0) ? (int)(remaining_bars / bars_per_sec) : 0;
            
            LOG_INFO("Zone detection progress: " + std::to_string(current_progress_pct) + "% (" + 
                     std::to_string(i) + "/" + std::to_string(max_bar) + " bars) | " +
                     std::to_string(zones.size()) + " zones found | " +
                     std::to_string((int)bars_per_sec) + " bars/sec | " +
                     "ETA: " + std::to_string(eta_sec) + "s");
            last_progress_pct = current_progress_pct;
        }
        bool is_sample = (std::find(sample_positions.begin(), sample_positions.end(), i) != sample_positions.end());
        
        std::vector<Zone> candidates;

        // Try different consolidation lengths
        for (int len = config.consolidation_min_bars; len <= config.consolidation_max_bars; len++) {
            total_checked++;
            
            double high, low;
            if (!is_consolidation(i, len, high, low)) {
                fail_consolidation++;
                if (is_sample && len == config.consolidation_min_bars) {
                    double range = high - low;
                    double atr = get_cached_atr(i + len - 1);  // Use cached ATR
                    LOG_DEBUG("Bar " + std::to_string(i) + " FAIL consolidation: range=" + 
                             std::to_string(range) + " ATR=" + std::to_string(atr) + 
                             " max_allowed=" + std::to_string(config.max_consolidation_range * atr));
                }
                continue;
            }
            
            if (!has_impulse_before(i)) {
                fail_impulse_before++;
                if (is_sample && len == config.consolidation_min_bars) {
                    double price_start = bars[i - 5].close;
                    double price_end = bars[i].close;
                    double move = std::abs(price_end - price_start);
                    double atr = get_cached_atr(i);  // Use cached ATR
                    LOG_DEBUG("Bar " + std::to_string(i) + " FAIL impulse_before: move=" + 
                             std::to_string(move) + " ATR=" + std::to_string(atr) + 
                             " min_required=" + std::to_string(config.min_impulse_atr * atr));
                }
                continue;
            }
            
            if (!has_impulse_after(i + len - 1)) {
                fail_impulse_after++;
                if (is_sample && len == config.consolidation_min_bars) {
                    int check_idx = i + len - 1;
                    if (check_idx + 6 < (int)bars.size()) {
                        double price_start = bars[check_idx].close;
                        double price_end = bars[check_idx + 5].close;
                        double move = std::abs(price_end - price_start);
                        double atr = get_cached_atr(check_idx);  // Use cached ATR
                        LOG_DEBUG("Bar " + std::to_string(i) + " FAIL impulse_after: move=" + 
                                 std::to_string(move) + " ATR=" + std::to_string(atr) + 
                                 " min_required=" + std::to_string(config.min_impulse_atr * atr));
                    } else {
                        LOG_DEBUG("Bar " + std::to_string(i) + " FAIL impulse_after: OUT OF BOUNDS");
                    }
                }
                continue;
            }
            
            // Directional validation
            double price_before = bars[i - 5].close;
            double price_at_base = bars[i].close;
            double price_after = bars[i + len - 1 + 5].close;
            
            // Determine directions
            bool rally_before = (price_at_base > price_before);
            bool drop_before = (price_at_base < price_before);
            bool rally_after = (price_after > price_at_base);
            bool drop_after = (price_after < price_at_base);
            
            // Validate zone type with proper directional logic
            ZoneType zone_type;
            bool valid_pattern = false;
            
            if (rally_before && drop_after) {
                zone_type = ZoneType::SUPPLY;
                valid_pattern = true;
            } else if (drop_before && rally_after) {
                zone_type = ZoneType::DEMAND;
                valid_pattern = true;
            } else {
                fail_direction++;
                if (is_sample && len == config.consolidation_min_bars) {
                    LOG_DEBUG("Bar " + std::to_string(i) + " FAIL direction pattern");
                }
                continue;
            }
            
            // Additional validation for SUPPLY zones
            if (zone_type == ZoneType::SUPPLY) {
                double atr = get_cached_atr(i);  // Use cached ATR
                double rejection_move = std::abs(price_after - price_at_base);
                
                if (rejection_move < 1.5 * config.min_impulse_atr * atr) {
                    fail_supply_rejection++;
                    if (is_sample && len == config.consolidation_min_bars) {
                        LOG_DEBUG("Bar " + std::to_string(i) + " FAIL supply rejection: move=" + 
                                 std::to_string(rejection_move) + " min=" + 
                                 std::to_string(1.5 * config.min_impulse_atr * atr));
                    }
                    continue;
                }
            }
            
            // Create zone
            Zone zone;
            zone.type = zone_type;
            zone.base_low = low;
            zone.base_high = high;
            zone.distal_line = (zone.type == ZoneType::DEMAND) ? low : high;
            zone.proximal_line = (zone.type == ZoneType::DEMAND) ? high : low;
            zone.formation_bar = i;
            zone.formation_datetime = bars[i].datetime;
            zone.state = ZoneState::FRESH;
            zone.touch_count = calculate_initial_touch_count(zone, bars, i);
            
            // ⭐ HYBRID: Zone width filter - dual threshold (ATR-based OR absolute minimum)
            // Width calculation depends on zone type:
            // DEMAND: width = proximal_line - distal_line (high - low)
            // SUPPLY: width = distal_line - proximal_line (high - low)
            double zone_width = (zone.type == ZoneType::DEMAND) ? 
                                (zone.proximal_line - zone.distal_line) :
                                (zone.distal_line - zone.proximal_line);
            double atr_for_width = get_cached_atr(i);  // Get ATR for width check
            
            // Hybrid approach: Zone must exceed BOTH thresholds
            double min_width_atr_based = config.min_zone_width_atr * atr_for_width;
            double min_width_absolute = 6.0;  // Hard minimum: 6 points for NIFTY
            double min_width = std::max(min_width_atr_based, min_width_absolute);
            
            if (zone_width < min_width) {
                LOG_DEBUG("Zone rejected: width " + std::to_string(zone_width) + 
                         " below max(ATR: " + std::to_string(min_width_atr_based) + 
                         ", Absolute: " + std::to_string(min_width_absolute) + ")");
                continue;  // Skip this micro-zone and continue searching
            }
            
            // Calculate strength
            zone.strength = calculate_zone_strength(zone);
            
            // V6.0: Calculate Volume/OI enhancements
            calculate_zone_enhancements(zone, i);
            
            if (zone.strength >= config.min_zone_strength) {
                analyze_elite_quality(zone);
                analyze_swing_position(zone);
                candidates.push_back(zone);
            } else {
                fail_strength++;
                if (is_sample && len == config.consolidation_min_bars) {
                    LOG_DEBUG("Bar " + std::to_string(i) + " FAIL strength: " + 
                             std::to_string(zone.strength) + " < " + std::to_string(config.min_zone_strength));
                }
            }
        }

        if (candidates.empty()) {
            continue;
        }

        auto strongest_it = std::max_element(
            candidates.begin(),
            candidates.end(),
            [](const Zone& a, const Zone& b) { return a.strength < b.strength; }
        );

        Zone candidate = *strongest_it;

        if (config.duplicate_prevention_enabled && config.check_existing_before_create) {
            ZoneReference reference;
            std::vector<Zone> empty_new;
            ZoneDecision decision = evaluate_zone_candidate(
                candidate,
                &zones,
                empty_new,
                reference
            );

            if (decision == ZoneDecision::CREATE_NEW) {
                pass_all_checks++;
                zones.push_back(candidate);
                created_count++;
                LOG_DEBUG("✅ ZONE FOUND at bar " + std::to_string(i) +
                         " type=" + std::string(candidate.type == ZoneType::SUPPLY ? "SUPPLY" : "DEMAND") +
                         " strength=" + std::to_string(candidate.strength));
            } else if (decision == ZoneDecision::MERGE_WITH_EXISTING) {
                Zone& existing = zones[reference.index];
                merge_into_existing_zone(existing, candidate);
                merged_count++;
            } else {
                skipped_count++;
            }
        } else {
            pass_all_checks++;
            zones.push_back(candidate);
            created_count++;
            LOG_DEBUG("✅ ZONE FOUND at bar " + std::to_string(i) +
                     " type=" + std::string(candidate.type == ZoneType::SUPPLY ? "SUPPLY" : "DEMAND") +
                     " strength=" + std::to_string(candidate.strength));
        }
        
        // OLD progress indicator - now handled by percentage-based logging above
        // if (i % 1000 == 0 && i > 0) {
        //     LOG_DEBUG("Scanned " + std::to_string(i - min_bar) + "/" + 
        //              std::to_string(max_bar - min_bar) + " bars, found " + 
        //              std::to_string(zones.size()) + " zones so far");
        // }
    }
    
    // ⏱️ DETECTION COMPLETE - Show timing summary
    auto detection_end = std::chrono::high_resolution_clock::now();
    auto detection_ms = std::chrono::duration_cast<std::chrono::milliseconds>(detection_end - detection_start).count();
    double detection_sec = detection_ms / 1000.0;
    double bars_per_sec = (detection_ms > 0) ? (total_bars * 1000.0 / detection_ms) : 0.0;
    
    LOG_INFO("✅ Zone detection COMPLETE in " + std::to_string(detection_sec) + "s (" + 
             std::to_string(detection_ms) + "ms) | " + 
             std::to_string((int)bars_per_sec) + " bars/sec | " +
             std::to_string(zones.size()) + " zones detected");
    
    // 🔍 DIAGNOSTIC SUMMARY - Show why zones failed
    LOG_INFO("===== ZONE DETECTION DIAGNOSTIC SUMMARY =====");
    LOG_INFO("Total positions checked: " + std::to_string(total_checked));
    LOG_INFO("Results: created=" + std::to_string(created_count) +
             ", merged=" + std::to_string(merged_count) +
             ", skipped=" + std::to_string(skipped_count));
    LOG_INFO("Failures breakdown:");
    LOG_INFO("  ❌ Consolidation too loose: " + std::to_string(fail_consolidation) + 
             " (" + std::to_string(100.0 * fail_consolidation / total_checked) + "%)");
    LOG_INFO("  ❌ No impulse before: " + std::to_string(fail_impulse_before) + 
             " (" + std::to_string(100.0 * fail_impulse_before / total_checked) + "%)");
    LOG_INFO("  ❌ No impulse after: " + std::to_string(fail_impulse_after) + 
             " (" + std::to_string(100.0 * fail_impulse_after / total_checked) + "%)");
    LOG_INFO("  ❌ Wrong direction pattern: " + std::to_string(fail_direction) + 
             " (" + std::to_string(100.0 * fail_direction / total_checked) + "%)");
    LOG_INFO("  ❌ Supply rejection weak: " + std::to_string(fail_supply_rejection) + 
             " (" + std::to_string(100.0 * fail_supply_rejection / total_checked) + "%)");
    LOG_INFO("  ❌ Strength too low: " + std::to_string(fail_strength) + 
             " (" + std::to_string(100.0 * fail_strength / total_checked) + "%)");
    LOG_INFO("  ✅ Passed all checks: " + std::to_string(pass_all_checks) + 
             " (" + std::to_string(100.0 * pass_all_checks / total_checked) + "%)");
    LOG_INFO("============================================");
    
    LOG_INFO("Full scan complete: Detected " + std::to_string(zones.size()) + " zones");

    last_detection_summary_.created = created_count;
    last_detection_summary_.merged = merged_count;
    last_detection_summary_.skipped = skipped_count;
    last_detection_summary_.detected = static_cast<int>(zones.size());
    return zones;
}

// Calculate initial touch count for a newly detected zone
int ZoneDetector::calculate_initial_touch_count(const Zone& zone, 
                                                const std::vector<Bar>& bars, 
                                                int formation_index) const {
    int touch_count = 0;
    
    // Count touches from formation onwards
    for (int i = formation_index + 1; i < (int)bars.size(); i++) {
        const Bar& bar = bars[i];
        
        // Check if bar touches the zone
        bool touches = false;
        if (zone.type == ZoneType::SUPPLY) {
            // SUPPLY: bar high touches proximal line
            touches = (bar.high >= zone.proximal_line);
        } else {
            // DEMAND: bar low touches proximal line
            touches = (bar.low <= zone.proximal_line);
        }
        
        if (touches) {
            touch_count++;
        }
    }
    
    return touch_count;
}

// ========================================
// V6.0: Volume/OI Integration Methods
// ========================================

void ZoneDetector::set_volume_baseline(Utils::VolumeBaseline* baseline) {
    LOG_INFO("🔧 ZoneDetector::set_volume_baseline called");
    LOG_INFO("   baseline pointer: " + std::string(baseline ? "VALID" : "NULL"));
    LOG_INFO("   baseline loaded: " + std::string(baseline && baseline->is_loaded() ? "YES" : "NO"));
    
    volume_baseline_ = baseline;
    
    if (volume_baseline_ != nullptr && volume_baseline_->is_loaded()) {
        // Clean up old scorers if they exist
        if (volume_scorer_ != nullptr) {
            delete volume_scorer_;
            volume_scorer_ = nullptr;
        }
        if (oi_scorer_ != nullptr) {
            delete oi_scorer_;
            oi_scorer_ = nullptr;
        }
        
        // Create scorers
        volume_scorer_ = new Core::VolumeScorer(*volume_baseline_);
        oi_scorer_ = new Core::OIScorer();
        
        LOG_INFO("✅ V6.0 Volume/OI scorers initialized in ZoneDetector");
        LOG_INFO("   volume_scorer: " + std::string(volume_scorer_ ? "CREATED" : "FAILED"));
        LOG_INFO("   oi_scorer: " + std::string(oi_scorer_ ? "CREATED" : "FAILED"));
    } else {
        LOG_WARN("⚠️  Volume baseline not loaded - V6.0 scorers NOT initialized");
        LOG_WARN("   Zones will have 0 V6 scores and use traditional scoring only");
    }
}

void ZoneDetector::calculate_zone_enhancements(Zone& zone, int formation_bar) {
    // DEBUG: Log entry
    LOG_DEBUG("🔍 [V6 DEBUG] calculate_zone_enhancements called for Zone " + std::to_string(zone.zone_id));
    LOG_DEBUG("    volume_scorer_: " + std::string(volume_scorer_ ? "VALID" : "NULL"));
    LOG_DEBUG("    oi_scorer_: " + std::string(oi_scorer_ ? "VALID" : "NULL"));
    
    // Only calculate if scorers are available
    if (volume_scorer_ == nullptr || oi_scorer_ == nullptr) {
        LOG_DEBUG("❌ [V6 DEBUG] V6.0 scorers not available for Zone " + std::to_string(zone.zone_id));
        return;
    }
    
    LOG_DEBUG("✅ [V6 DEBUG] Scorers available - calculating V6 fields");
    
    // Calculate Volume Profile
    zone.volume_profile = volume_scorer_->calculate_volume_profile(
        zone, 
        bars, 
        formation_bar
    );
    
    LOG_DEBUG("📊 [V6 DEBUG] Volume Profile calculated: " + zone.volume_profile.to_string());
    
    // Calculate OI Profile
    zone.oi_profile = oi_scorer_->calculate_oi_profile(
        zone,
        bars,
        formation_bar
    );
    
    LOG_DEBUG("📈 [V6 DEBUG] OI Profile calculated: " + zone.oi_profile.to_string());
    
    // Calculate Institutional Index
    zone.institutional_index = Utils::calculate_institutional_index(
        zone.volume_profile,
        zone.oi_profile
    );
    
    LOG_DEBUG("🏦 [V6 DEBUG] Institutional Index calculated: " + std::to_string(zone.institutional_index));
}

} // namespace Core
} // namespace SDTrading
