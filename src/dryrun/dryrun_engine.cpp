// ============================================================
// DRY RUN ENGINE - IMPLEMENTATION
// ============================================================


// Define NOMINMAX before any Windows includes to prevent min/max macro conflicts
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "dryrun_engine.h"
#include "utils/logger.h"
#include <iostream>
#include <cstdlib>
#include <string>
#include <algorithm>  // for std::min, std::max

namespace SDTrading {
namespace DryRun {

using namespace Utils;
using namespace Core;
using namespace Live;

DryRunEngine::DryRunEngine(
    const Config& cfg,
    const std::string& csv_path,
    const std::string& symbol,
    const std::string& interval,
    const std::string& output_dir,
    bool enable_ticks,
    int ticks_per_bar
) : LiveEngine(
        cfg,
        nullptr,  // Broker will be created in initialize()
        symbol,
        interval,
        output_dir,
        csv_path  // Pass CSV path for historical loading
    ),
    csv_file_path_(csv_path),
    symbol_(symbol),
    interval_(interval),
    enable_tick_simulation_(enable_ticks),
    ticks_per_bar_(ticks_per_bar),
    simulator_ptr_(nullptr),
    bootstrap_end_bar_(0),
    is_dryrun_bootstrap_active_(false) {
    
    LOG_INFO("DryRunEngine created");
    LOG_INFO("  CSV file:        " << csv_file_path_);
    LOG_INFO("  Symbol:          " << symbol);
    LOG_INFO("  Interval:        " << interval << " min");
    LOG_INFO("  Tick simulation: " << (enable_ticks ? "ENABLED" : "DISABLED"));
    LOG_INFO("  Output:          " << output_dir);
}

bool DryRunEngine::initialize() {
    LOG_INFO("==================================================");
    LOG_INFO("  Initializing DryRun Engine");
    LOG_INFO("  Mode: Live Logic + CSV Simulator (2-Phase)");
    LOG_INFO("==================================================");
    
    // Create CSV simulator broker
    LOG_INFO("Creating CSV simulator broker...");
    auto simulator = std::make_unique<CsvSimulatorBroker>(
        csv_file_path_,
        symbol_,
        interval_,
        enable_tick_simulation_,
        ticks_per_bar_
    );
    
    simulator_ptr_ = simulator.get();
    
    // Replace broker with simulator
    broker = std::move(simulator);
    
    LOG_INFO("✅ CSV simulator created");
    
    // Connect broker (CSV Simulator)
    if (!broker->connect()) {
        LOG_ERROR("Failed to connect to CSV simulator");
        return false;
    }
    
    LOG_INFO("✅ Broker connected");
    
    size_t total_bars = simulator_ptr_->get_total_bars();
    LOG_DEBUG("Total bars in CSV: " << total_bars);
    
    // ==========================================
    // PHASE 1: BOOTSTRAP ZONES FROM HISTORICAL DATA
    // ==========================================
    
    int bootstrap_bars = config.dryrun_bootstrap_bars;
    int test_start_bar = config.dryrun_test_start_bar;

    // Optional CLI/env override for bootstrap bars
    if (const char* env_bootstrap = std::getenv("SD_DRYRUN_BOOTSTRAP_BARS")) {
        try {
            int v = std::stoi(env_bootstrap);
            if (v > 0) {
                LOG_INFO("Bootstrap bars overridden via env: " << v);
                bootstrap_bars = v;
            } else {
                LOG_WARN("SD_DRYRUN_BOOTSTRAP_BARS provided but not > 0. Ignoring.");
            }
        } catch (...) {
            LOG_WARN("Invalid SD_DRYRUN_BOOTSTRAP_BARS value. Expect integer. Ignoring.");
        }
    }

    // Safety cap for extremely large bootstrap sizes
    const int MAX_BOOTSTRAP_CAP = 30000; // prevents hour-long scans on huge CSVs
    // Check config first, then fall back to environment variable for backward compatibility
    bool allow_large = config.allow_large_bootstrap;
    if (!allow_large) {
        const char* allow_env = std::getenv("SD_ALLOW_LARGE_BOOTSTRAP");
        allow_large = (allow_env && std::string(allow_env) == "1");
    }
    if (bootstrap_bars > MAX_BOOTSTRAP_CAP) {
        if (allow_large) {
            LOG_INFO("Large bootstrap override enabled (config: " << config.allow_large_bootstrap 
                     << ", env: " << (std::getenv("SD_ALLOW_LARGE_BOOTSTRAP") ? std::getenv("SD_ALLOW_LARGE_BOOTSTRAP") : "not set")
                     << "). Using " << bootstrap_bars << " bars.");
        } else {
            LOG_WARN("Requested bootstrap-bars=" << bootstrap_bars << " exceeds cap " << MAX_BOOTSTRAP_CAP
                     << ". Capping to " << MAX_BOOTSTRAP_CAP << " for performance. Set allow_large_bootstrap=1 in config or SD_ALLOW_LARGE_BOOTSTRAP=1 env var to override.");
            bootstrap_bars = MAX_BOOTSTRAP_CAP;
        }
    }
    
    // Auto-calculate bootstrap if -1
    if (bootstrap_bars == -1) {
        // Use last 10,000 bars for bootstrap (or 60% of data if smaller)
        bootstrap_bars = std::min<size_t>(10000, total_bars * 60 / 100);
    }
    
    // Auto-calculate test start if 0
    if (test_start_bar == 0 && bootstrap_bars > 0) {
        test_start_bar = bootstrap_bars;
    }

    // Ensure test_start_bar and bootstrap_bars are within dataset bounds
    if (bootstrap_bars > (int)total_bars) {
        LOG_WARN("Bootstrap bars " << bootstrap_bars << " exceed total bars " << total_bars
                 << ". Adjusting to dataset size.");
        bootstrap_bars = (int)total_bars;
    }
    if (test_start_bar > (int)total_bars) {
        LOG_WARN("Test start bar " << test_start_bar << " exceed total bars " << total_bars
                 << ". Adjusting to dataset size.");
        test_start_bar = (int)total_bars;
    }
    
    if (bootstrap_bars > 0 && bootstrap_bars < static_cast<int>(total_bars)) {
        // ⭐ CRITICAL FIX: Bootstrap window BEFORE test window
        int bootstrap_end = std::min(test_start_bar, static_cast<int>(total_bars));
        int bootstrap_start = std::max(0, bootstrap_end - bootstrap_bars);
        int actual_bootstrap_bars = bootstrap_end - bootstrap_start;
        
        LOG_DEBUG("");
        LOG_DEBUG("╔════════════════════════════════════════════════╗");
        LOG_DEBUG("║  PHASE 1: BOOTSTRAP ZONE DETECTION            ║");
        LOG_DEBUG("╚════════════════════════════════════════════════╝");
        LOG_DEBUG("");
        LOG_INFO("=== INITIAL ZONE DETECTION ===");
        LOG_INFO("  Bootstrap bars: " << bootstrap_bars);
        LOG_INFO("  Bootstrap Period: Bars " << bootstrap_start << " to " << bootstrap_end);
        LOG_INFO("  Loading historical data for zone detection...");
        LOG_DEBUG("  Loading historical data for zone detection...");
        
        bar_history.clear();
        
        // Load bootstrap bars immediately BEFORE test window
        for (int i = bootstrap_start; i < bootstrap_end; i++) {
            Core::Bar bar = simulator_ptr_->get_bar_at_index(i);
            bar_history.push_back(bar);
            detector.add_bar(bar);
            
            int loaded = i - bootstrap_start + 1;
            if (loaded % 5000 == 0) {
                LOG_INFO("  Loaded " << loaded << " / " << actual_bootstrap_bars << " bars...");
            }
        }
        
        LOG_INFO("  ✅ Loaded " << actual_bootstrap_bars << " bars");
        LOG_INFO("");
        LOG_INFO("  Detecting zones across bootstrap period...");
        
        // Detect ALL zones in bootstrap period
        active_zones = Core::ZoneInitializer::detect_initial_zones(bar_history, detector, next_zone_id_);
        
        LOG_DEBUG("  ✅ Detected " << active_zones.size() << " zones");
        LOG_DEBUG("");
        LOG_DEBUG("  Scoring zones...");
        
        // Score all zones
        Core::MarketRegime regime = Core::MarketAnalyzer::detect_regime(bar_history, 50, 5.0);
        for (auto& zone : active_zones) {
            zone.zone_score = scorer.evaluate_zone(zone, regime, bar_history.back());
        }

        // Constrain bootstrap zones to the price window around the test start LTP
        if (config.enable_live_zone_filtering && !bar_history.empty()) {
            double ref_price = bar_history.back().close;
            LOG_INFO("Filtering bootstrap zones to +/-" << config.live_zone_range_pct
                     << "% of " << ref_price << " (dryrun start LTP)");
            filter_zones_by_range(ref_price);
        } else {
            LOG_WARN("Live zone filtering disabled; bootstrap zones will include all price levels");
        }
        
        LOG_DEBUG("  ✅ Scored " << active_zones.size() << " zones");
        LOG_DEBUG("");
        LOG_DEBUG("╔════════════════════════════════════════════════╗");
        LOG_DEBUG("║  BOOTSTRAP COMPLETE                            ║");
        LOG_DEBUG("╚════════════════════════════════════════════════╝");
        LOG_DEBUG("  Zones Detected: " << active_zones.size());
        LOG_DEBUG("  Historical Bars: " << bar_history.size());
        LOG_DEBUG("");
        
        // ⭐ NEW: Store bootstrap end bar and set flag to skip trading during bootstrap
        bootstrap_end_bar_ = bootstrap_end;
        is_dryrun_bootstrap_active_ = true;
        dryrun_bootstrap_end_bar_ = bootstrap_end;  // Also set in LiveEngine
        skip_trading_until_bar_ = true;             // Enable skip flag in LiveEngine
        
        LOG_INFO("⭐ DryRun Bootstrap Configuration:");
        LOG_INFO("  Bootstrap End Bar: " << bootstrap_end_bar_);
        LOG_INFO("  Skip Trading Until Bar: " << bootstrap_end_bar_);
        LOG_INFO("  Trading disabled for bars 0-" << (bootstrap_end_bar_ - 1));
        LOG_INFO("  Trading will resume from bar " << bootstrap_end_bar_);
        
        // Position simulator at test start bar
        simulator_ptr_->skip_to_bar_index(bootstrap_end);
        
        LOG_DEBUG("╔════════════════════════════════════════════════╗");
        LOG_DEBUG("║  PHASE 2: LIVE TESTING MODE                   ║");
        LOG_DEBUG("╚════════════════════════════════════════════════╝");
        LOG_DEBUG("  Test Start Bar: " << bootstrap_end);
        LOG_DEBUG("  Test End Bar:   " << total_bars);
        LOG_DEBUG("  Test Bars:      " << (total_bars - bootstrap_end));
        LOG_DEBUG("  Trading:        ENABLED (starting from bar " << bootstrap_end << ")");
        LOG_DEBUG("  Zone Detection: Incremental (live mode)");
        LOG_DEBUG("");
        
    } else {
        LOG_INFO("⚠️  Bootstrap disabled or invalid - starting from bar 0");
        bar_history.clear();
        simulator_ptr_->skip_to_bar_index(0);
    }
    
    LOG_INFO("==================================================");
    LOG_INFO("  DryRun Engine Ready!");
    LOG_INFO("==================================================");
    
    return true;
}

void DryRunEngine::run(int duration_minutes) {
    LOG_DEBUG("==================================================");
    LOG_DEBUG("  Starting DryRun Mode");
    LOG_DEBUG("  CSV bars: " << get_total_bars());
    LOG_DEBUG("  Processing: Sequential (bar-by-bar)");
    LOG_DEBUG("==================================================");
    
    // Override duration if 0 (process until CSV exhausted)
    if (duration_minutes == 0) {
        LOG_DEBUG("Duration: Until CSV exhausted");
    } else {
        LOG_DEBUG("Duration: " << duration_minutes << " minutes");
    }
    
    // Call inherited run logic from LiveEngine
    // This will process bars sequentially using CSV simulator
    LiveEngine::run(duration_minutes);
}

void DryRunEngine::stop() {
    LOG_DEBUG("==================================================");
    LOG_DEBUG("  Stopping DryRun Mode");
    LOG_DEBUG("==================================================");
    
    // Call inherited stop logic
    LiveEngine::stop();
    
    // Export simulator order log
    if (simulator_ptr_) {
        LOG_INFO("Exporting simulator order log...");
        simulator_ptr_->export_order_log("dryrun_simulator_orders.csv");
    }
    
    // Print summary
    std::cout << "\n==================================================\n";
    std::cout << "  DRYRUN PERFORMANCE SUMMARY\n";
    std::cout << "==================================================\n";
    std::cout << "CSV Bars Processed: " << get_current_position() << "/" << get_total_bars() << "\n";
    std::cout << "Completion:         " << get_progress_pct() << "%\n";
    std::cout << "==================================================\n\n";
}

double DryRunEngine::get_progress_pct() const {
    if (!simulator_ptr_ || simulator_ptr_->get_total_bars() == 0) return 0.0;
    return (double)simulator_ptr_->get_current_position() / simulator_ptr_->get_total_bars() * 100.0;
}

bool DryRunEngine::has_more_bars() const {
    if (!simulator_ptr_) return false;
    return simulator_ptr_->get_current_position() < simulator_ptr_->get_total_bars();
}

size_t DryRunEngine::get_total_bars() const {
    if (!simulator_ptr_) return 0;
    return simulator_ptr_->get_total_bars();
}

size_t DryRunEngine::get_current_position() const {
    if (!simulator_ptr_) return 0;
    return simulator_ptr_->get_current_position();
}

} // namespace DryRun
} // namespace SDTrading
