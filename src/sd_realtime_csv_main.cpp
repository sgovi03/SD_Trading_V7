// ============================================================
// SD REALTIME CSV MAIN V5.0
// Simplified main file compatible with V5.0 architecture
// ============================================================

#include "sd_engine_core.h"
#include "zone_manager.h"
#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>

int main(int argc, char* argv[]) {
    std::cout << "========================================" << std::endl;
    std::cout << "SD Trading System V5.0 - CSV Real-time" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;

    try {
        // Load configuration
        std::string config_file = "phase1_enhanced_v3_1_config.txt";
        if (argc > 1) {
            config_file = argv[1];
        }

        std::cout << "[INFO] Loading configuration from: " << config_file << std::endl;
        BacktestConfig config = load_config(config_file);
        
        if (!config.validate()) {
            std::cerr << "❌ Configuration validation failed!" << std::endl;
            return 1;
        }
        
        config.print_summary();

        // Load CSV data
        std::string data_file = "data/live_data.csv";
        if (argc > 2) {
            data_file = argv[2];
        }

        std::cout << "\n[INFO] Loading CSV data from: " << data_file << std::endl;
        std::vector<Bar> bars = load_csv_data(data_file);
        
        if (bars.empty()) {
            std::cerr << "❌ No data loaded!" << std::endl;
            return 1;
        }

        std::cout << "[INFO] Loaded " << bars.size() << " bars" << std::endl;

        // Initialize zone manager
        ZoneManager zone_manager;

        // ========================================
        // OPTION 1: Full-dataset scan at startup
        // ========================================
        std::cout << "\n[INFO] OPTION 1: Full-dataset zone scan (matching backtest behavior)" << std::endl;
        zone_manager.scan_full_dataset(bars, config);
        
        std::cout << "[INFO] Historical zone loading complete!" << std::endl;
        std::cout << "[INFO] Now processing live bars with zone state updates..." << std::endl;

        // Process bars
        std::cout << "\n[INFO] Starting real-time processing..." << std::endl;
        std::cout << "Press Ctrl+C to stop" << std::endl;
        std::cout << std::endl;

        int start_idx = std::max(config.lookback_for_zones + 50, 0);
        
        // DEBUG: Track zone count changes
        int prev_zone_count = zone_manager.get_active_zone_count();
        int bars_processed = 0;
        bool debug_mode = true;  // Set to true to enable DEBUG output
        
        std::cout << "\n[DEBUG MAIN] Starting processing at bar " << start_idx << std::endl;
        std::cout << "[DEBUG MAIN] Initial zone count: " << prev_zone_count << std::endl;
        std::cout << "[DEBUG MAIN] Will process " << (bars.size() - start_idx) << " bars" << std::endl;
        std::cout << "[DEBUG MAIN] Debug mode: " << (debug_mode ? "ENABLED" : "DISABLED") << std::endl;
        std::cout << "\n" << std::endl;
        
        for (int i = start_idx; i < static_cast<int>(bars.size()); ++i) {
            // Update zone manager with current bar
            try {
                // Only show full DEBUG for first 200 bars or when zone count changes
                bool show_debug = (i < start_idx + 200) || 
                                 (zone_manager.get_active_zone_count() != prev_zone_count);
                
                if (!show_debug && debug_mode) {
                    // Temporarily disable debug output for this bar
                    std::cout.setstate(std::ios_base::failbit);
                }
                
                // OPTION 1 FIX: Only update states, don't detect new zones every bar
                // (We already have all historical zones from the full-scan)
                zone_manager.update(bars, i, config);
                
                if (!show_debug && debug_mode) {
                    // Re-enable output
                    std::cout.clear();
                }
                
                int current_zone_count = zone_manager.get_active_zone_count();
                
                // Detect zone count changes
                if (current_zone_count != prev_zone_count) {
                    int delta = current_zone_count - prev_zone_count;
                    std::cout << "\n⚠️⚠️⚠️ [ZONE COUNT CHANGE] Bar " << i << " [" << bars[i].datetime << "]" << std::endl;
                    std::cout << "⚠️⚠️⚠️ Zone count: " << prev_zone_count << " → " << current_zone_count 
                             << " (delta: " << (delta > 0 ? "+" : "") << delta << ")" << std::endl;
                    
                    if (delta < 0) {
                        std::cout << "🔴🔴🔴 ZONES LOST: " << (-delta) << " zones disappeared!" << std::endl;
                    } else {
                        std::cout << "🟢🟢🟢 ZONES ADDED: " << delta << " new zones detected!" << std::endl;
                
                bars_processed++;
                
                // Print status every 10 bars
                if (i % 10 == 0) {
                    std::cout << "[" << bars[i].datetime << "] "
                             << "Bar " << i << "/" << bars.size()
                             << " | Active zones: " << zone_manager.get_active_zone_count()
                             << " | Elite: " << zone_manager.get_elite_zone_count()
                             << std::endl;
                }
                
                // Get active zones
                const std::vector<Zone>& active_zones = zone_manager.get_active_zones();
                
                // Check for potential entries
                MarketRegime regime = determine_htf_regime(bars, i, config);
                
                for (const Zone& zone : active_zones) {
                    // Evaluate entry decision
                    EntryDecision decision = evaluate_entry(zone, bars, i, regime, config);
                    
                    if (decision.should_enter) {
                        std::cout << "\n🎯 ENTRY SIGNAL!" << std::endl;
                        std::cout << "  Type: " << zone_type_to_string(zone.type) << std::endl;
                        std::cout << "  Zone: " << zone.base_low << " - " << zone.base_high << std::endl;
                        std::cout << "  Entry: " << decision.entry_price << std::endl;
                        std::cout << "  Stop: " << decision.stop_loss << std::endl;
                        std::cout << "  Target: " << decision.take_profit << std::endl;
                        std::cout << "  Score: " << decision.score.total_score << std::endl;
                        std::cout << "  Elite: " << (zone.is_elite ? "YES" : "NO") << std::endl;
                        std::cout << std::endl;
                    }
                }
                
                // Simulate real-time delay (optional)
                if (config.enable_debug_logging) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                
            } catch (const std::exception& e) {
                std::cerr << "❌ [ERROR] Processing failed at bar " << i 
                         << ": " << e.what() << std::endl;
                return 1;
            }
        }

        // Final summary
        std::cout << "\n========================================" << std::endl;
        std::cout << "Processing Complete!" << std::endl;
        std::cout << "========================================" << std::endl;
        
        zone_manager.print_statistics();

        std::cout << "\nPress Enter to exit...";
        std::cin.get();

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "\n❌ [FATAL ERROR] " << e.what() << std::endl;
        std::cout << "\nPress Enter to exit...";
        std::cin.get();
        return 1;
    }
}
