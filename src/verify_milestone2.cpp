#include "core/config_loader.h"
#include "utils/logger.h"
#include "common_types.h"
#include <iostream>
#include <vector>
#include <fstream>
#include <sstream>
#include <filesystem>

using namespace SDTrading::Core;
using namespace SDTrading::Utils;
using namespace std;

namespace fs = std::filesystem;

// Simple CSV loader function
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

void print_config_summary(const Config& config) {
    cout << "\n=== Configuration Summary ===" << endl;
    cout << "Starting Capital:    $" << config.starting_capital << endl;
    cout << "Risk Per Trade:      " << config.risk_per_trade_pct << "%" << endl;
    cout << "ATR Period:          " << config.atr_period << endl;
    cout << "Min Zone Strength:   " << config.min_zone_strength << endl;
    cout << "Lot Size:            " << config.lot_size << endl;
}

void print_data_summary(const vector<Bar>& bars) {
    if (bars.empty()) {
        cout << "\nNo data loaded!" << endl;
        return;
    }
    
    cout << "\n=== Data Summary ===" << endl;
    cout << "First Date:    " << bars.front().datetime << endl;
    cout << "First Close:   " << bars.front().close << endl;
    cout << "Last Date:     " << bars.back().datetime << endl;
    cout << "Last Close:    " << bars.back().close << endl;
    cout << "Total Bars:    " << bars.size() << endl;
}

int main(int argc, char* argv[]) {
    // Initialize logger
    Logger::getInstance().enableConsole(true);
    Logger::getInstance().enableFile(true);
    Logger::getInstance().setLevel(LogLevel::INFO);
    
    LOG_INFO("=================================================");
    LOG_INFO("  SD Trading System V4.0 - Milestone 2 Verification");
    LOG_INFO("=================================================");
    
    // Determine file paths
    string config_file;
    string data_file;
    
    if (argc > 1) {
        config_file = argv[1];
    } else {
        config_file = "conf/phase1_enhanced_v3_1_config.txt";
        if (!fs::exists(config_file)) {
            config_file = "phase1_enhanced_v3_1_config.txt";
        }
        if (!fs::exists(config_file)) {
            cerr << "ERROR: No config file found!" << endl;
            cerr << "Usage: verify_milestone2.exe <config_file> <data_file>" << endl;
            return 1;
        }
    }
    
    if (argc > 2) {
        data_file = argv[2];
    } else {
        data_file = "data/sample_data.csv";
        if (!fs::exists(data_file)) {
            data_file = "sample_data.csv";
        }
        if (!fs::exists(data_file)) {
            cerr << "ERROR: No data file found!" << endl;
            cerr << "Usage: verify_milestone2.exe <config_file> <data_file>" << endl;
            return 1;
        }
    }
    
    cout << "\nConfiguration file: " << config_file << endl;
    cout << "Data file: " << data_file << endl;
    
    // Load configuration
    Config config;
    try {
        config = ConfigLoader::load_from_file(config_file);
        LOG_INFO("✅ Configuration loaded successfully");
        print_config_summary(config);
    } catch (const exception& e) {
        LOG_ERROR("❌ Failed to load configuration: " << e.what());
        return 1;
    }
    
    // Load data
    vector<Bar> bars;
    try {
        bars = load_csv(data_file);
        LOG_INFO("✅ Data loaded successfully");
        print_data_summary(bars);
    } catch (const exception& e) {
        LOG_ERROR("❌ Failed to load data: " << e.what());
        return 1;
    }
    
    // Verification
    cout << "\n=== Milestone 2 Verification ===" << endl;
    
    bool all_passed = true;
    
    // Test 1: Configuration loaded
    if (config.starting_capital > 0) {
        cout << "✅ Configuration system working" << endl;
    } else {
        cout << "❌ Configuration system failed" << endl;
        all_passed = false;
    }
    
    // Test 2: Data loaded
    if (!bars.empty()) {
        cout << "✅ Data loading system working" << endl;
    } else {
        cout << "❌ Data loading system failed" << endl;
        all_passed = false;
    }
    
    // Test 3: Data structure validation
    bool data_valid = true;
    for (size_t i = 0; i < min(size_t(10), bars.size()); i++) {
        if (bars[i].close <= 0 || bars[i].high < bars[i].low) {
            data_valid = false;
            break;
        }
    }
    
    if (data_valid) {
        cout << "✅ Data structure validation passed" << endl;
    } else {
        cout << "❌ Data structure validation failed" << endl;
        all_passed = false;
    }
    
    // Final result
    cout << "\n==================================================" << endl;
    if (all_passed) {
        cout << "  ✅ MILESTONE 2 VERIFIED SUCCESSFULLY!" << endl;
        LOG_INFO("✅ All Milestone 2 tests passed");
    } else {
        cout << "  ❌ MILESTONE 2 VERIFICATION FAILED" << endl;
        LOG_ERROR("❌ Some Milestone 2 tests failed");
    }
    cout << "==================================================" << endl;
    
    return all_passed ? 0 : 1;
}
