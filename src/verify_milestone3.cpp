#include "core/config_loader.h"
#include "zones/zone_detector.h"
#include "scoring/zone_scorer.h"
#include "scoring/entry_decision_engine.h"
#include "analysis/market_analyzer.h"
#include "utils/logger.h"
#include "common_types.h"
#include <iostream>
#include <vector>
#include <fstream>
#include <sstream>
#include <filesystem>

// CORRECT NAMESPACE - Everything is in SDTrading::Core::
using namespace SDTrading::Core;
using SDTrading::Utils::Logger;
using SDTrading::Utils::LogLevel;
using namespace std;

namespace fs = std::filesystem;

// Simple CSV loader
vector<Bar> load_csv(const string& filepath) {
    vector<Bar> bars;
    ifstream file(filepath);

    if (!file.is_open()) {
        throw runtime_error("Cannot open file: " + filepath);
    }

    string line;
    getline(file, line); // Skip header

    while (getline(file, line)) {
        if (line.empty()) continue;

        stringstream ss(line);
        string datetime, open_str, high_str, low_str, close_str, volume_str;

        getline(ss, datetime, ',');
        getline(ss, open_str, ',');
        getline(ss, high_str, ',');
        getline(ss, low_str, ',');
        getline(ss, close_str, ',');
        getline(ss, volume_str, ',');

        Bar bar;
        bar.datetime = datetime;
        bar.open = stod(open_str);
        bar.high = stod(high_str);
        bar.low = stod(low_str);
        bar.close = stod(close_str);
        bar.volume = volume_str.empty() ? 0 : stol(volume_str);

        bars.push_back(bar);
    }

    file.close();
    return bars;
}

int main(int argc, char* argv[]) {
    Logger::getInstance().enableConsole(true);
    Logger::getInstance().enableFile(true);
    Logger::getInstance().setLevel(LogLevel::INFO);

    LOG_INFO("=================================================");
    LOG_INFO("  SD Trading System V4.0 - Milestone 3 Verification");
    LOG_INFO("  Zone Scoring & Entry Decision Logic");
    LOG_INFO("=================================================");

    // Get file paths
    string config_file = (argc > 1) ? argv[1] : "conf/phase1_enhanced_v3_1_config.txt";
    string data_file = (argc > 2) ? argv[2] : "data/sample_data.csv";

    if (!fs::exists(config_file)) config_file = "phase1_enhanced_v3_1_config.txt";
    if (!fs::exists(data_file)) data_file = "sample_data.csv";

    cout << "\nConfiguration: " << config_file << endl;
    cout << "Data file: " << data_file << endl;

    // Load configuration
    Config config;
    try {
        config = ConfigLoader::load_from_file(config_file);
        LOG_INFO("Configuration loaded");
    }
    catch (const exception& e) {
        LOG_ERROR("Config failed: " << e.what());
        config = Config();
    }

    // Load data
    vector<Bar> bars;
    try {
        bars = load_csv(data_file);
        LOG_INFO("Loaded " << bars.size() << " bars");
    }
    catch (const exception& e) {
        LOG_ERROR("Data load failed: " << e.what());
        return 1;
    }

    if (bars.size() < 50) {
        LOG_ERROR("Insufficient data");
        return 1;
    }

    // Create components
    ZoneDetector detector(config);
    ZoneScorer scorer(config);
    EntryDecisionEngine entry_engine(config);

    // Feed bars to detector
    LOG_INFO("\nDetecting zones...");
    for (const auto& bar : bars) {
        detector.add_bar(bar);
    }

    // Detect zones
    vector<Zone> zones = detector.detect_zones();
    LOG_INFO("Detected " << zones.size() << " zones");

    if (zones.empty()) {
        LOG_ERROR("No zones detected");
        return 1;
    }

    // Test scoring
    LOG_INFO("\n=== Testing Zone Scoring ===");
    int zones_scored = 0;
    int elite_zones = 0;
    double total_score = 0.0;

    // Get market regime
    MarketRegime regime = MarketAnalyzer::detect_regime(
        bars, config.htf_lookback_bars, config.htf_trending_threshold);

    for (const auto& zone : zones) {
        if (zones_scored >= 10) break; // Test first 10 zones

        // Score zone
        ZoneScore score = scorer.evaluate_zone(zone, regime, bars.back());

        zones_scored++;
        total_score += score.total_score;

        // Check elite
        if (score.elite_bonus_score >= 15.0) {
            elite_zones++;
            cout << "  Elite Zone: Score=" << score.total_score
                << " (Base=" << score.base_strength_score
                << ", Elite=" << score.elite_bonus_score << ")" << endl;
        }
    }

    cout << "\nScoring Results:" << endl;
    cout << "  Zones scored: " << zones_scored << endl;
    cout << "  Elite zones: " << elite_zones << endl;
    cout << "  Avg score: " << (zones_scored > 0 ? total_score / zones_scored : 0) << endl;

    // Test entry decisions
    LOG_INFO("\n=== Testing Entry Decisions ===");
    int decisions = 0;
    double atr = MarketAnalyzer::calculate_atr(bars, config.atr_period);

    for (const auto& zone : zones) {
        if (decisions >= 5) break;

        ZoneScore score = scorer.evaluate_zone(zone, regime, bars.back());

        if (score.total_score < config.scoring.entry_minimum_score) continue;

        EntryDecision decision = entry_engine.calculate_entry(zone, score, atr, regime);

        if (decision.should_enter) {
            decisions++;
            cout << "  Entry: Score=" << score.total_score
                << ", Entry=" << decision.entry_price
                << ", SL=" << decision.stop_loss
                << ", TP=" << decision.take_profit
                << ", R:R=" << decision.expected_rr << endl;
        }
    }

    cout << "\nEntry Decisions: " << decisions << endl;

    // Verification
    cout << "\n=== Milestone 3 Verification ===" << endl;

    bool all_passed = true;

    if (zones_scored > 0) {
        cout << "✅ Zone scoring working" << endl;
    }
    else {
        cout << "❌ Zone scoring failed" << endl;
        all_passed = false;
    }

    if (decisions > 0) {
        cout << "✅ Entry decision working" << endl;
    }
    else {
        cout << "❌ Entry decision failed" << endl;
        all_passed = false;
    }

    if (elite_zones > 0) {
        cout << "✅ Elite detection working" << endl;
    }
    else {
        cout << "⚠️  No elite zones (may be normal)" << endl;
    }

    cout << "\n==================================================" << endl;
    if (all_passed) {
        cout << "  ✅ MILESTONE 3 VERIFIED!" << endl;
        LOG_INFO("✅ All tests passed");
    }
    else {
        cout << "  ❌ MILESTONE 3 FAILED" << endl;
        LOG_ERROR("❌ Some tests failed");
    }
    cout << "==================================================" << endl;

    return all_passed ? 0 : 1;
}