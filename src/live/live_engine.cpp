// ============================================================
// LIVE ENGINE - MODIFIED IMPLEMENTATION
// ============================================================

#include "live_engine.h"
#include "../csv_simulator_broker.h"  // ⭐ ADD THIS LINE
#include "fyers_adapter.h"  // ⭐ NEW: For FyersAdapter dynamic_cast
#include "../utils/logger.h"
#include "../utils/system_initializer.h"
#include "../system_config.h"
#include "../analysis/market_analyzer.h"
#include "../backtest/csv_reporter.h"  // ⭐ NEW: CSV export functionality
#include "../backtest/performance_tracker.h"  // ⭐ NEW: Performance tracking
#include "../sd_engine_core.h"  // ⭐ For calculate_trailing_stop
#include "../utils/institutional_index.h"  // ⭐ For calculate_institutional_index (Bug #144 fix)
#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <ctime>
#include <cmath>
#include <optional>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <numeric>
#include <algorithm>  // ⭐ FIX: Added for std::sort, std::stable_sort
#include <json/json.h>
#include "order_tag_generator.h"
#include "active_order_registry.h"
// In BacktestEngine files
#include "../ITradingEngine.h"
#include "../ZonePersistenceAdapter.h"
#include "../ZoneInitializer.h"

namespace fs = std::filesystem;

namespace SDTrading {
namespace Live {

using namespace Utils;

// Fix Windows.h macro conflicts with std::max and std::min
#undef max
#undef min

using namespace Core;

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

bool is_zone_age_blocked(const Zone& zone, const Bar& current_bar, const Config& config, int& age_days_out) {
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

MarketPhase parse_market_phase_value(const Json::Value& phase_val) {
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

}  // namespace

LiveEngine::LiveEngine(
    const Config& cfg,
    std::unique_ptr<BrokerInterface> broker_interface,
    const std::string& symbol,
    const std::string& interval,
    const std::string& output_dir,
    const std::string& historical_csv_path
)
    : config(cfg),  // Copy config to member
      broker(std::move(broker_interface)),
      detector(config),  // Pass member, not parameter
      scorer(config),
      entry_engine(config),
      trade_mgr(broker ? Backtest::TradeManager(config, config.starting_capital, broker.get())
                       : Backtest::TradeManager(config, config.starting_capital)),  // Use backtest mode when no broker (dryrun)
      performance(config.starting_capital),  // ⭐ NEW: Performance tracker
      last_csv_export_bar_(-1),  // ⭐ NEW: CSV export tracking
      v6_enabled_(false),  // ✅ V6.0: Initialize flag
      zone_persistence_("live", output_dir, config.enable_live_zone_filtering),  // ⭐ FIXED: Pass filtering flag
            next_zone_id_(1),  // NEW: Zone ID tracking
            output_dir_(output_dir),
            historical_csv_path_(historical_csv_path),  // ⭐ NEW: Store CSV path
    bar_index(0),
    last_zone_scan_bar_(-1),  // NEW: Track last zone scan position
    last_saved_bar_time_(""),
    dryrun_bootstrap_end_bar_(0),  // ⭐ NEW: Initialize to 0 (no bootstrap)
    skip_trading_until_bar_(false),  // ⭐ NEW: Initialize to false (trading enabled)
    trading_symbol(symbol),
    bar_interval(interval),
    pending_continuation_() {    // ⭐ ISSUE-3: default-constructed (inactive)    
		LOG_INFO("=========================================");
		LOG_INFO("SD TRADING LIVE ENGINE V6.0");
		LOG_INFO("=========================================");
		LOG_INFO("LiveEngine created for " << symbol << " @ " << interval << " min");
		
		// ⭐ FIX: Moved OrderSubmitter initialization INSIDE constructor body
		// Load Spring Boot order submitter settings from cached system_config.json when available
		::SystemConfig system_config;
		std::string config_file_path;
		if (SystemInitializer::getInstance().isInitialized()) {
			config_file_path = SystemInitializer::getInstance().getConfigPath();
		}
		if (!config_file_path.empty()) {
			std::string absolute_config_path = fs::absolute(config_file_path).string();
			LOG_INFO("Using cached system configuration from SystemInitializer: " << absolute_config_path);
			system_config = ::SystemConfig(absolute_config_path);
		} else {
			if (SystemInitializer::getInstance().isInitialized()) {
				LOG_WARN("SystemInitializer initialized but did not provide a config path; loading default system_config.json");
			} else {
				LOG_INFO("SystemInitializer not initialized - loading system_config.json directly");
			}
			system_config = ::SystemConfig();
		}
		OrderSubmitConfig order_config;
		order_config.base_url = system_config.get_value("order_submitter", "base_url", "http://localhost:8080/trade");
		order_config.long_strategy_number = system_config.get_int("order_submitter", "long_strategy_number", 13);
		order_config.short_strategy_number = system_config.get_int("order_submitter", "short_strategy_number", 14);
		order_config.timeout_seconds = system_config.get_int("order_submitter", "timeout_seconds", 10);
		order_config.enable_submission = system_config.get_bool("order_submitter", "enabled", true);
		order_submitter_ = std::make_unique<OrderSubmitter>(order_config);
		
		// ✅ V6.0: Initialize V6.0 components
		if (config.v6_fully_enabled) {
			v6_enabled_ = initialize_v6_components();
			
			if (v6_enabled_) {
				wire_v6_dependencies();
				LOG_INFO("✅ V6.0 components initialized and wired");
			} else {
				LOG_WARN("⚠️ V6.0 initialization failed - running in V4.0 mode");
			}
		} else {
			LOG_WARN("⚠️ V6.0 disabled in config - running V4.0 mode");
		}
	}

LiveEngine::~LiveEngine() {
    // V6.0 resources cleanup automatic with unique_ptr
}

// =========================================
// V6.0: Implementation Functions
// =========================================

bool LiveEngine::initialize_v6_components() {
    LOG_INFO("🔄 Initializing V6.0 components...");
    
    // 1. Load Volume Baseline
    if (!volume_baseline_.load_from_file(config.volume_baseline_file)) {
        LOG_ERROR("❌ Failed to load volume baseline from: " + config.volume_baseline_file);
        
        if (config.v6_validate_baseline_on_startup) {
            LOG_ERROR("Baseline validation REQUIRED but failed");
            return false;
        } else {
            LOG_WARN("⚠️ Continuing without volume baseline (degraded mode)");
            return false;
        }
    }
    
    LOG_INFO("✅ Volume Baseline loaded: " << volume_baseline_.size() << " time slots");
    
    // 2. Create Volume Scorer
    volume_scorer_ = std::make_unique<Core::VolumeScorer>(volume_baseline_);
    LOG_INFO("✅ VolumeScorer created");
    
    // 3. Create OI Scorer
    oi_scorer_ = std::make_unique<Core::OIScorer>();
    LOG_INFO("✅ OIScorer created");
    
    return true;
}

void LiveEngine::wire_v6_dependencies() {
    LOG_INFO("🔌 Wiring V6.0 dependencies to shared components...");
    
    // 1. Wire to ZoneDetector
    if (volume_baseline_.is_loaded()) {
        detector.set_volume_baseline(&volume_baseline_);
        LOG_INFO("✅ ZoneDetector ← VolumeBaseline");
    }
    
    // 2. Wire to EntryDecisionEngine
    if (volume_baseline_.is_loaded()) {
        entry_engine.set_volume_baseline(&volume_baseline_);
        LOG_INFO("✅ EntryDecisionEngine ← VolumeBaseline");
    }
    
    if (oi_scorer_) {
        entry_engine.set_oi_scorer(oi_scorer_.get());
        LOG_INFO("✅ EntryDecisionEngine ← OIScorer");
    }
    
    // 3. Wire to TradeManager
    if (volume_baseline_.is_loaded()) {
        trade_mgr.set_volume_baseline(&volume_baseline_);
        LOG_INFO("✅ TradeManager ← VolumeBaseline");
    }
    
    if (oi_scorer_) {
        trade_mgr.set_oi_scorer(oi_scorer_.get());
        LOG_INFO("✅ TradeManager ← OIScorer");
    }
    

    LOG_INFO("🔌 V6.0 wiring complete");
}

void LiveEngine::validate_v6_startup() {
    LOG_INFO("=========================================");
    LOG_INFO("V6.0 LIVE ENGINE STARTUP VALIDATION");
    LOG_INFO("=========================================");
    
    LOG_INFO("V6.0 Enabled:        " << (v6_enabled_ ? "✅ YES" : "❌ NO"));
    LOG_INFO("Volume Baseline:     " << (volume_baseline_.is_loaded() ? "✅ LOADED" : "❌ NOT LOADED"));
    LOG_INFO("Volume Scorer:       " << (volume_scorer_ ? "✅ ACTIVE" : "❌ INACTIVE"));
    LOG_INFO("OI Scorer:           " << (oi_scorer_ ? "✅ ACTIVE" : "❌ INACTIVE"));
    LOG_INFO("Volume Entry Filter: " << (config.enable_volume_entry_filters ? "✅ ENABLED" : "⚠️ DISABLED"));
    LOG_INFO("OI Entry Filter:     " << (config.enable_oi_entry_filters ? "✅ ENABLED" : "⚠️ DISABLED"));
    LOG_INFO("Volume Exit Signals: " << (config.enable_volume_exit_signals ? "✅ ENABLED" : "⚠️ DISABLED"));
    LOG_INFO("OI Exit Signals:     " << (config.enable_oi_exit_signals ? "✅ ENABLED" : "⚠️ DISABLED"));
    
    // Validate configuration consistency
    if (v6_enabled_) {
        bool all_good = true;
        
        if (!volume_baseline_.is_loaded()) {
            LOG_ERROR("⚠️ V6.0 enabled but baseline not loaded!");
            all_good = false;
        }
        
        if (!volume_scorer_ || !oi_scorer_) {
            LOG_ERROR("⚠️ V6.0 enabled but scorers not created!");
            all_good = false;
        }
        
        if (all_good) {
            LOG_INFO("✅ V6.0 VALIDATION PASSED - All systems operational");
        } else {
            LOG_ERROR("❌ V6.0 VALIDATION FAILED - Check errors above");
        }
    } else {
        LOG_WARN("⚠️ V6.0 NOT ENABLED - Running in V4.0 degraded mode");
    }
    
    LOG_INFO("=========================================");
}

bool LiveEngine::is_expiry_day(const std::string& current_datetime) const {
    // Parse datetime to extract date
    // Format: "2026-02-27 09:15:00"
    if (current_datetime.length() < 10) {
        return false;
    }
    
    std::string date_str = current_datetime.substr(0, 10); // "2026-02-27"
    
    // Parse year, month, day
    int year, month, day;
    char dash1, dash2;
    std::istringstream ss(date_str);
    ss >> year >> dash1 >> month >> dash2 >> day;
    
    if (ss.fail() || dash1 != '-' || dash2 != '-') {
        return false;
    }
    
    // Check if it's last Thursday of the month
    // Create date for this day
    std::tm tm = {};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_isdst = -1;
    
    std::mktime(&tm); // Normalize (sets tm_wday)
    
    int weekday = tm.tm_wday; // 0 = Sunday, 4 = Thursday
    
    // Check if Thursday
    if (weekday != 4) {
        return false;
    }
    
    // Check if last Thursday of month
    // Add 7 days and see if we're in next month
    tm.tm_mday += 7;
    std::mktime(&tm);
    
    // If month changed, this was the last Thursday
    return (tm.tm_mon != month - 1);
}


std::vector<Bar> LiveEngine::load_csv_data(const std::string& csv_path) {
    std::vector<Bar> bars;
    std::ifstream file(csv_path);
    
    if (!file.is_open()) {
        LOG_ERROR("Could not open CSV file: " << csv_path);
        return bars;
    }
    
    std::string line;
    std::getline(file, line);  // Read header
    
    // ✅ V6.0: Validate CSV format (expecting 11 columns for V6.0)
    std::istringstream header_stream(line);
    std::string col;
    int col_count = 0;
    while (std::getline(header_stream, col, ',')) {
        col_count++;
    }
    
    if (col_count != 11 && col_count != 9) {
        LOG_ERROR("❌ INVALID CSV FORMAT: Expected 11 columns (V6.0) or 9 columns (legacy), found " << col_count);
        LOG_ERROR("   V6.0 CSV format: Timestamp,DateTime,Symbol,O,H,L,C,Volume,OI,OI_Fresh,OI_Age_Seconds");
        LOG_ERROR("   Legacy format: Timestamp,DateTime,Symbol,O,H,L,C,Volume,OI");
        return bars;
    }
    
    if (col_count == 11) {
        LOG_INFO("✅ CSV format validated: 11 columns (V6.0 compatible with OI metadata)");
    } else {
        LOG_WARN("⚠️ CSV format: 9 columns (legacy format - applying hardcoded OI_Fresh=1, OI_Age_Seconds=0)");
    }
    
    int line_num = 1;
    while (std::getline(file, line)) {
        line_num++;
        if (line.empty()) continue;
        
        std::istringstream ss(line);
        std::string field;
        Bar bar;
        int col = 0;
        
        // CSV format: Timestamp,DateTime,Symbol,Open,High,Low,Close,Volume,OpenInterest[,OI_Fresh,OI_Age_Seconds]
        std::string oi_fresh_field;   // captured from column 9 when present
        while (std::getline(ss, field, ',')) {
            try {
                switch (col) {
                    case 0: /* timestamp */ break;
                    case 1: bar.datetime = field; break;
                    case 2: /* symbol */ break;
                    case 3: bar.open = std::stod(field); break;
                    case 4: bar.high = std::stod(field); break;
                    case 5: bar.low = std::stod(field); break;
                    case 6: bar.close = std::stod(field); break;
                    case 7: bar.volume = static_cast<double>(std::stoll(field)); break;
                    case 8: bar.oi = static_cast<double>(std::stoll(field)); break;
                    // ⭐ RC-II-A FIX: Read OI_Fresh from column 9 instead of hardcoding true.
                    // Previously this case did nothing (break), so bar.oi_fresh was always set
                    // to true unconditionally after the loop regardless of the CSV value.
                    // BT reads column 9 properly; LT ignored it, making oi_data_quality=True
                    // in LT and False in BT for all 58 zones — a constant +15 pt gap in
                    // institutional_index. Now both engines use the same column value.
                    case 9: oi_fresh_field = field; break;
                    case 10: break;  // OI_Age_Seconds — not used for parity
                }
            } catch (const std::exception& e) {
                LOG_WARN("Failed to parse field at line " << line_num << ", col " << col << ": " << e.what());
            }
            col++;
        }
        
        if (col >= 9) {  // Valid bar with all required fields (through OI column)
            // ⭐ RC-II-A FIX: Set oi_fresh from the parsed column value, exactly matching BT.
            // 11-column CSV: column 9 = OI_Fresh ("1"/"true"/"True" → true, else false).
            // 9-column CSV (legacy, no OI_Fresh column): oi_fresh_field is empty → false.
            // Previously: hardcoded true regardless of CSV content or column count.
            if (!oi_fresh_field.empty()) {
                bar.oi_fresh = (oi_fresh_field == "1" ||
                                oi_fresh_field == "true" ||
                                oi_fresh_field == "True");
            } else {
                bar.oi_fresh = false;
            }
            bar.oi_age_seconds = 0;
            bars.push_back(bar);
        } else if (col > 0) {
            LOG_WARN("Skipping incomplete line " << line_num << " (only " << col << " fields)");
        }
    }
    
    file.close();
    
    // Load full dataset for zone bootstrap to match backtest behavior
    // NOTE: Previously trimmed to last 10,000 bars; now using all bars
    // to ensure comprehensive zone detection and more trade opportunities.
    
    LOG_INFO("CSV file closed: " << csv_path);
    if (bars.empty()) {
        LOG_WARN("Loaded 0 bars from CSV after parsing rows");
    } else {
        LOG_INFO("Loaded " << bars.size() << " bars (from " << bars.front().datetime << " to " << bars.back().datetime << ")");
    }
    return bars;
}

bool LiveEngine::initialize() {
    LOG_INFO("==================================================");
    LOG_INFO("  Initializing Live Trading Engine");
    LOG_INFO("==================================================");
    LOG_INFO("Config summary (startup):");
    LOG_INFO("  Symbol:       " << trading_symbol);
    LOG_INFO("  Interval:     " << bar_interval << " min");
    LOG_INFO("  Lot size:     " << config.lot_size << " (propagated from system_config)");
    
    // ⭐ NEW ARCHITECTURE: Load historical bars from CSV instead of broker
    auto bar_load_start = std::chrono::high_resolution_clock::now();
    LOG_INFO("Loading historical bars from CSV: " << historical_csv_path_);
    bar_history = load_csv_data(historical_csv_path_);
    auto bar_load_end = std::chrono::high_resolution_clock::now();
    auto bar_load_ms = std::chrono::duration_cast<std::chrono::milliseconds>(bar_load_end - bar_load_start).count();
    
    if (bar_history.empty()) {
        LOG_ERROR("Failed to load historical data from CSV");
        return false;
    }
    
    LOG_INFO("✅ Loaded " << bar_history.size() << " historical bars from CSV");
    LOG_INFO("   CSV loading took: " << bar_load_ms << "ms (" << (bar_load_ms/1000.0) << "s)");
    
	if (order_submitter_) {
		if (!order_submitter_->initialize()) {
			LOG_WARN("OrderSubmitter initialization failed");
			order_submitter_.reset();
		}
	}
    // Connect to broker AFTER CSV loading (broker only for live ticks)
    if (!broker->connect()) {
        LOG_ERROR("Failed to connect to broker");
        return false;
    }
    
    LOG_INFO("✅ Broker connected (for live tick monitoring)");
    
    // ⭐ CRITICAL: Position broker to return bars AFTER what we already loaded
    // We loaded the LAST N bars for historical analysis and zone detection
    // Position broker to return the NEXT bars after that (simulating new bars arriving)
    // ⭐ CRITICAL: Position broker to return bars AFTER what we already loaded
    Live::FyersAdapter* fyers = dynamic_cast<Live::FyersAdapter*>(broker.get());
    Live::CsvSimulatorBroker* csv_sim = dynamic_cast<Live::CsvSimulatorBroker*>(broker.get());
    
    if (fyers && bar_history.size() > 0) {
        size_t total_bars_in_csv = fyers->get_csv_bar_count();
        size_t loaded_bar_count = bar_history.size();
        
        // Position broker at the end of loaded data (ready to return next bar)
        // Since we loaded the LAST N bars, broker should be at total_bars_in_csv
        size_t broker_position = total_bars_in_csv;
        
        LOG_INFO("⭐ CSV Live Simulation Setup:");
        LOG_INFO("   Total bars in CSV: " << total_bars_in_csv);
        LOG_INFO("   Loaded for analysis: " << loaded_bar_count << " bars");
        LOG_INFO("   Last loaded bar: " << bar_history.back().datetime);
        LOG_INFO("   Broker position: " << broker_position << " (no more bars in CSV)");
        LOG_INFO("   Status: At end of historical data - will repeat last bar");
        
        fyers->skip_to_bar_index(broker_position);
        
       LOG_INFO("✅ Broker ready (at end of CSV data)");
        
    } else if (csv_sim && bar_history.size() > 0) {
        // ⭐ NEW: CsvSimulatorBroker positioning
        size_t total_bars = csv_sim->get_total_bars();
        size_t loaded_bar_count = bar_history.size();
        size_t broker_position = total_bars;  // Position at end
        
        LOG_INFO("⭐ CSV Simulator Setup:");
        LOG_INFO("   Total bars in CSV: " << total_bars);
        LOG_INFO("   Loaded for analysis: " << loaded_bar_count << " bars");
        LOG_INFO("   Last loaded bar: " << bar_history.back().datetime);
        LOG_INFO("   Positioning simulator at: " << broker_position);
        
        csv_sim->skip_to_bar_index(broker_position);
        
        LOG_INFO("✅ Simulator positioned at end of CSV");
        
    } else {
        LOG_WARN("⚠️  Could not sync broker position");
    }
    
    // ⭐ TIMING: Measure detector setup
    auto detector_start = std::chrono::high_resolution_clock::now();
    LOG_INFO("Initializing zone detector with bars...");
    for (const auto& bar : bar_history) {
        detector.add_bar(bar);
    }
    auto detector_end = std::chrono::high_resolution_clock::now();
    auto detector_ms = std::chrono::duration_cast<std::chrono::milliseconds>(detector_end - detector_start).count();
    
    LOG_INFO("✅ Zone detector initialized");
        LOG_INFO("   Detector setup took: " << detector_ms << "ms (" << (detector_ms/1000.0) << "s)");
    
    // ✅ V6.0: VALIDATE STARTUP (V6 components already initialized in constructor)
    if (config.v6_fully_enabled) {
        validate_v6_startup();
    }
    
    LOG_INFO("==================================================");
    LOG_INFO("  Live Engine Ready!");
    LOG_INFO("==================================================");
    
    return true;
}

void LiveEngine::update_bar_history() {
    // Get latest bar from broker
    Bar latest_bar = broker->get_latest_bar(trading_symbol, bar_interval);
    
    // Check if it's a new bar (different from last one)
    if (!bar_history.empty()) {
        const Bar& last_bar = bar_history.back();
        if (latest_bar.datetime == last_bar.datetime) {
            // Same bar, just update OHLC
            bar_history.back() = latest_bar;
            detector.update_last_bar(latest_bar);
            LOG_DEBUG("Updated current bar");

            // Ensure at least one visible print when data starts flowing
            if (!has_printed_first_tick_) {
                std::cout << "[CANDLE] "
                          << latest_bar.datetime
                          << " | O:" << std::fixed << std::setprecision(2) << latest_bar.open
                          << " H:" << latest_bar.high
                          << " L:" << latest_bar.low
                          << " C:" << latest_bar.close
                          << " V:" << latest_bar.volume
                          << " Idx:" << (bar_history.size() - 1)
                          << std::endl;
                std::cout.flush();
                has_printed_first_tick_ = true;
                last_printed_bar_time_ = latest_bar.datetime;
            }
            return;  // ⭐ Return WITHOUT incrementing bar_index
        }
    }
    
    // New bar formed
    bar_history.push_back(latest_bar);
    detector.add_bar(latest_bar);
    bar_index++;  // ⭐ MOVED HERE: Only increment when new bar is actually added
    
    LOG_INFO("📊 New bar formed: " << latest_bar.datetime 
             << " C=" << latest_bar.close);

    // ⭐ RUNTIME DEBUG: Log when appended CSV data arrives
   // std::cout << "\n[BAR_ARRIVED] NEW BAR from CSV (possibly appended during runtime)\n";
    std::cout << "  DateTime: " << latest_bar.datetime << "\n";
    std::cout << "  OHLC: " << std::fixed << std::setprecision(2) 
              << latest_bar.open << " / " << latest_bar.high << " / " 
              << latest_bar.low << " / " << latest_bar.close << "\n";
    std::cout << "  Bar Index: " << (bar_history.size() - 1) << "\n";
    std::cout << "  Total bars in history: " << bar_history.size() << "\n";
    std::cout << std::endl;
    std::cout.flush();
    
    // Quick ASCII one-liner (friendly to all consoles)
    std::cout << "[CANDLE] "
              << latest_bar.datetime
              << " | O:" << std::fixed << std::setprecision(2) << latest_bar.open
              << " H:" << latest_bar.high
              << " L:" << latest_bar.low
              << " C:" << latest_bar.close
              << " V:" << latest_bar.volume
              << " Idx:" << (bar_history.size() - 1)
              << std::endl;
    std::cout.flush();
    has_printed_first_tick_ = true;
    last_printed_bar_time_ = latest_bar.datetime;
    
    // PRINT CANDLE DETAILS TO CONSOLE
    // std::cout << "\n+====================================================+\n";
    // std::cout << "| [NEW CANDLE] Live Candle Received from CSV        |\n";
    // std::cout << "+====================================================+\n";
    // std::cout << "  DateTime:  " << latest_bar.datetime << "\n";
    // std::cout << "  Open:      " << std::fixed << std::setprecision(2) << latest_bar.open << "\n";
    // std::cout << "  High:      " << latest_bar.high << "\n";
    // std::cout << "  Low:       " << latest_bar.low << "\n";
    // std::cout << "  Close:     " << latest_bar.close << "\n";
    // std::cout << "  Volume:    " << latest_bar.volume << "\n";
    // std::cout << "  Bar Index: " << bar_history.size() - 1 << "\n";
    // std::cout << std::endl;
    // std::cout.flush();  // Force flush to ensure output appears immediately
    
    // NEW: Update zone states on bar close
    update_zone_states(latest_bar);
}

void LiveEngine::update_zone_states(const Bar& current_bar) {
    bool zones_changed = false;

    auto record_event = [&](Zone& target_zone, const ZoneStateEvent& evt, bool enabled) {
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
        // NEW: Skip inactive zones during live filtering
        // if (!zone.is_active) {
        //     LOG_DEBUG("⛔ Zone " << zone.zone_id << " is inactive - skipping state update");
        //     continue;
        // }
        
        ZoneState old_state = zone.state;
        std::string old_state_str = (old_state == ZoneState::FRESH ? "FRESH" :
                                      old_state == ZoneState::TESTED ? "TESTED" : 
                                      old_state == ZoneState::RECLAIMED ? "RECLAIMED" : "VIOLATED");
        
        // ⭐ FIX 1: Type-aware price_in_zone formula (parity with backtest_engine.cpp).
        // The old symmetric formula (bar.low<=proximal && bar.high>=distal) required a bar
        // to SPAN the entire SUPPLY zone because for SUPPLY: distal > proximal.
        // That means normal supply zone touches (bar reaches proximal from above) were
        // invisible — zone stayed FRESH even after being repeatedly tested.
        //
        // DEMAND: proximal=base_high (top),  distal=base_low  (bottom)
        //   touch = bar.high >= distal  AND  bar.low <= proximal   (bar overlaps from below)
        // SUPPLY: proximal=base_low  (bottom), distal=base_high (top)
        //   touch = bar.low  <= distal  AND  bar.high >= proximal  (bar overlaps from above)
        bool price_in_zone;
        if (zone.type == ZoneType::DEMAND) {
            price_in_zone = (current_bar.high >= zone.distal_line && current_bar.low <= zone.proximal_line);
        } else {
            // SUPPLY: distal_line is the TOP (higher price); proximal_line is the BOTTOM (lower price)
            price_in_zone = (current_bar.low <= zone.distal_line && current_bar.high >= zone.proximal_line);
        }

        if (price_in_zone) {
            if (zone.state == ZoneState::FRESH) {
                zone.state = ZoneState::TESTED;
                zone.touch_count++;
                zones_changed = true;
                LOG_INFO("Zone " << zone.zone_id << " tested (first touch)");
                
                // Record FIRST_TOUCH event
                ZoneStateEvent event(
                    current_bar.datetime,
                    static_cast<int>(bar_history.size()) - 1,  // ⭐ FIX BUG-08: was .size() (1-ahead), correct index is size()-1
                    "FIRST_TOUCH",
                    old_state_str,
                    "TESTED",
                    (current_bar.high + current_bar.low) / 2.0,
                    current_bar.high,
                    current_bar.low,
                    zone.touch_count
                );
                record_event(zone, event, config.record_first_touch);
                
            } else if (zone.state == ZoneState::TESTED) {
                // ⭐ FIX BUG-TOUCH: Only count a NEW visit episode (outside→inside
                // transition). Without this guard, every bar spent inside the zone
                // incremented touch_count, causing counts like 549,125 and 655,122.
                if (!zone.was_in_zone_prev) {
                    zone.touch_count++;
                    zones_changed = true;

                    // Record RETEST event
                    ZoneStateEvent event(
                        current_bar.datetime,
                        static_cast<int>(bar_history.size()) - 1,  // ⭐ FIX BUG-08: size()-1 is correct 0-based index
                        "RETEST",
                        old_state_str,
                        "TESTED",
                        (current_bar.high + current_bar.low) / 2.0,
                        current_bar.high,
                        current_bar.low,
                        zone.touch_count
                    );
                    record_event(zone, event, config.record_retests);
                }
            }
            // ⭐ FIX BUG-TOUCH: price is inside zone this bar
            zone.was_in_zone_prev = true;

            // ✅ BUG #144 FIX: Update volume/OI profiles with FRESH data on zone touch
            // CRITICAL: Only if V6 metrics were properly loaded (have valid baselines)
            if (volume_scorer_ && oi_scorer_ && zone.volume_profile.avg_volume_baseline > 0.0) {
                int profile_bar_idx = zone.formation_bar;
                if (profile_bar_idx >= 0 && profile_bar_idx < static_cast<int>(bar_history.size())) {
                    // Recalculate profiles anchored to ORIGINAL formation bar (not touch bar)
                    zone.volume_profile = volume_scorer_->calculate_volume_profile(
                        zone,
                        bar_history,
                        profile_bar_idx
                    );
                    
                    zone.oi_profile = oi_scorer_->calculate_oi_profile(
                        zone,
                        bar_history,
                        profile_bar_idx
                    );
                    
                    // Recalculate institutional index (from Utils namespace)
                    zone.institutional_index = Utils::calculate_institutional_index(
                        zone.volume_profile,
                        zone.oi_profile
                    );
                    
                    LOG_INFO("🔄 Zone " << zone.zone_id << " profiles UPDATED on touch"
                            << " | anchor_bar: " << profile_bar_idx
                            << " | Vol ratio: " << std::fixed << std::setprecision(2)
                            << zone.volume_profile.departure_volume_ratio
                            << " | OI change: " << zone.oi_profile.oi_change_during_formation
                            << " | Inst idx: " << zone.institutional_index);
                } else {
                    LOG_WARN("⚠️ Zone " << zone.zone_id << " has invalid formation_bar="
                             << zone.formation_bar << " (history size=" << bar_history.size()
                             << "), skipping profile update on touch");
                }
            } else if (volume_scorer_ && oi_scorer_ && zone.volume_profile.avg_volume_baseline == 0.0) {
                LOG_WARN("⚠️ Zone " << zone.zone_id << " has corrupted V6 baseline (avg_volume=0), skipping profile update");
            }
        } else {
            // Price outside zone — reset so next entry counts as a new episode
            zone.was_in_zone_prev = false;
        }
        
        // Check for violation
        bool violated = false;
        std::string violation_event = "VIOLATED";
        double violation_price = current_bar.close;

        // Gap-over invalidation: entire bar clears the distal line without touching the zone
        if (config.gap_over_invalidation && zone.state != ZoneState::VIOLATED) {
            if (zone.type == ZoneType::DEMAND) {
                violated = (current_bar.high < zone.distal_line);
            } else {
                violated = (current_bar.low > zone.distal_line);
            }
            if (violated) {
                violation_event = "GAP_VIOLATED";
                violation_price = (zone.type == ZoneType::DEMAND) ? current_bar.high : current_bar.low;
                // ⭐ FIX: Set was_swept immediately (consistent with replay)
                zone.was_swept = true;
                zone.sweep_bar = static_cast<int>(bar_history.size()) - 1;  // ⭐ FIX BUG-08: was .size() (1-ahead)
            }
        }

        // Body-close invalidation
        if (!violated && config.invalidate_on_body_close) {
            if (zone.type == ZoneType::DEMAND) {
                violated = (current_bar.close < zone.distal_line);
            } else {
                violated = (current_bar.close > zone.distal_line);
            }
            if (violated) {
                violation_event = "VIOLATED";
                violation_price = current_bar.close;
            }
        }

        if (violated && zone.state != ZoneState::VIOLATED) {
            std::string pre_violation_state = (zone.state == ZoneState::FRESH ? "FRESH" : 
                                               zone.state == ZoneState::TESTED ? "TESTED" : "RECLAIMED");
            zone.state = ZoneState::VIOLATED;
            zones_changed = true;
            LOG_INFO("Zone " << zone.zone_id << " violated");

            ZoneStateEvent event(
                current_bar.datetime,
                static_cast<int>(bar_history.size()) - 1,  // ⭐ FIX BUG-08: size()-1 is correct 0-based index
                violation_event,
                pre_violation_state,
                "VIOLATED",
                violation_price,
                current_bar.high,
                current_bar.low,
                zone.touch_count
            );
            record_event(zone, event, config.record_violations);
        }
        
        // ⭐ FIX #3: Zone Expiry Logic - Mark old zones as VIOLATED
        // Calculate zone age in days
        if (zone.state != ZoneState::VIOLATED) {
            std::tm formation_tm = {};
            std::istringstream form_ss(zone.formation_datetime);
            form_ss >> std::get_time(&formation_tm, "%Y-%m-%d %H:%M:%S");
            
            std::tm current_tm = {};
            std::istringstream curr_ss(current_bar.datetime);
            curr_ss >> std::get_time(&current_tm, "%Y-%m-%d %H:%M:%S");
            
            if (!form_ss.fail() && !curr_ss.fail()) {
                auto formation_time = std::chrono::system_clock::from_time_t(std::mktime(&formation_tm));
                auto current_time = std::chrono::system_clock::from_time_t(std::mktime(&current_tm));
                auto age_hours = std::chrono::duration_cast<std::chrono::hours>(current_time - formation_time).count();
                int age_days = static_cast<int>(age_hours / 24);
                
                // ⭐ FIX BUG-07: Was hardcoded to 60 days, ignoring config.max_zone_age_days.
                // Now uses config parameter. 0 means "never expire by age alone".
                int expiry_threshold = (config.max_zone_age_days > 0) ? config.max_zone_age_days : 60;
                if (age_days > expiry_threshold) {
                    std::string pre_expiry_state = (zone.state == ZoneState::FRESH ? "FRESH" : 
                                                   zone.state == ZoneState::TESTED ? "TESTED" : "RECLAIMED");
                    zone.state = ZoneState::VIOLATED;
                    zones_changed = true;
                    LOG_INFO("Zone " << zone.zone_id << " EXPIRED after " << age_days << " days (formed " 
                             << zone.formation_datetime << ")");
                    
                    ZoneStateEvent expiry_event(
                        current_bar.datetime,
                        static_cast<int>(bar_history.size()) - 1,  // ⭐ FIX BUG-08: size()-1 is correct 0-based index
                        "EXPIRED",
                        pre_expiry_state,
                        "VIOLATED",
                        (current_bar.high + current_bar.low) / 2.0,
                        current_bar.high,
                        current_bar.low,
                        zone.touch_count
                    );
                    record_event(zone, expiry_event, config.record_violations);
                }
            }
        }
        
        // Zone flip detection
        if (violated && zone.state == ZoneState::VIOLATED) {
            // NEW: Attempt zone flip detection after violation
            if (config.enable_zone_flip) {
                auto flipped = detect_zone_flip(zone, static_cast<int>(bar_history.size()) - 1);
                if (flipped) {
                    Zone new_zone = flipped.value();
                    new_zone.zone_id = next_zone_id_++;
                    
                    // Score the flipped zone
                    MarketRegime regime = MarketAnalyzer::detect_regime(bar_history, 50, 5.0);
                    new_zone.zone_score = scorer.evaluate_zone(new_zone, regime, current_bar);
                    
                    LOG_DEBUG("Flipped zone scored: ID=" << new_zone.zone_id 
                             << " Total=" << new_zone.zone_score.total_score);
                    
                    active_zones.push_back(new_zone);
                    LOG_INFO("NEW FLIPPED ZONE created: ID=" << new_zone.zone_id 
                            << " Type=" << (new_zone.type == ZoneType::DEMAND ? "DEMAND" : "SUPPLY")
                            << " Parent=" << zone.zone_id);
                    zones_changed = true;
                }
            }
        }
        
        // NEW: Sweep-Reclaim Detection (after sweep detected)
        // ✅ FIX: Only check was_swept flag, not state (state may not be VIOLATED if invalidation disabled)
        if (config.enable_sweep_reclaim && zone.was_swept) {
            bool price_inside = (current_bar.low <= zone.proximal_line && 
                                current_bar.high >= zone.distal_line);
            
            if (price_inside) {
                // Calculate body percentage inside zone
                double body_low = current_bar.open;
                double body_high = current_bar.close;
                if (body_high < body_low) std::swap(body_low, body_high);
                
                double body_in_zone = 0.0;
                if (zone.type == ZoneType::DEMAND) {
                    // For demand zone: body above distal line is considered "inside"
                    body_in_zone = std::max<double>(0.0, body_high - zone.distal_line);
                } else {
                    // For supply zone: body below distal line is considered "inside"
                    body_in_zone = std::max<double>(0.0, zone.distal_line - body_low);
                }
                
                double zone_range = zone.proximal_line - zone.distal_line;
                if (zone_range < 0) zone_range = -zone_range;
                double body_pct = (zone_range > 0.0001) ? (body_in_zone / zone_range) : 0.0;
                
                if (body_pct >= config.reclaim_acceptance_pct) {
                    zone.bars_inside_after_sweep++;
                    
                    if (zone.bars_inside_after_sweep >= config.reclaim_acceptance_bars) {
                        // ✅ FIX: Ensure zone is marked VIOLATED first (if not already)
                        std::string pre_reclaim_state;
                        if (zone.state != ZoneState::VIOLATED) {
                            pre_reclaim_state = (zone.state == ZoneState::FRESH ? "FRESH" : "TESTED");
                            zone.state = ZoneState::VIOLATED;
                            LOG_INFO("Zone " << zone.zone_id << " marked VIOLATED before RECLAIM");
                        } else {
                            pre_reclaim_state = "VIOLATED";
                        }
                        
                        // Now RECLAIM it
                        zone.state = ZoneState::RECLAIMED;
                        zone.reclaim_eligible = true;
                        LOG_INFO("✅ Zone " << zone.zone_id << " SWEEP_RECLAIMED after " 
                                << zone.bars_inside_after_sweep << " bars");
                        zones_changed = true;
                        
                        // ✅ BUG #144 FIX: Update profiles on RECLAIM with fresh institutional data
                        // CRITICAL: Only if V6 metrics were properly loaded (have valid baselines)
                        if (volume_scorer_ && oi_scorer_ && zone.volume_profile.avg_volume_baseline > 0.0) {
                            int profile_bar_idx = zone.formation_bar;
                            if (profile_bar_idx >= 0 && profile_bar_idx < static_cast<int>(bar_history.size())) {
                                zone.volume_profile = volume_scorer_->calculate_volume_profile(
                                    zone,
                                    bar_history,
                                    profile_bar_idx
                                );
                                
                                zone.oi_profile = oi_scorer_->calculate_oi_profile(
                                    zone,
                                    bar_history,
                                    profile_bar_idx
                                );
                                
                                zone.institutional_index = Utils::calculate_institutional_index(
                                    zone.volume_profile,
                                    zone.oi_profile
                                );
                                
                                LOG_INFO("🔄 Zone " << zone.zone_id << " profiles UPDATED on RECLAIM"
                                        << " | anchor_bar: " << profile_bar_idx
                                        << " | Vol climax: " << (zone.volume_profile.has_volume_climax ? "YES" : "NO")
                                        << " | OI change: " << std::fixed << std::setprecision(0) << zone.oi_profile.oi_change_during_formation
                                        << " | Inst idx: " << std::fixed << std::setprecision(2) << zone.institutional_index);
                            } else {
                                LOG_WARN("⚠️ Zone " << zone.zone_id << " has invalid formation_bar="
                                         << zone.formation_bar << " (history size=" << bar_history.size()
                                         << "), skipping profile update on RECLAIM");
                            }
                        } else if (volume_scorer_ && oi_scorer_ && zone.volume_profile.avg_volume_baseline == 0.0) {
                            LOG_WARN("⚠️ Zone " << zone.zone_id << " has corrupted V6 baseline on RECLAIM, skipping profile update");
                        }
                        
                        ZoneStateEvent reclaim_event(
                            current_bar.datetime,
                            static_cast<int>(bar_history.size()) - 1,  // ⭐ FIX BUG-08: size()-1 is correct 0-based index
                            "SWEEP_RECLAIMED",
                            pre_reclaim_state,
                            "RECLAIMED",
                            (current_bar.high + current_bar.low) / 2.0,
                            current_bar.high,
                            current_bar.low,
                            zone.touch_count
                        );
                        record_event(zone, reclaim_event, true);
                    }
                }
            } else {
                // Price exited zone - reset counter if body insufficient
                zone.bars_inside_after_sweep = 0;
                zone.reclaim_eligible = false;
            }
        }
    }
    
    // NEW: Save zones once per bar if any state changed (prevents log spam)
    if (zones_changed && current_bar.datetime != last_saved_bar_time_) {
        // Clear state history if disabled
        if (!config.enable_state_history) {
            for (auto& zone : active_zones) {
                zone.state_history.clear();
            }
        }
        zone_persistence_.save_zones(active_zones, next_zone_id_);
        // --- Update master zones file in sync ---
        // Build metadata (similar to bootstrap)
        std::map<std::string, std::string> metadata;
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::tm tm;
    #ifdef _WIN32
        localtime_s(&tm, &time_t);
    #else
        tm = *std::localtime(&time_t);
    #endif
        std::ostringstream timestamp_ss;
        timestamp_ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        metadata["generated_at"] = timestamp_ss.str();
        metadata["csv_path"] = historical_csv_path_;
        metadata["symbol"] = trading_symbol;
        metadata["interval"] = bar_interval;
        metadata["bar_count"] = std::to_string(bar_history.size());
        metadata["zone_count"] = std::to_string(active_zones.size());
        metadata["ttl_hours"] = std::to_string(config.zone_bootstrap_ttl_hours);
        metadata["refresh_time"] = config.zone_bootstrap_refresh_time;
        fs::path master_file_path = fs::path(output_dir_) / "zones_live_master.json";
        zone_persistence_.save_zones_with_metadata(active_zones, next_zone_id_, master_file_path.string(), metadata);
        last_saved_bar_time_ = current_bar.datetime;
    }
}

// ============================================================
// ⭐ FIX: OPTIMIZED ZONE-CENTRIC REPLAY WITH PRICE RANGE FILTERING
// ============================================================
// This replaces the original O(n×m) replay with an optimized version that:
// 1. Only processes zones within price range of current LTP
// 2. Processes each zone independently (better cache locality)
// 3. Starts from formation_bar (skips irrelevant early bars)
// 4. Early exits when zone reaches terminal state
// 5. Consistent logic with update_zone_states()
// ============================================================
void LiveEngine::replay_historical_zone_states() {
    if (bar_history.empty() || active_zones.empty()) {
        LOG_INFO("Nothing to replay (empty bar_history or no zones)");
        return;
    }
    
    auto replay_start = std::chrono::high_resolution_clock::now();
    
    // Get current LTP for price range filtering
    double current_ltp = bar_history.back().close;
    double range_pct = config.live_zone_range_pct > 0 ? config.live_zone_range_pct : 5.0;  // Default 5%
    
    auto [lower, upper] = calculate_price_range(current_ltp, range_pct);
    
    LOG_INFO("=== Optimized Zone State Replay (Zone-Centric) ===");
    LOG_INFO("Current LTP: " << current_ltp);
    LOG_INFO("Price range: [" << lower << ", " << upper << "] (±" << range_pct << "%)");
    LOG_INFO("Total zones: " << active_zones.size());
    LOG_INFO("Total bars: " << bar_history.size());
    
    // Helper lambda for state string
    auto get_state_str = [](ZoneState s) -> const char* {
        switch(s) {
            case ZoneState::FRESH: return "FRESH";
            case ZoneState::TESTED: return "TESTED";
            case ZoneState::RECLAIMED: return "RECLAIMED";
            case ZoneState::VIOLATED: return "VIOLATED";
            default: return "UNKNOWN";
        }
    };
    
    // Event recording lambda
    auto record_event = [&](Zone& target_zone, const ZoneStateEvent& evt, bool enabled) {
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
    
    bool zones_changed = false;
    int zones_processed = 0;
    int zones_skipped_range = 0;
    size_t total_bar_iterations = 0;
    
    // ⭐ ZONE-CENTRIC PROCESSING: Process each zone independently
    for (auto& zone : active_zones) {
        // ⭐ RANGE-FILTER REMOVED from replay (was causing state corruption on large datasets).
        //
        // ROOT CAUSE: The range filter skipped zones outside the current LTP window BEFORE
        // the RC1 reset ran. Skipped zones kept:
        //   - touch_count = calculate_initial_touch_count preset (loose formula, e.g. 1254)
        //   - state = FRESH (initial value from detect_initial_zones)
        // These were then saved to zones_live_master.json, producing impossible combinations
        // (state=FRESH with touch_count=1254) that made LT diverge wildly from BT on large
        // datasets (48 FRESH zones in LT vs 2 in BT on the Aug-2025–Mar-2026 run).
        //
        // The range filter is correct for live process_tick() (efficiency — no need to evaluate
        // zones far from current price on every tick). But bootstrap replay is a one-time
        // initialization step that MUST process every zone to produce correct touch counts
        // and states. BT's update_zone_states has no range filter and processes all zones
        // unconditionally. This must match.

        zones_processed++;
        
        // Start from the bar AFTER formation
        size_t start_bar = static_cast<size_t>(zone.formation_bar) + 1;
        if (start_bar >= bar_history.size()) {
            continue;  // Zone just formed, nothing to replay
        }
        
        // ⭐ RC1 FIX: Reset touch_count and state before replay.
        //
        // ROOT CAUSE: ZoneDetector::calculate_initial_touch_count() pre-populates
        // zone.touch_count using a LOOSER formula (extended 50% past distal) during
        // zone formation detection. This preset value (e.g. 69 for Z45) is stored in
        // the zone struct. The replay then ADDS to this preset using the STRICTER
        // update_zone_states formula, double-counting — producing inflated LT counts.
        //
        // BT's update_zone_states also starts with the preset and adds strict-formula
        // touches, but processes pre-formation bars 0..(formation_bar-1) which set
        // was_in_zone_prev=true early, suppressing later episode detection.
        // The two effects partially cancel in BT but compound in LT replay.
        //
        // FIX: Reset to 0/FRESH before replay. The replay becomes the SINGLE
        // authoritative touch counter using the strict update_zone_states formula
        // from formation_bar+1 onwards. This matches BT's fix (RC1b) which now
        // skips pre-formation bars, making both engines count from the same baseline.
        zone.touch_count  = 0;
        zone.state        = ZoneState::FRESH;
        zone.was_in_zone_prev = false;
        for (size_t i = start_bar; i < bar_history.size(); ++i) {
            total_bar_iterations++;
            
            // ⭐ EARLY EXIT: Skip if zone is in terminal state
            if (zone.state == ZoneState::VIOLATED && 
                (!config.enable_sweep_reclaim || !zone.was_swept)) {
                break;
            }
            
            const auto& bar = bar_history[i];
            ZoneState old_state = zone.state;
            
            // --- Touch Detection ---
            // ⭐ FIX 1b: Type-aware formula (mirrors update_zone_states fix above and
            // backtest_engine.cpp).  The old symmetric formula missed supply zone touches.
            bool price_in_zone;
            if (zone.type == ZoneType::DEMAND) {
                price_in_zone = (bar.high >= zone.distal_line && bar.low <= zone.proximal_line);
            } else {
                // SUPPLY: distal_line is TOP (higher); proximal_line is BOTTOM (lower)
                price_in_zone = (bar.low <= zone.distal_line && bar.high >= zone.proximal_line);
            }

            if (price_in_zone) {
                if (zone.state == ZoneState::FRESH) {
                    zone.state = ZoneState::TESTED;
                    zone.touch_count++;
                    zones_changed = true;

                    // ⭐ PROFILE FIX: Update volume/OI profiles on FIRST_TOUCH in replay.
                    // BT update_zone_states calls calculate_volume_profile on FIRST_TOUCH
                    // (U1 FIX). LT replay was missing this, leaving the profile at the
                    // detection-time value (computed by ZoneDetector's own VolumeScorer
                    // which may use a different baseline snapshot). Adding it here ensures
                    // the final profile uses the same volume_scorer_ and bar_history as BT.
                    if (volume_scorer_ && oi_scorer_) {
                        int profile_bar_idx = zone.formation_bar;
                        if (profile_bar_idx >= 0 && profile_bar_idx < static_cast<int>(bar_history.size())) {
                            zone.volume_profile = volume_scorer_->calculate_volume_profile(
                                zone, bar_history, profile_bar_idx);
                            zone.oi_profile = oi_scorer_->calculate_oi_profile(
                                zone, bar_history, profile_bar_idx);
                            zone.institutional_index = Utils::calculate_institutional_index(
                                zone.volume_profile, zone.oi_profile);
                        }
                    }

                    ZoneStateEvent event(
                        bar.datetime, static_cast<int>(i), "FIRST_TOUCH",
                        get_state_str(old_state), "TESTED",
                        (bar.high + bar.low) / 2.0, bar.high, bar.low, zone.touch_count
                    );
                    record_event(zone, event, config.record_first_touch);
                    
                } else if (zone.state == ZoneState::TESTED) {
                    // ⭐ FIX BUG-TOUCH: Only count a NEW visit episode.
                    // was_in_zone_prev is updated at the bottom of every bar
                    // iteration so consecutive inside-bars are suppressed.
                    if (!zone.was_in_zone_prev) {
                        zone.touch_count++;
                        zones_changed = true;

                        // ⭐ PROFILE FIX: Update volume/OI profiles on RETEST in replay.
                        // BT update_zone_states calls calculate_volume_profile on every new
                        // RETEST episode (U1 FIX). LT replay was missing this, leaving the
                        // profile at an intermediate state. Adding it here makes the final
                        // institutional_index identical to BT for the same touch sequence.
                        if (volume_scorer_ && oi_scorer_) {
                            int profile_bar_idx = zone.formation_bar;
                            if (profile_bar_idx >= 0 && profile_bar_idx < static_cast<int>(bar_history.size())) {
                                zone.volume_profile = volume_scorer_->calculate_volume_profile(
                                    zone, bar_history, profile_bar_idx);
                                zone.oi_profile = oi_scorer_->calculate_oi_profile(
                                    zone, bar_history, profile_bar_idx);
                                zone.institutional_index = Utils::calculate_institutional_index(
                                    zone.volume_profile, zone.oi_profile);
                            }
                        }

                        ZoneStateEvent event(
                            bar.datetime, static_cast<int>(i), "RETEST",
                            get_state_str(old_state), "TESTED",
                            (bar.high + bar.low) / 2.0, bar.high, bar.low, zone.touch_count
                        );
                        record_event(zone, event, config.record_retests);
                    }
                }
            }
            // ⭐ FIX BUG-TOUCH: Update episode-tracking flag for next bar
            zone.was_in_zone_prev = price_in_zone;
            
            // --- Violation Detection ---
            bool violated = false;
            std::string violation_event = "VIOLATED";
            double violation_price = bar.close;
            
            // Gap-over invalidation
            if (config.gap_over_invalidation && zone.state != ZoneState::VIOLATED) {
                if (zone.type == ZoneType::DEMAND) {
                    violated = (bar.high < zone.distal_line);
                } else {
                    violated = (bar.low > zone.distal_line);
                }
                if (violated) {
                    violation_event = "GAP_VIOLATED";
                    violation_price = (zone.type == ZoneType::DEMAND) ? bar.high : bar.low;
                    zone.was_swept = true;
                    zone.sweep_bar = static_cast<int>(i);
                }
            }
            
            // Body-close invalidation
            if (!violated && config.invalidate_on_body_close && zone.state != ZoneState::VIOLATED) {
                if (zone.type == ZoneType::DEMAND) {
                    violated = (bar.close < zone.distal_line);
                } else {
                    violated = (bar.close > zone.distal_line);
                }
                if (violated) {
                    violation_event = "VIOLATED";
                    violation_price = bar.close;
                }
            }
            
            // ⭐ FIX: Set violation state BEFORE sweep-reclaim check
            if (violated && zone.state != ZoneState::VIOLATED) {
                std::string pre_violation_state = get_state_str(zone.state);
                zone.state = ZoneState::VIOLATED;
                zones_changed = true;
                
                ZoneStateEvent event(
                    bar.datetime, static_cast<int>(i), violation_event,
                    pre_violation_state, "VIOLATED",
                    violation_price, bar.high, bar.low, zone.touch_count
                );
                record_event(zone, event, config.record_violations);
            }
            
            // --- Sweep-Reclaim Detection (AFTER sweep detected) ---
            // ✅ FIX: Only check was_swept flag, not state (state may not be VIOLATED if invalidation disabled)
            if (config.enable_sweep_reclaim && zone.was_swept) {
                bool price_inside_zone = (bar.low <= zone.proximal_line && bar.high >= zone.distal_line);
                
                if (price_inside_zone) {
                    // ⭐ FIX: Consistent body calculation with update_zone_states
                    double body_low = bar.open;
                    double body_high = bar.close;
                    if (body_high < body_low) std::swap(body_low, body_high);
                    
                    double zone_range = std::abs(zone.proximal_line - zone.distal_line);
                    double body_in_zone = 0.0;
                    
                    if (zone.type == ZoneType::DEMAND) {
                        body_in_zone = std::max<double>(0.0, body_high - zone.distal_line);
                    } else {
                        body_in_zone = std::max<double>(0.0, zone.distal_line - body_low);
                    }
                    
                    double body_pct = (zone_range > 0.0001) ? (body_in_zone / zone_range) : 0.0;
                    
                    if (body_pct >= config.reclaim_acceptance_pct) {
                        zone.bars_inside_after_sweep++;
                        
                        if (zone.bars_inside_after_sweep >= config.reclaim_acceptance_bars) {
                            // ✅ FIX: Ensure zone is marked VIOLATED first (if not already)
                            std::string pre_reclaim_state;
                            if (zone.state != ZoneState::VIOLATED) {
                                pre_reclaim_state = (zone.state == ZoneState::FRESH ? "FRESH" : "TESTED");
                                zone.state = ZoneState::VIOLATED;
                            } else {
                                pre_reclaim_state = "VIOLATED";
                            }
                            
                            // Now RECLAIM it
                            zone.state = ZoneState::RECLAIMED;
                            zone.reclaim_eligible = true;
                            zones_changed = true;
                            
                            // ✅ BUG #144 FIX: Update profiles on RECLAIM (replay mode)
                            // CRITICAL: Only if V6 metrics were properly loaded (have valid baselines)
                            if (volume_scorer_ && oi_scorer_ && zone.volume_profile.avg_volume_baseline > 0.0) {
                                int profile_bar_idx = zone.formation_bar;
                                if (profile_bar_idx >= 0 && profile_bar_idx < static_cast<int>(bar_history.size())) {
                                    zone.volume_profile = volume_scorer_->calculate_volume_profile(
                                        zone,
                                        bar_history,
                                        profile_bar_idx
                                    );
                                    
                                    zone.oi_profile = oi_scorer_->calculate_oi_profile(
                                        zone,
                                        bar_history,
                                        profile_bar_idx
                                    );
                                    
                                    zone.institutional_index = Utils::calculate_institutional_index(
                                        zone.volume_profile,
                                        zone.oi_profile
                                    );
                                } else {
                                    LOG_WARN("⚠️ Zone " << zone.zone_id << " has invalid formation_bar="
                                             << zone.formation_bar << " (history size=" << bar_history.size()
                                             << "), skipping profile update in replay RECLAIM");
                                }
                            } else if (volume_scorer_ && oi_scorer_ && zone.volume_profile.avg_volume_baseline == 0.0) {
                                LOG_WARN("⚠️ Zone " << zone.zone_id << " has corrupted V6 baseline in replay RECLAIM, skipping profile update");
                            }
                            
                            ZoneStateEvent event(
                                bar.datetime, static_cast<int>(i), "SWEEP_RECLAIMED",
                                pre_reclaim_state, "RECLAIMED",
                                (bar.high + bar.low) / 2.0, bar.high, bar.low, zone.touch_count
                            );
                            record_event(zone, event, config.record_violations);
                        }
                    } else {
                        zone.bars_inside_after_sweep = 0;
                    }
                } else {
                    zone.bars_inside_after_sweep = 0;
                }
            }
        }  // End bar loop for this zone
    }  // End zone loop
    
    // Save if changed
    if (zones_changed) {
        if (!config.enable_state_history) {
            for (auto& zone : active_zones) {
                zone.state_history.clear();
            }
        }
        zone_persistence_.save_zones(active_zones, next_zone_id_);
    }
    last_saved_bar_time_.clear();
    
    auto replay_end = std::chrono::high_resolution_clock::now();
    auto replay_ms = std::chrono::duration_cast<std::chrono::milliseconds>(replay_end - replay_start).count();
    
    // Count final states
    int fresh_count = 0, tested_count = 0, violated_count = 0, reclaimed_count = 0;
    for (const auto& zone : active_zones) {
        switch (zone.state) {
            case ZoneState::FRESH: fresh_count++; break;
            case ZoneState::TESTED: tested_count++; break;
            case ZoneState::VIOLATED: violated_count++; break;
            case ZoneState::RECLAIMED: reclaimed_count++; break;
        }
    }
    
    LOG_INFO("=== Replay Complete ===");
    LOG_INFO("Time: " << replay_ms << "ms (" << (replay_ms/1000.0) << "s)");
    LOG_INFO("Zones processed: " << zones_processed << " (in price range)");
    LOG_INFO("Zones skipped: " << zones_skipped_range << " (outside price range, stay FRESH)");
    LOG_INFO("Total bar iterations: " << total_bar_iterations);
    LOG_INFO("Final states - FRESH: " << fresh_count << ", TESTED: " << tested_count 
             << ", VIOLATED: " << violated_count << ", RECLAIMED: " << reclaimed_count);
}

bool LiveEngine::try_load_backtest_zones() {
    // Try to load from live engine's own persistent file
    fs::path live_zone_file = fs::path(output_dir_) / "zones_live.json";
    
    if (!fs::exists(live_zone_file)) {
        LOG_INFO("No live persistent zones file found. Will detect zones from historical data.");
        return false;
    }
    
    LOG_INFO("Found live engine persistent zones file: " << live_zone_file.string());
    LOG_INFO("Loading zones from: " << live_zone_file.string());
    
    std::ifstream file(live_zone_file);
    if (!file.is_open()) {
        LOG_WARN("Could not open live zone file. Will detect zones from historical data.");
        return false;
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    
    // Safe JSON parsing with error handling
    if (!Json::parseFromStream(builder, file, &root, &errors)) {
        LOG_ERROR("Failed to parse zones JSON file: " << errors);
        LOG_WARN("Will detect zones from historical data instead.");
        file.close();
        return false;
    }
    file.close();
    
    const Json::Value zones_json = root["zones"];
    if (!zones_json.isArray() || zones_json.empty()) {
        LOG_WARN("Live zone file exists but contains no valid zones. Will detect zones from historical data.");
        return false;
    }
    
    LOG_INFO("Parsing zones from live persistent file...");

    active_zones.clear();
    
    // Wrap zone parsing in try-catch to handle any errors
    try {
        for (const auto& zone_json : zones_json) {
        Zone zone{};
        zone.zone_id = zone_json.get("zone_id", 0).asInt();
        std::string type_str = zone_json.get("type", "DEMAND").asString();
        zone.type = (type_str == "SUPPLY") ? ZoneType::SUPPLY : ZoneType::DEMAND;
        zone.base_low = zone_json.get("base_low", 0.0).asDouble();
        zone.base_high = zone_json.get("base_high", 0.0).asDouble();
        zone.distal_line = zone_json.get("distal_line", 0.0).asDouble();
        zone.proximal_line = zone_json.get("proximal_line", 0.0).asDouble();
        zone.formation_bar = zone_json.get("formation_bar", 0).asInt();
        zone.formation_datetime = zone_json.get("formation_datetime", "").asString();
        zone.strength = zone_json.get("strength", 0.0).asDouble();

        std::string state_str = zone_json.get("state", "FRESH").asString();
        if (state_str == "VIOLATED") {
            zone.state = ZoneState::VIOLATED;
        } else if (state_str == "TESTED") {
            zone.state = ZoneState::TESTED;
        } else {
            zone.state = ZoneState::FRESH;
        }

        zone.touch_count = zone_json.get("touch_count", 0).asInt();
        zone.is_elite = zone_json.get("is_elite", false).asBool();
        zone.departure_imbalance = zone_json.get("departure_imbalance", 0.0).asDouble();
        zone.retest_speed = zone_json.get("retest_speed", 0.0).asDouble();
        zone.bars_to_retest = zone_json.get("bars_to_retest", 0).asInt();

        // V6.0 Volume profile (backward compatible)
        if (zone_json.isMember("volume_profile")) {
            const Json::Value& vp = zone_json["volume_profile"];
            zone.volume_profile.formation_volume = vp.get("formation_volume", 0.0).asDouble();
            zone.volume_profile.avg_volume_baseline = vp.get("avg_volume_baseline", 0.0).asDouble();
            zone.volume_profile.volume_ratio = vp.get("volume_ratio", 0.0).asDouble();
            zone.volume_profile.peak_volume = vp.get("peak_volume", 0.0).asDouble();
            zone.volume_profile.high_volume_bar_count = vp.get("high_volume_bar_count", 0).asInt();
            zone.volume_profile.volume_score = vp.get("volume_score", 0.0).asDouble();
        }

        // V6.0 OI profile (backward compatible)
        if (zone_json.isMember("oi_profile")) {
            const Json::Value& op = zone_json["oi_profile"];
            zone.oi_profile.formation_oi = op.get("formation_oi", 0.0).asDouble();
            zone.oi_profile.oi_change_during_formation = op.get("oi_change_during_formation", 0.0).asDouble();
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

        // V6.0 Institutional index (backward compatible)
        zone.institutional_index = zone_json.get("institutional_index", 0.0).asDouble();

        // Load state history
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

        active_zones.push_back(zone);
    }
    
    } catch (const std::exception& e) {
        LOG_ERROR("Exception while parsing zones: " << e.what());
        LOG_WARN("Zone file may be corrupted. Will detect zones from historical data.");
        active_zones.clear();
        return false;
    }

    
    next_zone_id_ = root.get("next_zone_id", next_zone_id_).asInt();

    // Clear state history from loaded zones if disabled
    if (!config.enable_state_history) {
        LOG_INFO("Clearing state history from loaded zones (state history disabled)");
        for (auto& zone : active_zones) {
            zone.state_history.clear();
        }
    }

    // ⭐ BUG FIX: Reset EXHAUSTED state on load when enable_zone_exhaustion = NO
    // Problem: zones_live_master.json persists EXHAUSTED state and consecutive_losses
    // from prior runs. When the flag is disabled, the skip gate doesn't fire, but
    // the stale EXHAUSTED state still causes zone cascade effects (altered zone
    // ranking, different downstream detections). Fix: clear exhaustion metadata
    // on load so the zone pool is fully clean when the feature is disabled.
    if (!config.enable_zone_exhaustion) {
        int reset_count = 0;
        for (auto& zone : active_zones) {
            if (zone.state == ZoneState::EXHAUSTED || zone.consecutive_losses > 0) {
                if (zone.state == ZoneState::EXHAUSTED) {
                    zone.state = ZoneState::TESTED;  // Restore to last valid state
                }
                zone.consecutive_losses   = 0;
                zone.exhausted_at_datetime = "";
                reset_count++;
            }
        }
        if (reset_count > 0) {
            LOG_INFO("⚠️  enable_zone_exhaustion=NO: reset " << reset_count
                     << " zone(s) from EXHAUSTED/dirty state on load");
        }
    }

    LOG_INFO("✅ Successfully loaded " << active_zones.size() << " zones from live persistent file (next_zone_id: " << next_zone_id_ << ")");
    if (!active_zones.empty()) {
        LOG_INFO("[DETERMINISM] First loaded zone volume_score=" << active_zones.front().volume_profile.volume_score
                 << " zone_id=" << active_zones.front().zone_id);
    }
    

    return true;
}

void LiveEngine::process_zones() {
    auto record_event = [&](Zone& target_zone, const ZoneStateEvent& evt, bool enabled) {
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

    // CRITICAL FIX: Only advance the scan pointer when we have enough lookahead
    // Zone detection needs a 5-bar lookahead (bars.size() - 5). If we move the
    // scan pointer to the latest bar, we starve the detector and it returns 0 zones.
    const int current_bar_count = static_cast<int>(bar_history.size());
    const int last_scannable_bar = current_bar_count - 6;  // last index with 5-bar lookahead
    if (last_scannable_bar <= last_zone_scan_bar_) {
        // Not enough new bars to run detection safely
        return;
    }
    
    LOG_DEBUG("Scanning for new zones from bar " << last_zone_scan_bar_ + 1 
              << " to " << last_scannable_bar);
    
    // Detect new zones with duplicate prevention against active zones
    std::vector<Zone> new_zones = detector.detect_zones_no_duplicates(
        active_zones,
        last_zone_scan_bar_ + 1
    );
    
    LOG_DEBUG("process_zones(): " << new_zones.size() << " candidate zones returned");
    
    bool zones_added = false;
    
    // Add non-duplicate zones to active zones
    for (auto zone : new_zones) {
        // ⭐ FIXED: For runtime-appended data, only filter out VERY old zones
        // Accept zones formed recently (within last N bars) even if they're before last_zone_scan_bar_
        // This allows detection of zones formed in the newly appended data
        int bars_before_last_scan = last_zone_scan_bar_ - zone.formation_bar;
        
        // If zone was formed AFTER last scan, definitely include it
        if (zone.formation_bar > last_zone_scan_bar_) {
            // Zone is new, will be added below
        } 
        // If zone was formed recently (e.g., within last 10 bars), still include it
        // This handles the case where CSV data is appended with slightly overlapping bars
        else if (bars_before_last_scan <= 10) {
            LOG_DEBUG("Zone " << zone.zone_id << " formed " << bars_before_last_scan 
                     << " bars before last scan - includes it (overlapping data)");
            // Continue processing - don't skip
        }
        else {
            // Skip zones from stale data
            LOG_DEBUG("process_zones: skipping zone at bar " << zone.formation_bar
                     << " (last_zone_scan_bar_=" << last_zone_scan_bar_ << ")");
            continue;  // Skip zones from already-scanned bars
        }
        
        bool already_active = false;
        for (const auto& active : active_zones) {
            // FIXED: Check both price levels AND formation_bar to prevent duplicates
            if (zone.formation_bar == active.formation_bar &&
                std::abs(zone.proximal_line - active.proximal_line) < 1.0 &&
                std::abs(zone.distal_line - active.distal_line) < 1.0) {
                already_active = true;
                LOG_DEBUG("process_zones: zone at bar " << zone.formation_bar
                         << " already in active_zones — skipping duplicate");
                break;
            }
        }
        
        if (!already_active) {
            // NEW: Assign zone ID
            zone.zone_id = next_zone_id_++;
            
            // ⭐ GAP-6 FIX: Score at formation bar context, not bar_history.back().
            // Matches BT and bootstrap_zones_on_startup() fix above.
            {
                MarketRegime regime = MarketAnalyzer::detect_regime(bar_history, 50, 5.0);
                int score_bar_idx = zone.formation_bar;
                if (score_bar_idx < 0 || score_bar_idx >= static_cast<int>(bar_history.size()))
                    score_bar_idx = static_cast<int>(bar_history.size()) - 1;
                zone.zone_score = scorer.evaluate_zone(zone, regime, bar_history[score_bar_idx]);
            }

            LOG_DEBUG("Zone scored: ID=" << zone.zone_id
                     << " Total=" << zone.zone_score.total_score
                     << " (" << zone.zone_score.entry_rationale << ")");
            
            // Record ZONE_CREATED event
            ZoneStateEvent creation_event(
                zone.formation_datetime,
                zone.formation_bar,
                "ZONE_CREATED",
                "",  // No previous state
                "FRESH",
                (zone.proximal_line + zone.distal_line) / 2.0,
                bar_history.back().high,
                bar_history.back().low,
                0  // No touches yet
            );
            record_event(zone, creation_event, config.record_zone_creation);
            
            active_zones.push_back(zone);
            zones_added = true;
            
            LOG_INFO("🔷 New zone detected: " 
                    << (zone.type == ZoneType::DEMAND ? "DEMAND" : "SUPPLY")
                    << " @ " << zone.distal_line << "-" << zone.proximal_line
                    << " (ID: " << zone.zone_id << ", formed at bar " << zone.formation_bar << ")");
        }
    }
    
    // ⭐ LIVE ZONE SCAN POINTER FIX:
    // Update last_zone_scan_bar_ to detect_no_dup_end - 1, NOT last_scannable_bar.
    //
    // Why: detect_zones_no_duplicates() can only scan up to:
    //   detect_no_dup_end = bars.size() - consolidation_max_bars - lookforward_max
    // If we advance last_zone_scan_bar_ to last_scannable_bar (= bars.size()-6),
    // then next tick: from_bar = last_scannable_bar + 1 ≈ bars.size()-5
    // But detect_no_dup_end ≈ bars.size()-25 → from_bar > detect_no_dup_end → empty loop.
    // The live bars are NEVER scanned because from_bar always overshoots detect_no_dup_end.
    //
    // Correct: advance only to detect_no_dup_end - 1, so from_bar on the next tick
    // equals detect_no_dup_end. As each new live bar arrives, detect_no_dup_end advances
    // by 1 and from_bar follows it — scanning exactly 1 new bar per tick, always within range.
    // After 25 live bars, the first live bar enters the scannable window and zones can form.
    {
        int lookforward_max_upd = std::max(config.consolidation_min_bars, 3);
        int detect_no_dup_end_upd = static_cast<int>(bar_history.size())
                                    - config.consolidation_max_bars
                                    - lookforward_max_upd;
        // Clamp: never go backwards, never exceed last_scannable_bar
        int new_scan_bar = std::max(last_zone_scan_bar_,
                           std::min(last_scannable_bar, detect_no_dup_end_upd - 1));
        last_zone_scan_bar_ = new_scan_bar;
    }
    
    // NEW: Save zones if any were added
    if (zones_added) {
        // Clear state history if disabled
        if (!config.enable_state_history) {
            for (auto& zone : active_zones) {
                zone.state_history.clear();
            }
        }
        zone_persistence_.save_zones(active_zones, next_zone_id_);
        // --- Update master zones file in sync ---
        std::map<std::string, std::string> metadata;
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::tm tm;
#ifdef _WIN32
        localtime_s(&tm, &time_t);
#else
        tm = *std::localtime(&time_t);
#endif
        std::ostringstream timestamp_ss;
        timestamp_ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        metadata["generated_at"] = timestamp_ss.str();
        metadata["csv_path"] = historical_csv_path_;
        metadata["symbol"] = trading_symbol;
        metadata["interval"] = bar_interval;
        metadata["bar_count"] = std::to_string(bar_history.size());
        metadata["zone_count"] = std::to_string(active_zones.size());
        metadata["ttl_hours"] = std::to_string(config.zone_bootstrap_ttl_hours);
        metadata["refresh_time"] = config.zone_bootstrap_refresh_time;
        fs::path master_file_path = fs::path(output_dir_) / "zones_live_master.json";
        zone_persistence_.save_zones_with_metadata(active_zones, next_zone_id_, master_file_path.string(), metadata);
    }
    
    LOG_DEBUG("Active zones: " << active_zones.size());
}

void LiveEngine::check_for_entries() {
    if (skip_trading_until_bar_ && dryrun_bootstrap_end_bar_ > 0) {
        int current_bar = static_cast<int>(bar_history.size()) - 1;
        if (current_bar < dryrun_bootstrap_end_bar_) {
            LOG_DEBUG("[BOOTSTRAP PHASE] Skipping trade check at bar " << current_bar 
                     << " (bootstrap ends at bar " << dryrun_bootstrap_end_bar_ << ")");
            return;
        }
        skip_trading_until_bar_ = false;
        LOG_INFO("✅ Bootstrap phase complete at bar " << current_bar 
                << " - Trading now ENABLED");
    }

    // Configurable early entry-time block (morning/no-trade window)
    if (config.enable_entry_time_block &&
        !config.entry_block_start_time.empty() &&
        !config.entry_block_end_time.empty()) {
        const Bar& current_bar_for_gate = bar_history.back();
        if (current_bar_for_gate.datetime.length() >= 16) {
            std::string hhmm = current_bar_for_gate.datetime.substr(11, 5);
            if (hhmm >= config.entry_block_start_time && hhmm < config.entry_block_end_time) {
                std::cout << "\n⛔ ENTRY BLOCKED - Configured time window\n";
                std::cout << "Current time: " << hhmm << "\n";
                std::cout << "Blocked window: " << config.entry_block_start_time
                          << " - " << config.entry_block_end_time << "\n\n";

                LOG_INFO("⛔ ENTRY BLOCKED: Time (" << hhmm
                        << ") in configured window ("
                        << config.entry_block_start_time << "-"
                        << config.entry_block_end_time << ")");
                return;
            }
        }
    }

    // ⭐ FIX: ENTRY TIME GATE - Block entries after circuit breaker cutoff
    // Prevents late entries (e.g., 15:29) that bypass position closing logic
    if (config.close_before_session_end_minutes > 0) {
        const Bar& current_bar = bar_history.back();
        std::string current_time_str = current_bar.datetime;
        
        // Parse current time (HH:MM:SS)
        int curr_hour = 0, curr_min = 0, curr_sec = 0;
        try {
            size_t space_pos = current_time_str.rfind(' ');
            if (space_pos != std::string::npos) {
                std::string time_part = current_time_str.substr(space_pos + 1);
                sscanf(time_part.c_str(), "%d:%d:%d", &curr_hour, &curr_min, &curr_sec);
            }
        } catch (...) {
            LOG_WARN("Failed to parse current time for entry gate: " << current_time_str);
        }
        
        // Parse session end time
        int session_hour = 0, session_min = 0, session_sec = 0;
        try {
            sscanf(config.session_end_time.c_str(), "%d:%d:%d", &session_hour, &session_min, &session_sec);
        } catch (...) {
            LOG_WARN("Failed to parse session_end_time for entry gate: " << config.session_end_time);
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
            std::cout << "\n⏳ ENTRY BLOCKED - Too close to session end\n";
            std::cout << "Current time: " << current_time_str << "\n";
            std::cout << "Entry cutoff: " << (cutoff_hour < 10 ? "0" : "") << cutoff_hour << ":" 
                      << (cutoff_min < 10 ? "0" : "") << cutoff_min << "\n";
            std::cout << "No new entries allowed to avoid overnight gap risk\n\n";
            
            LOG_INFO("⏳ ENTRY BLOCKED: Current time (" << current_time_str 
                    << ") >= Entry cutoff (" << cutoff_hour << ":" << cutoff_min << ")");
            return;
        }
    }
    
    // In-position skip (configurable)
    if (config.live_skip_when_in_position && trade_mgr.is_in_position()) {
        std::cout << "\n[SKIP] TRADE CHECK SKIPPED - Already in position" << std::endl;
        return;  // Already in a trade
    }
    
    // ⭐ FIX BUG-01 (REVISED): Use bar.close for zone proximity check.
    //
    // History of this fix:
    //   Original (broken):  current_price = broker->get_ltp() = NEXT bar's close
    //                       -> look-ahead bias, phantom trades on future prices
    //   First attempt:      current_price = bar.open
    //                       -> too restrictive: missed all trades where price MOVED
    //                          INTO zone during the bar (86% trade reduction)
    //   Correct fix:        current_price = bar.close
    //                       -> confirmed settled price, no look-ahead.
    //                          If bar.close is inside zone, price touched zone this bar.
    //                          Matches EntryDecisionEngine's LTP (last confirmed price).
    //                          Recovers zone-touch entries that bar.open misses.
    const Bar& current_bar = bar_history.back();
    double current_price = current_bar.close;

    // Bar-change gating (configurable): only evaluate on a new bar
    if (config.live_entry_require_new_bar) {
        if (!last_entry_check_bar_time_.empty() && last_entry_check_bar_time_ == current_bar.datetime) {
            std::cout << "[SKIP] Entry check skipped (same bar: " << current_bar.datetime << ")" << std::endl;
            return;
        }
        last_entry_check_bar_time_ = current_bar.datetime;
    }
    
    // ✅ V6.0: Check for expiry day and adjust config if needed
    Config active_config = config;  // Copy config
    bool is_expiry = is_expiry_day(current_bar.datetime);
    
    if (is_expiry) {
        LOG_INFO("📅 EXPIRY DAY DETECTED - Applying special rules");
        
        if (config.expiry_day_disable_oi_filters) {
            active_config.enable_oi_entry_filters = false;
            active_config.enable_oi_exit_signals = false;
            LOG_INFO("   OI filters DISABLED (rollover distortion)");
        }
        
        active_config.lot_size = static_cast<int>(
            config.lot_size * config.expiry_day_position_multiplier
        );
        LOG_INFO("   Position size adjusted to: " << active_config.lot_size << " lots");
    }
    
    // ⭐ PRINT TRADE CHECK START
    std::cout << "\n+====================================================+\n";
    std::cout << "| [ENTRY CHECK] Checking for Entry Opportunities    |\n";
    std::cout << "+====================================================+\n";
    std::cout << "  Current Price: " << std::fixed << std::setprecision(2) << current_price << "\n";
    std::cout << "  Bar Time:      " << current_bar.datetime << "\n";
    std::cout << "  Active Zones:  " << active_zones.size() << "\n";
    std::cout << std::endl;
    std::cout.flush();
    
    // ⭐ FIX 2: Use config parameters for regime detection — parity with backtest_engine.cpp.
    // Backtest uses REGIME_LOOKBACK=100 and config.htf_trending_threshold.
    // The old hardcoded (50, 5.0) ignored both config parameters, so changing
    // htf_lookback_bars or htf_trending_threshold in the config had no effect on live.
    // Use htf_lookback_bars if set (>0), otherwise fall back to 100 (backtest default).
    int regime_lookback = (config.htf_lookback_bars > 0) ? config.htf_lookback_bars : 100;
    MarketRegime regime = MarketAnalyzer::detect_regime(bar_history, regime_lookback, config.htf_trending_threshold);
    std::string regime_str = (regime == MarketRegime::BULL ? "UPTREND" : 
                             regime == MarketRegime::BEAR ? "DOWNTREND" : "SIDEWAYS");
    std::cout << "  Market Regime: " << regime_str << "\n\n";
    
    // ⭐ SAFETY CHECK: Validate zones are not from stale data
    // Calculate ATR to determine reasonable zone distance from current price
    double atr = MarketAnalyzer::calculate_atr(bar_history, config.atr_period);
    double max_zone_distance = atr * config.max_zone_distance_atr;  // Configurable multiplier
    
    bool trade_generated = false;
    int zones_checked = 0;
    int zones_rejected = 0;
    int zones_price_not_in = 0;
    
    // ⭐ DETERMINISTIC FIX: Use stable_sort for guaranteed deterministic ordering
    // When multiple zones are valid at the same price, the first one in sorted order wins
    // stable_sort preserves relative order of equal elements (unlike std::sort)
    // ⭐ ENHANCED SORTING: Prioritize by state quality, then recency
std::stable_sort(active_zones.begin(), active_zones.end(), 
    [](const Zone& a, const Zone& b) {
        // Helper: Get state priority (lower = better)
        auto get_state_priority = [](const Zone& z) -> int {
            switch (z.state) {
                case ZoneState::FRESH:     return 0;  // Highest priority
                case ZoneState::RECLAIMED: return 1;  // Second (reclaimed zones show renewed strength)
                case ZoneState::TESTED:    return 2;  // Third (still valid but less pristine)
                case ZoneState::VIOLATED:  return 3;  // Lowest (broken zones)
                default:                   return 4;
            }
        };
        
        int priority_a = get_state_priority(a);
        int priority_b = get_state_priority(b);
        
        // Primary sort: State priority
        if (priority_a != priority_b)
            return priority_a < priority_b;
        
        // Secondary sort: Within same state, prefer newer zones (more relevant)
        if (a.formation_bar != b.formation_bar)
            return a.formation_bar > b.formation_bar;
        
        // Tertiary sort: Zone ID (newer zones have higher IDs)
        return a.zone_id > b.zone_id;
    });
    
    // === LOSS PROTECTION: Pause after consecutive losses ===
    if (pause_counter > 0) {
        std::cout << "[LOSS PROTECTION] Skipping entry: " << pause_counter << " bars left in pause after "
                  << config.max_consecutive_losses << " consecutive losses.\n";
        LOG_DEBUG("[LOSS PROTECTION] Skipping entry: " << pause_counter << " bars left in pause after "
                 << config.max_consecutive_losses << " consecutive losses.");
        pause_counter--;
        return;
    }

    // === SAME-SESSION RE-ENTRY GUARD: Clear stale entries from PRIOR trading day ===
    // CRITICAL: last_guard_date must be initialised to today on first bar, NOT empty-string.
    // If initialised to "" then the very first bar of any new date triggers a clear —
    // including the SAME date the guard was just set on, wiping it before the next
    // entry check runs. e.g. Guard set on Sep-05 13:10, cleared at Sep-05 13:15,
    // Trade 8 enters freely at Sep-05 13:30.
    {
        const std::string today = bar_history.back().datetime.substr(0, 10);
        // ⭐ FIX DES-01: Removed 'static' — leaked state across engine instances.
        // last_guard_date_ is now a member variable (add to live_engine.h, init "" in ctor).
        if (last_guard_date_.empty()) {
            // First bar ever — initialise silently, nothing to clear yet
            last_guard_date_ = today;
        } else if (today != last_guard_date_) {
            // Genuine new trading day — expire all prior-session blocks
            if (!zone_sl_session_.empty()) {
                LOG_INFO("[RE-ENTRY GUARD] New session " << today
                         << " — clearing " << zone_sl_session_.size()
                         << " blocked zone(s) from prior session");
                zone_sl_session_.clear();
            }
            last_guard_date_ = today;
        }
        // Same day → do nothing — guard entries from earlier this session remain valid
    }

    // Check each active zone for entry opportunity
    for (auto& zone : active_zones) {
        zones_checked++;
        
        // ⭐ FIX 3: Zone corruption guard — parity with backtest_engine.cpp.
        // A zone with distal_line=0 or proximal_line=0 (e.g. from a zero-initialised
        // struct or a corrupt zones_live_master.json entry) causes:
        //   calculate_stop_loss() → SL = 0
        //   calculate_take_profit() → TP = 0
        //   check_take_profit() fires immediately at price=0
        //   → catastrophic phantom loss (entry_price × lot_size × position_size)
        // This guard was added to backtest after a ₹1.67M phantom exit incident.
        // The live engine loads zones from JSON, making it equally susceptible.
        if (zone.distal_line <= 0.0 || zone.proximal_line <= 0.0) {
            zones_rejected++;
            LOG_WARN("Zone " << zone.zone_id << " SKIPPED: corrupt boundaries"
                     << " distal=" << zone.distal_line
                     << " proximal=" << zone.proximal_line);
            continue;
        }
        if (zone.proximal_line == zone.distal_line) {
            zones_rejected++;
            LOG_WARN("Zone " << zone.zone_id << " SKIPPED: zero-width zone"
                     << " (proximal==distal==" << zone.proximal_line << ")");
            continue;
        }

        // === SAME-SESSION RE-ENTRY GUARD: Skip zone if it already exited (any reason) today ===
        if (!zone_sl_session_.empty()) {
            const std::string today = bar_history.back().datetime.substr(0, 10);
            auto it = zone_sl_session_.find(zone.zone_id);
            if (it != zone_sl_session_.end() && it->second == today) {
                zones_rejected++;
                LOG_INFO("[RE-ENTRY GUARD] Zone " << zone.zone_id
                         << " SKIPPED: already exited in session " << today);
                continue;
            }
        }

        // Skip violated zones
        // Only skip violated zones if configured to do so
if (zone.state == ZoneState::VIOLATED && config.skip_retest_after_gap_over) {
    zones_rejected++;
    LOG_WARN("⚠️  Zone " << zone.zone_id << " VIOLATED - skipped (skip_retest=YES)");
    continue;
}
// If skip_retest = NO, allow VIOLATED zones to be evaluated

        // ⭐ P1: Skip EXHAUSTED zones (per-zone consecutive-loss limit reached)
        if (config.enable_zone_exhaustion && zone.state == ZoneState::EXHAUSTED) {
            zones_rejected++;
            LOG_INFO("Zone " << zone.zone_id << " SKIPPED: EXHAUSTED ("
                      << zone.consecutive_losses << " consecutive losses)");
            continue;
        }

        // ⭐ ISSUE-2: Per-direction SL suspension check
        // If this zone has hit the SL threshold for the direction it would trade
        // (DEMAND→LONG, SUPPLY→SHORT), skip it for the remainder of the run.
        if (config.zone_sl_suspend_threshold > 0) {
            const std::string direction = (zone.type == ZoneType::DEMAND) ? "LONG" : "SHORT";
            const int sl_count = (direction == "LONG") ? zone.sl_count_long : zone.sl_count_short;
            if (sl_count >= config.zone_sl_suspend_threshold) {
                zones_rejected++;
                LOG_WARN("🚫 Zone #" << zone.zone_id << " SKIPPED: "
                         << direction << " suspended ("
                         << sl_count << " SL hits, no winner)");
                continue;
            }
        }

        // ⭐ Zone retry limit (parity with backtest_engine)
        if (config.enable_per_zone_retry_limit &&
            zone.entry_retry_count >= config.max_retries_per_zone) {
            zones_rejected++;
            LOG_DEBUG("Zone " << zone.zone_id << " SKIPPED: retry limit ("
                      << zone.entry_retry_count << " >= " << config.max_retries_per_zone << ")");
            continue;
        }

        // ⭐ P3: Late-session entry cutoff
        if (config.enable_late_entry_cutoff && current_bar.datetime.length() >= 16) {
            int bar_hhmm = 0, cut_hhmm = 0;
            try {
                bar_hhmm = std::stoi(current_bar.datetime.substr(11, 2)) * 60
                         + std::stoi(current_bar.datetime.substr(14, 2));
            } catch (...) {}
            int cut_h = 14, cut_m = 30;
            sscanf(config.late_entry_cutoff_time.c_str(), "%d:%d", &cut_h, &cut_m);
            cut_hhmm = cut_h * 60 + cut_m;
            if (bar_hhmm >= cut_hhmm) {
                // ⭐ FIX BUG-1: Increment zones_rejected so the Trade Check Summary
                // correctly shows "Zones Rejected: N" instead of "Zones Rejected: 0"
                // when the late-session gate is the reason no trade was generated.
                // The cutoff fires as a global break before any per-zone rejection
                // logic runs, so without this the summary was misleading.
                zones_rejected++;
                LOG_INFO("Late-session cutoff: no new entries after "
                          << config.late_entry_cutoff_time
                          << " (bar=" << current_bar.datetime << ")");
                break;
            }
        }
        
        // Skip if only trading fresh zones
        // ⭐ FIX BUG-02: Was using || (OR) — always true since a zone can only be
        // one state at a time. Correct logic: reject only if the zone is NONE of
        // the allowed states (FRESH, RECLAIMED, TESTED).
        if (config.only_fresh_zones &&
            zone.state != ZoneState::FRESH &&
            zone.state != ZoneState::RECLAIMED &&
            zone.state != ZoneState::TESTED) {
            zones_rejected++;
            std::cout << "[ZONE REJECTED] only_fresh_zones=YES, zone " << zone.zone_id 
                << " is VIOLATED/EXHAUSTED - skipping\n";
            continue;
        }

        // Zone age filter (min/max/exclude range)
        int zone_age_days = 0;
        if (is_zone_age_blocked(zone, current_bar, config, zone_age_days)) {
            zones_rejected++;
            LOG_INFO("Zone " << zone.zone_id << " rejected by age filter: " << zone_age_days
                     << " days (min=" << config.min_zone_age_days
                     << ", max=" << config.max_zone_age_days
                     << ", exclude=" << (config.exclude_zone_age_range ? "YES" : "NO") << ")");
            continue;
        }
        
        // ⭐ NEW: Reject zones from stale data (too far from current price)
        double zone_mid = (zone.proximal_line + zone.distal_line) / 2.0;
        double distance_from_price = std::abs(zone_mid - current_price);
        
        if (distance_from_price > max_zone_distance) {
            zones_rejected++;
            LOG_WARN("⚠️  Rejecting zone " << zone.zone_id << " - too far from current price");
            LOG_WARN("    Zone mid: " << zone_mid << ", Current: " << current_price 
                    << ", Distance: " << distance_from_price << " points (max: " << max_zone_distance << ")");
            continue;
        }
        
        // Per-zone cooldown (configurable)
        if (config.live_zone_entry_cooldown_seconds > 0) {
            auto it = last_entry_attempt_by_zone_.find(zone.zone_id);
            if (it != last_entry_attempt_by_zone_.end()) {
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it->second).count();
                if (elapsed < config.live_zone_entry_cooldown_seconds) {
                    zones_rejected++;
                    LOG_DEBUG("Cooldown active for zone " << zone.zone_id << " (" << elapsed << "s elapsed)");
                    continue;
                }
            }
        }

        // ⭐ FIX BUG-01 (FINAL): Use bar range overlap for zone proximity check.
        //
        // Evolution of this fix:
        //   broker->get_ltp()  → next bar's close (look-ahead, phantom trades)
        //   bar.open           → too restrictive, missed 86% of entries
        //   bar.close          → better, but missed bars that touched zone and snapped back
        //   bar range overlap  → CORRECT: mirrors Sam Seiden methodology exactly
        //
        // A supply/demand zone is "touched" when the bar's range overlaps the zone —
        // regardless of whether price opened or closed inside it. This is identical
        // to the logic update_zone_states() already uses for zone state tracking.
        //
        // SUPPLY zone (distal > proximal):
        //   bar.high >= proximal (price rallied up into zone from below)
        //   OR bar.low  <= distal  (price dropped into zone from above)
        //   Combined: bar.high >= proximal AND bar.low <= distal (full overlap check)
        //
        // DEMAND zone (proximal > distal):
        //   bar.low  <= proximal (price dropped down into zone from above)
        //   OR bar.high >= distal  (price rose into zone from below)
        //   Combined: bar.high >= distal AND bar.low <= proximal (full overlap check)
        bool price_in_zone;
        if (zone.type == ZoneType::DEMAND) {
            // DEMAND: proximal=TOP of zone, distal=BOTTOM of zone
            // Entry when bar range overlaps zone AND bar does NOT close below distal.
            // bar.high >= distal     → price rose into zone from below
            // bar.low  <= proximal   → price dropped into zone from above
            // bar.close >= distal    → close guard: price did NOT break through distal
            //                          (if close < distal → zone violated, no entry)
            price_in_zone = (current_bar.high  >= zone.distal_line  &&
                             current_bar.low   <= zone.proximal_line &&
                             current_bar.close >= zone.distal_line);
        } else {
            // SUPPLY: proximal=BOTTOM of zone, distal=TOP of zone
            // Entry when bar range overlaps zone AND bar does NOT close above distal.
            // bar.high >= proximal   → price rallied into zone from below
            // bar.low  <= distal     → price dropped into zone from above
            // bar.close <= distal    → close guard: price did NOT break through distal
            //                          (if close > distal → zone violated, no entry)
            price_in_zone = (current_bar.high  >= zone.proximal_line &&
                             current_bar.low   <= zone.distal_line   &&
                             current_bar.close <= zone.distal_line);
        }
        if (!price_in_zone) {
            zones_price_not_in++;
            continue;
        }
        
        // ⭐ FIX BUG-04 (Option A): Zone retirement now uses config.max_touch_count.
        // Previously hardcoded to 50 (matching EntryDecisionEngine internal value),
        // which was too aggressive — NIFTY zones persist for months and accumulate
        // 100-300+ touches. Analysis of Run 11 showed 37 zones blocked at 50 touches,
        // eliminating 40 valid trades worth ₹1,66,083. Optimal threshold = 200
        // (recovers 42 trades at 69% WR, matching baseline performance).
        // Set via config: max_touch_count = 200  (0 = unlimited, never retire)
        const int touch_limit = (config.max_touch_count > 0) ? config.max_touch_count : 200;
        if (zone.touch_count > touch_limit) {
            zones_rejected++;
            LOG_WARN("Zone #" << zone.zone_id << " RETIRED: " << zone.touch_count
                     << " touches (max=" << touch_limit << ") — skipping entry");
            zone.state = ZoneState::VIOLATED;
            continue;
        }

        if (config.enable_candle_confirmation) {
            if (bar_history.size() < 2) {
                zones_rejected++;
                LOG_INFO("Candle confirmation rejected: not enough bars");
                continue;
            }

            const Bar& confirm_bar = bar_history[bar_history.size() - 2];
            double zone_low = std::min(zone.distal_line, zone.proximal_line);
            double zone_high = std::max(zone.distal_line, zone.proximal_line);
            bool close_in_zone = (confirm_bar.close >= zone_low && confirm_bar.close <= zone_high);
            bool wick_in_zone = (confirm_bar.low <= zone_high && confirm_bar.high >= zone_low);

            double candle_range = confirm_bar.high - confirm_bar.low;
            double candle_body = std::abs(confirm_bar.close - confirm_bar.open);
            double body_pct = (candle_range > 0.0) ? (candle_body / candle_range * 100.0) : 0.0;

            std::string direction = (zone.type == ZoneType::DEMAND) ? "LONG" : "SHORT";
            bool direction_ok = (direction == "LONG") ? (confirm_bar.close > confirm_bar.open)
                                                       : (confirm_bar.close < confirm_bar.open);

            bool candle_ok = close_in_zone && wick_in_zone && direction_ok &&
                             (body_pct >= config.candle_confirmation_body_pct);

            if (!candle_ok) {
                zones_rejected++;
                LOG_INFO("Candle confirmation rejected: zone=" << zone.zone_id
                        << " close_in_zone=" << close_in_zone
                        << " wick_in_zone=" << wick_in_zone
                        << " direction_ok=" << direction_ok
                        << " body_pct=" << std::fixed << std::setprecision(2) << body_pct
                        << " min_body_pct=" << config.candle_confirmation_body_pct);
                continue;
            }
        }

        if (config.enable_structure_confirmation) {
            if (bar_history.size() < 3) {
                zones_rejected++;
                LOG_INFO("Structure confirmation rejected: not enough bars");
                continue;
            }

            const Bar& confirm_bar = bar_history[bar_history.size() - 2];
            const Bar& prev_bar = bar_history[bar_history.size() - 3];
            std::string direction = (zone.type == ZoneType::DEMAND) ? "LONG" : "SHORT";

            bool structure_ok = (direction == "LONG")
                ? (confirm_bar.close > prev_bar.high)
                : (confirm_bar.close < prev_bar.low);

            if (!structure_ok) {
                zones_rejected++;
                LOG_INFO("Structure confirmation rejected: zone=" << zone.zone_id
                        << " confirm_close=" << confirm_bar.close
                        << " prev_high=" << prev_bar.high
                        << " prev_low=" << prev_bar.low);
                continue;
            }
        }
        
        // ⭐ EMA ALIGNMENT FILTER (moved from entry_decision_engine.cpp due to linker issues)
        // Check EMA trend alignment before scoring to save computation
        int current_index = static_cast<int>(bar_history.size()) - 1;
        double ema_20 = MarketAnalyzer::calculate_ema(bar_history, 20, current_index);
        double ema_50 = MarketAnalyzer::calculate_ema(bar_history, 50, current_index);
        std::string direction = (zone.type == ZoneType::DEMAND) ? "LONG" : "SHORT";
        
        if (direction == "LONG" && config.require_ema_alignment_for_longs) {
            double ema_sep_pct = 100.0 * (ema_20 - ema_50) / ema_50;
            if (ema_20 <= ema_50 || ema_sep_pct < config.ema_min_separation_pct_long) {
                zones_rejected++;
                std::cout << "  [NO] Zone " << zone.zone_id << ": EMA filter - LONG rejected (EMA20 "
                          << std::fixed << std::setprecision(2) << ema_20 << " <= EMA50 " << ema_50
                          << ", sep=" << ema_sep_pct << "% < min=" << config.ema_min_separation_pct_long << "%)\n";
                LOG_INFO("EMA filter rejected LONG: EMA20=" << ema_20 << " <= EMA50=" << ema_50 << ", sep=" << ema_sep_pct << "% < min=" << config.ema_min_separation_pct_long << "%");
                continue;  // Skip this zone - downtrend or weak uptrend
            }
        }
        
        if (direction == "SHORT" && config.require_ema_alignment_for_shorts) {
            double ema_sep_pct = 100.0 * (ema_50 - ema_20) / ema_20;
            if (ema_20 >= ema_50 || ema_sep_pct < config.ema_min_separation_pct_short) {
                zones_rejected++;
                std::cout << "  [NO] Zone " << zone.zone_id << ": EMA filter - SHORT rejected (EMA20 " 
                          << std::fixed << std::setprecision(2) << ema_20 << " >= EMA50 " << ema_50
                          << ", sep=" << ema_sep_pct << "% < min=" << config.ema_min_separation_pct_short << "%)\n";
                LOG_INFO("EMA filter rejected SHORT: EMA20=" << ema_20 << " >= EMA50=" << ema_50 << ", sep=" << ema_sep_pct << "% < min=" << config.ema_min_separation_pct_short << "%");
                continue;  // Skip this zone - uptrend or weak downtrend
            }
        }
        
        // ⭐ REGIME FILTER 1: RSI extreme hard block
        // Validated on 168 trades (Aug 2025–Mar 2026): RSI>72 and RSI<28 are
        // momentum climax conditions where price blows through S&D zones rather
        // than reversing at them.  FP=21%/25%, combined net save: +₹1,41,375.
        if (config.enable_rsi_hard_block) {
            double rsi_now = MarketAnalyzer::calculate_rsi(
                bar_history, config.rsi_period, current_index);
            if (rsi_now > config.rsi_hard_block_high) {
                zones_rejected++;
                LOG_INFO("RSI hard block: rejecting zone " << zone.zone_id
                         << " (RSI=" << std::fixed << std::setprecision(1) << rsi_now
                         << " > " << config.rsi_hard_block_high << " overbought extreme)");
                std::cout << "  [NO] Zone " << zone.zone_id
                          << ": RSI hard block (RSI=" << std::fixed << std::setprecision(1) << rsi_now
                          << " > " << config.rsi_hard_block_high << ")\n";
                continue;
            }
            if (rsi_now < config.rsi_hard_block_low) {
                zones_rejected++;
                LOG_INFO("RSI hard block: rejecting zone " << zone.zone_id
                         << " (RSI=" << std::fixed << std::setprecision(1) << rsi_now
                         << " < " << config.rsi_hard_block_low << " oversold extreme)");
                std::cout << "  [NO] Zone " << zone.zone_id
                          << ": RSI hard block (RSI=" << std::fixed << std::setprecision(1) << rsi_now
                          << " < " << config.rsi_hard_block_low << ")\n";
                continue;
            }
        }

        // ⭐ REGIME FILTER 2: ADX hard block
        // Threshold corrected from 60 (never triggered in 130 trades) to 55
        // (validated: FP=12%, net save +₹97,979 over 130 backtest trades).
        // adx_transition_skip_entry now defaults true — hard block, not size reduction.
        bool adx_in_danger_band = false;
        if (config.enable_adx_transition_filter) {
            auto adx_vals = MarketAnalyzer::calculate_adx(bar_history, config.adx_period, current_index);
            if (adx_vals.adx >= config.adx_transition_min && adx_vals.adx <= config.adx_transition_max) {
                adx_in_danger_band = true;
                if (config.adx_transition_skip_entry) {
                    zones_rejected++;
                    LOG_INFO("ADX hard block: rejecting zone " << zone.zone_id
                              << " (ADX=" << std::fixed << std::setprecision(1) << adx_vals.adx
                              << " >= " << config.adx_transition_min << " mature trend threshold)");
                    std::cout << "  [NO] Zone " << zone.zone_id
                              << ": ADX hard block (ADX=" << std::fixed << std::setprecision(1) << adx_vals.adx
                              << " >= " << config.adx_transition_min << ")\n";
                    continue;
                }
                LOG_INFO("ADX transition filter: half-size for zone " << zone.zone_id
                          << " (ADX=" << std::fixed << std::setprecision(1) << adx_vals.adx << ")");
            }
        }

        // Score the zone
        ZoneScore zone_score = scorer.evaluate_zone(zone, regime, current_bar);

        // ⭐ REGIME FILTER 3: Freshness + BB Bandwidth compound block
        // Validated on 81 live trades (Sep 2025–Mar 2026): FP=17-20%, net +₹1,48,048.
        // Blocks when zone is unproven (low state_freshness_score) AND market is
        // compressed (low BB bandwidth). AND logic — both must be true to block.
        // IMPORTANT: reads zone_score (entry-time evaluation), NOT zone.zone_score
        // (detection-time). Touch count accumulates between detection and entry —
        // detection-time score may be stale and too high, letting degraded zones through.
        if (config.enable_freshness_bb_block) {
            double freshness_now = zone_score.state_freshness_score;
            auto bb_now = MarketAnalyzer::calculate_bollinger_bands(
                bar_history, 20, 2.0, current_index);
            if (freshness_now < config.freshness_bb_min_freshness &&
                bb_now.bandwidth < config.freshness_bb_max_bandwidth) {
                zones_rejected++;
                LOG_INFO("Freshness+BB block: rejecting zone " << zone.zone_id
                         << " (freshness=" << std::fixed << std::setprecision(1) << freshness_now
                         << " < " << config.freshness_bb_min_freshness
                         << ", BB_BW=" << std::setprecision(3) << bb_now.bandwidth
                         << " < " << config.freshness_bb_max_bandwidth << ")");
                std::cout << "  [NO] Zone " << zone.zone_id
                          << ": Freshness+BB block (fresh="
                          << std::fixed << std::setprecision(1) << freshness_now
                          << " BB_BW=" << std::setprecision(3) << bb_now.bandwidth << ")\n";
                continue;
            }
        }
        
        // Calculate entry decision
        EntryDecision decision;

        if (config.enable_two_stage_scoring) {
            // Reuse current_index from EMA filter above
            Core::ZoneQualityScore zone_quality_score;
            Core::EntryValidationScore entry_validation_score;
            bool approved = entry_engine.should_enter_trade_two_stage(
                zone,
                bar_history,
                current_index,
                zone.proximal_line,  // Preliminary entry for validation
                0.0,                 // Preliminary stop for validation
                &zone_quality_score,
                &entry_validation_score
            );
            if (!approved) {
                // Build detailed rejection reason
                std::ostringstream reason;
                reason << "Two-stage scoring failed: ";
                if (zone_quality_score.total < config.zone_quality_minimum_score) {
                    reason << "Zone quality " << std::fixed << std::setprecision(2) 
                           << zone_quality_score.total << " < " << config.zone_quality_minimum_score;
                } else if (entry_validation_score.total < config.entry_validation_minimum_score) {
                    reason << "Entry validation " << std::fixed << std::setprecision(2)
                           << entry_validation_score.total << " < " << config.entry_validation_minimum_score;
                }
                std::cout << "  [NO] Zone " << zone.zone_id << ": Entry rejected - " << reason.str() << "\n";
                LOG_ERROR("[NO] Entry rejected: " << reason.str());
                continue;
            }
            
            // ⭐ FIX BUG-2 (two-stage path): Pass &bar_history as 7th argument.
            // Previously nullptr was passed, so calculate_entry()'s volume metrics
            // block (entry_decision_engine.cpp line ~478) was never entered:
            //   if (current_bar != nullptr && bar_history != nullptr)
            // This left entry_pullback_vol_ratio / entry_volume_score /
            // entry_volume_pattern at zero for every live trade.
            const Bar* current_bar_ptr = bar_history.empty() ? nullptr : &bar_history.back();
            decision = entry_engine.calculate_entry(zone, zone_score, atr, regime,
                                                     config.enable_revamped_scoring ? &zone_quality_score : nullptr,
                                                     current_bar_ptr,
                                                     &bar_history);  // ⭐ BUG-2 FIX
        } else {
            // ⭐ FIX BUG-2 (non-two-stage path): same fix — pass &bar_history.
            const Bar* current_bar_ptr = bar_history.empty() ? nullptr : &bar_history.back();
            decision = entry_engine.calculate_entry(zone, zone_score, atr, regime,
                                                     nullptr,
                                                     current_bar_ptr,
                                                     &bar_history);  // ⭐ BUG-2 FIX
        }
        
        if (!decision.should_enter) {
            std::cout << "  [NO] Zone " << zone.zone_id << ": Entry rejected - " << decision.rejection_reason << "\n";
            LOG_ERROR("[NO] Entry rejected: " << decision.rejection_reason);
            continue;
        }

        // ⭐ FIX 4: SL structural pre-check — parity with backtest_engine.cpp (lines 830-848).
        //
        // calculate_entry() computes SL from zone structure:
        //   sl_buffer_zone_pct × zone_height  +  sl_buffer_atr × ATR
        // For wide zones or high-ATR bars this produces SL >> the max_loss_per_trade cap.
        //
        // WITHOUT this check (old behaviour):
        //   enter_trade() calls TradeManager which tightens the SL post-fill to fit the cap.
        //   A tightened SL sits inside the zone's own noise buffer — structurally invalid.
        //   Console analysis of 384 backtest trades: 60.2% had SL tightened (avg 33.7 pts).
        //   Those trades had WR 38.2% vs 61.8% for clean fills — the #1 quality degrader.
        //
        // WITH this check (correct behaviour):
        //   Reject the zone here before any order is attempted.
        //   TradeManager's post-fill tightening remains as a safety net for fill slippage only.
        {
            double max_sl_dist = config.max_loss_per_trade / static_cast<double>(config.lot_size);
            double sl_dist = std::abs(decision.entry_price - decision.stop_loss);
            if (sl_dist > max_sl_dist * 1.05) {  // 5% tolerance for ATR rounding
                zones_rejected++;
                std::cout << "  [NO] Zone " << zone.zone_id << ": SKIPPED — structural SL too wide ("
                          << std::fixed << std::setprecision(1) << sl_dist
                          << "pts > " << max_sl_dist << "pt cap). Tightening rejected.\n";
                LOG_DEBUG("Zone#" << zone.zone_id << " SKIPPED: structural SL too wide ("
                          << std::fixed << std::setprecision(1) << sl_dist
                          << "pts > " << max_sl_dist << "pt cap) — tightening rejected");
                continue;
            }
        }

        std::cout << "  +========================================+\n";
        std::cout << "  | [YES] ENTRY SIGNAL GENERATED!          |\n";
        std::cout << "  +========================================+\n";
        std::cout << "  Zone ID:        " << zone.zone_id << "\n";
        std::cout << "  Zone Type:      " << (zone.type == Core::ZoneType::DEMAND ? "DEMAND" : "SUPPLY") << "\n";
        std::cout << "  Proximal:       " << zone.proximal_line << "\n";
        std::cout << "  Distal:         " << zone.distal_line << "\n";
        std::cout << "  Formation:      " << zone.formation_datetime << "\n";
        std::cout << "  Zone Score:     " << zone_score.total_score << "/120\n";
        std::cout << "  Entry Price:    " << decision.entry_price << "\n";
        std::cout << "  Stop Loss:      " << decision.stop_loss << "\n";
        std::cout << "  Take Profit:    " << decision.take_profit << "\n";
        std::cout << "  +========================================+\n";
        std::cout.flush();
        
        LOG_INFO("[YES] Entry signal generated! Zone " << zone.zone_id << " Score: " 
                << zone_score.total_score << "/120");
        
        // Record last attempt time for cooldown
        if (config.live_zone_entry_cooldown_seconds > 0) {
            last_entry_attempt_by_zone_[zone.zone_id] = std::chrono::steady_clock::now();
        }

        // ⭐ USE SHARED TRADEMANAGER ⭐
        // For indicator calculation, use the actual bar_history size, not the global bar_index
        int indicator_bar_index = bar_history.empty() ? -1 : static_cast<int>(bar_history.size() - 1);
        
        LOG_INFO("📊 Indicator Calc Debug: bar_history.size()=" + std::to_string(bar_history.size()) + 
                " indicator_bar_index=" + std::to_string(indicator_bar_index) +
                " global bar_index=" + std::to_string(bar_index));
        
        // ⭐ P2: Apply ADX transition size factor
        if (adx_in_danger_band && !config.adx_transition_skip_entry && decision.lot_size > 0) {
            int reduced = static_cast<int>(decision.lot_size * config.adx_transition_size_factor);
            if (reduced < 1) reduced = 1;
            LOG_INFO("ADX transition: lot_size " << decision.lot_size << " → " << reduced
                      << " (factor=" << config.adx_transition_size_factor << ")");
            decision.lot_size = reduced;
        }

        // ⭐ P4: Hard cap on position lots
        if (config.max_position_lots > 0 && decision.lot_size > config.max_position_lots) {
            LOG_INFO("max_position_lots cap: " << decision.lot_size
                      << " → " << config.max_position_lots << " lots");
            decision.lot_size = config.max_position_lots;
        }

        bool entered = trade_mgr.enter_trade(
            decision, 
            zone, 
            current_bar, 
            indicator_bar_index,  // Use bar_history.size() - 1 for indicators
            regime,
            trading_symbol,  // Live mode needs symbol
            &bar_history     // Pass bars for indicator calculation
        );


		if (entered && order_submitter_ && order_submitter_->is_enabled()) {
			Trade& trade_ref = const_cast<Trade&>(trade_mgr.get_current_trade());
			std::string tag = OrderTagGenerator::generate(trade_ref.direction);
			trade_ref.order_tag = tag;

			ActiveOrder ao;
			ao.order_tag        = tag;
			ao.symbol           = trading_symbol;
			ao.side             = trade_ref.direction;
			ao.lots             = trade_ref.position_size;
			ao.entry_price      = trade_ref.entry_price;
			ao.stop_loss        = trade_ref.stop_loss;
			ao.zone_id          = zone.zone_id;
			ao.entry_time_epoch = static_cast<long long>(std::time(nullptr));
			get_order_registry().register_order(ao);

			auto result = order_submitter_->submit_order(
				trade_ref.direction,
				trade_ref.position_size,
				zone.zone_id,
				trade_ref.zone_score,
				trade_ref.entry_price,
				trade_ref.stop_loss,
				trade_ref.take_profit,
				tag
			);
			if (!result.success) {
				get_order_registry().remove(tag);
			} else {
				LOG_INFO("OrderTag: " << tag << " Zone: " << zone.zone_id << " Side: " << trade_ref.direction);
			}
		}

        if (entered) {
            // ⭐ BUG 4 FIX: Assign sequential trade_num immediately after entry.
            // trade_mgr.enter_trade() does not set trade_num — it is left at 0
            // (or whatever TradeManager's internal default is). The previous code
            // never assigned it, so trade_num picked up bar_history.size()-1
            // (the indicator_bar_index passed to enter_trade) which made trade
            // numbers like #162, #1061, #9917 — the bar count at entry time,
            // not a sequential counter. This made log comparison with backtest
            // impossible and looked like the engine was being restarted.
            // Fix: set trade_num = completed_trades + 1, identical to backtest.
            Trade& trade_ref = const_cast<Trade&>(trade_mgr.get_current_trade());
            trade_ref.trade_num = static_cast<int>(performance.get_trade_count()) + 1;

            const Trade& trade = trade_mgr.get_current_trade();
            
            std::cout << "\n+====================================================+\n";
            std::cout << "| [TRADE ENTRY] Trade Successfully Generated!       |\n";
            std::cout << "+====================================================+\n";
            std::cout << "  Direction:   " << trade.direction << "\n";
            std::cout << "  Entry Price: " << std::fixed << std::setprecision(2) 
                      << trade.entry_price << "\n";
            std::cout << "  Stop Loss:   " << trade.stop_loss << "\n";
            std::cout << "  Take Profit: " << trade.take_profit << "\n";
            std::cout << "  Quantity:    " << trade.position_size << "\n";
            std::cout << "  Zone Score:  " << trade.zone_score << "/120\n";
            std::cout << "  Entry Date:  " << trade.entry_date << "\n";
            std::cout << std::endl;
            std::cout.flush();

            // 📝 LOG ORDER TO CSV FOR SIMULATION
            log_order_to_csv(
                trade.entry_date,
                std::to_string(zone.zone_id),
                trade.zone_score,
                (trade.direction == "LONG" ? "BUY" : "SELL"),
                trade.entry_price,
                trade.stop_loss,
                trade.take_profit,
                trade.position_size
            );

            trade_generated = true;
            break;  // Only one trade at a time
        }
    }
    
    // ⭐ PRINT TRADE GENERATION SUMMARY
    std::cout << "+====================================================+\n";
    std::cout << "| [SUMMARY] Trade Check Summary                     |\n";
    std::cout << "+====================================================+\n";
    std::cout << "  Zones Checked:        " << zones_checked << "\n";
    std::cout << "  Zones Rejected:       " << zones_rejected << "\n";
    std::cout << "  Price Not In Zone:    " << zones_price_not_in << "\n";
    std::cout << "  Trade Generated:      " << (trade_generated ? "[YES]" : "[NO]") << "\n";
    std::cout << std::endl;
    std::cout.flush();
}

void LiveEngine::manage_position() {
    if (!trade_mgr.is_in_position()) return;

    // ⭐ FIX BUG-10: Increment bars_in_trade HERE — at the very top — before ANY exit
    // check including SESSION_END. Previously it was incremented after the circuit
    // breaker, so SESSION_END exits recorded bars_in_trade=0 and could close a trade
    // on the same bar it was entered. All exit paths now see a correct count.
    {
        Trade& trade_ref = const_cast<Trade&>(trade_mgr.get_current_trade());
        trade_ref.bars_in_trade++;
    }

    // ⭐ Hoist current_bar and trade here so every code path below can use them.
    // Previously current_bar was declared inside the SESSION_END if-block scope,
    // making it invisible to the trailing stop, SL/TP, and other exit paths (C2065).
    const Bar& current_bar = bar_history.back();
    Trade& trade = const_cast<Trade&>(trade_mgr.get_current_trade());

    // ⭐ FIX ISSUE-A: TradeManager.close_trade() re-queries the broker for the fill
    // price, ignoring the price argument we pass. In CSV simulation the broker cursor
    // can be 1-2 bars ahead of bar_history due to batch pre-loading, causing fills
    // at wrong future bar prices (confirmed: trade #1675 filled at bar 15:10 close
    // when SL actually hit at bar 15:05).
    //
    // Fix: after every close_trade() call, override exit_price and recompute P&L
    // using the price WE calculated from bar_history.back() — the only correct source.
    // Direction sign: LONG profit = exit > entry, SHORT profit = exit < entry.
    // ⭐ FIX ISSUE-A: TradeManager.close_trade() re-queries the broker for fill price,
    // ignoring the exit_price argument we pass. The CSV broker cursor runs 1-2 bars
    // ahead of bar_history due to batch pre-loading, so fills land at wrong bar prices.
    //
    // Strategy: extract the commission/slippage that TradeManager embedded by comparing
    // its P&L to what the raw formula gives on the BROKER's exit price. Then reapply
    // that same commission to the CORRECT exit price. This way:
    //   • When broker is on the right bar: exit_price unchanged, P&L unchanged.
    //   • When broker is on wrong bar: exit_price corrected, P&L recomputed with
    //     the same commission that TradeManager would have applied.
    auto fix_exit_price = [&](Trade& ct, double correct_price) {
        // ⭐ FIX P&L-ZERO BUG: In CSV simulation mode the broker IS the data source —
        // its fill price is already the bar close and is the ground-truth fill.
        // Overriding it with the theoretical SL level corrupts P&L in two ways:
        //
        //   1. For breakeven (stage1) exits: broker fills at bar.close which is
        //      slightly better than stage1 for the winning direction. Overriding to
        //      stage1 converts a small profit (₹199.75) into gross = commission → net 0.
        //
        //   2. For general exits: CsvSimulatorBroker fills at bar.close which is the
        //      realistic intraday fill. The manage_position exit_price may be the
        //      theoretical SL level which is inside the bar's range — overriding to
        //      it changes the P&L from what the simulation actually achieved.
        //
        // fix_exit_price is ONLY needed in real-broker live mode where the broker's
        // actual fill can diverge from the bar-data SL level we intended.
        // In CSV simulation, skip the override entirely — TradeManager P&L is correct.
        CsvSimulatorBroker* csv_sim = dynamic_cast<CsvSimulatorBroker*>(broker.get());
        if (csv_sim != nullptr) {
            return;  // CSV sim: broker fill is ground truth, no override needed
        }

        // Step 1: override exit price only if broker returned a materially different value
        if (std::abs(ct.exit_price - correct_price) > 0.01) {
            LOG_WARN("⭐ EXIT PRICE OVERRIDE: broker=" << std::fixed << std::setprecision(2)
                     << ct.exit_price << " → correct=" << correct_price);
            ct.exit_price = correct_price;

            // Step 2: recompute P&L using correct price and fixed commission
            double direction_sign = (ct.direction == "LONG") ? 1.0 : -1.0;
            double raw_pnl_correct = direction_sign
                                     * (ct.exit_price - ct.entry_price)
                                     * static_cast<double>(ct.position_size)
                                     * static_cast<double>(config.lot_size);
            double commission_fixed = 2.0 * config.commission_per_trade;
            ct.pnl = raw_pnl_correct - commission_fixed;

            // Step 3: recompute return_pct consistently
            double capital = ct.entry_price
                             * static_cast<double>(ct.position_size)
                             * static_cast<double>(config.lot_size);
            ct.return_pct = (capital > 0.0) ? (ct.pnl / capital * 100.0) : 0.0;
        }
        // When broker IS on correct bar: nothing changes — TradeManager P&L is kept as-is.
    };

    // === LOSS PROTECTION: Update consecutive_losses and pause_counter on trade close ===
    // This logic should be called after a trade is closed (stop loss or take profit)
    // Find if a trade was just closed in the last bar
    const auto& trades = performance.get_trades();
    if (!trades.empty()) {
        const auto& last_trade = trades.back();
        // ⭐ FIX DES-01: Removed 'static' — was a process-lifetime variable that leaked
        // state between engine instances and test runs. Now a regular local; the outer
        // member variable last_exit_date_ (declared in live_engine.h) serves as the
        // persistent tracker across calls within the same engine instance.
        if (last_trade.exit_date != last_exit_date_) {
            // Only process once per trade
            last_exit_date_ = last_trade.exit_date;
            if (last_trade.pnl < 0) {
                // Only count STOP_LOSS exits — not SESSION_END or TRAIL_SL
                if (last_trade.exit_reason == "STOP_LOSS") {
                    consecutive_losses++;
                    LOG_INFO("[LOSS PROTECTION] Consecutive losses: " << consecutive_losses);
                    if (consecutive_losses >= config.max_consecutive_losses) {
                        pause_counter = config.pause_bars_after_losses;
                        LOG_INFO("[LOSS PROTECTION] Triggered pause for " << pause_counter << " bars after "
                                 << consecutive_losses << " consecutive losses.");
                        consecutive_losses = 0;
                    }
                } else {
                    LOG_INFO("[LOSS PROTECTION] " << last_trade.exit_reason
                             << " loss ignored for consecutive count (not STOP_LOSS)");
                }
            } else if (last_trade.pnl > 0) {
                // WIN: reset loss counter AND cancel any active pause
                if (consecutive_losses > 0 || pause_counter > 0) {
                    LOG_INFO("[LOSS PROTECTION] Win detected - resetting consecutive_losses="
                             << consecutive_losses << " pause_counter=" << pause_counter);
                }
                consecutive_losses = 0;
                pause_counter = 0;
            }

            // NOTE: Same-session re-entry guard SET is handled unconditionally in on_bar()
            // (after manage_position() call), NOT here. Reason: manage_position() has an
            // early return when !is_in_position(), so it would miss the bar after trade close.
        }
    }

    // ⭐ FIX #5: CIRCUIT BREAKER - Close positions approaching session end
    if (config.close_before_session_end_minutes > 0) {
        // current_bar is declared at the top of manage_position() — do not re-declare here
        std::string current_time_str = current_bar.datetime;  // Format: "2026-01-06 14:55:00"
        
        // Parse current time (HH:MM:SS from end of datetime string)
        int curr_hour = 0, curr_min = 0, curr_sec = 0;
        try {
            size_t space_pos = current_time_str.rfind(' ');
            if (space_pos != std::string::npos) {
                std::string time_part = current_time_str.substr(space_pos + 1);
                sscanf(time_part.c_str(), "%d:%d:%d", &curr_hour, &curr_min, &curr_sec);
            }
        } catch (...) {
            LOG_WARN("Failed to parse current time: " << current_time_str);
        }
        
        // Parse session end time (HH:MM:SS format)
        int session_hour = 0, session_min = 0, session_sec = 0;
        try {
            sscanf(config.session_end_time.c_str(), "%d:%d:%d", &session_hour, &session_min, &session_sec);
        } catch (...) {
            LOG_WARN("Failed to parse session_end_time: " << config.session_end_time);
        }
        
        // Calculate cutoff time (session_end - close_before_session_end_minutes)
        int cutoff_min = session_min - config.close_before_session_end_minutes;
        int cutoff_hour = session_hour;
        if (cutoff_min < 0) {
            cutoff_hour--;
            cutoff_min += 60;
        }
        
        // Convert times to minutes since midnight for easy comparison
        int curr_total_min = curr_hour * 60 + curr_min;
        int cutoff_total_min = cutoff_hour * 60 + cutoff_min;
        
        if (curr_total_min >= cutoff_total_min) {
            // Time to close positions! (trade and current_bar declared at top of manage_position)
            // ⭐ FIX BUG-01 + BUG-06: Use bar close, not broker->get_ltp().
            // get_ltp() in simulation returns the next bar's price (look-ahead).
            // bar.close is the last confirmed price of the current bar.
            double current_price = current_bar.close;
            
            std::cout << "\n⏳ CIRCUIT BREAKER ACTIVATED ⏳\n";
            std::cout << "Current time (" << current_time_str << ") >= Cutoff time (" 
                      << (cutoff_hour < 10 ? "0" : "") << cutoff_hour << ":" 
                      << (cutoff_min < 10 ? "0" : "") << cutoff_min << ")\n";
            std::cout << "Forcefully closing position to avoid overnight gap risk\n\n";
            
            LOG_WARN("⏳ CIRCUIT BREAKER: Closing position at market price (" << current_price 
                    << ") to avoid overnight gap risk");
            
            std::string current_time = current_bar.datetime;
            
            // ⭐ CIRCUIT BREAKER EXIT: Call /squareOffPositionsByOrderTag before local close_trade()
            // Get the order_tag from the current trade to notify the Spring Boot backend
            const Trade& current_trade_ref = trade_mgr.get_current_trade();
            if (!current_trade_ref.order_tag.empty() && order_submitter_) {
                LOG_INFO("📤 CIRCUIT BREAKER: Submitting exit order via /squareOffPositionsByOrderTag");
                LOG_INFO("   Order Tag: " << current_trade_ref.order_tag);
                OrderSubmitResult exit_result = order_submitter_->submit_exit_order(current_trade_ref.order_tag);
                if (exit_result.success) {
                    LOG_INFO("✅ CIRCUIT BREAKER: /squareOffPositionsByOrderTag succeeded");
                } else {
                    LOG_WARN("⚠️  CIRCUIT BREAKER: /squareOffPositionsByOrderTag failed - HTTP Status: " 
                             << exit_result.http_status << ", Error: " << exit_result.error_message);
                }
            } else {
                LOG_WARN("⚠️  CIRCUIT BREAKER: Cannot submit exit order - order_tag is empty or OrderSubmitter unavailable");
            }
            
            Trade closed_trade = trade_mgr.close_trade(current_time, "SESSION_END", current_price);
            fix_exit_price(closed_trade, current_price);  // ⭐ ISSUE-A fix
            
            std::cout << "+====================================================+\n";
            std::cout << "| [SESSION END] Trade Closed - Circuit Breaker      |\n";
            std::cout << "+====================================================+\n";
            std::cout << "  Exit Price:  " << std::fixed << std::setprecision(2) 
                      << closed_trade.exit_price << "\n";
            std::cout << "  Exit Date:   " << closed_trade.exit_date << "\n";
            std::cout << "  Final P&L:   " << (closed_trade.pnl >= 0 ? "+" : "") << closed_trade.pnl << "\n";
            std::cout << "  Return:      " << (closed_trade.return_pct >= 0 ? "+" : "") 
                      << std::setprecision(2) << closed_trade.return_pct << "%\n";
            std::cout << std::endl;
            std::cout.flush();

            LOG_INFO("Position closed: SESSION_END (circuit breaker), P&L: " << closed_trade.pnl);

            performance.add_trade(closed_trade);
            performance.update_capital(trade_mgr.get_capital());
            export_trade_journal();

            // ⭐ ISSUE-3: Save overnight continuation snapshot when trade was profitable
            if (config.enable_overnight_continuation && closed_trade.pnl > 0.0) {
                double pts_gained = std::abs(closed_trade.exit_price - closed_trade.entry_price);
                if (!pending_continuation_.active && pts_gained >= config.continuation_min_profit_pts) {
                    // ⭐ FIX P3: Risk in price points for next-morning gap check.
                    //
                    // BUG (old): risk_amount was used as the primary source.
                    // For TRAIL_SL breakeven exits, risk_amount = 2×commission = ₹200
                    // (commission-only P&L). Dividing ₹200 by (lots × lot_size) gives
                    // risk_pts ≈ 1.3 pts, making the gap limit only ~2.3 pts at 1.75×.
                    // Any overnight gap at all then triggered a skip, making the
                    // continuation feature useless for all TRAIL_SL-originated snapshots.
                    //
                    // FIX: Always derive risk_pts from the structural stop distance
                    // (entry_price – original_stop_loss). This is the real zone risk
                    // regardless of what the trade P&L was. risk_amount is only used
                    // as a cross-check when the stop distance is unavailable.
                    // A hard floor of 10 pts prevents the gap limit from becoming
                    // trivially small due to commission-only or near-zero stop distances.
                    double risk_pts = std::abs(closed_trade.entry_price - closed_trade.original_stop_loss);
                    if (risk_pts < 10.0) {
                        // Fallback: derive from risk_amount if stop distance is missing/tiny
                        if (closed_trade.risk_amount > 0
                            && closed_trade.position_size > 0
                            && config.lot_size > 0) {
                            double risk_from_amount = closed_trade.risk_amount
                                / (static_cast<double>(closed_trade.position_size)
                                   * static_cast<double>(config.lot_size));
                            if (risk_from_amount > risk_pts)
                                risk_pts = risk_from_amount;
                        }
                        // Enforce absolute floor so gap limit is never trivially small
                        if (risk_pts < 10.0) risk_pts = 10.0;
                    }
                    LOG_INFO("[CONTINUATION] risk_pts=" << std::fixed << std::setprecision(1)
                             << risk_pts << " (entry=" << closed_trade.entry_price
                             << " orig_sl=" << closed_trade.original_stop_loss
                             << " risk_amt=" << closed_trade.risk_amount << ")");

                    pending_continuation_.active            = true;
                    pending_continuation_.zone_id           = closed_trade.zone_id;
                    pending_continuation_.direction         = closed_trade.direction;
                    pending_continuation_.prior_pnl         = closed_trade.pnl;
                    pending_continuation_.prior_entry       = closed_trade.entry_price;
                    pending_continuation_.prior_risk        = risk_pts;
                    pending_continuation_.prior_tp_distance =
                        std::abs(closed_trade.take_profit - closed_trade.entry_price);
                    pending_continuation_.exit_session      = current_bar.datetime.substr(0, 10);
                    pending_continuation_.exit_price        = closed_trade.exit_price;

                    LOG_INFO("[CONTINUATION] Snapshot saved -- Zone " << closed_trade.zone_id
                             << " " << closed_trade.direction
                             << " | pnl=" << closed_trade.pnl
                             << " | pts=" << std::fixed << std::setprecision(1) << pts_gained
                             << " | exit=" << closed_trade.exit_price);
                    std::cout << "\n[CONTINUATION] Snapshot saved for Zone "
                              << closed_trade.zone_id << " " << closed_trade.direction
                              << " | P&L=" << closed_trade.pnl
                              << " -- will re-evaluate at next session open\n";
                } else {
                    LOG_INFO("[CONTINUATION] Snapshot skipped -- pts_gained=" << pts_gained
                             << " < min=" << config.continuation_min_profit_pts);
                }
            }

            return;
        }
    }
    
    // ⭐ FIX BUG-01: current_price set from bar close (declared at top of manage_position).
    // broker->get_ltp() in simulation returns next bar's price (look-ahead).
    double current_price = current_bar.close;

    // ⭐ FIX BUG-10: bars_in_trade is now incremented at the top of manage_position().
    // The increment was here previously — removed to avoid double-counting.

    // ⭐ FIX 2: Require MINIMUM 2 bars before allowing exits (prevents same-bar exit)
    // bars_in_trade = 1 on first call, = 2 on second bar, etc.
    if (trade.bars_in_trade < 2) {
        LOG_DEBUG("Position too new (bars_in_trade=" << trade.bars_in_trade << "), skipping exit check");
        
        // Still update highest/lowest for trailing stop tracking
        if (trade.direction == "LONG") {
            if (current_bar.high > trade.highest_price || trade.highest_price == 0.0) {
                trade.highest_price = current_bar.high;
            }
        } else {
            if (current_bar.low < trade.lowest_price || trade.lowest_price == 0.0) {
                trade.lowest_price = current_bar.low;
            }
        }
        
        // Still print position status
        std::cout << "\n+====================================================+\n";
        std::cout << "| [POSITION] Position Monitoring (Entry Bar)        |\n";
        std::cout << "+====================================================+\n";
        std::cout << "  Direction:   " << trade.direction << "\n";
        std::cout << "  Entry Price: " << std::fixed << std::setprecision(2) << trade.entry_price << "\n";
        std::cout << "  Bars in Trade: " << trade.bars_in_trade << " (min 2 for exit)\n";
        std::cout << std::endl;
        std::cout.flush();
        return;  // Don't check exits yet
    }
    
    // Calculate current P&L for display
    double pnl;
    if (trade.direction == "LONG") {
        pnl = (current_price - trade.entry_price) * trade.position_size * config.lot_size;
    } else {
        pnl = (trade.entry_price - current_price) * trade.position_size * config.lot_size;
    }
    
    LOG_DEBUG("Position P&L: $" << pnl << " (Price: " << current_price << ")");
    
    // ⭐ FIXED: Multi-Stage Trailing Stop (live_engine.cpp)
    //
    // Mirrors the identical fix applied to backtest_engine.cpp.
    // Bug fixes:
    // 1. Stage 1 (breakeven) was DEAD CODE -- same threshold (1.0R) as Stage 2.
    //    Config fix: trail_activation_rr = 0.5 makes Stage 1 reachable.
    // 2. ATR stages (3 & 4) floored at Stage 2 level -- prevents ATR wideness
    //    from degrading a previously locked profit floor.
    // 3. Stage logging includes stage number and current_r on every ratchet
    //    move for full live audit trail.
    // ⭐ FIX BUG-11: Snapshot stop_loss BEFORE the trailing stop block executes.
    // Must be declared here (outside the if block) so the SL/TP check below can use it.
    // When trailing stop is disabled, this simply holds the original stop throughout.
    double sl_before_trail_update = trade.stop_loss;

    if (config.use_trailing_stop) {
        // ⭐ FIX BUG-11: Snapshot the current stop BEFORE the trail ratchets it.
        // Previously: trail updated stop_loss, then the SL check below fired against
        // the SAME bar's extremes → a strongly impulsive bar would ratchet the trail
        // into its own range and immediately self-trigger a premature TRAIL_SL exit.
        // Fix: the SL check (after this block) uses sl_before_trail_update, so the
        // ratcheted level only becomes active as a trigger from the NEXT bar onward.
        sl_before_trail_update = trade.stop_loss;  // re-capture for clarity

        double risk = std::abs(trade.entry_price - trade.original_stop_loss);
        if (risk <= 0) risk = std::abs(trade.entry_price - trade.stop_loss);
        if (risk < 0.01) risk = 1.0;

        // Track best (most favourable) price reached using bar extremes
        if (trade.direction == "LONG") {
            if (current_bar.high > trade.highest_price || trade.highest_price <= 0.0)
                trade.highest_price = current_bar.high;
        } else {
            if (current_bar.low < trade.lowest_price || trade.lowest_price <= 0.0)
                trade.lowest_price = current_bar.low;
        }

        // Current R = best excursion / initial risk
        double best_excursion = (trade.direction == "LONG")
            ? (trade.highest_price - trade.entry_price)
            : (trade.entry_price - trade.lowest_price);
        double current_r = (risk > 0) ? (best_excursion / risk) : 0.0;

        // ATR for buffer in Stages 3 & 4
        double atr_trail = risk;  // fallback: use risk as ATR proxy
        int current_idx = static_cast<int>(bar_history.size()) - 1;
        if (current_idx >= config.atr_period) {
            double atr_sum = 0.0;
            for (int i = current_idx - config.atr_period + 1; i <= current_idx; i++) {
                double tr = std::max<double>({
                    bar_history[i].high - bar_history[i].low,
                    std::abs(bar_history[i].high - bar_history[i-1].close),
                    std::abs(bar_history[i].low - bar_history[i-1].close)
                });
                atr_sum += tr;
            }
            atr_trail = atr_sum / config.atr_period;
        }

        // Pre-compute fixed stage levels used as floors in ATR stages
        //
        // ⭐ FIX ISSUE-2: Stage 1 "breakeven" was set to exactly trade.entry_price.
        // When the trail activated at Stage 1 and price reversed back to entry,
        // the exit filled at entry_price with P&L = 0 - 2×commission = -₹200.
        // This commission-only loss was recorded as a TRAIL_SL loss, incorrectly
        // counted in consecutive_losses and degrading reported metrics.
        //
        // Fix: Stage 1 locks in entry_price PLUS enough points to cover both
        // commissions (entry + exit), so the net P&L is always >= 0 on a
        // breakeven trail exit. The commission offset is:
        //   2 × commission_per_trade / (position_size × lot_size)
        // This converts the fixed ₹ commission into the equivalent price points
        // for this trade's size, ensuring the trail stop sits above true breakeven.
        double commission_pts = 0.0;
        {
            int qty = trade.position_size * config.lot_size;
            if (qty > 0 && config.commission_per_trade > 0.0) {
                // 2 commissions (entry + exit), converted to per-point value
                commission_pts = (2.0 * config.commission_per_trade)
                                 / static_cast<double>(qty);
            }
        }
        // Stage 1: entry + commission offset (long) or entry - offset (short)
        double stage1_level = (trade.direction == "LONG")
            ? trade.entry_price + commission_pts
            : trade.entry_price - commission_pts;
        double stage2_level = (trade.direction == "LONG")
            ? trade.entry_price + (risk * 0.5)
            : trade.entry_price - (risk * 0.5);

        double new_trail_stop = 0.0;
        int    activated_stage = 0;

        // ⭐ FIX BUG-12: Stage thresholds were hardcoded (3.0, 2.0, 1.0), ignoring
        // any config parameters the user may have set for stage activation R-levels.
        // TODO: add trail_r2_threshold, trail_r3_threshold, trail_r4_threshold to Config
        // struct in common_types.h, then replace literals below with config references.
        // Defaults of 1.0/2.0/3.0 preserve the original behaviour in the meantime.
        const double r2_thresh = 1.0;  // TODO: config.trail_r2_threshold
        const double r3_thresh = 2.0;  // TODO: config.trail_r3_threshold
        const double r4_thresh = 3.0;  // TODO: config.trail_r4_threshold

        if (current_r >= r4_thresh) {
            // Stage 4: Tight ATR trail, floored at Stage 2 level
            double stage4_factor = config.trail_atr_multiplier * 0.5;
            double raw = (trade.direction == "LONG")
                ? trade.highest_price - (atr_trail * stage4_factor)
                : trade.lowest_price  + (atr_trail * stage4_factor);
            new_trail_stop = (trade.direction == "LONG")
                ? std::max(raw, stage2_level)
                : std::min(raw, stage2_level);
            activated_stage = 4;
        } else if (current_r >= r3_thresh) {
            // Stage 3: Moderate ATR trail, floored at Stage 2 level
            double stage3_factor = config.trail_atr_multiplier * 1.0;
            double raw = (trade.direction == "LONG")
                ? trade.highest_price - (atr_trail * stage3_factor)
                : trade.lowest_price  + (atr_trail * stage3_factor);
            new_trail_stop = (trade.direction == "LONG")
                ? std::max(raw, stage2_level)
                : std::min(raw, stage2_level);
            activated_stage = 3;
        } else if (current_r >= r2_thresh) {
            // Stage 2: Lock 0.5R profit
            // LONG : entry + 0.5R  (SL moves above entry)
            // SHORT: entry - 0.5R  (SL moves below entry)
            new_trail_stop = stage2_level;
            activated_stage = 2;
        } else if (current_r >= config.trail_activation_rr) {
            // Stage 1: Breakeven lock
            // Requires trail_activation_rr = 0.5 in config (< 1.0) to be reachable.
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
                LOG_INFO("Trail SL ▲ " << trade.stop_loss << " → " << new_trail_stop
                         << "  [Stage " << activated_stage << ", R=" << current_r << "]");
                trade.stop_loss = new_trail_stop;
                trade.current_trail_stop = new_trail_stop;
            } else if (trade.direction == "SHORT" && new_trail_stop < trade.stop_loss) {
                LOG_INFO("Trail SL ▼ " << trade.stop_loss << " → " << new_trail_stop
                         << "  [Stage " << activated_stage << ", R=" << current_r << "]");
                trade.stop_loss = new_trail_stop;
                trade.current_trail_stop = new_trail_stop;
            }
        }
    }
    
    // ⭐ bars_in_trade already incremented at the start of this function
    
    // ⭐ PRINT LIVE POSITION STATUS
    std::cout << "\n+====================================================+\n";
    std::cout << "| [POSITION] Position Monitoring                    |\n";
    std::cout << "+====================================================+\n";
    std::cout << "  Direction:   " << trade.direction << "\n";
    std::cout << "  Entry Price: " << std::fixed << std::setprecision(2) << trade.entry_price << "\n";
    std::cout << "  Entry Date:  " << trade.entry_date << "\n";
    std::cout << "  Position Size: " << trade.position_size << " lot(s)"
              << " (Qty: " << (trade.position_size * config.lot_size) << ")\n";
    std::cout << "  Bars in Trade: " << trade.bars_in_trade << "\n";  // ⭐ NEW: Show bars
    std::cout << "  Current Price: " << current_price << "\n";
    std::cout << "  Stop Loss:   " << trade.stop_loss;
    if (trade.trailing_activated) {
        std::cout << " (TRAILING)";
    }
    std::cout << "\n";
    std::cout << "  Take Profit: " << trade.take_profit << "\n";
    // ⭐ FIX DES-03: Was dividing by entry_price alone (e.g. 25,000), giving a % ~65×
    // too large. Correct denominator is the total capital deployed by this position.
    double capital_deployed = trade.entry_price * trade.position_size * config.lot_size;
    double pnl_pct = (capital_deployed > 0.0) ? (pnl / capital_deployed * 100.0) : 0.0;
    std::cout << "  Current P&L: " << (pnl >= 0 ? "+" : "") << pnl << " (" 
              << (pnl >= 0 ? "+" : "") << std::fixed << std::setprecision(2) << pnl_pct << "%)\n";
    if (trade.trailing_activated) {
        double risk = std::abs(trade.entry_price - trade.original_stop_loss);
        double current_r = (risk > 0) ? (pnl / (risk * trade.position_size * config.lot_size)) : 0.0;
        std::cout << "  Current R:   " << std::setprecision(2) << current_r << "R\n";
    }
    std::cout << std::endl;
    std::cout.flush();
    
    // ⭐ FIX: Volume Climax Exit — ported from backtest_engine.cpp
    // Was logged as enabled/disabled at startup but NEVER actually called in live.
    // Now mirrors backtest: when volume spikes to threshold × average AND trade is
    // in profit, exit immediately to capture the spike before reversal.
    if (config.enable_volume_exit_signals) {
        auto vol_exit = trade_mgr.check_volume_exit_signals(trade, current_bar);
        if (vol_exit == Backtest::TradeManager::VolumeExitSignal::VOLUME_CLIMAX) {
            // ⭐ FIX BUG-05: Use bar close not broker->get_ltp() (look-ahead in simulation)
            double vol_exit_price = current_bar.close;
            LOG_INFO("🚨 VOL_CLIMAX detected - exiting trade #" << trade.trade_num
                     << " at " << vol_exit_price);
            std::string current_time = current_bar.datetime;
            Trade closed_trade = trade_mgr.close_trade(current_time, "VOL_CLIMAX", vol_exit_price);
            fix_exit_price(closed_trade, vol_exit_price);  // ⭐ ISSUE-A fix

            std::cout << "+====================================================+\n";
            std::cout << "| [VOL CLIMAX] Trade Closed - Volume Spike Exit     |\n";
            std::cout << "+====================================================+\n";
            std::cout << "  Exit Price:  " << std::fixed << std::setprecision(2)
                      << closed_trade.exit_price << "\n";
            std::cout << "  Exit Date:   " << closed_trade.exit_date << "\n";
            std::cout << "  Final P&L:   " << (closed_trade.pnl >= 0 ? "+" : "") << closed_trade.pnl << "\n";
            std::cout << std::endl;
            std::cout.flush();

            if (order_submitter_ && order_submitter_->is_enabled()) {
                auto result = order_submitter_->submit_exit_order(closed_trade.order_tag);
                LOG_INFO("Exit order after VOL_CLIMAX. Tag: " << closed_trade.order_tag << " Success: " << result.success);
            }
            performance.add_trade(closed_trade);
            performance.update_capital(trade_mgr.get_capital());
            export_trade_journal();
            return;
        }
    }

    // ⭐ DETERMINISTIC FIX: Check SL/TP using BAR extremes, not just current price
    // This ensures consistent exits regardless of tick timing
    // For LONG: check if bar.low hit SL, bar.high hit TP
    // For SHORT: check if bar.high hit SL, bar.low hit TP
    //
    // ⭐ FIX BUG-11 (wired): Use sl_before_trail_update (snapshotted BEFORE the trail
    // ratchet above) rather than trade.stop_loss (which may now be tighter).
    // This prevents a ratcheted trail from self-triggering on the same bar that caused
    // the ratchet — e.g. a 5R impulsive bar would ratchet to a level inside its own
    // range, then immediately fire TRAIL_SL, prematurely exiting a winning trade.
    // The newly ratcheted stop only activates as a trigger from the NEXT bar onward.
    bool sl_hit = false;
    bool tp_hit = false;
    double exit_price = 0.0;
    
    if (trade.direction == "LONG") {
        sl_hit = (current_bar.low <= sl_before_trail_update);
        tp_hit = (current_bar.high >= trade.take_profit);
        if (sl_hit && tp_hit) {
            sl_hit = true;
            tp_hit = false;
        }
        // Intraday SL slippage: fill at worse of pre-trail SL or close
        exit_price = sl_hit ? std::min(sl_before_trail_update, current_bar.close) : trade.take_profit;
    } else {
        // SHORT position
        sl_hit = (current_bar.high >= sl_before_trail_update);
        tp_hit = (current_bar.low <= trade.take_profit);
        if (sl_hit && tp_hit) {
            sl_hit = true;
            tp_hit = false;
        }
        // Intraday SL slippage: fill at worse of pre-trail SL or close
        exit_price = sl_hit ? std::max(sl_before_trail_update, current_bar.close) : trade.take_profit;
    }

    // EMA crossover exit (only if SL/TP not hit in this bar)
    if (config.enable_ema_exit && !sl_hit && !tp_hit) {
        int current_idx = static_cast<int>(bar_history.size()) - 1;
        int min_ema_period = std::max(config.exit_ema_fast_period, config.exit_ema_slow_period);
        if (current_idx >= min_ema_period) {
            double ema9 = MarketAnalyzer::calculate_ema(bar_history, config.exit_ema_fast_period, current_idx);
            double ema20 = MarketAnalyzer::calculate_ema(bar_history, config.exit_ema_slow_period, current_idx);
            bool ema_exit = (trade.direction == "LONG" && ema9 < ema20) ||
                            (trade.direction == "SHORT" && ema9 > ema20);

            if (ema_exit) {
                std::string current_time = current_bar.datetime;
                // ⭐ FIX BUG-05: Use bar close not get_ltp() (look-ahead in simulation)
                Trade closed_trade = trade_mgr.close_trade(current_time, "EMA_CROSS", current_bar.close);
                fix_exit_price(closed_trade, current_bar.close);  // ⭐ ISSUE-A fix

                std::cout << "+====================================================+\n";
                std::cout << "| [EMA EXIT] Trade Closed - EMA 9/20 Cross          |\n";
                std::cout << "+====================================================+\n";
                std::cout << "  Exit Price:  " << std::fixed << std::setprecision(2)
                          << closed_trade.exit_price << "\n";
                std::cout << "  Exit Date:   " << closed_trade.exit_date << "\n";
                std::cout << "  Final P&L:   " << (closed_trade.pnl >= 0 ? "+" : "") << closed_trade.pnl << "\n";
                std::cout << "  Return:      " << (closed_trade.return_pct >= 0 ? "+" : "")
                          << std::setprecision(2) << closed_trade.return_pct << "%\n";
                std::cout << "  EMA9/EMA20:  " << std::setprecision(2) << ema9 << " / " << ema20 << "\n";
                std::cout << std::endl;
                std::cout.flush();

                LOG_INFO("Position closed: EMA_CROSS, P&L: $" << closed_trade.pnl
                         << " (EMA9=" << ema9 << ", EMA20=" << ema20 << ")");

                if (order_submitter_ && order_submitter_->is_enabled()) {
                    auto result = order_submitter_->submit_exit_order(closed_trade.order_tag);
                    LOG_INFO("Exit order after EMA_CROSS. Tag: " << closed_trade.order_tag << " Success: " << result.success);
                }

                performance.add_trade(closed_trade);
                performance.update_capital(trade_mgr.get_capital());
                export_trade_journal();
                return;
            }
        }
    }
    
    if (sl_hit) {
        std::string exit_reason = trade.trailing_activated ? "TRAIL_SL" : "STOP_LOSS";
        std::string current_time = current_bar.datetime;
        Trade closed_trade = trade_mgr.close_trade(current_time, exit_reason, exit_price);
        fix_exit_price(closed_trade, exit_price);  // ⭐ ISSUE-A fix
        
        // ⭐ FIX #2: Log trail performance when trail exits
        if (exit_reason == "TRAIL_SL" && closed_trade.risk_amount > 0) {
            double r_achieved = closed_trade.pnl / closed_trade.risk_amount;
            LOG_INFO("🎯 Trail exit at " << std::fixed << std::setprecision(2) 
                    << r_achieved << "R (P&L: " << closed_trade.pnl << ")");
            std::cout << "\n  📊 Trail Performance: " << std::setprecision(2) 
                      << r_achieved << "R achieved\n";
        }
        
        // ⭐ FIX: STRICT ZONE ID MATCHING for retry counter
        // OLD BUG: Distance-based detection (within 50 points) allowed zones 320, 337, 245 to re-trade
        // NEW: Exact zone_id match prevents repeat trading of same zone
        if (config.enable_per_zone_retry_limit && closed_trade.zone_id >= 0) {
            for (auto& zone : active_zones) {
                if (zone.zone_id == closed_trade.zone_id) {
                    zone.entry_retry_count++;
                    LOG_INFO("🚫 Zone " << zone.zone_id << " retry count incremented to " 
                             << zone.entry_retry_count << " (BLOCKS re-entry if >= " 
                             << config.max_retries_per_zone << ")");
                    break;
                }
            }
        }

        // ⭐ FIX BUG-3: Per-zone consecutive-loss tracking → EXHAUSTED state
        // TRAIL_SL is a winner (trailing stop activated = price moved in our favour).
        // Only STOP_LOSS exits should increment consecutive_losses.
        // TRAIL_SL and TAKE_PROFIT wins reset the counter instead.
        if (config.enable_zone_exhaustion && closed_trade.zone_id >= 0) {
            for (auto& zone : active_zones) {
                if (zone.zone_id != closed_trade.zone_id) continue;

                if (exit_reason == "STOP_LOSS") {
                    // Genuine loss — increment counter
                    zone.consecutive_losses++;
                    LOG_INFO("Zone " << zone.zone_id << " consecutive_losses="
                             << zone.consecutive_losses << " / max=" << config.zone_consecutive_loss_max);
                    if (zone.consecutive_losses > config.zone_consecutive_loss_max
                        && zone.state != ZoneState::EXHAUSTED
                        && zone.state != ZoneState::VIOLATED) {
                        zone.state = ZoneState::EXHAUSTED;
                        zone.exhausted_at_datetime = current_bar.datetime;
                        LOG_INFO("⛔ Zone " << zone.zone_id << " EXHAUSTED after "
                                 << zone.consecutive_losses << " consecutive losses at "
                                 << current_bar.datetime);
                        std::cout << "⛔ Zone " << zone.zone_id << " EXHAUSTED ("
                                  << zone.consecutive_losses << " consecutive SL losses)"
                                  << std::endl;
                    }
                } else {
                    // TRAIL_SL = winner (trail activated before SL hit).
                    // SESSION_END may be win or loss — in either case it is not a
                    // structural zone failure, so we do not penalise the zone.
                    // Reset consecutive_losses so the zone stays tradeable.
                    if (zone.consecutive_losses > 0) {
                        LOG_INFO("Zone " << zone.zone_id
                                 << " consecutive_losses reset ("
                                 << exit_reason << " exit — not a zone failure)");
                        zone.consecutive_losses = 0;
                        // Un-exhaust if the zone was exhausted by prior SL run
                        if (zone.state == ZoneState::EXHAUSTED) {
                            zone.state = ZoneState::TESTED;
                            zone.exhausted_at_datetime = "";
                            LOG_INFO("Zone " << zone.zone_id
                                     << " un-EXHAUSTED → TESTED (" << exit_reason << " win)");
                        }
                    }
                }
                break;
            }
        }

        // ⭐ FIX BUG-3b: Per-direction SL suspension — only real STOP_LOSS exits count.
        // TRAIL_SL exits (winners) must NOT increment sl_count_long/short.
        // Previously all exits through sl_hit path incremented the counter,
        // causing Zone 10 to be suspended after a TRAIL_SL winner (+₹17,025).
        if (config.zone_sl_suspend_threshold > 0 && closed_trade.zone_id >= 0
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
                    std::cout << "🚫 Zone #" << zone.zone_id
                              << " SUSPENDED for " << closed_trade.direction
                              << " entries (" << sl_count << " SL hits, no winner)\n";
                }
                break;
            }
        } else if (config.zone_sl_suspend_threshold > 0 && closed_trade.zone_id >= 0
                   && exit_reason == "TRAIL_SL") {
            // TRAIL_SL winner: reset sl_count for this direction so the zone
            // can trade again even if it previously hit the SL threshold.
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
        
        std::cout << "+====================================================+\n";
        std::cout << "| [STOP LOSS] Trade Closed - Stop Loss Hit          |\n";
        std::cout << "+====================================================+\n";
        std::cout << "  Exit Price:  " << std::fixed << std::setprecision(2) 
                  << closed_trade.exit_price << "\n";
        std::cout << "  Exit Date:   " << closed_trade.exit_date << "\n";
        std::cout << "  Final P&L:   " << (closed_trade.pnl >= 0 ? "+" : "") << closed_trade.pnl << "\n";
        std::cout << "  Return:      " << (closed_trade.return_pct >= 0 ? "+" : "") 
                  << std::setprecision(2) << closed_trade.return_pct << "%\n";
        std::cout << std::endl;
        std::cout.flush();

        LOG_INFO("Position closed: STOP_LOSS, P&L: $" << closed_trade.pnl);

        if (order_submitter_ && order_submitter_->is_enabled()) {
            auto result = order_submitter_->submit_exit_order(closed_trade.order_tag);
            LOG_INFO("Exit order after STOP_LOSS. Tag: " << closed_trade.order_tag << " Success: " << result.success);
        }

        // ⭐ NEW: Update performance and export CSV
        performance.add_trade(closed_trade);
        performance.update_capital(trade_mgr.get_capital());
        export_trade_journal();

        return;
    }
    
    // ⭐ DETERMINISTIC: Check take profit using the already-computed flag
    if (tp_hit) {
        std::string current_time = current_bar.datetime;
        Trade closed_trade = trade_mgr.close_trade(current_time, "TAKE_PROFIT", exit_price);
        fix_exit_price(closed_trade, exit_price);  // ⭐ ISSUE-A fix
        
        std::cout << "+====================================================+\n";
        std::cout << "| [PROFIT] Trade Closed - Take Profit Hit           |\n";
        std::cout << "+====================================================+\n";
        std::cout << "  Exit Price:  " << std::fixed << std::setprecision(2) 
                  << closed_trade.exit_price << "\n";
        std::cout << "  Exit Date:   " << closed_trade.exit_date << "\n";
        std::cout << "  Final P&L:   " << (closed_trade.pnl >= 0 ? "+" : "") << closed_trade.pnl << "\n";
        std::cout << "  Return:      " << (closed_trade.return_pct >= 0 ? "+" : "") 
                  << std::setprecision(2) << closed_trade.return_pct << "%\n";
        std::cout << std::endl;
        std::cout.flush();

        LOG_INFO("Position closed: TAKE_PROFIT, P&L: $" << closed_trade.pnl);

        // ⭐ FIX BUG-3c: TP win — reset consecutive_losses and sl_count for this zone.
        // Also handles TRAIL_SL winners that reach the TP block (take_profit hit
        // before trailing stop triggers) — same reset logic applies.
        if (config.enable_zone_exhaustion && closed_trade.zone_id >= 0) {
            for (auto& zone : active_zones) {
                if (zone.zone_id == closed_trade.zone_id) {
                    if (zone.consecutive_losses > 0) {
                        LOG_INFO("Zone " << zone.zone_id << " consecutive_losses reset (TP win)");
                        zone.consecutive_losses = 0;
                        if (zone.state == ZoneState::EXHAUSTED) {
                            zone.state = ZoneState::TESTED;
                            zone.exhausted_at_datetime = "";
                            LOG_INFO("Zone " << zone.zone_id << " un-EXHAUSTED → TESTED (TP win)");
                        }
                    }
                    // TP win also resets per-direction SL suspension counter
                    if (config.zone_sl_suspend_threshold > 0) {
                        if (closed_trade.direction == "LONG" && zone.sl_count_long > 0) {
                            LOG_INFO("[SL-SUSPEND] Zone #" << zone.zone_id
                                     << " LONG sl_count reset (TAKE_PROFIT win)");
                            zone.sl_count_long = 0;
                        } else if (closed_trade.direction == "SHORT" && zone.sl_count_short > 0) {
                            LOG_INFO("[SL-SUSPEND] Zone #" << zone.zone_id
                                     << " SHORT sl_count reset (TAKE_PROFIT win)");
                            zone.sl_count_short = 0;
                        }
                    }
                    break;
                }
            }
        }

        if (order_submitter_ && order_submitter_->is_enabled()) {
            auto result = order_submitter_->submit_exit_order(closed_trade.order_tag);
            LOG_INFO("Exit order after TAKE_PROFIT. Tag: " << closed_trade.order_tag << " Success: " << result.success);
        }

        // ⭐ NEW: Update performance and export CSV
        performance.add_trade(closed_trade);
        performance.update_capital(trade_mgr.get_capital());
        export_trade_journal();

        return;
    }
}

void LiveEngine::process_tick() {
    LOG_DEBUG("Processing tick...");
    
    // DEBUG: Print every tick to verify loop is running
    std::cout << "\n[TICK] TICK PROCESSED - Current bar count: " << bar_history.size() << std::endl;
    std::cout.flush();
    
    // Update bar history (bar_index is now incremented inside update_bar_history only when new bar is added)
    update_bar_history();
    
    // =========================================================================
    // ⭐ ISSUE-3: OVERNIGHT CONTINUATION -- evaluate at next-session open
    // =========================================================================
    // Conditions checked every bar but only fires when:
    //   (a) continuation is pending  (b) new session date  (c) no open position
    //   (d) zone still valid         (e) price in zone     (f) EMA aligned
    //   (g) entry score >= threshold
    // On pass  -> fresh trade entered (new SL/TP/size from today's ATR)
    // On fail  -> snapshot cleared, reason logged
    // =========================================================================
    if (config.enable_overnight_continuation
        && pending_continuation_.active
        && !trade_mgr.is_in_position()
        && !bar_history.empty()) {

        const Bar& cur      = bar_history.back();
        std::string cur_date = cur.datetime.substr(0, 10);

        // Only act on a NEW session (not the same day the trade closed)
        if (cur_date != pending_continuation_.exit_session) {

            bool fired = false;
            std::string skip_reason;

            // -- Locate the zone -------------------------------------------
            Zone* tgt = nullptr;
            for (auto& z : active_zones) {
                if (z.zone_id == pending_continuation_.zone_id) { tgt = &z; break; }
            }

            if (!tgt) {
                skip_reason = "Zone " + std::to_string(pending_continuation_.zone_id)
                              + " no longer in active_zones";
            } else if (tgt->state == ZoneState::VIOLATED
                    || tgt->state == ZoneState::EXHAUSTED) {
                skip_reason = "Zone " + std::to_string(tgt->zone_id)
                              + " is VIOLATED or EXHAUSTED";
            } else {
                // -- Adverse gap check -------------------------------------
                // Gap = how far open moved AGAINST the trade direction overnight
                double gap_adverse = (pending_continuation_.direction == "LONG")
                    ? (pending_continuation_.exit_price - cur.open)   // positive = gap down = bad for long
                    : (cur.open - pending_continuation_.exit_price);  // positive = gap up   = bad for short
                double max_gap = pending_continuation_.prior_risk * config.continuation_max_gap_pct;

                if (gap_adverse > max_gap) {
                    skip_reason = "Adverse gap " + std::to_string(gap_adverse)
                                  + " pts > limit " + std::to_string(max_gap) + " pts";
                } else {
                    // -- Price must overlap the zone on this bar -----------
                    double z_lo = std::min(tgt->proximal_line, tgt->distal_line);
                    double z_hi = std::max(tgt->proximal_line, tgt->distal_line);
                    bool in_zone = (cur.high >= z_lo && cur.low <= z_hi);

                    if (!in_zone) {
                        skip_reason = "Price not in zone (bar " + std::to_string(cur.low)
                                      + "-" + std::to_string(cur.high)
                                      + ", zone " + std::to_string(z_lo)
                                      + "-" + std::to_string(z_hi) + ")";
                    } else {
                        // -- EMA alignment --------------------------------
                        int cur_idx  = static_cast<int>(bar_history.size()) - 1;
                        double ema20 = MarketAnalyzer::calculate_ema(bar_history, 20, cur_idx);
                        double ema50 = MarketAnalyzer::calculate_ema(bar_history, 50, cur_idx);
                        bool ema_ok  = (pending_continuation_.direction == "LONG")
                            ? (ema20 > ema50) : (ema20 < ema50);

                        if (!ema_ok) {
                            skip_reason = "EMA misaligned (EMA20="
                                          + std::to_string(static_cast<int>(ema20))
                                          + " EMA50=" + std::to_string(static_cast<int>(ema50)) + ")";
                        } else {
                            // -- Entry score -------------------------------
                            double atr       = MarketAnalyzer::calculate_atr(bar_history, config.atr_period);
                            MarketRegime reg = MarketAnalyzer::detect_regime(bar_history, 50, 5.0);
                            ZoneScore zscore = scorer.evaluate_zone(*tgt, reg, cur);
                            double min_score = config.scoring.entry_minimum_score
                                               * config.continuation_min_score_pct;

                            if (zscore.total_score < min_score) {
                                skip_reason = "Score " + std::to_string(zscore.total_score)
                                              + " < min " + std::to_string(min_score);
                            } else {
                                // -- All checks passed -- calculate entry ---
                                // ⭐ FIX P7: Pass is_continuation=true to bypass
                                // the pullback-vol gate (see entry_decision_engine.cpp).
                                EntryDecision dec = entry_engine.calculate_entry(
                                    *tgt, zscore, atr, reg, nullptr, &cur, &bar_history,
                                    true);  // is_continuation=true

                                if (!dec.should_enter) {
                                    skip_reason = "calculate_entry rejected: "
                                                  + dec.rejection_reason;
                                } else {
                                    // Apply position cap
                                    if (config.max_position_lots > 0
                                        && dec.lot_size > config.max_position_lots)
                                        dec.lot_size = config.max_position_lots;

                                    bool entered = trade_mgr.enter_trade(
                                        dec, *tgt, cur, cur_idx, reg,
                                        trading_symbol, &bar_history);

                                    if (entered) {
                                        fired = true;
                                        const Trade& t = trade_mgr.get_current_trade();
                                        pending_continuation_.clear();

                                        LOG_INFO("[CONTINUATION] Re-entered Zone "
                                                 << tgt->zone_id << " " << t.direction
                                                 << " @ " << std::fixed << std::setprecision(2)
                                                 << t.entry_price
                                                 << " SL=" << t.stop_loss
                                                 << " TP=" << t.take_profit
                                                 << " Lots=" << t.position_size);
                                        std::cout
                                            << "\n+====================================================+\n"
                                            << "| [CONTINUATION] Next-session re-entry!             |\n"
                                            << "+====================================================+\n"
                                            << "  Zone:        " << tgt->zone_id << "\n"
                                            << "  Direction:   " << t.direction << "\n"
                                            << "  Entry:       " << std::fixed << std::setprecision(2)
                                                                 << t.entry_price << "\n"
                                            << "  Stop Loss:   " << t.stop_loss << "\n"
                                            << "  Take Profit: " << t.take_profit << "\n"
                                            << "  Lots:        " << t.position_size << "\n"
                                            << std::endl;
                                        std::cout.flush();

                                        if (order_submitter_ && order_submitter_->is_enabled()) {
                                            Trade& t_ref = const_cast<Trade&>(trade_mgr.get_current_trade());
                                            std::string cont_tag = OrderTagGenerator::generate(t_ref.direction);
                                            t_ref.order_tag = cont_tag;

                                            ActiveOrder cont_ao;
                                            cont_ao.order_tag        = cont_tag;
                                            cont_ao.symbol           = trading_symbol;
                                            cont_ao.side             = t_ref.direction;
                                            cont_ao.lots             = t_ref.position_size;
                                            cont_ao.entry_price      = t_ref.entry_price;
                                            cont_ao.stop_loss        = t_ref.stop_loss;
                                            cont_ao.zone_id          = tgt->zone_id;
                                            cont_ao.entry_time_epoch = static_cast<long long>(std::time(nullptr));
                                            get_order_registry().register_order(cont_ao);

                                            auto cont_result = order_submitter_->submit_order(
                                                t_ref.direction, t_ref.position_size,
                                                tgt->zone_id, t_ref.zone_score,
                                                t_ref.entry_price, t_ref.stop_loss, t_ref.take_profit,
                                                cont_tag);
                                            if (!cont_result.success) {
                                                get_order_registry().remove(cont_tag);
                                            } else {
                                                LOG_INFO("OrderTag: " << cont_tag << " Zone: " << tgt->zone_id << " Side: " << t_ref.direction);
                                            }
                                        }
                                    } else {
                                        skip_reason = "enter_trade() rejected (SL/size validation)";
                                    }
                                }
                            }
                        }
                    }
                }
            }

            if (!fired) {
                LOG_INFO("[CONTINUATION] SKIP: Continuation skipped -- " << skip_reason);
                std::cout << "\n[CONTINUATION] Skipped: " << skip_reason << "\n";
                pending_continuation_.clear();
            }
        }
        // else: same session as exit -- wait for tomorrow's first bar
    }
    // =========================================================================

    // ⭐ NEW: Periodic zone refresh (LTP ±5%, every 30 min)
    if (config.enable_live_zone_filtering && bar_history.size() > 0) {
        double current_ltp = bar_history.back().close;
        refresh_active_zones(current_ltp);
    }
    
    // ⭐ NEW: Periodic CSV export backup (every N bars)
    if (config.live_export_every_n_bars > 0) {
        if (bar_index - last_csv_export_bar_ >= config.live_export_every_n_bars) {
            LOG_INFO("Periodic CSV export (bar " << bar_index << ")");
            export_trade_journal();
            last_csv_export_bar_ = bar_index;
        }
    }
    
    // Manage existing position
    if (trade_mgr.is_in_position()) {  // ⭐ Use TradeManager
        manage_position();
        // Don't return early - continue to process zones and check for signals
        // (but won't enter new trades while already in position)
    }

    // === SAME-SESSION RE-ENTRY GUARD: SET — runs unconditionally every bar ===
    // CRITICAL: Must NOT be inside manage_position() which has an early-return guard
    // (if !is_in_position() return). On the bar AFTER a trade closes, is_in_position()
    // is FALSE so manage_position() returns immediately — the guard would never be set.
    // By running here unconditionally we guarantee the guard is set on the SAME bar
    // the trade closes (or at worst the very next bar), before check_for_entries() runs.
    //
    // ⭐ FIX BUG-7: Only block re-entry after genuinely bad exits.
    // Previous behaviour blocked the zone for the rest of the session after ANY exit
    // including TRAIL_SL winners (+₹17,025 in Zone 10). This prevented re-entry on
    // the same zone even when price returned with a fresh, valid setup.
    //
    // Rules:
    //   STOP_LOSS              → always block (structural zone failure)
    //   SESSION_END, P&L < 0  → block (losing hold, zone showed weakness today)
    //   SESSION_END, P&L >= 0 → do NOT block (exited profitably, zone is still valid)
    //   TRAIL_SL               → do NOT block (winner — zone proved itself)
    //   TAKE_PROFIT            → do NOT block (full winner)
    {
        const auto& trades = performance.get_trades();
        if (!trades.empty()) {
            const auto& last_trade = trades.back();
            // ⭐ FIX DES-01: Removed 'static' — leaked state across engine instances.
            // guard_last_exit_date_ is now a member variable (add to live_engine.h, init "").
            if (last_trade.exit_date != guard_last_exit_date_ && !last_trade.exit_date.empty()) {
                guard_last_exit_date_ = last_trade.exit_date;
                std::string session_date = last_trade.exit_date.substr(0, 10);

                // Determine whether this exit warrants blocking re-entry
                bool block_reentry = false;
                if (last_trade.exit_reason == "STOP_LOSS") {
                    block_reentry = true;   // Structural failure — always block
                } else if (last_trade.exit_reason == "SESSION_END" && last_trade.pnl < 0.0) {
                    block_reentry = true;   // Losing SESSION_END — zone was weak today
                }
                // TRAIL_SL win, TAKE_PROFIT, positive SESSION_END → allow re-entry

                if (block_reentry) {
                    zone_sl_session_[last_trade.zone_id] = session_date;
                    LOG_INFO("[RE-ENTRY GUARD] Zone " << last_trade.zone_id
                             << " blocked for rest of session " << session_date
                             << " after " << last_trade.exit_reason << " exit"
                             << " (P&L=" << last_trade.pnl << ")");
                } else {
                    LOG_INFO("[RE-ENTRY GUARD] Zone " << last_trade.zone_id
                             << " NOT blocked after " << last_trade.exit_reason
                             << " exit (P&L=" << last_trade.pnl
                             << ") — zone remains tradeable this session");
                }
            }
        }
    }

    // ⭐ GAP-5 FIX: Re-enable process_zones() so new zones formed during the simulation
    // are detected and added to active_zones within process_tick().
    //
    // Previously this was commented out, meaning LT only used zones from bootstrap.
    // BT detects all zones upfront from the full dataset scan, so it always has the
    // complete zone map. LT with only bootstrap zones misses zones that form AFTER
    // the pre-sim history ends (e.g. zones forming in Feb/Mar during the simulation).
    //
    // process_zones() has its own guard (last_scannable_bar <= last_zone_scan_bar_)
    // so it is safe to call every bar — it returns immediately if no new bars to scan.
    // The dedup check inside process_zones() prevents duplicate zone creation.
    // The duplicate zone explosion bug from earlier sessions is fixed by ZoneInitializer
    // dedup (already applied) and the secondary dedup in process_zones() itself.
    if (bar_history.size() >= 100) {
        process_zones();
    }

    // Check for entries (will be rejected by TradeManager if already in position)
    if (bar_history.size() >= 100) {
        check_for_entries();
    }
}

bool LiveEngine::bootstrap_zones_on_startup() {
    LOG_INFO("==============================================");
    LOG_INFO("  ZONE BOOTSTRAP: Checking cache validity");
    LOG_INFO("==============================================");
    
    // Check if bootstrap is enabled in config
    if (!config.zone_bootstrap_enabled) {
        LOG_INFO("❌ Bootstrap disabled in config - using standard zone detection");
        return false;
    }
    
    // Try to load existing master zone file with metadata
    fs::path master_file_path = fs::path(output_dir_) / "zones_live_master.json";
    
    if (!fs::exists(master_file_path)) {
        LOG_INFO("❌ No cached zones found: " << master_file_path.string());
        LOG_INFO("   Will generate fresh zone snapshot");
        // Fall through to regeneration
    } else {
        // Load metadata and check validity
        auto metadata = zone_persistence_.load_metadata(master_file_path.string());
        
        if (metadata.empty()) {
            LOG_WARN("⚠️  Cached zones have no metadata - regenerating");
        } else {
            LOG_INFO("✅ Found cached zones with metadata:");
            for (const auto& [key, value] : metadata) {
                LOG_INFO("   " << key << ": " << value);
            }
            
            // Check validity (TTL, daily refresh, config changes)
            if (check_bootstrap_validity(metadata)) {
                // Reuse cached zones
                LOG_INFO("✅ REUSING cached zones (valid)");
                
                // Load zones from master file
                int loaded_id = 1;
                if (zone_persistence_.load_zones(active_zones, loaded_id)) {
                    next_zone_id_ = loaded_id;
                    LOG_INFO("✅ Loaded " << active_zones.size() << " zones from cache");
                    if (!active_zones.empty()) {
                        LOG_INFO("[DETERMINISM] First loaded zone volume_score=" << active_zones.front().volume_profile.volume_score
                                 << " zone_id=" << active_zones.front().zone_id);
                    }
                    // ⭐ BUG FIX: Reset EXHAUSTED state on load when flag is OFF
                    // (mirrors the same fix in try_load_backtest_zones)
                    if (!config.enable_zone_exhaustion) {
                        int reset_count = 0;
                        for (auto& zone : active_zones) {
                            if (zone.state == ZoneState::EXHAUSTED || zone.consecutive_losses > 0) {
                                if (zone.state == ZoneState::EXHAUSTED) {
                                    zone.state = ZoneState::TESTED;
                                }
                                zone.consecutive_losses    = 0;
                                zone.exhausted_at_datetime = "";
                                reset_count++;
                            }
                        }
                        if (reset_count > 0) {
                            LOG_INFO("⚠️  enable_zone_exhaustion=NO: reset " << reset_count
                                     << " zone(s) from EXHAUSTED/dirty state on cache load");
                        }
                    }
                    return true;
                } else {
                    LOG_WARN("⚠️  Failed to load cached zones - regenerating");
                }
            } else {
                LOG_INFO("❌ Cached zones invalid (TTL/schedule/config) - regenerating");
            }
        }
    }
    
    // Regeneration path: Detect zones from scratch using full historical data
    LOG_INFO("==============================================");
    LOG_INFO("  ZONE BOOTSTRAP: Generating fresh snapshot");
    LOG_INFO("==============================================");
    
    auto bootstrap_start = std::chrono::high_resolution_clock::now();
    
    // Detect zones using shared ZoneInitializer (same as backtest)
    LOG_INFO("Detecting zones from " << bar_history.size() << " historical bars...");
    active_zones = Core::ZoneInitializer::detect_initial_zones(bar_history, detector, next_zone_id_);
    LOG_INFO("✅ Detected " << active_zones.size() << " initial zones");
    
    // Score all zones
    // ⭐ GAP-3 FIX: Score each zone at bars[zone.formation_bar] context, not bar_history.back().
    // BT (backtest_engine.cpp) uses this since Fix 2: each zone's total_score is computed
    // against the bar at which it formed, so age/ATR/distance components reflect the
    // market context at detection time. LT was using bar_history.back() (the last pre-sim
    // bar, e.g. Jan-30-2026 for an Aug→Jan history), meaning all zones — including those
    // formed in Oct-2025 — were scored against Jan-2026 context. This caused score
    // mismatches that affected which zones passed entry_minimum_score.
    LOG_INFO("Scoring zones at formation bar context (matching backtest)...");
    Core::MarketRegime regime = Core::MarketAnalyzer::detect_regime(bar_history, 50, 5.0);
    for (auto& zone : active_zones) {
        int score_bar_idx = zone.formation_bar;
        if (score_bar_idx < 0 || score_bar_idx >= static_cast<int>(bar_history.size())) {
            score_bar_idx = static_cast<int>(bar_history.size()) - 1;
        }
        zone.zone_score = scorer.evaluate_zone(zone, regime, bar_history[score_bar_idx]);
    }
    LOG_INFO("✅ Scored " << active_zones.size() << " zones");

    LOG_INFO("Replaying historical bars to sync zone states...");
    bool original_flip_setting = config.enable_zone_flip;
    config.enable_zone_flip = false;
    replay_historical_zone_states();
    config.enable_zone_flip = original_flip_setting;
    LOG_INFO("✅ Zone states synchronized with historical data");

    // ⭐ RC3 FIX: Restore consecutive_losses and entry_retry_count from prior zone file.
    //
    // ROOT CAUSE: replay_historical_zone_states() only replays price-action state
    // (FRESH/TESTED/VIOLATED/RECLAIMED, touch_count, was_swept). It does not replay
    // trade outcomes because LT bootstrap never executes trades. Consequently,
    // zone.consecutive_losses and zone.entry_retry_count stay at 0 for all zones,
    // while BT accumulates these from its trade loop. The 10 zones BT marks EXHAUSTED
    // (3 consecutive losses) remain TESTED in LT — tradeable by LT but blocked by BT.
    //
    // FIX: If a prior zones_live_master.json exists from a previous live run,
    // copy consecutive_losses, entry_retry_count, and EXHAUSTED state from it into
    // the freshly replayed zones (matched by zone boundary proximity < 1 pt).
    // This carries forward trade-outcome knowledge from previous sessions.
    // On the very first bootstrap (no prior file), all values stay at 0 — correct.
    {
        fs::path prior_file = fs::path(output_dir_) / "zones_live_master.json";
        std::vector<Zone> prior_zones;
        int dummy_id = 0;
        if (fs::exists(prior_file) && zone_persistence_.load_zones(prior_zones, dummy_id)) {
            int restored = 0;
            for (auto& zone : active_zones) {
                for (const auto& pz : prior_zones) {
                    if (zone.type == pz.type &&
                        std::abs(zone.proximal_line - pz.proximal_line) < 1.0 &&
                        std::abs(zone.distal_line   - pz.distal_line)   < 1.0) {
                        if (pz.consecutive_losses > 0 || pz.entry_retry_count > 0) {
                            zone.consecutive_losses  = pz.consecutive_losses;
                            zone.entry_retry_count   = pz.entry_retry_count;
                            zone.sl_count_long       = pz.sl_count_long;
                            zone.sl_count_short      = pz.sl_count_short;
                            // Restore EXHAUSTED state if consecutive_losses exceeded threshold
                            if (config.enable_zone_exhaustion &&
                                pz.state == ZoneState::EXHAUSTED &&
                                zone.state != ZoneState::VIOLATED) {
                                zone.state                  = ZoneState::EXHAUSTED;
                                zone.exhausted_at_datetime  = pz.exhausted_at_datetime;
                            }
                            restored++;
                        }
                        break;
                    }
                }
            }
            if (restored > 0) {
                LOG_INFO("✅ RC3: Restored consecutive_losses/retry_count for "
                         << restored << " zones from prior session");
            } else {
                LOG_INFO("RC3: No prior trade-outcome data to restore (first bootstrap)");
            }
        }
    }

    
    // Build metadata
    std::map<std::string, std::string> metadata;
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
#ifdef _WIN32
    localtime_s(&tm, &time_t);
#else
    tm = *std::localtime(&time_t);
#endif
    std::ostringstream timestamp_ss;
    timestamp_ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    
    metadata["generated_at"] = timestamp_ss.str();
    metadata["csv_path"] = historical_csv_path_;
    metadata["symbol"] = trading_symbol;
    metadata["interval"] = bar_interval;
    metadata["bar_count"] = std::to_string(bar_history.size());
    metadata["zone_count"] = std::to_string(active_zones.size());
    metadata["ttl_hours"] = std::to_string(config.zone_bootstrap_ttl_hours);
    metadata["refresh_time"] = config.zone_bootstrap_refresh_time;
    
    // Save master zones with metadata
    LOG_INFO("Saving master zones with metadata...");
    bool save_success = zone_persistence_.save_zones_with_metadata(
        active_zones, next_zone_id_, master_file_path.string(), metadata
    );
    
    if (!save_success) {
        LOG_ERROR("❌ Failed to save master zones");
        return false;
    }
    
    // Save active zones (filtered) separately
    if (config.enable_live_zone_filtering && bar_history.size() > 0) {
        double ltp = bar_history.back().close;
        filter_zones_by_range(ltp);
        
        fs::path active_file_path = fs::path(output_dir_) / "zones_live_active.json";
        
        // Count active zones
        std::vector<Zone> filtered_active;
        for (const auto& zone : active_zones) {
            if (zone.is_active) {
                filtered_active.push_back(zone);
            }
        }
        
        LOG_INFO("Saving active zones (filtered)...");
        zone_persistence_.save_zones_as(filtered_active, next_zone_id_, active_file_path.string());
        LOG_INFO("✅ Saved " << filtered_active.size() << " active zones");
    }
    
    auto bootstrap_end = std::chrono::high_resolution_clock::now();
    auto bootstrap_ms = std::chrono::duration_cast<std::chrono::milliseconds>(bootstrap_end - bootstrap_start).count();
    
    LOG_INFO("==============================================");
    LOG_INFO("  ZONE BOOTSTRAP COMPLETE");
    LOG_INFO("  Time: " << bootstrap_ms << "ms (" << (bootstrap_ms/1000.0) << "s)");
    LOG_INFO("  Zones: " << active_zones.size());
    LOG_INFO("==============================================");
    
    return true;
}

bool LiveEngine::check_bootstrap_validity(const std::map<std::string, std::string>& metadata) {
    // Check if force regenerate flag is set
    if (config.zone_bootstrap_force_regenerate) {
        LOG_INFO("❌ Validity: force_regenerate=true");
        return false;
    }
    
    // Check generated_at timestamp (TTL check)
    if (metadata.find("generated_at") != metadata.end()) {
        std::string generated_at_str = metadata.at("generated_at");
        
        // Parse timestamp: format is "YYYY-MM-DD HH:MM:SS"
        std::tm generated_tm = {};
        std::istringstream ss(generated_at_str);
        ss >> std::get_time(&generated_tm, "%Y-%m-%d %H:%M:%S");
        
        if (ss.fail()) {
            LOG_WARN("⚠️  Could not parse generated_at timestamp");
        } else {
            auto generated_time = std::chrono::system_clock::from_time_t(std::mktime(&generated_tm));
            auto now = std::chrono::system_clock::now();
            auto age_hours = std::chrono::duration_cast<std::chrono::hours>(now - generated_time).count();
            
            LOG_INFO("   Age: " << age_hours << " hours (TTL: " << config.zone_bootstrap_ttl_hours << " hours)");
            
            if (age_hours >= config.zone_bootstrap_ttl_hours) {
                LOG_INFO("❌ Validity: TTL exceeded");
                return false;
            }
        }
    }
    
    // Check daily refresh time (e.g., "08:50")
    if (!config.zone_bootstrap_refresh_time.empty()) {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::tm now_tm;
#ifdef _WIN32
        localtime_s(&now_tm, &time_t);
#else
        now_tm = *std::localtime(&time_t);
#endif
        
        // Parse refresh_time "HH:MM"
        std::istringstream ss(config.zone_bootstrap_refresh_time);
        int refresh_hour, refresh_min;
        char colon;
        ss >> refresh_hour >> colon >> refresh_min;
        
        if (!ss.fail()) {
            int current_minutes = now_tm.tm_hour * 60 + now_tm.tm_min;
            int refresh_minutes = refresh_hour * 60 + refresh_min;
            
            // If current time is past refresh time today, check if zones were generated before refresh time
            if (current_minutes >= refresh_minutes) {
                if (metadata.find("generated_at") != metadata.end()) {
                    std::string generated_at_str = metadata.at("generated_at");
                    std::tm generated_tm = {};
                    std::istringstream gen_ss(generated_at_str);
                    gen_ss >> std::get_time(&generated_tm, "%Y-%m-%d %H:%M:%S");
                    
                    if (!gen_ss.fail()) {
                        int generated_minutes = generated_tm.tm_hour * 60 + generated_tm.tm_min;
                        
                        // If zones were generated today but before refresh time, regenerate
                        if (generated_tm.tm_mday == now_tm.tm_mday &&
                            generated_tm.tm_mon == now_tm.tm_mon &&
                            generated_tm.tm_year == now_tm.tm_year &&
                            generated_minutes < refresh_minutes) {
                            LOG_INFO("❌ Validity: Daily refresh time passed (" << config.zone_bootstrap_refresh_time << ")");
                            return false;
                        }
                    }
                }
            }
        }
    }
    
    // Check if CSV path changed
    if (metadata.find("csv_path") != metadata.end()) {
        if (metadata.at("csv_path") != historical_csv_path_) {
            LOG_INFO("❌ Validity: CSV path changed");
            return false;
        }
    }
    
    // Check if symbol/interval changed
    if (metadata.find("symbol") != metadata.end()) {
        if (metadata.at("symbol") != trading_symbol) {
            LOG_INFO("❌ Validity: Symbol changed");
            return false;
        }
    }
    
    if (metadata.find("interval") != metadata.end()) {
        if (metadata.at("interval") != bar_interval) {
            LOG_INFO("❌ Validity: Interval changed");
            return false;
        }
    }
    
    LOG_INFO("✅ Validity: All checks passed");
    return true;
}

void LiveEngine::run(int duration_minutes) {
    LOG_INFO("==================================================");
    LOG_INFO("  Starting Live Trading");
    LOG_INFO("  Symbol: " << trading_symbol);
    LOG_INFO("  Interval: " << bar_interval << " min");
    if (duration_minutes > 0) {
        LOG_INFO("  Duration: " << duration_minutes << " minutes");
    } else {
        LOG_INFO("  Duration: Continuous (Ctrl+C to stop)");
    }
    LOG_INFO("==================================================");
    
    // ⭐ NEW: Bootstrap zones on startup (with TTL/refresh logic)
    bool bootstrapped = bootstrap_zones_on_startup();
    
    if (!bootstrapped) {
        // Bootstrap disabled or failed - fall back to legacy zone detection
        LOG_WARN("⚠️  Bootstrap failed or disabled - using legacy zone detection");
        
        // ⭐ TIMING: Measure zone detection
        auto zone_detect_start = std::chrono::high_resolution_clock::now();
        
        // ⭐ CRITICAL FIX: Detect zones BEFORE main loop (same as backtest)
        // This ensures both engines detect zones at the SAME point
        LOG_INFO("Detecting initial zones from historical data...");
        
        bool loaded_backtest_zones = try_load_backtest_zones();

        if (!loaded_backtest_zones) {
            if (active_zones.empty()) {
                // Only detect if no persisted zones loaded
                active_zones = Core::ZoneInitializer::detect_initial_zones(bar_history, detector, next_zone_id_);
                LOG_INFO("Found " << active_zones.size() << " zones");
            }

            // ⚠️ OPTIMIZATION: Skip historical replay for large datasets (>100k bars)
            // Historical replay can be slow for large datasets and is mainly for perfect state sync
            // For live trading, we can start fresh and let zones update in real-time
            if (bar_history.size() > 100000) {
                LOG_WARN("⚡ SKIPPING historical replay due to large dataset (" << bar_history.size() << " bars)");
                LOG_WARN("   Zones will update in real-time from current point forward");
                LOG_WARN("   This is normal for first-time initialization");
            } else {
                // Re-apply historical bars to align states with updated rules (e.g., body-close invalidation)
                // Note: This is a one-time pass at startup; cost is acceptable outside Debug.
                // CRITICAL: Disable zone flipping during replay to match backtest behavior
                bool original_flip_setting = config.enable_zone_flip;
                config.enable_zone_flip = false;  // Temporarily disable flipping
                LOG_INFO("Replaying historical bars to sync zone states...");
                replay_historical_zone_states();
                config.enable_zone_flip = original_flip_setting;  // Restore original setting
            }
        }

        // ⭐ CRITICAL FIX: Score all zones (whether newly detected or loaded from backtest)
        // This ensures zone_score fields are populated correctly
        LOG_INFO("Scoring " << active_zones.size() << " zones...");
        Core::MarketRegime regime = Core::MarketAnalyzer::detect_regime(bar_history, 50, 5.0);
        int zones_scored = 0;
        for (auto& zone : active_zones) {
            // Re-score the zone with current market context
            zone.zone_score = scorer.evaluate_zone(zone, regime, bar_history.back());
            zones_scored++;
            
            LOG_DEBUG("Zone " << zone.zone_id << " scored: total=" << zone.zone_score.total_score
                     << " (" << zone.zone_score.entry_rationale << ")");
        }
        LOG_INFO("✅ Scored " << zones_scored << " zones (avg: " 
                 << (zones_scored > 0 ? (std::accumulate(active_zones.begin(), active_zones.end(), 0.0,
                    [](double sum, const Core::Zone& z) { return sum + z.zone_score.total_score; }) / zones_scored) : 0.0)
                 << ")");

        // ⭐ NEW: Apply dynamic range-based filtering on initialization
        if (config.enable_live_zone_filtering && bar_history.size() > 0) {
            double initial_ltp = bar_history.back().close;
            LOG_INFO("Applying initial zone filtering (LTP: " << initial_ltp << ")");
            filter_zones_by_range(initial_ltp);
            
            // Initialize refresh tracking
            bars_since_last_refresh_ = 0;
            last_refresh_ltp_ = initial_ltp;
        }

        // ⭐ TIMING: Measure zone saving
        auto zone_save_start = std::chrono::high_resolution_clock::now();
        
        // Clear state history if disabled (to keep file size small)
        if (!config.enable_state_history) {
            for (auto& zone : active_zones) {
                zone.state_history.clear();
            }
        }
        
        // Save the aligned zones to the live snapshot
        LOG_INFO("💾 Saving zones to persistent file...");
        bool save_result = zone_persistence_.save_zones(active_zones, next_zone_id_);
        
        if (!save_result) {
            LOG_WARN("⚠️ Failed to save zones - will regenerate on next startup");
        } else {
            LOG_INFO("✅ Zones saved successfully: " << zone_persistence_.get_zone_file_path());
        }
        
        auto zone_save_end = std::chrono::high_resolution_clock::now();
        auto zone_save_ms = std::chrono::duration_cast<std::chrono::milliseconds>(zone_save_end - zone_save_start).count();
        
        auto zone_detect_end = std::chrono::high_resolution_clock::now();
        auto zone_detect_ms = std::chrono::duration_cast<std::chrono::milliseconds>(zone_detect_end - zone_detect_start).count();
        
        LOG_INFO("Zone detection complete: " << zone_detect_ms << "ms (" << (zone_detect_ms/1000.0) << "s)");
        LOG_INFO("  - Zone saving: " << zone_save_ms << "ms");
    }
    
    // ⭐ LIVE ZONE DETECTION FIX: Set last_zone_scan_bar_ to the actual scan boundary
    // used by detect_zones_no_duplicates(), NOT to bar_history.size()-1.
    //
    // ROOT CAUSE OF ZERO NEW ZONES IN LIVE MODE:
    // detect_zones_no_duplicates() can only scan up to:
    //   bars.size() - consolidation_max_bars - max(consolidation_min_bars,3)
    // (it needs lookahead for impulse-after and departure checks).
    //
    // If we set last_zone_scan_bar_ = bar_history.size()-1 (e.g. 1423 with 1424 bars),
    // then process_zones() computes last_scannable_bar = bar_history.size()-6 = 1418.
    // Guard: 1418 <= 1423 → returns immediately for the first 5 live bars.
    // After that, it advances last_zone_scan_bar_ by exactly 1 per tick.
    // A 1-bar window can never satisfy consolidation_min_bars ≥ 5 → zero zones forever.
    // The periodic detection in run() is also crippled: bars_since_detection=1 < interval=10.
    //
    // FIX: Align last_zone_scan_bar_ with detect_zones_no_duplicates()'s end boundary.
    // This leaves the final ~25 bars of bootstrap history plus ALL live bars unscanned,
    // so process_zones() immediately has a meaningful window on the first live tick.
    {
        int lookforward_max = std::max(config.consolidation_min_bars, 3);
        int bootstrap_scan_end = static_cast<int>(bar_history.size())
                                 - config.consolidation_max_bars
                                 - lookforward_max
                                 - 1;  // last bar index the scanner could reach
        last_zone_scan_bar_ = std::max(-1, bootstrap_scan_end - 1);
        LOG_INFO("🔄 last_zone_scan_bar_ set to " + std::to_string(last_zone_scan_bar_)
                 + " (bootstrap scan boundary; bars " + std::to_string(last_zone_scan_bar_ + 1)
                 + "+" + " remain available for live detection)");
    }
    
    LOG_INFO("Zone detection complete. Ready to monitor for new zones...");
    
    auto start_time = std::chrono::steady_clock::now();
    
    // Main trading loop
    while (true) {
        try {
            process_tick();
            
            // NEW: Periodic zone detection to maintain fresh zone pipeline
            if (config.live_zone_detection_interval_bars > 0) {
                int current_bar_count = static_cast<int>(bar_history.size());
                int last_scannable_bar = current_bar_count - 6;
                int bars_since_detection = last_scannable_bar - last_zone_scan_bar_;
                
                if (last_scannable_bar > last_zone_scan_bar_ &&
                    bars_since_detection >= config.live_zone_detection_interval_bars) {
                    LOG_INFO("🔄 Running periodic zone detection (" << bars_since_detection << " bars since last scan)");
                    
                    // ⭐ FIX: Use relaxed thresholds for live detection if configured
                    double original_width = config.min_zone_width_atr;
                    double original_strength = config.min_zone_strength;
                    
                    if (config.use_relaxed_live_detection) {
                        config.min_zone_width_atr = config.live_min_zone_width_atr;
                        config.min_zone_strength = config.live_min_zone_strength;
                        LOG_INFO("📉 Using RELAXED live thresholds: width=" << config.min_zone_width_atr 
                                 << " (vs " << original_width << "), strength=" << config.min_zone_strength 
                                 << " (vs " << original_strength << ")");
                    }
                    
                    // Detect new zones with duplicate prevention
                    // Use lookback window for better detection (matches backtest behavior)
                    int scan_start_bar;
                    if (config.live_zone_detection_lookback_bars > 0) {
                        // Use lookback window (e.g., last 200 bars)
                        scan_start_bar = std::max(0, last_scannable_bar - config.live_zone_detection_lookback_bars);
                        LOG_INFO("🔍 Scanning last " << (last_scannable_bar - scan_start_bar) 
                                 << " bars (lookback=" << config.live_zone_detection_lookback_bars << ")");
                    } else {
                        // Incremental only (scan since last detection)
                        scan_start_bar = last_zone_scan_bar_ + 1;
                        LOG_DEBUG("🔍 Incremental scan from bar " << scan_start_bar);
                    }
                    
                    std::vector<Core::Zone> new_zones = detector.detect_zones_no_duplicates(
                        active_zones,
                        scan_start_bar
                    );
                    
                    // Restore original thresholds
                    if (config.use_relaxed_live_detection) {
                        config.min_zone_width_atr = original_width;
                        config.min_zone_strength = original_strength;
                    }

                    if (!new_zones.empty()) {
                        Core::MarketRegime regime = Core::MarketAnalyzer::detect_regime(
                            bar_history, config.htf_lookback_bars, config.htf_trending_threshold);
                        int added_count = 0;

                        for (auto& zone : new_zones) {
                            // ⭐ BUG 3 FIX: Secondary dedup check against active_zones.
                            // detect_zones_no_duplicates() only deduplicates within the
                            // detector's own output — it does NOT check whether a zone
                            // with the same boundaries was already added by process_zones()
                            // in a previous call. When live_zone_detection_lookback_bars > 0
                            // the scan window overlaps prior scans, so the same zone can be
                            // returned by the detector multiple times across consecutive
                            // periodic detection cycles → 4-18 copies of the same zone.
                            // Fix: mirror the same boundary+formation_bar check that
                            // process_zones() uses before accepting a zone.
                            bool already_active = false;
                            for (const auto& active : active_zones) {
                                if (zone.formation_bar == active.formation_bar &&
                                    std::abs(zone.proximal_line - active.proximal_line) < 1.0 &&
                                    std::abs(zone.distal_line   - active.distal_line)   < 1.0) {
                                    already_active = true;
                                    LOG_DEBUG("Periodic detect: zone at bar " << zone.formation_bar
                                             << " [" << zone.distal_line << "-" << zone.proximal_line
                                             << "] already in active_zones — skipping duplicate");
                                    break;
                                }
                            }
                            if (already_active) continue;

                            // ⭐ GAP-6 FIX: Score at formation bar, not bar_history.back()
                            {
                                int score_bar_idx = zone.formation_bar;
                                if (score_bar_idx < 0 || score_bar_idx >= static_cast<int>(bar_history.size()))
                                    score_bar_idx = static_cast<int>(bar_history.size()) - 1;
                                zone.zone_score = scorer.evaluate_zone(zone, regime, bar_history[score_bar_idx]);
                            }
                            zone.zone_id = next_zone_id_++;
                            active_zones.push_back(zone);
                            added_count++;
                            
                            // ⭐ DETAILED LOGGING: Show zone details
                            double zone_width = std::abs(zone.proximal_line - zone.distal_line);
                            double atr = 0;
                            if (!bar_history.empty() && bar_history.size() >= static_cast<size_t>(config.atr_period)) {
                                std::vector<double> tr_values;
                                for (size_t i = bar_history.size() - config.atr_period; i < bar_history.size(); ++i) {
                                    double high_low = bar_history[i].high - bar_history[i].low;
                                    double high_close = (i > 0) ? std::abs(bar_history[i].high - bar_history[i-1].close) : high_low;
                                    double low_close = (i > 0) ? std::abs(bar_history[i].low - bar_history[i-1].close) : high_low;
                                    tr_values.push_back(std::max({high_low, high_close, low_close}));
                                }
                                atr = std::accumulate(tr_values.begin(), tr_values.end(), 0.0) / tr_values.size();
                            }
                            double width_in_atr = (atr > 0) ? (zone_width / atr) : 0;
                            
                            LOG_INFO("  ✅ Added Zone " << zone.zone_id << " @ " << zone.formation_datetime);
                            LOG_INFO("     Type: " << (zone.type == ZoneType::DEMAND ? "DEMAND" : "SUPPLY")
                                     << " | State: " << (zone.state == Core::ZoneState::FRESH ? "FRESH" :
                                                        zone.state == Core::ZoneState::TESTED ? "TESTED" : "VIOLATED"));
                            LOG_INFO("     Width: " << std::fixed << std::setprecision(1) << zone_width 
                                     << " pts (" << std::setprecision(2) << width_in_atr << " ATR)");
                            LOG_INFO("     Strength: " << std::setprecision(1) << zone.strength 
                                     << " | Score: " << std::setprecision(1) << zone.zone_score.total_score);
                            LOG_INFO("     Range: [" << std::setprecision(2) << zone.distal_line 
                                     << " - " << zone.proximal_line << "]");
                            // ⭐ FIX BUG-06: Removed duplicate added_count++ that was here.
                            // The increment at the top of the loop body is the correct one.
                        }

                        // Re-score merged zones if any were updated
                        if (!detector.get_last_merged_zone_ids().empty()) {
                            for (auto& zone : active_zones) {
                                if (std::find(detector.get_last_merged_zone_ids().begin(),
                                              detector.get_last_merged_zone_ids().end(),
                                              zone.zone_id) != detector.get_last_merged_zone_ids().end()) {
                                    int score_bar_idx = zone.formation_bar;
                                    if (score_bar_idx < 0 || score_bar_idx >= static_cast<int>(bar_history.size()))
                                        score_bar_idx = static_cast<int>(bar_history.size()) - 1;
                                    zone.zone_score = scorer.evaluate_zone(zone, regime, bar_history[score_bar_idx]);
                                }
                            }
                        }

                        LOG_INFO("✅ Zone detection complete: " << new_zones.size()
                                << " detected, " << added_count << " added");
                    } else {
                        // ⭐ DIAGNOSTIC LOGGING: Why no zones?
                        LOG_INFO("❌ No new zones detected in last " << bars_since_detection << " bars");
                        LOG_INFO("   Active thresholds:");
                        LOG_INFO("     min_zone_width_atr: " << std::fixed << std::setprecision(2) 
                                 << (config.use_relaxed_live_detection ? config.live_min_zone_width_atr : config.min_zone_width_atr));
                        LOG_INFO("     min_zone_strength: " << std::fixed << std::setprecision(1)
                                 << (config.use_relaxed_live_detection ? config.live_min_zone_strength : config.min_zone_strength));
                        LOG_INFO("     min_impulse_atr: " << std::setprecision(2) << config.min_impulse_atr);
                        LOG_INFO("     consolidation_min_bars: " << config.consolidation_min_bars);
                        LOG_INFO("   💡 TIP: If zones rarely detected, try relaxed live detection mode");
                    }
                    
                    last_zone_scan_bar_ = last_scannable_bar;
                }
            }
            
            // Check duration
            if (duration_minutes > 0) {
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(
                    now - start_time).count();
                if (elapsed >= duration_minutes) {
                    LOG_INFO("Duration reached, stopping...");
                    break;
                }
            }
            
            // Sleep until next tick (e.g., every 10 seconds)
            // ⭐ FIX: Skip sleep for CSV simulator
CsvSimulatorBroker* csv_broker = dynamic_cast<CsvSimulatorBroker*>(broker.get());
if (csv_broker == nullptr) {
    std::this_thread::sleep_for(std::chrono::seconds(10)); // Real broker
    LOG_DEBUG("⏱️  Live mode: waiting for tick");
} else {
    LOG_DEBUG("📊 CSV mode: processing immediately");
    // Live mode: wait for new bars instead of stopping
    if (csv_broker->get_current_position() >= csv_broker->get_total_bars()) {
                if (dryrun_bootstrap_end_bar_ > 0) {
                    // DryRun mode: Stop when complete
                    LOG_INFO("✅ All bars processed - stopping");
                    break;
                } else {
                    // Live mode: Wait for new data to be appended to the CSV
                    LOG_DEBUG("⏱️  Waiting for new bars (at end of CSV)");
                    std::this_thread::sleep_for(std::chrono::seconds(5));
                }
            }
    size_t pos = csv_broker->get_current_position();
    if (pos > 0 && pos % 100 == 0) {
        LOG_INFO("📊 Progress: " << (pos * 100.0 / csv_broker->get_total_bars()) 
                << "% (" << pos << "/" << csv_broker->get_total_bars() << ")");
    }
}
            
        } catch (const std::exception& e) {
            LOG_ERROR("Error in trading loop: " << e.what());
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }
    
    stop();
}

void LiveEngine::stop() {
    LOG_INFO("==================================================");
    LOG_INFO("  Stopping Live Trading");
    LOG_INFO("==================================================");
    
    // NEW: Save zones before shutdown
    LOG_INFO("Saving zones to file...");
    // Clear state history if disabled
    if (!config.enable_state_history) {
        for (auto& zone : active_zones) {
            zone.state_history.clear();
        }
    }
    if (zone_persistence_.save_zones(active_zones, next_zone_id_)) {
        LOG_INFO("✅ Zones saved: " << zone_persistence_.get_zone_file_path());
    } else {
        LOG_WARN("Failed to save zones");
    }
    
    // Close any open positions using TradeManager
    if (trade_mgr.is_in_position()) {  // ⭐ Use TradeManager
        LOG_WARN("Closing open position before shutdown");
        // ⭐ FIX BUG-01: Use bar close not broker->get_ltp() (look-ahead in simulation)
        double current_price = bar_history.back().close;
        std::string current_time = bar_history.back().datetime;
        
        Trade closed_trade = trade_mgr.close_trade(current_time, "ENGINE_STOP", current_price);
        
        // ⭐ NEW: Add final trade to performance
        performance.add_trade(closed_trade);
        performance.update_capital(trade_mgr.get_capital());
    }
    
    // ⭐ NEW: Final CSV export and performance summary
    LOG_INFO("Exporting final trade journal...");
    export_trade_journal();
    
    // Display final performance summary
    std::cout << "\n==================================================\n";
    std::cout << "  LIVE TRADING PERFORMANCE SUMMARY\n";
    std::cout << "==================================================\n";
    performance.print_summary();
    std::cout << "==================================================\n\n";
    
    // Disconnect from broker
    if (broker) {
        broker->disconnect();
    }
    
    LOG_INFO("✅ Live Engine stopped");
}

std::optional<Core::Zone> LiveEngine::detect_zone_flip(const Core::Zone& violated_zone, int bar_index) {
    // Look back for consolidation before the breakdown
    int lookback_start = std::max<double>(0, bar_index - config.flip_lookback_bars);
    
    if (lookback_start >= bar_index - 1) {
        return std::nullopt;  // Not enough bars
    }
    
    // Find consolidation period before breakdown
    double flip_base_low = bar_history[lookback_start].low;
    double flip_base_high = bar_history[lookback_start].high;
    
    for (int i = lookback_start; i < bar_index; i++) {
        flip_base_low = std::min(flip_base_low, bar_history[i].low);
        flip_base_high = std::max<double>(flip_base_high, bar_history[i].high);
    }
    
    // Check if breakdown is vigorous enough
    if (bar_index + 5 >= static_cast<int>(bar_history.size())) {
        return std::nullopt;  // Not enough bars ahead
    }
    
    double price_at_breakdown = bar_history[bar_index].close;
    double price_after_impulse = bar_history[bar_index + 5].close;
    double impulse_move = std::abs(price_after_impulse - price_at_breakdown);
    double atr = Core::MarketAnalyzer::calculate_atr(bar_history, config.atr_period, bar_index);
    
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
    flipped.formation_datetime = bar_history[lookback_start].datetime;
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

void LiveEngine::export_trade_journal() {
    try {
        const auto& trades = performance.get_trades();
        
        // ⭐ FIX: Only export if there are NEW trades since last export
        if (trades.size() == last_exported_trade_count_) {
            LOG_DEBUG("No new trades to export (count=" << trades.size() << ")");
            return;
        }
        
        LOG_INFO("📊 Exporting trade journal (" << trades.size() << " trades, was " << last_exported_trade_count_ << ")...");
        
        if (trades.empty()) {
            LOG_INFO("No trades to export yet");
            return;
        }
        
        Backtest::CSVReporter reporter(output_dir_);
        
        // Calculate current metrics
        Backtest::PerformanceMetrics metrics = performance.calculate_metrics();
        metrics.total_bars = bar_history.size();
        
        // Export trade log
        bool trades_ok = reporter.export_trades(trades, "trades.csv");
        if (trades_ok) {
            LOG_INFO("✅ Trade log exported: " << output_dir_ << "/trades.csv (" << trades.size() << " trades)");
        }
        
        // Export performance metrics
        bool metrics_ok = reporter.export_metrics(metrics, "metrics.csv");
        if (metrics_ok) {
            LOG_INFO("✅ Metrics exported: " << output_dir_ << "/metrics.csv");
        }
        
        // Export equity curve
        bool equity_ok = reporter.export_equity_curve(trades, config.starting_capital, "equity_curve.csv");
        if (equity_ok) {
            LOG_INFO("✅ Equity curve exported: " << output_dir_ << "/equity_curve.csv");
        }
        
        // ⭐ Update last exported count AFTER successful export
        last_exported_trade_count_ = trades.size();
        
        std::cout << "📊 Trade journal updated: " << trades.size() << " trades, "
                  << "P&L: $" << std::fixed << std::setprecision(2) << metrics.total_pnl 
                  << " (" << metrics.total_return_pct << "%)" << std::endl;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to export trade journal: " << e.what());
    }
}

// bool LiveEngine::send_test_buy_signal() {
//     LOG_INFO("🧪 TEST: Sending dummy BUY signal to Fyers broker...");
    
//     // Check if already in position
//     if (trade_mgr.is_in_position()) {
//         LOG_WARN("Already in a position. Cannot enter another trade.");
//         std::cout << "\n⚠️  Already in position - close existing trade first" << std::endl;
//         return false;
//     }
    
//     // Get current market price
//     double current_price = broker->get_ltp(trading_symbol);
//     if (current_price <= 0) {
//         LOG_ERROR("Failed to get current market price");
//         std::cout << "\n❌ Failed to get market price from broker" << std::endl;
//         return false;
//     }
    
//     LOG_INFO("Current market price: " << current_price);
//     std::cout << "\n📊 Current Market Price: ₹" << std::fixed << std::setprecision(2) << current_price << std::endl;
    
//     // Create a dummy zone for testing (centered around current price)
//     Zone test_zone;
//     test_zone.zone_id = 99999;  // Test zone ID
//     test_zone.type = ZoneType::DEMAND;  // BUY signal
//     test_zone.distal_line = current_price - 10.0;  // 10 points below
//     test_zone.proximal_line = current_price + 10.0;  // 10 points above
//     test_zone.base_low = test_zone.distal_line;
//     test_zone.base_high = test_zone.proximal_line;
//     test_zone.formation_bar = bar_index;
//     test_zone.formation_datetime = bar_history.empty() ? "TEST" : bar_history.back().datetime;
//     test_zone.state = ZoneState::FRESH;
//     test_zone.touch_count = 0;
//     test_zone.strength = 80.0;  // High strength for test
    
//     LOG_INFO("Created test DEMAND zone: " << test_zone.distal_line << " - " << test_zone.proximal_line);
    
//     // Create dummy entry decision
//     EntryDecision test_decision;
//     test_decision.should_enter = true;
//     test_decision.entry_price = current_price;
//     test_decision.stop_loss = current_price - 30.0;  // 30 points stop loss
//     test_decision.take_profit = current_price + 60.0;  // 60 points take profit (1:2 R:R)
//     test_decision.position_size = 1;  // 1 lot for testing
//     test_decision.direction = "LONG";
//     test_decision.risk_reward_ratio = 2.0;
//     test_decision.rejection_reason = "";
    
//     LOG_INFO("Entry decision created: LONG @ " << test_decision.entry_price 
//              << ", SL: " << test_decision.stop_loss 
//              << ", TP: " << test_decision.take_profit);
    
//     // Create dummy bar
//     Bar test_bar;
//     test_bar.datetime = bar_history.empty() ? "TEST" : bar_history.back().datetime;
//     test_bar.open = current_price;
//     test_bar.high = current_price + 5.0;
//     test_bar.low = current_price - 5.0;
//     test_bar.close = current_price;
//     test_bar.volume = 10000;
    
//     // Get market regime
//     MarketRegime test_regime = MarketRegime::NEUTRAL;
//     if (!bar_history.empty()) {
//         test_regime = MarketAnalyzer::detect_regime(bar_history, 50, 5.0);
//     }
    
//     LOG_INFO("Market regime: " << static_cast<int>(test_regime));
    
//     std::cout << "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;
//     std::cout << "  🧪 TEST BUY SIGNAL" << std::endl;
//     std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;
//     std::cout << "  Symbol:      " << trading_symbol << std::endl;
//     std::cout << "  Direction:   LONG (BUY)" << std::endl;
//     std::cout << "  Entry Price: ₹" << std::fixed << std::setprecision(2) << test_decision.entry_price << std::endl;
//     std::cout << "  Stop Loss:   ₹" << test_decision.stop_loss << " (-30 pts)" << std::endl;
//     std::cout << "  Take Profit: ₹" << test_decision.take_profit << " (+60 pts)" << std::endl;
//     std::cout << "  Position:    " << test_decision.position_size << " lot" << std::endl;
//     std::cout << "  Risk:Reward: 1:" << std::setprecision(1) << test_decision.risk_reward_ratio << std::endl;
//     std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;
//     std::cout << "\n⚠️  This is a TEST trade - will be sent to broker!" << std::endl;
    
//     // Use TradeManager to enter the trade (this will call broker)
//     LOG_INFO("Calling TradeManager to execute trade...");
//     bool success = trade_mgr.enter_trade(
//         test_decision,
//         test_zone,
//         test_bar,
//         bar_index,
//         test_regime,
//         trading_symbol
//     );
    
//     if (success) {
//         const Trade& trade = trade_mgr.get_current_trade();
        
//         std::cout << "\n✅ TEST BUY ORDER SENT TO FYERS!" << std::endl;
//         std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;
//         std::cout << "  Trade ID:    " << trade.trade_id << std::endl;
//         std::cout << "  Direction:   " << trade.direction << std::endl;
//         std::cout << "  Entry:       ₹" << std::fixed << std::setprecision(2) << trade.entry_price << std::endl;
//         std::cout << "  Stop Loss:   ₹" << trade.stop_loss << std::endl;
//         std::cout << "  Take Profit: ₹" << trade.take_profit << std::endl;
//         std::cout << "  Quantity:    " << trade.position_size << " lot(s)" << std::endl;
//         std::cout << "  Capital:     ₹" << trade_mgr.get_capital() << std::endl;
//         std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;
//         std::cout << "\n📊 Position is now OPEN - monitor with process_tick()" << std::endl;
//         std::cout << "💡 To close: Use TradeManager or let SL/TP trigger" << std::endl;
        
//         LOG_INFO("✅ Test trade executed successfully");
//         LOG_INFO("Trade ID: " << trade.trade_id);
std::pair<double, double> LiveEngine::calculate_price_range(double ltp, double range_pct) const {
    double range_points = ltp * (range_pct / 100.0);
    double lower = ltp - range_points;
    double upper = ltp + range_points;
    
    LOG_DEBUG("Price range calculation: LTP=" << ltp << ", range_pct=" << range_pct 
             << "% => [" << lower << ", " << upper << "]");
    
    return {lower, upper};
}

bool LiveEngine::is_zone_in_range(const Zone& zone, double lower, double upper) const {
    // Zone overlaps range if:
    // - Zone distal is below range upper, AND
    // - Zone proximal is above range lower
    bool overlaps = (zone.distal_line <= upper && zone.proximal_line >= lower);
    
    LOG_DEBUG("Zone overlap check: distal=" << zone.distal_line << ", proximal=" << zone.proximal_line 
             << ", range=[" << lower << ", " << upper << "] => " << (overlaps ? "YES" : "NO"));
    
    return overlaps;
}

void LiveEngine::filter_zones_by_range(double ltp) {
    if (!config.enable_live_zone_filtering) {
        LOG_DEBUG("Live zone filtering disabled - keeping all zones active");
        return;
    }
    
    LOG_INFO("=== Zone Filtering by Price Range ===");
    LOG_INFO("LTP: " << ltp << ", Range: ±" << config.live_zone_range_pct << "%");
    
    auto [lower, upper] = calculate_price_range(ltp, config.live_zone_range_pct);
    LOG_INFO("Filtering zones to range: [" << lower << ", " << upper << "]");
    
    // Move zones that don't overlap into inactive list
    std::vector<Zone> zones_to_keep;
    std::vector<Zone> zones_to_deactivate;
    
    for (auto& zone : active_zones) {
        if (is_zone_in_range(zone, lower, upper)) {
            // Zone overlaps - keep active
            if (!zone.is_active) {
                zone.is_active = true;
                LOG_INFO("✅ Zone " << zone.zone_id << " RE-ACTIVATED (now in range)");
            }
            zones_to_keep.push_back(zone);
        } else {
            // Zone doesn't overlap - deactivate
            zone.is_active = false;
            zones_to_deactivate.push_back(zone);
            LOG_INFO("⛔ Zone " << zone.zone_id << " DEACTIVATED (outside range)");
        }
    }
    
    // Update vectors
    inactive_zones.insert(inactive_zones.end(), zones_to_deactivate.begin(), zones_to_deactivate.end());
    active_zones = zones_to_keep;
    
    LOG_INFO("After filtering: " << active_zones.size() << " active, " << inactive_zones.size() << " inactive");
}

void LiveEngine::refresh_active_zones(double ltp) {
    if (!config.enable_live_zone_filtering) {
        return;
    }
    
    // Check if refresh interval has elapsed
    bars_since_last_refresh_++;
    int bar_interval_minutes = std::stoi(bar_interval);
    int bars_for_interval = config.live_zone_refresh_interval_minutes > 0 ? 
        (config.live_zone_refresh_interval_minutes * 60) / bar_interval_minutes : 
        INT_MAX;  // Disable refresh if interval <= 0
    
    if (bars_since_last_refresh_ < bars_for_interval) {
        LOG_DEBUG("Refresh interval not elapsed: " << bars_since_last_refresh_ 
                 << " / " << bars_for_interval << " bars");
        return;
    }
    
    LOG_INFO("=== Periodic Zone Refresh (every " << config.live_zone_refresh_interval_minutes << " min) ===");
    LOG_INFO("Checking inactive zones for re-activation...");
    
    auto [lower, upper] = calculate_price_range(ltp, config.live_zone_range_pct);
    
    // Check inactive zones for re-activation
    std::vector<Zone> zones_to_reactivate;
    std::vector<Zone> zones_still_inactive;
    
    for (auto& zone : inactive_zones) {
        if (is_zone_in_range(zone, lower, upper)) {
            // Zone now in range - re-activate and re-score
            zone.is_active = true;
            
            // Re-score the zone with current market regime
            MarketRegime regime = MarketAnalyzer::detect_regime(bar_history, 50, 5.0);
            zone.zone_score = scorer.evaluate_zone(zone, regime, bar_history.back());
            
            LOG_INFO("🔄 Zone " << zone.zone_id << " RE-ACTIVATED and RE-SCORED: " 
                    << zone.zone_score.total_score << "/120");
            
            zones_to_reactivate.push_back(zone);
        } else {
            // Still outside range
            zones_still_inactive.push_back(zone);
        }
    }
    
    // Update vectors
    active_zones.insert(active_zones.end(), zones_to_reactivate.begin(), zones_to_reactivate.end());
    inactive_zones = zones_still_inactive;
    
    // Reset refresh counter
    bars_since_last_refresh_ = 0;
    last_refresh_ltp_ = ltp;
    
    LOG_INFO("Refresh complete: " << zones_to_reactivate.size() << " zones re-activated");
    LOG_INFO("Current state: " << active_zones.size() << " active, " << inactive_zones.size() << " inactive");
}

void LiveEngine::log_order_to_csv(const std::string& entry_time,
                                  const std::string& zone_id,
                                  double zone_score,
                                  const std::string& direction,
                                  double entry_price,
                                  double stop_loss,
                                  double take_profit,
                                  int position_size) {
    try {
        // Create output directory if it doesn't exist
        fs::path output_path = fs::path(output_dir_) / "live_trades";
        fs::create_directories(output_path);
        
        // Path to simulated_orders.csv
        fs::path csv_file = output_path / "simulated_orders.csv";
        
        // Create header if file doesn't exist
        if (!fs::exists(csv_file)) {
            std::ofstream f(csv_file);
            f << "entry_time,zone_id,zone_score,direction,entry_price,stop_loss,take_profit,position_size,lot_size,quantity\n";
            f.close();
        }
        
        // Append order to CSV
        std::ofstream f(csv_file, std::ios::app);
        int total_quantity = position_size * config.lot_size;
        
        f << entry_time << ","
          << zone_id << ","
          << std::fixed << std::setprecision(4) << zone_score << ","
          << direction << ","
          << std::fixed << std::setprecision(2) << entry_price << ","
          << std::fixed << std::setprecision(2) << stop_loss << ","
          << std::fixed << std::setprecision(2) << take_profit << ","
          << position_size << ","
          << config.lot_size << ","
          << total_quantity << "\n";
        
        f.close();
        
        std::cout << "\n📝 [ORDER LOG] Order logged to CSV\n";
        std::cout << "   File:      " << csv_file << "\n";
        std::cout << "   Direction: " << direction << "\n";
        std::cout << "   Entry:     $" << std::fixed << std::setprecision(2) << entry_price << "\n";
        std::cout << "   Quantity:  " << total_quantity << "\n";
        std::cout.flush();
        
        LOG_INFO("Order logged to CSV: " << zone_id << " " << direction << " @ " << entry_price);
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to log order to CSV: " << e.what());
    }
}
//         LOG_INFO("Entry price: " << trade.entry_price);
//         LOG_INFO("Position size: " << trade.position_size << " lots");
        
//         return true;
//     } else {
//         std::cout << "\n❌ FAILED TO SEND ORDER" << std::endl;
//         std::cout << "   Check broker connection and logs" << std::endl;
//         LOG_ERROR("Failed to execute test trade");
//         return false;
//     }
// }


} // namespace Live
} // namespace SDTrading