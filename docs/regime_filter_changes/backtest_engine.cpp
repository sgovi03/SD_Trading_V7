// ============================================================
// BACKTEST ENGINE - MODIFIED IMPLEMENTATION
// ============================================================

#include "backtest_engine.h"
#include "csv_reporter.h"
#include "../utils/logger.h"
#include "../ZoneInitializer.h"
#include "../analysis/market_analyzer.h"
#include "../sd_engine_core.h"  // For calculate_trailing_stop
#include "../utils/institutional_index.h"  // ⭐ U1: For calculate_institutional_index on zone touch
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <ctime>
#include <filesystem>
#include <optional>
#include <numeric>  // For std::accumulate
#include <algorithm>
#include <map>  // ⭐ D7: for zone_sl_session_
// In BacktestEngine files
#include "../ITradingEngine.h"
#include "../ZonePersistenceAdapter.h"

namespace fs = std::filesystem;

namespace SDTrading {
namespace Backtest {

namespace {

int calculate_days_difference(const std::string& from_dt, const std::string& to_dt) {
    auto parse_date = [](const std::string& dt, std::tm& tm_out) -> bool {
        std::istringstream ss(dt);
        ss >> std::get_time(&tm_out, "%Y-%m-%d %H:%M:%S");
        if (!ss.fail()) {
            return true;
        }
        ss.clear();
        ss.str(dt);
        ss >> std::get_time(&tm_out, "%Y-%m-%d");
        return !ss.fail();
    };

    std::tm from_tm{};
    std::tm to_tm{};
    if (!parse_date(from_dt, from_tm) || !parse_date(to_dt, to_tm)) {
        return 0;
    }

    from_tm.tm_isdst = -1;
    to_tm.tm_isdst = -1;

    std::time_t from_time = std::mktime(&from_tm);
    std::time_t to_time = std::mktime(&to_tm);
    if (from_time == static_cast<std::time_t>(-1) || to_time == static_cast<std::time_t>(-1)) {
        return 0;
    }

    double diff_sec = std::difftime(to_time, from_time);
    int days = static_cast<int>(diff_sec / (60.0 * 60.0 * 24.0));
    return std::max(0, days);
}

bool is_zone_age_blocked(const Core::Zone& zone, const Core::Bar& current_bar, const Core::Config& config, int& age_days_out) {
    age_days_out = calculate_days_difference(zone.formation_datetime, current_bar.datetime);

    if (config.min_zone_age_days > 0 && age_days_out < config.min_zone_age_days) {
        return true;
    }
    if (config.max_zone_age_days > 0 && age_days_out > config.max_zone_age_days) {
        return true;
    }
    if (config.exclude_zone_age_range &&
        age_days_out >= config.exclude_zone_age_start &&
        age_days_out <= config.exclude_zone_age_end) {
        return true;
    }

    return false;
}

}  // namespace

BacktestEngine::BacktestEngine(
    const Core::Config& cfg,
    const std::string& data_file,
    const std::string& output_directory
)
    : config(cfg),
      detector(config),  // FIX: Use member config, not parameter cfg
      scorer(config),     // FIX: Use member config, not parameter cfg
      entry_engine(config), // FIX: Use member config, not parameter cfg
      trade_manager(config, config.starting_capital), // FIX: Use member config
      performance(config.starting_capital),  // FIX: Use member config
      volume_baseline_(),  // V6.0: Initialize volume baseline
      volume_scorer_(nullptr),  // V6.0: Will be created after baseline loads
      oi_scorer_(nullptr),      // V6.0: Will be created after baseline loads
      zone_persistence_("backtest", output_directory, false),  // ⭐ FIXED: Pass false (no filtering in backtest)
      next_zone_id_(1),  // NEW: Zone ID tracking
      data_file_path_(data_file),
      output_dir_(output_directory),
      pause_counter(0),
      consecutive_losses(0),
      zone_sl_session_(),    // ⭐ D7: empty on construction
      last_guard_date_(""),  // ⭐ D7: empty = first bar will initialise silently
      guard_last_exit_date_("") { // ⭐ P1: empty = no prior trade exit seen
    
    // Pre-allocate vectors for performance
    bars.reserve(50000);  // Reserve for ~50K bars (1-min: ~1 month)
    active_zones.reserve(1000);  // Reserve for up to 1000 zones
    
    // Create output directory
    fs::create_directories(output_dir_);
    
    LOG_INFO("BacktestEngine created");
    LOG_INFO("  Data file: " << data_file_path_);
    LOG_INFO("  Output:    " << output_dir_);
    
    // ========================================
    // V6.0: Initialize Volume/OI Components
    // ========================================
    if (config.v6_fully_enabled) {
        LOG_INFO("🚀 V6.0 Volume & OI Integration ENABLED");
        
        // Load volume baseline
        std::string baseline_path = config.volume_baseline_file;
        if (volume_baseline_.load_from_file(baseline_path)) {
            LOG_INFO("✅ Volume Baseline loaded: " << volume_baseline_.size() << " time slots");
            
            // Create scorers
            volume_scorer_ = new Core::VolumeScorer(volume_baseline_);
            oi_scorer_ = new Core::OIScorer();
            LOG_INFO("✅ V6.0 Scorers initialized");
            
            // Inject into subsystems
            detector.set_volume_baseline(&volume_baseline_);
            entry_engine.set_volume_baseline(&volume_baseline_);
            entry_engine.set_oi_scorer(oi_scorer_);
            trade_manager.set_volume_baseline(&volume_baseline_);
            trade_manager.set_oi_scorer(oi_scorer_);
            
            LOG_INFO("✅ V6.0 Components wired into engine");
            
            if (config.v6_validate_baseline_on_startup) {
                LOG_INFO("📊 Volume Baseline Statistics:");
                LOG_INFO("   - Time slots: " << volume_baseline_.size());
                LOG_INFO("   - Market start: 09:15");
                LOG_INFO("   - Market end:   15:30");
            }
        } else {
            LOG_WARN("⚠️  Volume baseline not loaded - V6.0 running in degraded mode");
            LOG_WARN("   Expected file: " << baseline_path);
        }
    } else {
        LOG_INFO("ℹ️  V6.0 Integration DISABLED (v6_fully_enabled=false)");
    }
}

BacktestEngine::~BacktestEngine() {
    // Clean up V6.0 resources
    if (volume_scorer_ != nullptr) {
        delete volume_scorer_;
        volume_scorer_ = nullptr;
    }
    if (oi_scorer_ != nullptr) {
        delete oi_scorer_;
        oi_scorer_ = nullptr;
    }

    LOG_INFO("BacktestEngine destroyed");
}

bool BacktestEngine::load_csv_data() {
    LOG_INFO("Loading CSV data: " << data_file_path_);
    
    std::ifstream file(data_file_path_);
    if (!file.is_open()) {
        LOG_ERROR("Cannot open data file: " << data_file_path_);
        return false;
    }
    
    bars.clear();
    std::string line;
    std::getline(file, line); // Skip header
    
    int line_number = 1;
    while (std::getline(file, line)) {
        line_number++;
        
        if (line.empty()) continue;
        
        try {
            std::stringstream ss(line);
            std::string timestamp, datetime, symbol;
            std::string open_str, high_str, low_str, close_str, volume_str, oi_str;
            std::string oi_fresh_str, oi_age_seconds_str;  // NEW V6.0
            
            // Parse CSV columns (Fyers format: 11 columns in V6.0, backward compatible with 9)
            std::getline(ss, timestamp, ',');      // Column 0: Unix timestamp
            std::getline(ss, datetime, ',');       // Column 1: DateTime
            std::getline(ss, symbol, ',');         // Column 2: Symbol
            std::getline(ss, open_str, ',');       // Column 3: Open
            std::getline(ss, high_str, ',');       // Column 4: High
            std::getline(ss, low_str, ',');        // Column 5: Low
            std::getline(ss, close_str, ',');      // Column 6: Close
            std::getline(ss, volume_str, ',');     // Column 7: Volume
            std::getline(ss, oi_str, ',');         // Column 8: OpenInterest
            std::getline(ss, oi_fresh_str, ',');   // Column 9: OI_Fresh (NEW V6.0)
            std::getline(ss, oi_age_seconds_str, ','); // Column 10: OI_Age_Seconds (NEW V6.0)
            
            // Create Bar
            Core::Bar bar;
            bar.datetime = datetime;
            bar.open = std::stod(open_str);
            bar.high = std::stod(high_str);
            bar.low = std::stod(low_str);
            bar.close = std::stod(close_str);
            bar.volume = volume_str.empty() ? 0.0 : std::stod(volume_str);
            bar.oi = oi_str.empty() ? 0.0 : std::stod(oi_str);
            
            // NEW V6.0: Parse OI metadata (backward compatible)
            if (!oi_fresh_str.empty()) {
                bar.oi_fresh = (oi_fresh_str == "1" || oi_fresh_str == "true" || oi_fresh_str == "True");
            } else {
                bar.oi_fresh = false;
            }
            
            if (!oi_age_seconds_str.empty()) {
                bar.oi_age_seconds = std::stoi(oi_age_seconds_str);
            } else {
                bar.oi_age_seconds = 0;
            }
            
            bars.push_back(bar);
            
        } catch (const std::exception& e) {
            LOG_WARN("Skipping line " << line_number << ": " << e.what());
            continue;
        }
    }
    
    file.close();
    
    if (bars.empty()) {
        LOG_ERROR("No data loaded from CSV");
        return false;
    }
    
    LOG_INFO("Loaded " << bars.size() << " bars");
    LOG_INFO("  First: " << bars.front().datetime);
    LOG_INFO("  Last:  " << bars.back().datetime);
    
    // Add bars to detector
    for (const auto& bar : bars) {
        detector.add_bar(bar);
    }
    
    return true;
}

bool BacktestEngine::initialize() {
    LOG_INFO("Initializing BacktestEngine...");
    
    // Load CSV data
    if (!load_csv_data()) {
        LOG_ERROR("Failed to load CSV data");
        return false;
    }
    
    // Backtest always starts with fresh zones (no loading)
    active_zones.clear();
    next_zone_id_ = 1;
    LOG_INFO("Starting with fresh zones (backtest mode)");
    
    LOG_INFO("BacktestEngine initialized successfully");
    return true;
}

void BacktestEngine::update_zone_states(const Core::Bar& bar, int bar_index) {
    bool zones_changed = false;

    auto record_event = [&](Core::Zone& target_zone, const Core::ZoneStateEvent& evt, bool enabled) {
        if (!config.enable_state_history || !enabled) {
            return;
        }
        target_zone.state_history.push_back(evt);
        if (config.max_state_history_events > 0 &&
            target_zone.state_history.size() > static_cast<size_t>(config.max_state_history_events)) {
            auto erase_count = target_zone.state_history.size() - static_cast<size_t>(config.max_state_history_events);
            target_zone.state_history.erase(
                target_zone.state_history.begin(),
                target_zone.state_history.begin() + static_cast<long long>(erase_count));
        }
    };
    
    // Update states of active zones based on price action
    for (auto& zone : active_zones) {
        // ⭐ RC1 FIX: Skip bars that occurred AT OR BEFORE this zone's formation bar.
        //
        // ROOT CAUSE of touch_count divergence between BT and LT:
        // ZoneInitializer adds ALL zones to active_zones before bar 0.
        // Without this guard, update_zone_states processes every zone on every bar,
        // including bars 0..formation_bar that predate or coincide with zone creation.
        //
        // The formation bar (bar_index == formation_bar) is special: by definition the
        // zone's price range overlaps this bar (that's how the zone was detected).
        // Processing it fires FIRST_TOUCH immediately → was_in_zone_prev=true.
        // If NIFTY stays near the zone level on the next several bars, they are all
        // suppressed as consecutive (was_in_zone_prev=true), and the genuine retest
        // episodes that LT replay counts from formation_bar+1 are never seen by BT.
        // Result: BT touch_count=1 (formation bar only), LT touch_count=57+ (real episodes).
        //
        // FIX: Skip bar_index <= zone.formation_bar (formation bar AND all prior bars).
        // BT now starts counting at formation_bar+1, exactly matching LT replay's
        // start_bar = formation_bar + 1. Both engines count from the same baseline.
        if (bar_index <= zone.formation_bar) {
            continue;
        }
        Core::ZoneState old_state = zone.state;
        std::string old_state_str = (old_state == Core::ZoneState::FRESH ? "FRESH" :
                                      old_state == Core::ZoneState::TESTED ? "TESTED" : "VIOLATED");
        
        // price_in_zone: bar overlaps the zone body in any way.
        // DEMAND: proximal=base_high (top), distal=base_low (bottom)
        //   overlap = bar.high >= distal AND bar.low <= proximal  → current formula works
        // SUPPLY: proximal=base_low (bottom), distal=base_high (top)
        //   overlap = bar.low <= distal AND bar.high >= proximal
        //   The old formula (bar.low<=proximal && bar.high>=distal) required the bar
        //   to span the ENTIRE supply zone — normal touches were invisible.
        bool price_in_zone;
        if (zone.type == Core::ZoneType::DEMAND) {
            price_in_zone = (bar.high >= zone.distal_line && bar.low <= zone.proximal_line);
        } else {
            // Fix #4: SUPPLY touch = bar overlaps zone from above (bar.high touches proximal or higher)
            // Old code required bar to SPAN entire zone; now any overlap with proximal upward is a touch
            price_in_zone = (bar.low <= zone.distal_line && bar.high >= zone.proximal_line);
        }

        if (price_in_zone) {
            if (zone.state == Core::ZoneState::FRESH) {
                zone.state = Core::ZoneState::TESTED;
                zone.touch_count++;
                // V6.0: Record normalised touch volume
                if (volume_baseline_.is_loaded()) {
                    std::string slot = volume_baseline_.extract_time_slot(bar.datetime);
                    double ratio = volume_baseline_.get_normalized_ratio(slot, bar.volume);
                    zone.volume_profile.touch_volumes.push_back(ratio);

                    if (zone.volume_profile.touch_volumes.size() >= 3) {
                        const auto& tv = zone.volume_profile.touch_volumes;
                        int n = static_cast<int>(tv.size());
                        int rc = 0;
                        for (int i = n - 2; i >= std::max(0, n - 3); --i)
                            if (tv[i + 1] > tv[i]) rc++;
                        zone.volume_profile.volume_rising_on_retests = (rc >= 2);
                        if (zone.volume_profile.volume_rising_on_retests) {
                            LOG_WARN("Zone " + std::to_string(zone.zone_id) +
                                     " rising volume on retests — Component 5 penalty active");
                        }
                    }
                }
                // ⭐ U1 FIX: Refresh V6 volume/OI profiles on first touch — matches live engine.
                // Live recalculates volume_profile, oi_profile and institutional_index
                // on every zone touch so entry scoring uses fresh institutional data.
                // Backtest was missing this, causing different zone scores vs live.
                if (volume_scorer_ && oi_scorer_ && zone.volume_profile.avg_volume_baseline > 0.0) {
                    int profile_bar_idx = zone.formation_bar;
                    if (profile_bar_idx >= 0 && profile_bar_idx < static_cast<int>(bars.size())) {
                        zone.volume_profile = volume_scorer_->calculate_volume_profile(
                            zone, bars, profile_bar_idx);
                        zone.oi_profile = oi_scorer_->calculate_oi_profile(
                            zone, bars, profile_bar_idx);
                        zone.institutional_index = Utils::calculate_institutional_index(
                            zone.volume_profile, zone.oi_profile);
                        LOG_DEBUG("Zone " << zone.zone_id << " V6 profiles refreshed on FIRST_TOUCH"
                                 << " | inst_idx=" << std::fixed << std::setprecision(2)
                                 << zone.institutional_index);
                    }
                }
                zones_changed = true;
                
                // Record FIRST_TOUCH event
                Core::ZoneStateEvent event(
                    bar.datetime,
                    static_cast<int>(bars.size() - 1),
                    "FIRST_TOUCH",
                    old_state_str,
                    "TESTED",
                    (bar.high + bar.low) / 2.0,  // Mid-price
                    bar.high,
                    bar.low,
                    zone.touch_count
                );
                record_event(zone, event, config.record_first_touch);
                
            } else if (zone.state == Core::ZoneState::TESTED) {
                // ⭐ D2 FIX: Only count a NEW visit episode (outside→inside transition).
                // Live engine has this guard. Without it every consecutive bar inside a zone
                // increments touch_count, causing counts in the hundreds and premature
                // max_touch_count retirement — far fewer tradeable zones than live.
                if (!zone.was_in_zone_prev) {
                    zone.touch_count++;
                    // V6.0: Record normalised touch volume
                    if (volume_baseline_.is_loaded()) {
                        std::string slot = volume_baseline_.extract_time_slot(bar.datetime);
                        double ratio = volume_baseline_.get_normalized_ratio(slot, bar.volume);
                        zone.volume_profile.touch_volumes.push_back(ratio);

                        if (zone.volume_profile.touch_volumes.size() >= 3) {
                            const auto& tv = zone.volume_profile.touch_volumes;
                            int n = static_cast<int>(tv.size());
                            int rc = 0;
                            for (int i = n - 2; i >= std::max(0, n - 3); --i)
                                if (tv[i + 1] > tv[i]) rc++;
                            zone.volume_profile.volume_rising_on_retests = (rc >= 2);
                            if (zone.volume_profile.volume_rising_on_retests) {
                                LOG_WARN("Zone " + std::to_string(zone.zone_id) +
                                         " rising volume on retests — Component 5 penalty active");
                            }
                        }
                    }
                    // ⭐ U1 FIX: Refresh V6 volume/OI profiles on each new RETEST — matches live engine.
                    // Live recalculates profiles on every new touch episode so entry scoring
                    // always uses the most current institutional data. Without this, backtest
                    // uses stale profiles from zone formation, causing different zone scores.
                    if (volume_scorer_ && oi_scorer_ && zone.volume_profile.avg_volume_baseline > 0.0) {
                        int profile_bar_idx = zone.formation_bar;
                        if (profile_bar_idx >= 0 && profile_bar_idx < static_cast<int>(bars.size())) {
                            zone.volume_profile = volume_scorer_->calculate_volume_profile(
                                zone, bars, profile_bar_idx);
                            zone.oi_profile = oi_scorer_->calculate_oi_profile(
                                zone, bars, profile_bar_idx);
                            zone.institutional_index = Utils::calculate_institutional_index(
                                zone.volume_profile, zone.oi_profile);
                            LOG_DEBUG("Zone " << zone.zone_id << " V6 profiles refreshed on RETEST"
                                     << " | inst_idx=" << std::fixed << std::setprecision(2)
                                     << zone.institutional_index);
                        }
                    }
                    zones_changed = true;

                    // Record RETEST event
                    Core::ZoneStateEvent event(
                        bar.datetime,
                        static_cast<int>(bars.size() - 1),
                        "RETEST",
                        old_state_str,
                        "TESTED",
                        (bar.high + bar.low) / 2.0,
                        bar.high,
                        bar.low,
                        zone.touch_count
                    );
                    record_event(zone, event, config.record_retests);
                }
            }
        }
        
        // ⭐ D2 FIX: Update episode-tracking flag — MUST be outside if(price_in_zone).
        // When price is NOT in zone, this sets was_in_zone_prev=false, allowing the
        // next in-zone bar to be counted as a new episode (RETEST).
        // Previously this line was inside if(price_in_zone): when price left the zone
        // was_in_zone_prev was never reset to false, permanently suppressing all future
        // RETEST events. BT got touch_count=1 (only FIRST_TOUCH) for every zone.
        zone.was_in_zone_prev = price_in_zone;
        
        // Check for violation
        bool violated = false;
        std::string violation_event = "VIOLATED";
        double violation_price = bar.close;

        // Gap-over invalidation: entire bar clears distal without touching the zone
        if (config.gap_over_invalidation && zone.state != Core::ZoneState::VIOLATED) {
            if (zone.type == Core::ZoneType::DEMAND) {
                violated = (bar.high < zone.distal_line);
            } else {
                violated = (bar.low > zone.distal_line);
            }
            if (violated) {
                violation_event = "GAP_VIOLATED";
                violation_price = (zone.type == Core::ZoneType::DEMAND) ? bar.high : bar.low;
                zone.was_swept = true;
                zone.sweep_bar = static_cast<int>(bars.size() - 1);
            }
        }

        // Body-close invalidation
        if (!violated && config.invalidate_on_body_close) {
            if (zone.type == Core::ZoneType::DEMAND) {
                violated = (bar.close < zone.distal_line);
            } else {
                violated = (bar.close > zone.distal_line);
            }
            if (violated) {
                violation_event = "VIOLATED";
                violation_price = bar.close;
            }
        }

        // SWEEP-RECLAIM DETECTION: Check if swept zone is reclaiming
        if (config.enable_sweep_reclaim && zone.was_swept && zone.state == Core::ZoneState::VIOLATED) {
            bool price_inside_zone = (bar.low <= zone.proximal_line && bar.high >= zone.distal_line);
            
            if (price_inside_zone) {
                // Check if bar closes inside zone (acceptance)
                double zone_range = std::abs(zone.proximal_line - zone.distal_line);
                double body_in_zone = 0.0;
                
                if (zone.type == Core::ZoneType::DEMAND) {
                    // For demand, check how much body is above distal
                    if (bar.close >= zone.distal_line) {
                        body_in_zone = std::min(bar.close, zone.proximal_line) - zone.distal_line;
                    }
                } else {
                    // For supply, check how much body is below distal
                    if (bar.close <= zone.distal_line) {
                        body_in_zone = zone.distal_line - std::max(bar.close, zone.proximal_line);
                    }
                }
                
                double body_pct = (zone_range > 0) ? (body_in_zone / zone_range) : 0.0;
                
                if (body_pct >= config.reclaim_acceptance_pct) {
                    zone.bars_inside_after_sweep++;
                    
                    // Check if acceptance period met
                    if (zone.bars_inside_after_sweep >= config.reclaim_acceptance_bars) {
                        zone.state = Core::ZoneState::RECLAIMED;
                        zone.reclaim_eligible = true;
                        zones_changed = true;
                        
                        // Record reclaim event
                        Core::ZoneStateEvent event(
                            bar.datetime,
                            static_cast<int>(bars.size() - 1),
                            "SWEEP_RECLAIMED",
                            "VIOLATED",
                            "RECLAIMED",
                            (bar.high + bar.low) / 2.0,
                            bar.high,
                            bar.low,
                            zone.touch_count
                        );
                        record_event(zone, event, config.record_violations);
                        LOG_DEBUG("Zone " << zone.zone_id << " RECLAIMED after sweep");
                    }
                } else {
                    // Reset counter if body not sufficiently inside
                    zone.bars_inside_after_sweep = 0;
                }
            } else {
                // Reset counter if price exits zone
                zone.bars_inside_after_sweep = 0;
            }
        }

        if (violated && zone.state != Core::ZoneState::VIOLATED) {
            std::string pre_violation_state = (zone.state == Core::ZoneState::FRESH ? "FRESH" : "TESTED");
            zone.state = Core::ZoneState::VIOLATED;
            zones_changed = true;
            
            // Record violation event
            Core::ZoneStateEvent event(
                bar.datetime,
                static_cast<int>(bars.size() - 1),
                violation_event,
                pre_violation_state,
                "VIOLATED",
                violation_price,
                bar.high,
                bar.low,
                zone.touch_count
            );
            record_event(zone, event, config.record_violations);
            
            // ZONE FLIP DETECTION: Check for opposite zone at breakdown point
            if (config.enable_zone_flip) {
                auto flipped_zone = detect_zone_flip(zone, static_cast<int>(bars.size() - 1));
                if (flipped_zone.has_value()) {
                    flipped_zone->zone_id = next_zone_id_++;
                    
                    // Score the flipped zone
                    // ⭐ LOOKAHEAD FIX: pass bar_index so regime is computed at the
                    // current bar, not at bars.back() (which is a future bar in BT).
                    Core::MarketRegime regime = Core::MarketAnalyzer::detect_regime(
                        bars, 50, 5.0, bar_index);
                    flipped_zone->zone_score = scorer.evaluate_zone(flipped_zone.value(), regime, bar);
                    
                    LOG_DEBUG("Flipped zone scored: ID=" << flipped_zone->zone_id 
                             << " Total=" << flipped_zone->zone_score.total_score
                             << " (" << flipped_zone->zone_score.entry_rationale << ")");
                    
                    active_zones.push_back(flipped_zone.value());
                    zones_changed = true;
                    LOG_DEBUG("Zone " << zone.zone_id << " flipped -> new " << 
                            (flipped_zone->type == Core::ZoneType::DEMAND ? "DEMAND" : "SUPPLY") <<
                            " zone " << flipped_zone->zone_id);
                }
            }
        }
    }
    
    // NOTE: Zone persistence disabled during backtest loop for performance
    // Zones will be saved once at the end in export_results()
}

void BacktestEngine::detect_and_add_zones(const Core::Bar& bar, int bar_index) {
    auto record_event = [&](Core::Zone& target_zone, const Core::ZoneStateEvent& evt, bool enabled) {
        if (!config.enable_state_history || !enabled) {
            return;
        }
        target_zone.state_history.push_back(evt);
        if (config.max_state_history_events > 0 &&
            target_zone.state_history.size() > static_cast<size_t>(config.max_state_history_events)) {
            auto erase_count = target_zone.state_history.size() - static_cast<size_t>(config.max_state_history_events);
            target_zone.state_history.erase(
                target_zone.state_history.begin(),
                target_zone.state_history.begin() + static_cast<long long>(erase_count));
        }
    };

    // Detect new zones with duplicate prevention
    std::vector<Core::Zone> new_zones = detector.detect_zones_no_duplicates(active_zones);

    const auto& summary = detector.get_last_detection_summary();
    total_zones_created_ += summary.created;
    total_zones_merged_ += summary.merged;
    total_zones_skipped_ += summary.skipped;
    
    // Optimize: Skip duplication check if no new zones detected
    if (new_zones.empty()) {
        return;
    }
    
    // Add new zones (duplicates are already filtered)
    for (auto& zone : new_zones) {
        zone.zone_id = next_zone_id_++;

        // ⭐ LOOKAHEAD FIX: pass bar_index so regime is computed at the
        // current bar, not at bars.back() (future bar in BT's full dataset).
        Core::MarketRegime regime = Core::MarketAnalyzer::detect_regime(
            bars, 50, 5.0, bar_index);
        zone.zone_score = scorer.evaluate_zone(zone, regime, bar);

        LOG_DEBUG("Zone scored: ID=" << zone.zone_id
                 << " Total=" << zone.zone_score.total_score
                 << " (" << zone.zone_score.entry_rationale << ")");

        Core::ZoneStateEvent creation_event(
            bar.datetime,
            bar_index,
            "ZONE_CREATED",
            "",
            "FRESH",
            (zone.proximal_line + zone.distal_line) / 2.0,
            bar.high,
            bar.low,
            0
        );
        record_event(zone, creation_event, config.record_zone_creation);

        active_zones.push_back(zone);

        LOG_DEBUG("New zone added: "
                 << (zone.type == Core::ZoneType::DEMAND ? "DEMAND" : "SUPPLY")
                 << " @ " << zone.distal_line << "-" << zone.proximal_line
                 << " (ID: " << zone.zone_id << ")");
    }

    // Re-score merged zones if any were updated
    if (!detector.get_last_merged_zone_ids().empty()) {
        // ⭐ LOOKAHEAD FIX: pass bar_index so regime is computed at the
        // current bar, not at bars.back() (future bar in BT's full dataset).
        Core::MarketRegime regime = Core::MarketAnalyzer::detect_regime(
            bars, 50, 5.0, bar_index);
        for (auto& zone : active_zones) {
            if (std::find(detector.get_last_merged_zone_ids().begin(),
                          detector.get_last_merged_zone_ids().end(),
                          zone.zone_id) != detector.get_last_merged_zone_ids().end()) {
                zone.zone_score = scorer.evaluate_zone(zone, regime, bar);
            }
        }
    }
}

void BacktestEngine::check_for_entries(const Core::Bar& bar, int bar_index) {
    // Configurable early entry-time block (morning/no-trade window)
    if (config.enable_entry_time_block &&
        !config.entry_block_start_time.empty() &&
        !config.entry_block_end_time.empty() &&
        bar.datetime.length() >= 16) {
        std::string hhmm = bar.datetime.substr(11, 5);
        if (hhmm >= config.entry_block_start_time && hhmm < config.entry_block_end_time) {
            LOG_DEBUG("⛔ ENTRY BLOCKED: Time (" << hhmm
                    << ") in configured window ("
                    << config.entry_block_start_time << "-"
                    << config.entry_block_end_time << ")");
            return;
        }
    }

    // ⭐ FIX: ENTRY TIME GATE - Block entries after circuit breaker cutoff
    // Prevents late entries (e.g., 15:29) that bypass position closing logic
    if (config.close_before_session_end_minutes > 0) {
        std::string current_time_str = bar.datetime;
        
        // Parse current time (HH:MM:SS)
        int curr_hour = 0, curr_min = 0, curr_sec = 0;
        try {
            size_t space_pos = current_time_str.rfind(' ');
            if (space_pos != std::string::npos) {
                std::string time_part = current_time_str.substr(space_pos + 1);
                sscanf(time_part.c_str(), "%d:%d:%d", &curr_hour, &curr_min, &curr_sec);
            }
        } catch (...) {
            // Silently skip if parsing fails
        }
        
        // Parse session end time
        int session_hour = 0, session_min = 0, session_sec = 0;
        try {
            sscanf(config.session_end_time.c_str(), "%d:%d:%d", &session_hour, &session_min, &session_sec);
        } catch (...) {
            // Silently skip if parsing fails
        }
        
        // Calculate cutoff time (session_end - close_before_session_end_minutes)
        int cutoff_min = session_min - config.close_before_session_end_minutes;
        int cutoff_hour = session_hour;
        if (cutoff_min < 0) {
            cutoff_hour--;
            cutoff_min += 60;
        }
        
        // Convert to minutes since midnight
        int curr_total_min = curr_hour * 60 + curr_min;
        int cutoff_total_min = cutoff_hour * 60 + cutoff_min;
        
        if (curr_total_min >= cutoff_total_min) {
            // Entry blocked - too close to session end
            LOG_DEBUG("⏳ ENTRY BLOCKED: Time (" << current_time_str 
                    << ") >= Entry cutoff (" << cutoff_hour << ":" << cutoff_min << ")");
            return;
        }
    }
    
    // ⭐ E4 FIX: Use config.htf_lookback_bars for regime detection, matching live engine.
    // Old: REGIME_LOOKBACK hardcoded to 100, ignoring any config value.
    // Live: uses config.htf_lookback_bars if > 0, falls back to 100.
    // If htf_lookback_bars is set (e.g. 200), backtest and live detected different
    // regimes on the same bar, causing divergent entry decisions.
    const int REGIME_LOOKBACK = (config.htf_lookback_bars > 0) ? config.htf_lookback_bars : 100;
    // ⭐ LOOKAHEAD FIX: pass bar_index so regime is computed at the current bar,
    // not at bars.back() (which is a future Mar-2026 bar when processing a Feb-02 bar).
    // Without this, BT used future price action to determine regime at entry time,
    // causing BT=TRENDING vs LT=RANGING for the same entry bar.
    Core::MarketRegime regime = Core::MarketAnalyzer::detect_regime(
        bars, REGIME_LOOKBACK, config.htf_trending_threshold, bar_index);
    
    // Debug: Log regime detection (reduced frequency for performance)
    static int regime_log_counter = 0;
    if (regime_log_counter++ % 500 == 0) {
        std::string regime_str = (regime == Core::MarketRegime::BULL) ? "BULL" : 
                                  (regime == Core::MarketRegime::BEAR) ? "BEAR" : "RANGING";
        LOG_DEBUG("Market Regime: " << regime_str << " (lookback=" << REGIME_LOOKBACK <<  
                 ", allow_ranging=" << (config.allow_ranging_trades ? "YES" : "NO") 
                 << ", trend_only=" << (config.trade_with_trend_only ? "YES" : "NO") << ")");
    }
    
    // ⭐ D7 FIX: Same-session re-entry guard — mirrors live engine exactly.
    // Live engine maintains zone_sl_session_: zones that hit STOP_LOSS or a losing
    // SESSION_END are blocked from re-entry for the rest of that trading day.
    // Without this, backtest re-enters the same zone multiple times per day after
    // losses, producing different trade sequences and inflated loss counts vs live.
    {
        const std::string today = bars[bar_index].datetime.substr(0, 10);
        if (last_guard_date_.empty()) {
            last_guard_date_ = today;
        } else if (today != last_guard_date_) {
            if (!zone_sl_session_.empty()) {
                LOG_DEBUG("[RE-ENTRY GUARD] New session " << today
                         << " — clearing " << zone_sl_session_.size() << " blocked zone(s)");
                zone_sl_session_.clear();
            }
            last_guard_date_ = today;
        }
    }

    // ⭐ D4 FIX: Sort zones by state quality then recency before entry loop.
    // Live engine runs stable_sort here; backtest previously iterated in insertion
    // order (oldest first). When multiple zones qualify on the same bar, both engines
    // must select the same zone — FRESH > RECLAIMED > TESTED, then newer formation first.
    std::stable_sort(active_zones.begin(), active_zones.end(),
        [](const Core::Zone& a, const Core::Zone& b) {
            auto state_priority = [](const Core::Zone& z) -> int {
                switch (z.state) {
                    case Core::ZoneState::FRESH:     return 0;
                    case Core::ZoneState::RECLAIMED: return 1;
                    case Core::ZoneState::TESTED:    return 2;
                    case Core::ZoneState::VIOLATED:  return 3;
                    default:                         return 4;
                }
            };
            int pa = state_priority(a), pb = state_priority(b);
            if (pa != pb) return pa < pb;
            if (a.formation_bar != b.formation_bar) return a.formation_bar > b.formation_bar;
            return a.zone_id > b.zone_id;
        });

    // Check each active zone for entry opportunity
    for (auto& zone : active_zones) {
        // ⭐ ZONE SANITY GUARD: Skip corrupt zones with zero or invalid boundaries.
        // A zone with distal_line=0 causes calculate_stop_loss() → SL=0,
        // which cascades into: TP=0 → phantom TP exit at price=0 → loss=entry*lot_size*65.
        // These are zero-initialized Zone structs that were never properly populated.
        if (zone.distal_line <= 0.0 || zone.proximal_line <= 0.0) {
            LOG_WARN("Zone " << zone.zone_id << " SKIPPED: corrupt boundaries"
                     << " distal=" << zone.distal_line
                     << " proximal=" << zone.proximal_line);
            continue;
        }
        if (zone.proximal_line == zone.distal_line) {
            LOG_WARN("Zone " << zone.zone_id << " SKIPPED: zero-width zone"
                     << " (proximal==distal==" << zone.proximal_line << ")");
            continue;
        }

        // ⭐ D7 FIX: Same-session re-entry guard check
        if (!zone_sl_session_.empty()) {
            const std::string today = bar.datetime.substr(0, 10);
            auto it = zone_sl_session_.find(zone.zone_id);
            if (it != zone_sl_session_.end() && it->second == today) {
                LOG_DEBUG("[RE-ENTRY GUARD] Zone " << zone.zone_id
                         << " SKIPPED: already exited in session " << today);
                continue;
            }
        }

        // ⭐ E1 FIX: Config-driven violated zone skip — matches live engine.
        // Old: always skip VIOLATED zones unconditionally.
        // Live: only skip if config.skip_retest_after_gap_over = YES.
        // With skip_retest_after_gap_over=NO, both engines evaluate VIOLATED
        // zones as entry candidates (e.g. for sweep-reclaim setups).
        if (zone.state == Core::ZoneState::VIOLATED && config.skip_retest_after_gap_over) continue;

        // ⭐ P1: Skip EXHAUSTED zones (consecutive-loss limit reached)
        if (config.enable_zone_exhaustion && zone.state == Core::ZoneState::EXHAUSTED) {
            LOG_DEBUG("Zone " << zone.zone_id << " SKIPPED: EXHAUSTED ("
                      << zone.consecutive_losses << " consecutive losses)");
            continue;
        }

        // ⭐ D8 FIX: Per-direction SL suspension check — mirrors live engine.
        // If this zone has hit zone_sl_suspend_threshold STOP_LOSS exits in the
        // direction it would trade (DEMAND→LONG, SUPPLY→SHORT) with no intervening
        // winner, skip it permanently for this run. Live engine does this — without
        // it backtest keeps re-entering zones that live has already suspended.
        if (config.zone_sl_suspend_threshold > 0) {
            const std::string direction = (zone.type == Core::ZoneType::DEMAND) ? "LONG" : "SHORT";
            const int sl_count = (direction == "LONG") ? zone.sl_count_long : zone.sl_count_short;
            if (sl_count >= config.zone_sl_suspend_threshold) {
                LOG_DEBUG("Zone #" << zone.zone_id << " SKIPPED: "
                         << direction << " suspended ("
                         << sl_count << " SL hits, no winner)");
                continue;
            }
        }

        // ⭐ Zone retry limit (existing mechanism — parity with live engine)
        if (config.enable_per_zone_retry_limit &&
            zone.entry_retry_count >= config.max_retries_per_zone) {
            LOG_DEBUG("Zone " << zone.zone_id << " SKIPPED: retry limit ("
                      << zone.entry_retry_count << " >= " << config.max_retries_per_zone << ")");
            continue;
        }

        // ⭐ E3 FIX: max_touch_count zone retirement — matches live engine.
        // Live engine retires high-touch zones by setting state=VIOLATED and skipping.
        // Without this, backtest keeps trading zones that live has already retired,
        // producing different trade sets on the same dataset.
        // Default 200 matches the live engine's fallback (0 = unlimited in config).
        {
            const int touch_limit = (config.max_touch_count > 0) ? config.max_touch_count : 200;
            if (zone.touch_count > touch_limit) {
                LOG_DEBUG("Zone #" << zone.zone_id << " RETIRED: " << zone.touch_count
                         << " touches (max=" << touch_limit << ")");
                zone.state = Core::ZoneState::VIOLATED;
                continue;
            }
        }

        // ⭐ D3 FIX: Match live engine — only_fresh_zones blocks VIOLATED/EXHAUSTED only.
        // Live allows FRESH, RECLAIMED, and TESTED zones when only_fresh_zones=YES.
        // Old backtest only allowed FRESH, silently rejecting all RECLAIMED/TESTED zones
        // and causing far fewer trades than live on the same dataset.
        if (config.only_fresh_zones &&
            zone.state != Core::ZoneState::FRESH &&
            zone.state != Core::ZoneState::RECLAIMED &&
            zone.state != Core::ZoneState::TESTED) continue;

        // ⭐ P3: Late-session entry cutoff — no new entries at or after cutoff time
        if (config.enable_late_entry_cutoff && bar.datetime.length() >= 16) {
            int bar_hhmm = 0, cut_hhmm = 0;
            try {
                bar_hhmm = std::stoi(bar.datetime.substr(11, 2)) * 60
                         + std::stoi(bar.datetime.substr(14, 2));
            } catch (...) {}
            int cut_h = 14, cut_m = 30;
            sscanf(config.late_entry_cutoff_time.c_str(), "%d:%d", &cut_h, &cut_m);
            cut_hhmm = cut_h * 60 + cut_m;
            if (bar_hhmm >= cut_hhmm) {
                LOG_DEBUG("Late-session cutoff: no new entries after "
                          << config.late_entry_cutoff_time << " (bar=" << bar.datetime << ")");
                break;  // No point checking remaining zones — time-based gate for whole bar
            }
        }

        // Zone age filter (min/max/exclude range)
        int zone_age_days = 0;
        if (is_zone_age_blocked(zone, bar, config, zone_age_days)) {
            LOG_DEBUG("Zone " << zone.zone_id << " rejected by age filter: " << zone_age_days
                     << " days (min=" << config.min_zone_age_days
                     << ", max=" << config.max_zone_age_days
                     << ", exclude=" << (config.exclude_zone_age_range ? "YES" : "NO") << ")");
            continue;
        }
        
        // ⭐ E2 FIX: Max zone distance filter — matches live engine check_for_entries.
        // Live rejects zones whose midpoint is farther than atr*max_zone_distance_atr
        // from the current bar close. Without this, backtest evaluates zones that are
        // hundreds of points away — trades that live would never take.
        if (config.max_zone_distance_atr > 0.0) {
            double zone_mid = (zone.proximal_line + zone.distal_line) / 2.0;
            double atr_dist = Core::MarketAnalyzer::calculate_atr(bars, config.atr_period, bar_index);
            double max_dist = atr_dist * config.max_zone_distance_atr;
            if (std::abs(zone_mid - bar.close) > max_dist) {
                LOG_DEBUG("Zone " << zone.zone_id << " SKIPPED: too far from price ("
                         << std::fixed << std::setprecision(1)
                         << std::abs(zone_mid - bar.close) << " pts > max " << max_dist << " pts)");
                continue;
            }
        }

        // price_in_zone: bar overlaps zone AND close does not break through distal.
        bool price_in_zone;
        if (zone.type == Core::ZoneType::DEMAND) {
            price_in_zone = (bar.high >= zone.distal_line &&
                             bar.low  <= zone.proximal_line &&
                             bar.close >= zone.distal_line);  // close guard
        } else {
            price_in_zone = (bar.low  <= zone.distal_line &&
                             bar.high >= zone.proximal_line &&
                             bar.close <= zone.distal_line);  // close guard
        }
        if (!price_in_zone) continue;

        std::string direction = (zone.type == Core::ZoneType::DEMAND) ? "LONG" : "SHORT";

        // ⭐ CANDLE CONFIRMATION FILTER (ported from live_engine.cpp)
        // Requires the previous bar to show rejection evidence before entering:
        //   - Previous bar close is inside the zone
        //   - Candle direction matches trade direction
        //   - Body size >= candle_confirmation_body_pct of total range
        if (config.enable_candle_confirmation && bar_index >= 1) {
            const Core::Bar& confirm_bar = bars[bar_index - 1];
            double zone_low  = std::min(zone.distal_line, zone.proximal_line);
            double zone_high = std::max(zone.distal_line, zone.proximal_line);
            bool close_in_zone = (confirm_bar.close >= zone_low && confirm_bar.close <= zone_high);
            bool wick_in_zone  = (confirm_bar.low <= zone_high && confirm_bar.high >= zone_low);
            double candle_range = confirm_bar.high - confirm_bar.low;
            double candle_body  = std::abs(confirm_bar.close - confirm_bar.open);
            double body_pct = (candle_range > 0.0) ? (candle_body / candle_range * 100.0) : 0.0;
            bool direction_ok = (direction == "LONG") ? (confirm_bar.close > confirm_bar.open)
                                                       : (confirm_bar.close < confirm_bar.open);
            bool candle_ok = close_in_zone && wick_in_zone && direction_ok &&
                             (body_pct >= config.candle_confirmation_body_pct);
            if (!candle_ok) {
                LOG_DEBUG("Candle confirmation rejected zone " << zone.zone_id
                         << " close_in=" << close_in_zone
                         << " wick_in=" << wick_in_zone
                         << " dir_ok=" << direction_ok
                         << " body=" << std::fixed << std::setprecision(1) << body_pct << "%");
                continue;
            }
        }

        // ⭐ STRUCTURE CONFIRMATION FILTER (ported from live_engine.cpp)
        // For LONG: confirm_bar.close > prev_bar.high (broke structure upward)
        // For SHORT: confirm_bar.close < prev_bar.low  (broke structure downward)
        if (config.enable_structure_confirmation && bar_index >= 2) {
            const Core::Bar& confirm_bar = bars[bar_index - 1];
            const Core::Bar& prev_bar    = bars[bar_index - 2];
            bool structure_ok = (direction == "LONG")
                ? (confirm_bar.close > prev_bar.high)
                : (confirm_bar.close < prev_bar.low);
            if (!structure_ok) {
                LOG_DEBUG("Structure confirmation rejected zone " << zone.zone_id
                         << " confirm_close=" << confirm_bar.close
                         << " prev_high=" << prev_bar.high
                         << " prev_low=" << prev_bar.low);
                continue;
            }
        }

        // ⭐ EMA ALIGNMENT FILTER (moved from entry_decision_engine.cpp due to linker issues)
        // Check EMA trend alignment before scoring to save computation.
        //
        // ⭐ FIX A: Upgraded LOG_DEBUG → LOG_INFO + std::cout to match live engine.
        // Old: LOG_DEBUG only — silent at INFO log level, giving the false impression
        //      that the EMA filter was not running in backtest (0 visible rejections).
        // Live engine: LOG_INFO + std::cout with "[NO] Zone N: EMA filter - ..." prefix.
        // Fix: identical output format so both consoles show the same rejection lines,
        // making it straightforward to verify the filter fires on the same bars.
        double ema_20 = Core::MarketAnalyzer::calculate_ema(bars, 20, bar_index);
        double ema_50 = Core::MarketAnalyzer::calculate_ema(bars, 50, bar_index);
        
        if (direction == "LONG" && config.require_ema_alignment_for_longs) {
            double ema_sep_pct = (ema_50 > 0) ? (100.0 * (ema_20 - ema_50) / ema_50) : 0.0;
            if (ema_20 <= ema_50 || ema_sep_pct < config.ema_min_separation_pct_long) {
                std::cout << "  [NO] Zone " << zone.zone_id
                          << ": EMA filter - LONG rejected (EMA20 "
                          << std::fixed << std::setprecision(2) << ema_20
                          << " <= EMA50 " << ema_50
                          << ", sep=" << ema_sep_pct
                          << "% < min=" << config.ema_min_separation_pct_long << "%)\n";
                LOG_INFO("EMA filter rejected LONG zone " << zone.zone_id
                         << ": EMA20=" << ema_20 << " EMA50=" << ema_50
                         << " sep=" << ema_sep_pct
                         << "% < min=" << config.ema_min_separation_pct_long << "%");
                continue;
            }
        }
        
        if (direction == "SHORT" && config.require_ema_alignment_for_shorts) {
            double ema_sep_pct = (ema_20 > 0) ? (100.0 * (ema_50 - ema_20) / ema_20) : 0.0;
            if (ema_20 >= ema_50 || ema_sep_pct < config.ema_min_separation_pct_short) {
                std::cout << "  [NO] Zone " << zone.zone_id
                          << ": EMA filter - SHORT rejected (EMA20 "
                          << std::fixed << std::setprecision(2) << ema_20
                          << " >= EMA50 " << ema_50
                          << ", sep=" << ema_sep_pct
                          << "% < min=" << config.ema_min_separation_pct_short << "%)\n";
                LOG_INFO("EMA filter rejected SHORT zone " << zone.zone_id
                         << ": EMA20=" << ema_20 << " EMA50=" << ema_50
                         << " sep=" << ema_sep_pct
                         << "% < min=" << config.ema_min_separation_pct_short << "%");
                continue;
            }
        }
        
        // ⭐ REGIME FILTER 1: RSI extreme hard block
        // Validated on 168 trades (Aug 2025–Mar 2026): RSI>72 and RSI<28 are
        // momentum climax conditions where price blows through S&D zones rather
        // than reversing at them.  FP=21%/25%, combined net save: +₹1,41,375.
        if (config.enable_rsi_hard_block) {
            double rsi_now = Core::MarketAnalyzer::calculate_rsi(
                bars, config.rsi_period, bar_index);
            if (rsi_now > config.rsi_hard_block_high) {
                LOG_DEBUG("RSI hard block: rejecting zone " << zone.zone_id
                         << " (RSI=" << std::fixed << std::setprecision(1) << rsi_now
                         << " > " << config.rsi_hard_block_high << " overbought extreme)");
                continue;
            }
            if (rsi_now < config.rsi_hard_block_low) {
                LOG_DEBUG("RSI hard block: rejecting zone " << zone.zone_id
                         << " (RSI=" << std::fixed << std::setprecision(1) << rsi_now
                         << " < " << config.rsi_hard_block_low << " oversold extreme)");
                continue;
            }
        }

        // ⭐ REGIME FILTER 2: ADX hard block
        // Threshold corrected from 60 (never triggered in 130 trades) to 55
        // (validated: FP=12%, net save +₹97,979 over 130 backtest trades).
        // adx_transition_skip_entry now defaults true — hard block, not size reduction.
        bool adx_in_danger_band = false;
        if (config.enable_adx_transition_filter) {
            auto adx_vals = Core::MarketAnalyzer::calculate_adx(bars, config.adx_period, bar_index);
            if (adx_vals.adx >= config.adx_transition_min && adx_vals.adx <= config.adx_transition_max) {
                adx_in_danger_band = true;
                if (config.adx_transition_skip_entry) {
                    LOG_DEBUG("ADX hard block: rejecting zone " << zone.zone_id
                              << " (ADX=" << std::fixed << std::setprecision(1) << adx_vals.adx
                              << " >= " << config.adx_transition_min << " mature trend threshold)");
                    continue;
                }
                LOG_DEBUG("ADX transition filter: half-size for zone " << zone.zone_id
                          << " (ADX=" << std::fixed << std::setprecision(1) << adx_vals.adx << ")");
            }
        }

        // Score the zone
        // ⭐ E5 FIX: Apply expiry day overrides before scoring — matches live engine.
        // On monthly expiry day (last Thursday of month): disable OI filters and
        // scale lot_size by expiry_day_position_multiplier. Without this, backtest
        // trades full size with OI filters on expiry days while live does not.
        Core::Config entry_config = config;  // Copy per-zone to allow expiry override
        {
            // Inline expiry day detection (last Thursday of month)
            bool is_expiry = false;
            if (bar.datetime.length() >= 10) {
                int year = 0, month = 0, day = 0; char d1, d2;
                std::istringstream dts(bar.datetime.substr(0, 10));
                dts >> year >> d1 >> month >> d2 >> day;
                if (!dts.fail() && d1=='-' && d2=='-') {
                    std::tm tm_exp = {};
                    tm_exp.tm_year = year - 1900;
                    tm_exp.tm_mon  = month - 1;
                    tm_exp.tm_mday = day;
                    tm_exp.tm_isdst = -1;
                    std::mktime(&tm_exp);
                    if (tm_exp.tm_wday == 4) {  // Thursday
                        tm_exp.tm_mday += 7;
                        std::mktime(&tm_exp);
                        is_expiry = (tm_exp.tm_mon != month - 1);
                    }
                }
            }
            if (is_expiry) {
                if (entry_config.expiry_day_disable_oi_filters) {
                    entry_config.enable_oi_entry_filters  = false;
                    entry_config.enable_oi_exit_signals   = false;
                }
                entry_config.lot_size = static_cast<int>(
                    entry_config.lot_size * entry_config.expiry_day_position_multiplier);
                LOG_DEBUG("Expiry day: lot_size=" << entry_config.lot_size
                         << " OI_filters=" << (entry_config.enable_oi_entry_filters ? "ON":"OFF"));
            }
        }
        Core::ZoneScore zone_score = scorer.evaluate_zone(zone, regime, bar);
        
        // Calculate entry decision
        double atr = Core::MarketAnalyzer::calculate_atr(bars, config.atr_period, bar_index);
        Core::EntryDecision decision;

        // ⭐ D5 FIX: Pass full bars vector to calculate_entry, not a 10-bar slice.
        // Live engine passes &bar_history (full history). calculate_entry_volume_metrics
        // uses the full vector for volume baseline normalisation — a 10-bar window
        // produces different pullback volume ratios and scores vs the full dataset,
        // causing different entry decisions on the same bars.
        if (config.enable_two_stage_scoring) {
            Core::ZoneQualityScore zone_quality_score;
            Core::EntryValidationScore entry_validation_score;
            bool approved = entry_engine.should_enter_trade_two_stage(
                zone,
                bars,
                bar_index,
                zone.proximal_line,
                0.0,
                &zone_quality_score,
                &entry_validation_score
            );
            if (!approved) {
                LOG_DEBUG("Zone rejected by two-stage scoring");
                continue;
            }
            decision = entry_engine.calculate_entry(zone, zone_score, atr, regime,
                config.enable_revamped_scoring ? &zone_quality_score : nullptr, &bar, &bars);
        } else {
            decision = entry_engine.calculate_entry(zone, zone_score, atr, regime, nullptr, &bar, &bars);
        }
        
        if (!decision.should_enter) {
            LOG_DEBUG("Zone rejected: " << decision.rejection_reason);
            continue;
        }
        
        // ⭐ FIX: Pre-check SL width before calling enter_trade().
        // calculate_entry() computes SL from zone structure (sl_buffer_zone_pct × zone_height
        // + sl_buffer_atr × ATR). For wide zones or high-ATR bars this produces SL >> cap.
        // The old approach tightened SL post-fill — but a tightened SL is NOT the same trade
        // the engine validated. It places SL inside the zone's intended buffer, making normal
        // price noise hit the stop artificially. Analysis of 384 trades showed 60.2% had SL
        // tightened (avg 33.7pts), directly inflating the SL hit rate vs the 327% backtest.
        // FIX: reject here if structural SL exceeds cap. Post-fill tightening in trade_manager
        // remains as safety net for fill slippage only (~5% of cases).
        {
            double max_sl_dist = config.max_loss_per_trade / static_cast<double>(config.lot_size);
            double sl_dist = std::abs(decision.entry_price - decision.stop_loss);
            if (sl_dist > max_sl_dist * 1.05) {  // 5% tolerance for ATR rounding
                LOG_DEBUG("Zone#" << zone.zone_id << " SKIPPED: structural SL too wide ("
                          << std::fixed << std::setprecision(1) << sl_dist
                          << "pts > " << max_sl_dist << "pt cap) — tightening rejected");
                continue;
            }
        }

        // ⭐ P2: Apply ADX transition size factor (reduces lots in danger band)
        if (adx_in_danger_band && !config.adx_transition_skip_entry && decision.lot_size > 0) {
            int reduced = static_cast<int>(decision.lot_size * config.adx_transition_size_factor);
            if (reduced < 1) reduced = 1;
            LOG_INFO("ADX transition: lot_size " << decision.lot_size << " → " << reduced
                     << " (factor=" << config.adx_transition_size_factor << ")");
            decision.lot_size = reduced;
        }

        // ⭐ P4: Hard cap on position lots (final ceiling after all sizing)
        if (config.max_position_lots > 0 && decision.lot_size > config.max_position_lots) {
            LOG_INFO("max_position_lots cap: " << decision.lot_size
                     << " → " << config.max_position_lots << " lots");
            decision.lot_size = config.max_position_lots;
        }

        // Enter trade
        if (trade_manager.enter_trade(decision, zone, bar, bar_index, regime, "", &bars)) {
            Trade& current_trade = const_cast<Trade&>(trade_manager.get_current_trade());
            current_trade.trade_num = performance.get_trade_count() + 1;
            
            std::cout << "Trade #" << current_trade.trade_num << " entered: "
                     << current_trade.direction << " @ " 
                     << std::fixed << std::setprecision(2) << current_trade.entry_price
                     << ", Score: " << zone_score.total_score << "/120"
                     << std::endl;
            
            break;  // Only one trade at a time
        }
    }
}

void BacktestEngine::manage_open_position(const Core::Bar& bar, int bar_index) {
    // Get mutable reference to current trade for trailing stop updates
    Trade& trade = const_cast<Trade&>(trade_manager.get_current_trade());

    // ⭐ SESSION CLOSE: Force-close intraday positions at end of each trading day.
    //
    // CRITICAL FIX: Without this, backtest_engine held positions overnight and
    // across multiple days, causing:
    //   1. Overnight gap risk: NIFTY can gap 200-300 pts on open vs 92pt SL
    //   2. Multi-day adverse moves on intraday 5-min signals
    //   3. Avg loss 8-25x the max_loss_per_trade cap (impossible intraday)
    //   4. Capital going to -$58M from $300k starting capital
    //
    // This mirrors live_engine.cpp SESSION_END behavior exactly.
    // Uses: session_end_time (e.g. "15:30:00") and
    //       close_before_session_end_minutes (e.g. 10 → closes at 15:20)
    if (!config.session_end_time.empty() && bar.datetime.length() >= 16) {
        // Extract HH:MM from bar.datetime ("YYYY-MM-DD HH:MM:SS")
        int bar_hour = 0, bar_min = 0;
        try {
            bar_hour = std::stoi(bar.datetime.substr(11, 2));
            bar_min  = std::stoi(bar.datetime.substr(14, 2));
        } catch (...) {}

        // Parse session_end_time ("HH:MM:SS")
        int sess_hour = 15, sess_min = 30;  // safe default: NSE close
        sscanf(config.session_end_time.c_str(), "%d:%d", &sess_hour, &sess_min);

        // Cutoff = session_end minus close_before_session_end_minutes
        int cutoff_total = sess_hour * 60 + sess_min - config.close_before_session_end_minutes;
        int bar_total    = bar_hour  * 60 + bar_min;

        if (bar_total >= cutoff_total) {
            LOG_INFO("SESSION_END: Closing trade at " << bar.datetime
                     << " (bar=" << bar_hour << ":" << std::setw(2) << std::setfill('0') << bar_min
                     << " >= cutoff=" << (cutoff_total/60) << ":"
                     << std::setw(2) << std::setfill('0') << (cutoff_total%60) << ")");
            Trade closed = trade_manager.close_trade(bar, "SESSION_END", bar.close);
            performance.add_trade(closed);
            performance.update_capital(trade_manager.get_capital());
            consecutive_losses = (closed.pnl < 0 && !trade.trailing_activated)
                                 ? consecutive_losses + 1 : 0;
            std::cout << "SESSION_END: Trade #" << closed.trade_num
                      << " closed @ " << std::fixed << std::setprecision(2) << bar.close
                      << "  P&L " << closed.pnl << std::endl;

            // ⭐ D7 FIX: Block re-entry on losing SESSION_END — mirrors live engine rule.
            // Live blocks same-session re-entry when SESSION_END P&L < 0 (zone was weak).
            // Profitable SESSION_END does NOT block — zone still valid for continuation.
            if (closed.pnl < 0 && closed.zone_id >= 0) {
                std::string session_date = bar.datetime.substr(0, 10);
                zone_sl_session_[closed.zone_id] = session_date;
                LOG_DEBUG("[RE-ENTRY GUARD] Zone " << closed.zone_id
                         << " blocked for rest of session " << session_date
                         << " after SESSION_END loss (P&L=" << closed.pnl << ")");
            }

            // ⭐ D8 FIX: Profitable SESSION_END resets sl_count — zone proved itself today.
            if (closed.pnl >= 0 && config.zone_sl_suspend_threshold > 0
                && closed.zone_id >= 0) {
                for (auto& zone : active_zones) {
                    if (zone.zone_id != closed.zone_id) continue;
                    if (closed.direction == "LONG" && zone.sl_count_long > 0) {
                        zone.sl_count_long = 0;
                        LOG_INFO("[SL-SUSPEND] Zone #" << zone.zone_id
                                 << " LONG sl_count reset (SESSION_END win)");
                    } else if (closed.direction == "SHORT" && zone.sl_count_short > 0) {
                        zone.sl_count_short = 0;
                        LOG_INFO("[SL-SUSPEND] Zone #" << zone.zone_id
                                 << " SHORT sl_count reset (SESSION_END win)");
                    }
                    break;
                }
            }
            return;
        }
    }

    // ⭐ FIX #3: Volume Exhaustion Exit (check BEFORE volume climax)
    if (config.enable_volume_exhaustion_exit && config.v6_fully_enabled) {
        auto vol_ex_signal = trade_manager.check_volume_exhaustion_signals(
            trade, bar, bars, bar_index
        );
        
        if (trade_manager.should_exit_on_exhaustion(vol_ex_signal, trade, bar)) {
            std::string signal_name;
            switch (vol_ex_signal) {
                case TradeManager::VolumeExhaustionSignal::AGAINST_TREND_SPIKE:
                    signal_name = "VOL_EXHAUSTION_SPIKE"; break;
                case TradeManager::VolumeExhaustionSignal::ABSORPTION:
                    signal_name = "VOL_EXHAUSTION_ABSORPTION"; break;
                case TradeManager::VolumeExhaustionSignal::FLOW_REVERSAL:
                    signal_name = "VOL_EXHAUSTION_FLOW"; break;
                case TradeManager::VolumeExhaustionSignal::LOW_VOLUME_DRIFT:
                    signal_name = "VOL_EXHAUSTION_DRIFT"; break;
                default:
                    signal_name = "VOL_EXHAUSTION"; break;
            }
            
            Trade closed = trade_manager.close_trade(bar, signal_name, bar.close);
            performance.add_trade(closed);
            
            LOG_INFO("🚨 " << signal_name << " - Early exit at bar " << bar_index);
            LOG_INFO("   Entry: " << trade.entry_price 
                     << ", Exit: " << bar.close 
                     << ", P&L: $" << closed.pnl);
            double sl_dist = std::abs(trade.entry_price - trade.stop_loss);
            double saved = (sl_dist * trade.position_size * config.lot_size) - std::abs(closed.pnl);
            LOG_INFO("   Saved ₹" << saved << " vs full SL");
            
            return;
        }
    }
    
    // ✅ NEW V6.0: Check volume-based exit signals
    if (config.enable_volume_exit_signals) {
        auto vol_exit = trade_manager.check_volume_exit_signals(trade, bar);
        if (vol_exit == TradeManager::VolumeExitSignal::VOLUME_CLIMAX) {
            LOG_INFO("🚨 VOLUME CLIMAX detected - Exiting trade #" << trade.trade_num);
            Trade closed = trade_manager.close_trade(bar, "VOL_CLIMAX", bar.close);
            performance.add_trade(closed);
            return;
        }
    }
    
    // ✅ NEW V6.0: Check OI-based exit signals
    if (config.enable_oi_exit_signals) {
        auto oi_exit = trade_manager.check_oi_exit_signals(trade, bar, bars, bar_index);
        if (oi_exit == TradeManager::OIExitSignal::OI_UNWINDING) {
            LOG_INFO("🚨 OI UNWINDING detected - Exiting trade #" << trade.trade_num);
            Trade closed = trade_manager.close_trade(bar, "OI_UNWIND", bar.close);
            performance.add_trade(closed);
            return;
        }
    }
    
    // ⭐ FIXED: Multi-Stage Trailing Stop
    //
    // BUG FIXES vs. original:
    // 1. Stage ordering fixed — 4 distinct R thresholds with no overlap.
    //    Original had Stage 2 (>=1.0R) and Stage 1 (>=trail_activation_rr=1.0R)
    //    sharing the same threshold, making Stage 1 (breakeven) DEAD CODE.
    //    Fix: Stage 1 = 0.5R, Stage 2 = 1.0R, Stage 3 = 2.0R, Stage 4 = 3.0R.
    //
    // 2. Stage 2 profit-lock direction fix for SHORT.
    //    Original: SHORT trail = entry - 0.5*risk (BELOW entry).
    //    check_stop_loss for SHORT fires when bar.high >= stop_loss.
    //    A stop BELOW entry fires instantly on any bar above that level,
    //    which for a SHORT at entry means it triggers the VERY NEXT BAR.
    //    Fix: SHORT profit lock = entry + 0.5*risk (ABOVE entry but below orig SL),
    //    mirroring LONG logic symmetrically.
    //    LONG:  entry + 0.5R  → above entry, locks profit if price pulls back
    //    SHORT: entry - 0.5R  → below entry, locks profit if price rallies back
    //    Wait — for SHORT, check_stop_loss fires on bar.high >= stop_loss.
    //    To protect SHORT profit, the stop must be ABOVE current price but BELOW orig SL.
    //    After price has moved 1R in our favour (DOWN for SHORT), current price ≈ entry - R.
    //    We want the stop to sit at entry + 0.5R so a rally back to that level exits us.
    //    That is ABOVE entry and below the original SL — correct protective placement.
    //    Fix (corrected): SHORT Stage 2 = entry + 0.5*risk  (ratchet: only if < orig SL)
    //
    // 3. ATR stages (3 & 4) now use a FLOOR of the previous stage's fixed level.
    //    Original ATR stages could compute a new_trail_stop WORSE than the locked
    //    Stage 2 level if ATR was wide, bypassing the ratchet due to direction check.
    //    Fix: clamp new_trail_stop so it never retreats past the Stage 2 level.
    //
    // 4. trailing_activated is set on first stage transition regardless of which stage.
    //    The original only set trailing_activated on the FIRST call to any stage's
    //    !trailing_activated branch, meaning once set it was never logged again.
    //    Kept consistent — trailing_activated = true on first activation.
    //
    // Config-driven thresholds (add to config if needed; hard-coded sensible defaults):
    //   trail_activation_rr   (config) → Stage 1 (breakeven) threshold, default 0.5R
    //   Stage 2 fixed at 1.0R
    //   Stage 3 fixed at 2.0R
    //   Stage 4 fixed at 3.0R

    // ⭐ M1 FIX: Snapshot SL BEFORE trail ratchet runs.
    // If the trail ratchets the stop on the same bar that also breaches it,
    // the SL check below would use the NEW (tighter) stop — triggering an
    // exit that shouldn't happen until the NEXT bar. This mirrors live engine
    // BUG-11 fix: capture the pre-trail stop, use it for the SL exit check,
    // and only let the ratcheted level take effect from the next bar onward.
    double sl_before_trail_update = trade.stop_loss;

    if (config.use_trailing_stop) {
        // ⭐ M1 FIX: Re-capture here for clarity (already set above).
        sl_before_trail_update = trade.stop_loss;

        // Risk = distance from entry to ORIGINAL stop loss (never changes)
        double risk = std::abs(trade.entry_price - trade.original_stop_loss);
        if (risk <= 0) risk = std::abs(trade.entry_price - trade.stop_loss);
        if (risk < 0.01) risk = 1.0;

        // Track best (most favourable) price reached using bar extremes
        if (trade.direction == "LONG") {
            if (bar.high > trade.highest_price || trade.highest_price <= 0.0)
                trade.highest_price = bar.high;
        } else {
            if (bar.low < trade.lowest_price || trade.lowest_price <= 0.0)
                trade.lowest_price = bar.low;
        }

        // Current R = best excursion / initial risk
        double best_excursion = (trade.direction == "LONG")
            ? (trade.highest_price - trade.entry_price)
            : (trade.entry_price - trade.lowest_price);
        double current_r = (risk > 0) ? (best_excursion / risk) : 0.0;

        // ATR for buffer in Stages 3 & 4
        double atr_trail = risk;  // fallback: use risk as ATR proxy
        if (bar_index >= config.atr_period) {
            double atr_sum = 0.0;
            for (int i = bar_index - config.atr_period + 1; i <= bar_index; i++) {
                double tr = std::max({
                    bars[i].high - bars[i].low,
                    std::abs(bars[i].high - bars[i - 1].close),
                    std::abs(bars[i].low  - bars[i - 1].close)
                });
                atr_sum += tr;
            }
            atr_trail = atr_sum / config.atr_period;
        }

        // ── Compute stage-based candidate trail level ─────────────────────────
        // Direction-aware profit-lock levels:
        //   LONG : stop moves UP   (check_stop_loss fires when bar.low <= stop)
        //   SHORT: stop moves DOWN (check_stop_loss fires when bar.high >= stop)
        //
        // Stage 1 (breakeven): stop = entry_price
        //   LONG : entry_price — price must fall back to entry to exit
        //   SHORT: entry_price — price must rally back to entry to exit
        //
        // Stage 2 (0.5R profit lock):
        //   LONG : entry + 0.5R — price falls back 0.5R to exit, locking 0.5R profit
        //   SHORT: entry - 0.5R — price rallies 0.5R to exit, locking 0.5R profit
        //
        // Stage 3 (1.0×ATR from extreme):
        //   LONG : highest_price - 1.0×ATR  (tighter as market extends)
        //   SHORT: lowest_price  + 1.0×ATR
        //
        // Stage 4 (tight, 0.5×ATR from extreme):
        //   LONG : highest_price - 0.5×ATR
        //   SHORT: lowest_price  + 0.5×ATR

        // ⭐ M2 FIX: Stage 1 breakeven uses commission_pts offset, matching live engine.
        // Old: stage1_level = entry_price → TRAIL_SL exit at entry fills at
        //   entry_price - 2×commission, recording a small loss and incrementing
        //   consecutive_losses incorrectly.
        // Fix: stage1_level = entry ± commission_pts so the exit nets exactly ₹0.
        double commission_pts = 0.0;
        {
            int qty = trade.position_size * config.lot_size;
            if (qty > 0 && config.commission_per_trade > 0.0)
                commission_pts = (2.0 * config.commission_per_trade) / static_cast<double>(qty);
        }
        double stage1_level = (trade.direction == "LONG")
            ? trade.entry_price + commission_pts
            : trade.entry_price - commission_pts;
        double stage2_level = (trade.direction == "LONG")
            ? trade.entry_price + (risk * 0.5)
            : trade.entry_price - (risk * 0.5);

        double new_trail_stop = 0.0;
        int    activated_stage = 0;

        // ⭐ TRAIL_ATR_MULTIPLIER FIX: Apply config.trail_atr_multiplier to stages 3 and 4,
        // matching the live engine exactly.
        // Old: hardcoded 0.5 and 1.0 — ignoring trail_atr_multiplier (config = 0.70).
        // Live: uses multiplier * 0.5 and multiplier * 1.0.
        // With multiplier=0.70: stage4 = 0.35×ATR, stage3 = 0.70×ATR (tighter stops).
        double stage4_factor = config.trail_atr_multiplier * 0.5;
        double stage3_factor = config.trail_atr_multiplier * 1.0;

        if (current_r >= 3.0) {
            // Stage 4: Tight ATR trail, floored at Stage 2 level
            double raw = (trade.direction == "LONG")
                ? trade.highest_price - (atr_trail * stage4_factor)
                : trade.lowest_price  + (atr_trail * stage4_factor);
            // Floor: never worse than Stage 2 lock
            new_trail_stop = (trade.direction == "LONG")
                ? std::max(raw, stage2_level)
                : std::min(raw, stage2_level);
            activated_stage = 4;
        } else if (current_r >= 2.0) {
            // Stage 3: Moderate ATR trail, floored at Stage 2 level
            double raw = (trade.direction == "LONG")
                ? trade.highest_price - (atr_trail * stage3_factor)
                : trade.lowest_price  + (atr_trail * stage3_factor);
            new_trail_stop = (trade.direction == "LONG")
                ? std::max(raw, stage2_level)
                : std::min(raw, stage2_level);
            activated_stage = 3;
        } else if (current_r >= 1.0) {
            // Stage 2: Lock 0.5R profit
            // LONG : entry + 0.5R  (SL moves above entry — ratchets up as trade progresses)
            // SHORT: entry - 0.5R  (SL moves below entry — ratchets down as trade progresses)
            new_trail_stop = stage2_level;
            activated_stage = 2;
        } else if (current_r >= config.trail_activation_rr) {
            // Stage 1: Breakeven lock — config-driven, default 0.5R in config
            // trail_activation_rr should be set to 0.5 (not 1.0) so Stage 1 fires
            // before Stage 2.  With the new ordering this is now reachable.
            new_trail_stop = stage1_level;
            activated_stage = 1;
        }

        // Log first activation
        if (activated_stage > 0 && !trade.trailing_activated) {
            trade.trailing_activated = true;
            LOG_INFO("📈 Trail STAGE " << activated_stage << " first activated at "
                     << std::fixed << std::setprecision(2) << current_r << "R"
                     << "  new_trail=" << new_trail_stop);
        }

        // Ratchet: only advance stop in the profitable direction, never retreat
        if (new_trail_stop > 0.0) {
            if (trade.direction == "LONG" && new_trail_stop > trade.stop_loss) {
                LOG_DEBUG("Trail SL ▲ " << trade.stop_loss << " → " << new_trail_stop
                          << "  [Stage " << activated_stage << ", R=" << current_r << "]");
                trade.stop_loss = new_trail_stop;
                trade.current_trail_stop = new_trail_stop;
            } else if (trade.direction == "SHORT" && new_trail_stop < trade.stop_loss) {
                LOG_DEBUG("Trail SL ▼ " << trade.stop_loss << " → " << new_trail_stop
                          << "  [Stage " << activated_stage << ", R=" << current_r << "]");
                trade.stop_loss = new_trail_stop;
                trade.current_trail_stop = new_trail_stop;
            }
        }
    }
    
    // ⭐ FIX: Increment bars_in_trade FIRST, before any exit checks.
    // This mirrors live_engine.cpp manage_position() FIX 1.
    trade.bars_in_trade++;

    // ⭐ FIX: Require minimum 2 bars before allowing SL/TP exits.
    // Prevents same-bar exits where entry-bar volatility immediately breaches SL/TP.
    // Live engine enforces this — backtest must match.
    // Still update highest/lowest for trailing stop tracking on the first bar.
    if (trade.bars_in_trade < 2) {
        if (trade.direction == "LONG") {
            if (bar.high > trade.highest_price || trade.highest_price <= 0.0)
                trade.highest_price = bar.high;
        } else {
            if (bar.low < trade.lowest_price || trade.lowest_price <= 0.0)
                trade.lowest_price = bar.low;
        }
        LOG_DEBUG("Position too new (bars_in_trade=" << trade.bars_in_trade
                  << "), skipping exit check this bar");
        return;
    }

    // ⭐ FIX: EMA crossover exit — ported from live_engine.cpp manage_position().
    // If enable_ema_exit=YES and fast EMA crosses slow EMA against trade direction,
    // exit immediately. Only checked if SL/TP not about to fire this bar.
    // Checked BEFORE SL/TP so it uses bar.close as exit price (cleaner fill).
    // Use inline bar checks (not trade_manager calls) to avoid premature side-effects.
    bool sl_would_hit = (trade.direction == "LONG")  ? (bar.low  <= trade.stop_loss)
                                                     : (bar.high >= trade.stop_loss);
    bool tp_would_hit = (trade.direction == "LONG")  ? (bar.high >= trade.take_profit && trade.take_profit > 0.0)
                                                     : (bar.low  <= trade.take_profit && trade.take_profit > 0.0);
    if (config.enable_ema_exit && !sl_would_hit && !tp_would_hit) {
        int min_ema_period = std::max(config.exit_ema_fast_period, config.exit_ema_slow_period);
        if (bar_index >= min_ema_period) {
            double ema_fast = Core::MarketAnalyzer::calculate_ema(bars, config.exit_ema_fast_period, bar_index);
            double ema_slow = Core::MarketAnalyzer::calculate_ema(bars, config.exit_ema_slow_period, bar_index);
            bool ema_exit = (trade.direction == "LONG"  && ema_fast < ema_slow) ||
                            (trade.direction == "SHORT" && ema_fast > ema_slow);
            if (ema_exit) {
                LOG_INFO("EMA_CROSS exit: " << trade.direction
                         << " EMA" << config.exit_ema_fast_period << "=" << std::fixed << std::setprecision(2) << ema_fast
                         << " EMA" << config.exit_ema_slow_period << "=" << ema_slow);
                Trade closed_trade = trade_manager.close_trade(bar, "EMA_CROSS", bar.close);
                performance.add_trade(closed_trade);
                performance.update_capital(trade_manager.get_capital());
                consecutive_losses = (closed_trade.pnl < 0) ? consecutive_losses + 1 : 0;
                std::cout << "Trade #" << closed_trade.trade_num << " EMA_CROSS exit: "
                          << "P&L $" << std::fixed << std::setprecision(2) << closed_trade.pnl
                          << "  EMA" << config.exit_ema_fast_period << "=" << ema_fast
                          << " EMA" << config.exit_ema_slow_period << "=" << ema_slow << std::endl;
                return;
            }
        }
    }

    // ⭐ M1+M3 FIX: Compute SL/TP hits with bar extremes + pre-trail snapshot.
    // M1: Use sl_before_trail_update (not trade.stop_loss) so a trail ratchet on
    //     this bar cannot immediately self-trigger an exit on the same bar.
    // M3: Compute both flags together and resolve same-bar SL/TP conflict (SL wins)
    //     before calling close_trade — mirrors live engine manage_position() exactly.
    bool sl_hit = false;
    bool tp_hit = false;
    double exit_price_resolved = 0.0;
    if (trade.direction == "LONG") {
        sl_hit = (bar.low  <= sl_before_trail_update);
        tp_hit = (bar.high >= trade.take_profit && trade.take_profit > 0.0);
        if (sl_hit && tp_hit) { sl_hit = true; tp_hit = false; }
        exit_price_resolved = sl_hit
            ? std::min(sl_before_trail_update, bar.close)
            : trade.take_profit;
    } else {
        sl_hit = (bar.high >= sl_before_trail_update);
        tp_hit = (bar.low  <= trade.take_profit && trade.take_profit > 0.0);
        if (sl_hit && tp_hit) { sl_hit = true; tp_hit = false; }
        exit_price_resolved = sl_hit
            ? std::max(sl_before_trail_update, bar.close)
            : trade.take_profit;
    }

    // Check stop loss
    if (sl_hit) {
        std::string exit_reason = trade.trailing_activated ? "TRAIL_SL" : "STOP_LOSS";
        double sl_fill = exit_price_resolved;
        Trade closed_trade = trade_manager.close_trade(
            bar, exit_reason, sl_fill);
        performance.add_trade(closed_trade);
        performance.update_capital(trade_manager.get_capital());
        if (!trade.trailing_activated) {
            consecutive_losses++;
        } else {
            // Trailing stop hit is usually a win or small loss
            if (closed_trade.pnl > 0) {
                consecutive_losses = 0;
            }
        }
        
        // ⭐ FIX: Increment retry counter using EXACT zone_id match.
        // Old bug: distance-based match (within 50pts) could increment the WRONG zone,
        // blocking re-entries on zones that shouldn't be blocked.
        // Live engine uses exact zone_id match — backtest now mirrors this.
        if (config.enable_per_zone_retry_limit && closed_trade.zone_id >= 0) {
            for (auto& zone : active_zones) {
                if (zone.zone_id == closed_trade.zone_id) {
                    zone.entry_retry_count++;
                    LOG_INFO("Zone " << zone.zone_id << " retry count incremented to "
                             << zone.entry_retry_count
                             << " (blocks re-entry if >= " << config.max_retries_per_zone << ")");
                    break;
                }
            }
        }

        // ⭐ P1: Per-zone consecutive-loss tracking → EXHAUSTED state
        // Only SL/TRAIL_SL count as "loss" for exhaustion (not SESSION_END).
        // A TP win on the same zone resets the counter (see TP block below).
        if (config.enable_zone_exhaustion && closed_trade.zone_id >= 0) {
            for (auto& zone : active_zones) {
                if (zone.zone_id == closed_trade.zone_id) {
                    zone.consecutive_losses++;
                    LOG_INFO("Zone " << zone.zone_id << " consecutive_losses="
                             << zone.consecutive_losses << " / max=" << config.zone_consecutive_loss_max);
                    if (zone.consecutive_losses > config.zone_consecutive_loss_max
                        && zone.state != Core::ZoneState::EXHAUSTED
                        && zone.state != Core::ZoneState::VIOLATED) {
                        zone.state = Core::ZoneState::EXHAUSTED;
                        zone.exhausted_at_datetime = bar.datetime;
                        LOG_INFO("⛔ Zone " << zone.zone_id << " EXHAUSTED after "
                                 << zone.consecutive_losses << " consecutive losses at " << bar.datetime);
                        std::cout << "⛔ Zone " << zone.zone_id << " EXHAUSTED ("
                                  << zone.consecutive_losses << " consecutive SL losses)"
                                  << std::endl;
                    }
                    break;
                }
            }
        }

        std::cout << "Trade #" << closed_trade.trade_num << " stopped out (" << exit_reason << "): "
                 << "P&L $" << std::fixed << std::setprecision(2) << closed_trade.pnl
                 << std::endl;

        // ⭐ D7 FIX: Set same-session re-entry guard after STOP_LOSS.
        // Live engine blocks re-entry into a zone that hit STOP_LOSS in the same session.
        // Without this, backtest re-enters toxic zones repeatedly on the same day.
        if (exit_reason == "STOP_LOSS" && closed_trade.zone_id >= 0) {
            std::string session_date = bar.datetime.substr(0, 10);
            zone_sl_session_[closed_trade.zone_id] = session_date;
            LOG_DEBUG("[RE-ENTRY GUARD] Zone " << closed_trade.zone_id
                     << " blocked for rest of session " << session_date
                     << " after STOP_LOSS exit (P&L=" << closed_trade.pnl << ")");
        }

        // ⭐ D8 FIX: Per-direction SL suspension — track sl_count_long/sl_count_short.
        // Live engine suspends a zone direction after zone_sl_suspend_threshold STOP_LOSS
        // exits with no intervening winner. Backtest must mirror or it keeps re-entering
        // zones the live engine has already suspended, producing opposite results.
        if (config.zone_sl_suspend_threshold > 0
            && closed_trade.zone_id >= 0
            && exit_reason == "STOP_LOSS") {
            for (auto& zone : active_zones) {
                if (zone.zone_id != closed_trade.zone_id) continue;
                if (closed_trade.direction == "LONG")
                    zone.sl_count_long++;
                else
                    zone.sl_count_short++;
                const int sl_count = (closed_trade.direction == "LONG")
                                     ? zone.sl_count_long : zone.sl_count_short;
                LOG_INFO("[SL-SUSPEND] Zone #" << zone.zone_id
                         << " " << closed_trade.direction
                         << " sl_count=" << sl_count
                         << " / threshold=" << config.zone_sl_suspend_threshold);
                if (sl_count >= config.zone_sl_suspend_threshold) {
                    LOG_WARN("🚫 Zone #" << zone.zone_id
                             << " " << closed_trade.direction
                             << " entries SUSPENDED: " << sl_count
                             << " SL hits with no winner");
                }
                break;
            }
        } else if (config.zone_sl_suspend_threshold > 0
                   && closed_trade.zone_id >= 0
                   && exit_reason == "TRAIL_SL") {
            // TRAIL_SL winner: reset sl_count so zone can trade again
            for (auto& zone : active_zones) {
                if (zone.zone_id != closed_trade.zone_id) continue;
                if (closed_trade.direction == "LONG" && zone.sl_count_long > 0) {
                    LOG_INFO("[SL-SUSPEND] Zone #" << zone.zone_id
                             << " LONG sl_count reset (TRAIL_SL winner)");
                    zone.sl_count_long = 0;
                } else if (closed_trade.direction == "SHORT" && zone.sl_count_short > 0) {
                    LOG_INFO("[SL-SUSPEND] Zone #" << zone.zone_id
                             << " SHORT sl_count reset (TRAIL_SL winner)");
                    zone.sl_count_short = 0;
                }
                break;
            }
        }

        // Check if need to pause (only for regular SL, not trailing)
        if (!trade.trailing_activated && consecutive_losses >= config.max_consecutive_losses) {
            pause_counter = config.pause_bars_after_losses;
            LOG_INFO("Pausing for " << pause_counter << " bars after "
                    << consecutive_losses << " consecutive losses");
        }
        return;
    }
    
    // Check take profit (uses tp_hit flag — see M1/M3 fix above)
    if (tp_hit) {
        Trade closed_trade = trade_manager.close_trade(
            bar, "TAKE_PROFIT", exit_price_resolved);
        
        performance.add_trade(closed_trade);
        performance.update_capital(trade_manager.get_capital());
        
        consecutive_losses = 0;  // Reset on win

        // ⭐ P1: TP win resets zone consecutive-loss counter (zone is working again)
        if (config.enable_zone_exhaustion && closed_trade.zone_id >= 0) {
            for (auto& zone : active_zones) {
                if (zone.zone_id == closed_trade.zone_id) {
                    if (zone.consecutive_losses > 0) {
                        LOG_INFO("Zone " << zone.zone_id << " consecutive_losses reset (TP win)");
                        zone.consecutive_losses = 0;
                        // If zone was EXHAUSTED, un-exhaust it (TP proves it still works)
                        if (zone.state == Core::ZoneState::EXHAUSTED) {
                            zone.state = Core::ZoneState::TESTED;
                            zone.exhausted_at_datetime = "";
                            LOG_INFO("Zone " << zone.zone_id << " un-EXHAUSTED → TESTED (TP win)");
                        }
                    }
                    break;
                }
            }
        }

        // ⭐ D8 FIX: TP win resets per-direction sl_count — zone proved itself.
        // Live engine resets sl_count_long/short on TAKE_PROFIT so the zone
        // can trade again even if it previously hit the suspension threshold.
        if (config.zone_sl_suspend_threshold > 0 && closed_trade.zone_id >= 0) {
            for (auto& zone : active_zones) {
                if (zone.zone_id != closed_trade.zone_id) continue;
                if (closed_trade.direction == "LONG" && zone.sl_count_long > 0) {
                    LOG_INFO("[SL-SUSPEND] Zone #" << zone.zone_id
                             << " LONG sl_count reset (TAKE_PROFIT win)");
                    zone.sl_count_long = 0;
                } else if (closed_trade.direction == "SHORT" && zone.sl_count_short > 0) {
                    LOG_INFO("[SL-SUSPEND] Zone #" << zone.zone_id
                             << " SHORT sl_count reset (TAKE_PROFIT win)");
                    zone.sl_count_short = 0;
                }
                break;
            }
        }

        std::cout << "Trade #" << closed_trade.trade_num << " hit target: "
                 << "P&L $" << std::fixed << std::setprecision(2) << closed_trade.pnl
                 << std::endl;
    }
}

void BacktestEngine::process_bar(const Core::Bar& bar, int bar_index) {
    // ⭐ FIX: Always update zone states on every bar, even while in position.
    // Live engine does this — zones accumulate touches, detect violations, and
    // flip during open trades. Backtest must mirror this or zone state diverges.
    update_zone_states(bar, bar_index);

    // Manage existing position (returns early — no new entry while in position)
    if (trade_manager.is_in_position()) {
        manage_open_position(bar, bar_index);
        return;
    }

    // ⭐ P1 FIX: RE-ENTRY GUARD SET — runs unconditionally every bar.
    //
    // BUG (old): The guard was only SET inside manage_open_position() exit blocks
    // (STOP_LOSS, SESSION_END). But manage_open_position() has an early return at
    // the top: "if (!is_in_position()) return". On the bar AFTER a trade closes,
    // is_in_position() is already FALSE, so manage_open_position() returns before
    // reaching any guard-set code. The guard was therefore NEVER set in backtest —
    // a zone that lost could be re-entered immediately on the very next bar.
    //
    // FIX: Mirror live engine process_tick() which runs the guard-SET block
    // unconditionally after manage_position(), before check_for_entries().
    // This guarantees the guard fires on the SAME bar the trade closes
    // (manage_open_position sets is_in_position=false then returns), so
    // check_for_entries() on the SAME bar already sees the guard.
    {
        const auto& trades = performance.get_trades();
        if (!trades.empty()) {
            const auto& last_trade = trades.back();
            if (last_trade.exit_date != guard_last_exit_date_ && !last_trade.exit_date.empty()) {
                guard_last_exit_date_ = last_trade.exit_date;
                std::string session_date = last_trade.exit_date.substr(0, 10);

                bool block_reentry = false;
                if (last_trade.exit_reason == "STOP_LOSS") {
                    block_reentry = true;   // Structural failure — always block
                } else if (last_trade.exit_reason == "SESSION_END" && last_trade.pnl < 0.0) {
                    block_reentry = true;   // Losing SESSION_END — zone was weak today
                }
                // TRAIL_SL win, TAKE_PROFIT, positive SESSION_END → allow re-entry

                if (block_reentry) {
                    zone_sl_session_[last_trade.zone_id] = session_date;
                    LOG_DEBUG("[RE-ENTRY GUARD] Zone " << last_trade.zone_id
                             << " blocked for rest of session " << session_date
                             << " after " << last_trade.exit_reason
                             << " exit (P&L=" << last_trade.pnl << ")");
                }
            }
        }
    }

    // Handle pause after consecutive losses
    if (pause_counter > 0) {
        pause_counter--;
        if (pause_counter == 0) {
            LOG_INFO("Resuming trading after pause");
        }
        return;
    }

    // Check for entry opportunities (zone states already updated above)
    check_for_entries(bar, bar_index);
}

void BacktestEngine::run(int duration_minutes) {
    LOG_INFO("=================================================");
    LOG_INFO("  Starting backtest on " << bars.size() << " bars");
    LOG_INFO("=================================================");
    
    // Use shared zone initialization (same as live engine)
    active_zones = Core::ZoneInitializer::detect_initial_zones(bars, detector, next_zone_id_);

    initial_zone_detection_summary_ = detector.get_last_detection_summary();
    total_zones_created_ += initial_zone_detection_summary_.created;
    total_zones_merged_ += initial_zone_detection_summary_.merged;
    total_zones_skipped_ += initial_zone_detection_summary_.skipped;
    
    LOG_INFO("Found " << active_zones.size() << " zones across full dataset");
    
    // ⭐ FIX 2: Score each zone against bars[zone.formation_bar], not bars.back().
    // bars.back() = final bar of entire dataset — all zones scored against Mar-2026
    // regardless of when they formed. Live scores each zone when it is detected
    // (scorer.evaluate_zone(zone, regime, bar_history.back()) where back() = detection bar).
    // Fix: use bars[zone.formation_bar] so zone age, ATR, and distance components
    // are computed with the same market context the live engine would have used.
    LOG_INFO("Scoring " << active_zones.size() << " zones at formation bar context...");
    int zones_scored = 0;
    for (auto& zone : active_zones) {
        int score_bar_idx = zone.formation_bar;
        if (score_bar_idx < 0 || score_bar_idx >= static_cast<int>(bars.size())) {
            LOG_WARN("Zone " << zone.zone_id << " has invalid formation_bar=" << zone.formation_bar
                     << " (bars.size()=" << bars.size() << ") — clamping to last bar");
            score_bar_idx = static_cast<int>(bars.size()) - 1;
        }
        const Core::Bar& scoring_bar = bars[score_bar_idx];
        // ⭐ LOOKAHEAD FIX: pass score_bar_idx (= zone.formation_bar) so regime is
        // computed at the bar the zone formed on, not at bars.back() (future data).
        Core::MarketRegime zone_regime = Core::MarketAnalyzer::detect_regime(
            bars, 50, 5.0, score_bar_idx);
        zone.zone_score = scorer.evaluate_zone(zone, zone_regime, scoring_bar);
        zones_scored++;
        LOG_DEBUG("Zone " << zone.zone_id
                 << " scored at bar " << score_bar_idx
                 << " (" << scoring_bar.datetime << ")"
                 << ": total=" << zone.zone_score.total_score
                 << " (" << zone.zone_score.entry_rationale << ")");
    }
    LOG_INFO("Scored " << zones_scored << " zones (avg: "
             << (zones_scored > 0 ? (std::accumulate(active_zones.begin(), active_zones.end(), 0.0,
                [](double sum, const Core::Zone& z) { return sum + z.zone_score.total_score; }) / zones_scored) : 0.0)
             << ")");
    
    // NOTE: Unlike live engine, backtest doesn't need historical replay because
    // the forward processing loop (process_bar) will update all zone states anyway
    // as it simulates trading through each bar
    
    // ⭐ RC1 FIX (BT complement): Reset touch_count and state before the bar loop.
    //
    // ZoneDetector::calculate_initial_touch_count() pre-populates zone.touch_count
    // using a LOOSER formula (50% zone-height extension past distal). For Z33 (SUPPLY
    // 26010-26019), this preset was 2859 — the detector counted 2859 near-zone touches
    // using its extended formula across bars 4285..9157.
    //
    // Without this reset, update_zone_states starts with touch_count=2859 (FRESH state).
    // The first bar that overlaps the zone via the STRICT formula fires FIRST_TOUCH
    // → touch_count becomes 2860. The final BT count (2860) is dominated by the
    // detector's looser pre-count, not by the strict-formula episode tracking.
    //
    // LT replay already resets to 0/FRESH (RC1 fix applied earlier) and correctly
    // counts 57 strict-formula episodes for Z33. BT must do the same to match.
    //
    // Reset here (after scoring, before bar loop) so:
    //   - zone_score is already computed (scoring is unaffected)
    //   - update_zone_states starts each zone from 0/FRESH at bar formation_bar
    //   - was_in_zone_prev is false (default struct value) — no extra reset needed
    //     since no bars have been processed yet at this point
    for (auto& zone : active_zones) {
        zone.touch_count = 0;
        zone.state       = Core::ZoneState::FRESH;
        // was_in_zone_prev stays false (struct default) — correct starting state
    }
    LOG_INFO("Reset touch_count=0 and state=FRESH for " << active_zones.size()
             << " zones before bar loop (detector preset discarded)");

    std::cout << "\nProcessing bars..." << std::endl;
    
    // Process each bar
    for (size_t i = 0; i < bars.size(); i++) {
        process_bar(bars[i], i);
        
        // Progress indicator (reduced frequency for better performance)
        if (i % 5000 == 0 && i > 0) {
            std::cout << "Progress: " << i << "/" << bars.size() 
                     << " (" << (i * 100 / bars.size()) << "%)" << std::endl;
        }
    }
    
    // Close any open position at end
    if (trade_manager.is_in_position()) {
        Trade closed_trade = trade_manager.close_trade(
            bars.back(), "EOD", bars.back().close);
        performance.add_trade(closed_trade);
        performance.update_capital(trade_manager.get_capital());
        
        LOG_INFO("Closed open position at end of data");
    }
    
    // Batch save zones once at end of backtest (performance optimization)
    LOG_INFO("Saving final zone state...");
    if (zone_persistence_.save_zones(active_zones, next_zone_id_)) {
        LOG_INFO("Zones saved: " << active_zones.size() << " zones");
    } else {
        LOG_WARN("Failed to save zones");
    }

    write_zone_detection_summary();
    
    LOG_INFO("=================================================");
    LOG_INFO("  Backtest complete");
    LOG_INFO("=================================================");
}

void BacktestEngine::export_results() {
    LOG_INFO("Exporting results to CSV...");
    
    try {
        // ⭐ FIX: Delete output files before writing.
        //
        // ROOT CAUSE OF DUPLICATE TRADES:
        // CSVReporter::export_trades() opens its file with ios::app (append mode).
        // If a trades.csv from a prior run exists in the output directory,
        // the new run's trades are appended after the old ones — producing
        // duplicates where the old run's trades appear first, followed by the
        // current run's trades starting from bar 1 again.
        //
        // The symptom: trades #1-5 are identical to trades #6-10 (the first 5
        // trades of the current run duplicated from the prior run's file).
        // Reported P&L was inflated by exactly the prior run's P&L.
        //
        // FIX: Explicitly delete all three output files before calling CSVReporter.
        // This is safe because CSVReporter writes all trades from the in-memory
        // performance tracker in a single pass — no incremental appending is needed.
        // write_zone_detection_summary() already uses ios::trunc correctly.
        //
        // Note: if csv_reporter.cpp is ever fixed to use ios::trunc, this
        // pre-delete becomes a harmless no-op (deleting a non-existent file
        // is silently ignored by std::filesystem::remove).
        for (const std::string& filename : {"trades.csv", "metrics.csv", "equity_curve.csv"}) {
            fs::path filepath = fs::path(output_dir_) / filename;
            if (fs::exists(filepath)) {
                std::error_code ec;
                fs::remove(filepath, ec);
                if (ec) {
                    LOG_WARN("Could not delete stale output file: " << filepath.string()
                             << " (" << ec.message() << ") — results may contain duplicates");
                } else {
                    LOG_DEBUG("Deleted stale output file: " << filepath.string());
                }
            }
        }

        CSVReporter reporter(output_dir_);
        const auto& trades = get_trades();
        PerformanceMetrics metrics = performance.calculate_metrics();
        metrics.total_bars = bars.size();
        
        // Export trade log
        bool trades_ok = reporter.export_trades(trades, "trades.csv");
        if (trades_ok) {
            std::cout << "  ✓ Trade log:    " << output_dir_ << "/trades.csv" << std::endl;
        }
        
        // Export performance metrics
        bool metrics_ok = reporter.export_metrics(metrics, "metrics.csv");
        if (metrics_ok) {
            std::cout << "  ✓ Performance:  " << output_dir_ << "/metrics.csv" << std::endl;
        }
        
        // Export equity curve
        bool equity_ok = reporter.export_equity_curve(
            trades, config.starting_capital, "equity_curve.csv");
        if (equity_ok) {
            std::cout << "  ✓ Equity curve: " << output_dir_ << "/equity_curve.csv" << std::endl;
        }
        
        // Display zone file location
        std::cout << "  ✓ Zones file:   " << zone_persistence_.get_zone_file_path() << std::endl;
        
        std::cout << std::endl;
        
        // Display performance summary
        performance.print_summary();
        
        // Display zone statistics
        print_zone_statistics();
        
    } catch (const std::exception& e) {
        LOG_ERROR("Export error: " << e.what());
    }
}

void BacktestEngine::write_zone_detection_summary() const {
    std::string output_path = output_dir_ + "/zone_detection_summary.txt";
    std::ofstream file(output_path, std::ios::out | std::ios::trunc);
    if (!file.is_open()) {
        LOG_WARN("Failed to write zone detection summary to " << output_path);
        return;
    }

    file << "ZONE DETECTION SUMMARY" << std::endl;
    if (!bars.empty()) {
        file << "Last bar datetime: " << bars.back().datetime << std::endl;
    }
    file << std::endl;
    file << "Initial detection:" << std::endl;
    file << "  created=" << initial_zone_detection_summary_.created
         << ", merged=" << initial_zone_detection_summary_.merged
         << ", skipped=" << initial_zone_detection_summary_.skipped
         << ", detected=" << initial_zone_detection_summary_.detected << std::endl;
    file << "Totals (initial + incremental):" << std::endl;
    file << "  created=" << total_zones_created_
         << ", merged=" << total_zones_merged_
         << ", skipped=" << total_zones_skipped_ << std::endl;
    file << "Active zones: " << active_zones.size() << std::endl;

    file.close();
    LOG_DEBUG("Zone detection summary saved: " << output_path);
}

void BacktestEngine::print_zone_statistics() const {
    std::cout << "\n==================================================" << std::endl;
    std::cout << "  ZONE STATISTICS" << std::endl;
    std::cout << "==================================================" << std::endl;
    
    // Count zones by state
    int total_zones = active_zones.size();
    int fresh_zones = 0;
    int tested_zones = 0;
    int violated_zones = 0;
    int reclaimed_zones = 0;
    int demand_zones = 0;
    int supply_zones = 0;
    
    double total_touches = 0;
    int zones_with_touches = 0;
    int swept_zones = 0;
    
    for (const auto& zone : active_zones) {
        // Count by state
        switch (zone.state) {
            case Core::ZoneState::FRESH:
                fresh_zones++;
                break;
            case Core::ZoneState::TESTED:
                tested_zones++;
                break;
            case Core::ZoneState::VIOLATED:
                violated_zones++;
                break;
            case Core::ZoneState::RECLAIMED:
                reclaimed_zones++;
                break;
        }
        
        // Count by type
        if (zone.type == Core::ZoneType::DEMAND) {
            demand_zones++;
        } else {
            supply_zones++;
        }
        
        // Touch statistics
        if (zone.touch_count > 0) {
            total_touches += zone.touch_count;
            zones_with_touches++;
        }
        
        // Sweep statistics
        if (zone.was_swept) {
            swept_zones++;
        }
    }
    
    double avg_touches = (zones_with_touches > 0) ? (total_touches / zones_with_touches) : 0.0;
    
    std::cout << std::fixed << std::setprecision(2);
    
    std::cout << "\nZONE COUNTS:" << std::endl;
    std::cout << "  Total Zones:     " << total_zones << std::endl;
    std::cout << "  Demand Zones:    " << demand_zones << std::endl;
    std::cout << "  Supply Zones:    " << supply_zones << std::endl;
    
    std::cout << "\nZONE STATES:" << std::endl;
    std::cout << "  Fresh:           " << fresh_zones 
              << " (" << (total_zones > 0 ? (fresh_zones * 100.0 / total_zones) : 0.0) << "%)" << std::endl;
    std::cout << "  Tested:          " << tested_zones 
              << " (" << (total_zones > 0 ? (tested_zones * 100.0 / total_zones) : 0.0) << "%)" << std::endl;
    std::cout << "  Violated:        " << violated_zones 
              << " (" << (total_zones > 0 ? (violated_zones * 100.0 / total_zones) : 0.0) << "%)" << std::endl;
    std::cout << "  Reclaimed:       " << reclaimed_zones 
              << " (" << (total_zones > 0 ? (reclaimed_zones * 100.0 / total_zones) : 0.0) << "%)" << std::endl;
    
    std::cout << "\nZONE ACTIVITY:" << std::endl;
    std::cout << "  Zones Touched:   " << zones_with_touches << std::endl;
    std::cout << "  Total Touches:   " << static_cast<int>(total_touches) << std::endl;
    std::cout << "  Avg Touches:     " << avg_touches << std::endl;
    std::cout << "  Zones Swept:     " << swept_zones 
              << " (" << (total_zones > 0 ? (swept_zones * 100.0 / total_zones) : 0.0) << "%)" << std::endl;
    
    // Calculate zones per bar (density)
    if (bars.size() > 0) {
        double zones_per_100_bars = (total_zones * 100.0) / bars.size();
        std::cout << "\nZONE DENSITY:" << std::endl;
        std::cout << "  Total Bars:      " << bars.size() << std::endl;
        std::cout << "  Zones/100 Bars:  " << zones_per_100_bars << std::endl;
    }
    
    std::cout << "\n==================================================" << std::endl;
}

void BacktestEngine::stop() {
    LOG_INFO("Stopping BacktestEngine...");
    
    // Export all results
    export_results();
    
    LOG_INFO("BacktestEngine stopped");
}

std::optional<Core::Zone> BacktestEngine::detect_zone_flip(const Core::Zone& violated_zone, int bar_index) {
    // Look back for consolidation before the breakdown
    int lookback_start = std::max(0, bar_index - config.flip_lookback_bars);
    
    if (lookback_start >= bar_index - 1) {
        return std::nullopt;  // Not enough bars
    }
    
    // Find consolidation period before breakdown
    double flip_base_low = bars[lookback_start].low;
    double flip_base_high = bars[lookback_start].high;
    
    for (int i = lookback_start; i < bar_index; i++) {
        flip_base_low = std::min(flip_base_low, bars[i].low);
        flip_base_high = std::max(flip_base_high, bars[i].high);
    }
    
    // Check if breakdown is vigorous enough
    if (bar_index + 5 >= static_cast<int>(bars.size())) {
        return std::nullopt;  // Not enough bars ahead
    }
    
    double price_at_breakdown = bars[bar_index].close;
    double price_after_impulse = bars[bar_index + 5].close;
    double impulse_move = std::abs(price_after_impulse - price_at_breakdown);
    double atr = Core::MarketAnalyzer::calculate_atr(bars, config.atr_period, bar_index);
    
    if (impulse_move < config.flip_min_impulse_atr * atr) {
        return std::nullopt;  // Impulse too weak
    }
    
    // Create flipped zone (opposite type)
    Core::Zone flipped;
    flipped.type = (violated_zone.type == Core::ZoneType::DEMAND) ? 
                   Core::ZoneType::SUPPLY : Core::ZoneType::DEMAND;
    flipped.base_low = flip_base_low;
    flipped.base_high = flip_base_high;
    flipped.distal_line = (flipped.type == Core::ZoneType::DEMAND) ? flip_base_low : flip_base_high;
    flipped.proximal_line = (flipped.type == Core::ZoneType::DEMAND) ? flip_base_high : flip_base_low;
    flipped.formation_bar = lookback_start;
    flipped.formation_datetime = bars[lookback_start].datetime;
    flipped.state = Core::ZoneState::FRESH;
    flipped.touch_count = 0;
    flipped.is_flipped_zone = true;
    flipped.parent_zone_id = violated_zone.zone_id;
    
    // Calculate strength
    double range = flip_base_high - flip_base_low;
    if (atr > 0.0001) {
        double ratio = range / atr;
        flipped.strength = 100.0 * (1.0 - std::min(ratio / config.max_consolidation_range, 1.0));
    } else {
        flipped.strength = 0.0;
    }
    
    // Only return if strength is acceptable
    if (flipped.strength < config.min_zone_strength) {
        return std::nullopt;
    }
    
    return flipped;
}

const std::vector<Trade>& BacktestEngine::get_trades() const {
    return performance.get_trades();
}

const PerformanceTracker& BacktestEngine::get_performance() const {
    return performance;
}

} // namespace Backtest
} // namespace SDTrading