// ============================================================
// SD ENGINE CORE V5.0 - IMPLEMENTATION
// Multi-Dimensional Zone Scoring with Modular Architecture
// FIXED: Broken down monolithic functions into smaller pieces
// FIXED: Better error handling
// FIXED: Cleaner code structure
// ============================================================

#include "sd_engine_core.h"
#include <fstream>
#include <sstream>
#include <cmath>
#include <algorithm>

// ============================================================
// HELPER FUNCTIONS - Simple and Focused
// ============================================================

std::string zone_type_to_string(ZoneType type) {
    return (type == ZoneType::DEMAND) ? "DEMAND" : "SUPPLY";
}

std::string zone_state_to_string(ZoneState state) {
    switch(state) {
        case ZoneState::FRESH: return "FRESH";
        case ZoneState::TESTED: return "TESTED";
        case ZoneState::VIOLATED: return "VIOLATED";
        default: return "UNKNOWN";
    }
}

std::string market_regime_to_string(MarketRegime regime) {
    switch(regime) {
        case MarketRegime::BULL: return "BULL";
        case MarketRegime::BEAR: return "BEAR";
        case MarketRegime::RANGING: return "RANGING";
        default: return "UNKNOWN";
    }
}

// ============================================================
// CONFIGURATION - Validation and Loading
// ============================================================

bool BacktestConfig::validate() const {
    if (starting_capital <= 0) return false;
    if (risk_per_trade_pct <= 0 || risk_per_trade_pct > 100) return false;
    if (consolidation_min_bars >= consolidation_max_bars) return false;
    if (atr_period < 1) return false;
    return scoring_config.validate();
}

bool ScoringConfig::validate() const {
    double total_weight = weight_base_strength + weight_elite_bonus + 
                         weight_regime_alignment + weight_state_freshness + 
                         weight_rejection_confirmation;
    return std::abs(total_weight - 1.0) < 0.01;  // Allow small floating point error
}

void BacktestConfig::print_summary() const {
    std::cout << "\n=== Backtest Configuration ===" << std::endl;
    std::cout << "Capital: $" << starting_capital << std::endl;
    std::cout << "Risk per trade: " << risk_per_trade_pct << "%" << std::endl;
    std::cout << "Lot size: " << lot_size << std::endl;
    std::cout << "Adaptive entry: " << (use_adaptive_entry ? "YES" : "NO") << std::endl;
    std::cout << "Min zone strength: " << min_zone_strength << std::endl;
}

BacktestConfig load_config(const std::string& config_file) {
    BacktestConfig config;
    std::ifstream file(config_file);
    
    if (!file.is_open()) {
        std::cerr << "⚠️  Could not open config file: " << config_file << std::endl;
        std::cerr << "Using default configuration" << std::endl;
        return config;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        // Skip comments and empty lines
        if (line.empty() || line[0] == '#') continue;
        
        // Parse key = value
        size_t pos = line.find('=');
        if (pos == std::string::npos) continue;
        
        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);
        
        // Trim whitespace
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);
        
        // Parse configuration values
        if (key == "starting_capital") config.starting_capital = std::stod(value);
        else if (key == "risk_per_trade_pct") config.risk_per_trade_pct = std::stod(value);
        else if (key == "lot_size") config.lot_size = std::stoi(value);
        else if (key == "base_height_max_atr") config.base_height_max_atr = std::stod(value);
        else if (key == "consolidation_min_bars") config.consolidation_min_bars = std::stoi(value);
        else if (key == "consolidation_max_bars") config.consolidation_max_bars = std::stoi(value);
        else if (key == "min_impulse_atr") config.min_impulse_atr = std::stod(value);
        else if (key == "min_zone_strength") config.min_zone_strength = std::stod(value);
        else if (key == "use_adaptive_entry") config.use_adaptive_entry = (value == "YES" || value == "yes" || value == "1");
        else if (key == "max_loss_per_trade") {
            std::string clean_val = value.substr(0, value.find('#'));
            clean_val.erase(clean_val.find_last_not_of(" \t") + 1);
            if (!clean_val.empty()) config.max_loss_per_trade = std::stod(clean_val);
        }
        // ... add more config parsing as needed
    }
    
    file.close();
    
    if (!config.validate()) {
        std::cerr << "❌ Configuration validation failed!" << std::endl;
        throw std::runtime_error("Invalid configuration");
    }
    
    return config;
}

// ============================================================
// DATA LOADING
// ============================================================
/*
std::vector<Bar> load_csv_data(const std::string& filename) {
    std::vector<Bar> bars;
    bars.reserve(10000);  // Pre-allocate
    
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "❌ Could not open data file: " << filename << std::endl;
        throw std::runtime_error("Failed to open CSV file");
    }
    
    std::string line;
    // Skip header
    std::getline(file, line);
    
    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string field;
        Bar bar;
        
        // Parse CSV: datetime,open,high,low,close,volume,oi
        std::getline(ss, bar.datetime, ',');
        std::getline(ss, field, ','); bar.open = std::stod(field);
        std::getline(ss, field, ','); bar.high = std::stod(field);
        std::getline(ss, field, ','); bar.low = std::stod(field);
        std::getline(ss, field, ','); bar.close = std::stod(field);
        std::getline(ss, field, ','); bar.volume = std::stod(field);
        std::getline(ss, field, ','); bar.oi = std::stod(field);
        
        bars.push_back(bar);
    }
    
    file.close();
    std::cout << "[INFO] Loaded " << bars.size() << " bars from " << filename << std::endl;
    
    return bars;
}*/

// ============================================================
// HEADER-AWARE CSV LOADER - For Fyers Format
// Handles: Timestamp,DateTime,Symbol,Open,High,Low,Close,Volume,OpenInterest
// ============================================================

#include "sd_engine_core.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <map>

// Trim whitespace
std::string trim_str(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

// Convert to lowercase
std::string to_lower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

// Safe string to double
double safe_to_double(const std::string& str) {
    std::string cleaned = trim_str(str);
    if (cleaned.empty()) return 0.0;

    try {
        return std::stod(cleaned);
    }
    catch (...) {
        std::cerr << "⚠️  Cannot convert '" << str << "' to double" << std::endl;
        return 0.0;
    }
}

std::vector<Bar> load_csv_data(const std::string& filename) {
    std::vector<Bar> bars;
    bars.reserve(10000);

    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "❌ Could not open: " << filename << std::endl;
        throw std::runtime_error("File not found");
    }

    std::string line;
    int line_num = 0;
    char delimiter = ',';

    // Column mapping
    std::map<std::string, int> col_map;
    int datetime_col = -1;
    int open_col = -1;
    int high_col = -1;
    int low_col = -1;
    int close_col = -1;
    int volume_col = -1;
    int oi_col = -1;
    int oi_fresh_col = -1;         // NEW V6.0
    int oi_age_seconds_col = -1;   // NEW V6.0

    std::cout << "[INFO] Reading: " << filename << std::endl;

    // Read and parse header
    if (std::getline(file, line)) {
        line_num++;
        line = trim_str(line);

        if (line.empty()) {
            std::cerr << "❌ Empty first line" << std::endl;
            throw std::runtime_error("Invalid CSV");
        }

        // Detect delimiter
        if (std::count(line.begin(), line.end(), ';') > std::count(line.begin(), line.end(), ',')) {
            delimiter = ';';
        }

        std::cout << "[INFO] Delimiter: '" << delimiter << "'" << std::endl;

        // Parse header columns
        std::stringstream ss(line);
        std::string col_name;
        int col_idx = 0;

        std::cout << "[INFO] Columns detected:" << std::endl;
        while (std::getline(ss, col_name, delimiter)) {
            col_name = trim_str(col_name);
            std::string col_lower = to_lower(col_name);

            std::cout << "   [" << col_idx << "] " << col_name << std::endl;

            // Map column names to indices
            if (col_lower.find("datetime") != std::string::npos ||
                col_lower.find("date") != std::string::npos ||
                col_lower.find("time") != std::string::npos) {
                datetime_col = col_idx;
            }
            else if (col_lower == "open") {
                open_col = col_idx;
            }
            else if (col_lower == "high") {
                high_col = col_idx;
            }
            else if (col_lower == "low") {
                low_col = col_idx;
            }
            else if (col_lower == "close") {
                close_col = col_idx;
            }
            else if (col_lower == "volume" || col_lower == "vol") {
                volume_col = col_idx;
            }
            else if (col_lower.find("interest") != std::string::npos ||
                col_lower == "oi") {
                oi_col = col_idx;
            }
            // NEW V6.0: OI metadata columns
            else if (col_lower == "oi_fresh") {
                oi_fresh_col = col_idx;
            }
            else if (col_lower == "oi_age_seconds") {
                oi_age_seconds_col = col_idx;
            }

            col_idx++;
        }

        std::cout << std::endl;
        std::cout << "[INFO] Column mapping:" << std::endl;
        std::cout << "   DateTime: " << (datetime_col >= 0 ? std::to_string(datetime_col) : "NOT FOUND") << std::endl;
        std::cout << "   Open:     " << (open_col >= 0 ? std::to_string(open_col) : "NOT FOUND") << std::endl;
        std::cout << "   High:     " << (high_col >= 0 ? std::to_string(high_col) : "NOT FOUND") << std::endl;
        std::cout << "   Low:      " << (low_col >= 0 ? std::to_string(low_col) : "NOT FOUND") << std::endl;
        std::cout << "   Close:    " << (close_col >= 0 ? std::to_string(close_col) : "NOT FOUND") << std::endl;
        std::cout << "   Volume:   " << (volume_col >= 0 ? std::to_string(volume_col) : "NOT FOUND") << std::endl;
        std::cout << "   OI:       " << (oi_col >= 0 ? std::to_string(oi_col) : "NOT FOUND") << std::endl;
        std::cout << "   OI_Fresh: " << (oi_fresh_col >= 0 ? std::to_string(oi_fresh_col) : "NOT FOUND (V6.0)") << std::endl;
        std::cout << "   OI_Age_Seconds: " << (oi_age_seconds_col >= 0 ? std::to_string(oi_age_seconds_col) : "NOT FOUND (V6.0)") << std::endl;
        std::cout << std::endl;

        // Validate required columns
        if (datetime_col < 0 || open_col < 0 || high_col < 0 ||
            low_col < 0 || close_col < 0) {
            std::cerr << "❌ Missing required columns!" << std::endl;
            std::cerr << "   Required: DateTime, Open, High, Low, Close" << std::endl;
            throw std::runtime_error("Missing columns");
        }
    }

    // Read data rows
    int valid_bars = 0;
    int skipped_bars = 0;

    while (std::getline(file, line)) {
        line_num++;
        line = trim_str(line);

        if (line.empty()) {
            continue;
        }

        try {
            std::stringstream ss(line);
            std::string field;
            std::vector<std::string> fields;

            // Split line into fields
            while (std::getline(ss, field, delimiter)) {
                fields.push_back(trim_str(field));
            }

            // Validate field count
            if (fields.size() <= std::max({ datetime_col, open_col, high_col, low_col, close_col })) {
                std::cerr << "⚠️  Line " << line_num << ": Not enough fields, skipping" << std::endl;
                skipped_bars++;
                continue;
            }

            // Create bar
            Bar bar;

            // Extract fields based on column mapping
            bar.datetime = fields[datetime_col];
            bar.open = safe_to_double(fields[open_col]);
            bar.high = safe_to_double(fields[high_col]);
            bar.low = safe_to_double(fields[low_col]);
            bar.close = safe_to_double(fields[close_col]);

            if (volume_col >= 0 && volume_col < static_cast<int>(fields.size())) {
                bar.volume = safe_to_double(fields[volume_col]);
            }
            else {
                bar.volume = 0.0;
            }

            if (oi_col >= 0 && oi_col < static_cast<int>(fields.size())) {
                bar.oi = safe_to_double(fields[oi_col]);
            }
            else {
                bar.oi = 0.0;
            }

            // NEW V6.0: Parse OI metadata
            if (oi_fresh_col >= 0 && oi_fresh_col < static_cast<int>(fields.size())) {
                std::string oi_fresh_str = fields[oi_fresh_col];
                bar.oi_fresh = (oi_fresh_str == "1" || oi_fresh_str == "true" || oi_fresh_str == "True");
            }
            else {
                bar.oi_fresh = false;  // Default if column not present
            }

            if (oi_age_seconds_col >= 0 && oi_age_seconds_col < static_cast<int>(fields.size())) {
                bar.oi_age_seconds = static_cast<int>(safe_to_double(fields[oi_age_seconds_col]));
            }
            else {
                bar.oi_age_seconds = 0;  // Default if column not present
            }

            // Validate OHLC
            if (bar.high < bar.low) {
                std::cerr << "⚠️  Line " << line_num << ": Invalid OHLC (high < low), skipping" << std::endl;
                skipped_bars++;
                continue;
            }

            if (bar.close < bar.low || bar.close > bar.high) {
                std::cerr << "⚠️  Line " << line_num << ": Invalid OHLC (close out of range), skipping" << std::endl;
                skipped_bars++;
                continue;
            }

            if (bar.open <= 0 || bar.high <= 0 || bar.low <= 0 || bar.close <= 0) {
                std::cerr << "⚠️  Line " << line_num << ": Invalid prices (<=0), skipping" << std::endl;
                skipped_bars++;
                continue;
            }

            bars.push_back(bar);
            valid_bars++;

            // Progress for large files
            if (line_num % 1000 == 0) {
                std::cout << "[INFO] Processed " << line_num << " lines, "
                    << valid_bars << " valid bars..." << std::endl;
            }

        }
        catch (const std::exception& e) {
            std::cerr << "❌ Line " << line_num << ": " << e.what() << std::endl;
            skipped_bars++;

            if (skipped_bars > 50) {
                std::cerr << "\n❌ Too many errors, stopping" << std::endl;
                break;
            }
        }
    }

    file.close();

    if (bars.empty()) {
        std::cerr << "\n❌ No valid data loaded!" << std::endl;
        throw std::runtime_error("No data");
    }

    std::cout << "\n[SUCCESS] Loaded " << bars.size() << " bars" << std::endl;
    if (skipped_bars > 0) {
        std::cout << "[WARNING] Skipped " << skipped_bars << " invalid bars" << std::endl;
    }
    std::cout << "[SUCCESS] Date range: " << bars.front().datetime
        << " → " << bars.back().datetime << std::endl;
    std::cout << "[SUCCESS] Price range: " << bars.front().close
        << " to " << bars.back().close << std::endl;
    std::cout << std::endl;

    return bars;
}

// ============================================================
// TECHNICAL INDICATORS - Simple, Focused Functions
// ============================================================

double calculate_atr(const std::vector<SDTrading::Core::Bar>& bars, int period, int end_idx) {
    if (end_idx < period || end_idx >= static_cast<int>(bars.size())) {
        return 0.0;
    }
    
    double sum = 0.0;
    for (int i = end_idx - period + 1; i <= end_idx; ++i) {
        double tr = bars[i].high - bars[i].low;
        if (i > 0) {
            tr = std::max(tr, std::abs(bars[i].high - bars[i-1].close));
            tr = std::max(tr, std::abs(bars[i].low - bars[i-1].close));
        }
        sum += tr;
    }
    
    return sum / period;
}

double calculate_ema(const std::vector<SDTrading::Core::Bar>& bars, int period, int end_idx) {
    if (end_idx < period || end_idx >= static_cast<int>(bars.size())) {
        return 0.0;
    }
    
    double multiplier = 2.0 / (period + 1.0);
    
    // Calculate initial SMA
    double sum = 0.0;
    for (int i = end_idx - period + 1; i <= end_idx - period + period; ++i) {
        sum += bars[i].close;
    }
    double ema = sum / period;
    
    // Calculate EMA
    for (int i = end_idx - period + period + 1; i <= end_idx; ++i) {
        ema = (bars[i].close - ema) * multiplier + ema;
    }
    
    return ema;
}

// ============================================================
// V5.0: MODULAR ZONE DETECTION - Broken into Smaller Functions
// Each function has a single, clear purpose
// ============================================================

// V5.0 FIX: Separated consolidation detection into its own function
// Was part of 375-line monolith, now standalone 30-line function
bool is_consolidation(const std::vector<Bar>& bars, int start_idx, int end_idx, 
                      double max_height_atr, double atr) {
    if (start_idx < 0 || end_idx >= static_cast<int>(bars.size()) || start_idx >= end_idx) {
        return false;
    }
    
    double highest = bars[start_idx].high;
    double lowest = bars[start_idx].low;
    
    for (int i = start_idx + 1; i <= end_idx; ++i) {
        highest = std::max(highest, bars[i].high);
        lowest = std::min(lowest, bars[i].low);
    }
    
    double height = highest - lowest;
    double max_allowed_height = atr * max_height_atr;
    
    return height <= max_allowed_height;
}

// V5.0 FIX: Separated impulse detection into its own function
// Was part of 375-line monolith, now standalone 40-line function
bool is_impulse(const std::vector<Bar>& bars, int base_end, int departure_end,
                double min_atr, double atr) {
    if (base_end < 0 || departure_end >= static_cast<int>(bars.size()) || 
        base_end >= departure_end) {
        return false;
    }
    
    // Calculate move size
    double move_high = bars[base_end].high;
    double move_low = bars[base_end].low;
    
    for (int i = base_end + 1; i <= departure_end; ++i) {
        move_high = std::max(move_high, bars[i].high);
        move_low = std::min(move_low, bars[i].low);
    }
    
    double move_size = move_high - move_low;
    double min_required_move = atr * min_atr;
    
    return move_size >= min_required_move;
}

// V5.0 FIX: Separated elite metrics calculation
// Was embedded in 375-line monolith, now standalone 60-line function
void calculate_elite_metrics(Zone& zone, const std::vector<Bar>& bars, 
                             int current_idx, const BacktestConfig& config) {
    // Calculate departure imbalance
    int formation_idx = zone.formation_bar;
    if (formation_idx < 0 || formation_idx >= static_cast<int>(bars.size())) {
        zone.is_elite = false;
        return;
    }
    
    // Find departure move
    double departure_distance = 0.0;
    if (zone.type == ZoneType::DEMAND) {
        // Find highest high after formation
        double highest = zone.base_high;
        for (int i = formation_idx + 1; i <= current_idx && i < formation_idx + 50; ++i) {
            highest = std::max(highest, bars[i].high);
        }
        departure_distance = highest - zone.base_high;
    } else {
        // Find lowest low after formation
        double lowest = zone.base_low;
        for (int i = formation_idx + 1; i <= current_idx && i < formation_idx + 50; ++i) {
            lowest = std::min(lowest, bars[i].low);
        }
        departure_distance = zone.base_low - lowest;
    }
    
    // Calculate approach distance
    double approach_distance = zone.base_high - zone.base_low;
    
    // Calculate imbalance ratio
    if (approach_distance > 0) {
        zone.departure_imbalance = departure_distance / approach_distance;
    }
    
    // Determine if elite
    double atr = calculate_atr(bars, config.atr_period, formation_idx);
    zone.is_elite = (zone.departure_imbalance >= config.min_departure_imbalance) &&
                    (zone.bars_to_retest >= config.min_bars_to_retest);
}

// V5.0 FIX: Separated swing analysis
// Was embedded in 375-line monolith, now standalone 70-line function
void calculate_swing_analysis(Zone& zone, const std::vector<Bar>& bars, 
                              int current_idx, const BacktestConfig& config) {
    int formation_idx = zone.formation_bar;
    int lookback = config.swing_detection_bars;
    
    if (formation_idx < lookback || formation_idx >= static_cast<int>(bars.size())) {
        zone.swing_analysis.swing_score = 0.0;
        return;
    }
    
    // Find swing extremes in lookback window
    int start_idx = formation_idx - lookback;
    double highest = bars[start_idx].high;
    double lowest = bars[start_idx].low;
    int highest_idx = start_idx;
    int lowest_idx = start_idx;
    
    for (int i = start_idx + 1; i <= formation_idx; ++i) {
        if (bars[i].high > highest) {
            highest = bars[i].high;
            highest_idx = i;
        }
        if (bars[i].low < lowest) {
            lowest = bars[i].low;
            lowest_idx = i;
        }
    }
    
    // Determine if zone is at swing extreme
    if (zone.type == ZoneType::SUPPLY) {
        zone.swing_analysis.is_at_swing_high = (formation_idx == highest_idx);
        zone.swing_analysis.bars_to_higher_high = 0;
        
        // Calculate swing score based on position
        double range = highest - lowest;
        if (range > 0) {
            double position = (zone.base_high - lowest) / range;
            zone.swing_analysis.swing_percentile = position * 100.0;
            
            // Score based on how close to swing high
            if (zone.swing_analysis.is_at_swing_high) {
                zone.swing_analysis.swing_score = 30.0;  // Maximum score
            } else if (position > 0.9) {
                zone.swing_analysis.swing_score = 20.0;
            } else if (position > 0.75) {
                zone.swing_analysis.swing_score = 10.0;
            } else {
                zone.swing_analysis.swing_score = 0.0;
            }
        }
    } else {  // DEMAND
        zone.swing_analysis.is_at_swing_low = (formation_idx == lowest_idx);
        zone.swing_analysis.bars_to_lower_low = 0;
        
        // Calculate swing score
        double range = highest - lowest;
        if (range > 0) {
            double position = (zone.base_low - lowest) / range;
            zone.swing_analysis.swing_percentile = position * 100.0;
            
            // Score based on how close to swing low
            if (zone.swing_analysis.is_at_swing_low) {
                zone.swing_analysis.swing_score = 30.0;  // Maximum score
            } else if (position < 0.1) {
                zone.swing_analysis.swing_score = 20.0;
            } else if (position < 0.25) {
                zone.swing_analysis.swing_score = 10.0;
            } else {
                zone.swing_analysis.swing_score = 0.0;
            }
        }
    }
}

// V5.0 FIX: Main detection function is now much smaller
// Orchestrates smaller functions instead of doing everything
// Reduced from 375 lines to ~150 lines
void detect_zones_in_window(const std::vector<Bar>& bars, int current_idx,
                            const BacktestConfig& config,
                            std::vector<Zone>& result) {
    // Clear and pre-allocate
    result.clear();
    result.reserve(50);
    
    // Validation
    int min_bars = config.consolidation_max_bars + 15;
    if (current_idx < min_bars || current_idx >= static_cast<int>(bars.size())) {
        return;
    }
    
    // Calculate ATR once
    double atr = calculate_atr(bars, config.atr_period, current_idx);
    if (atr <= 0) return;
    
    // Determine scan range
    int lookback = std::min(config.lookback_for_zones, current_idx - min_bars);
    int scan_start = current_idx - lookback;
    int scan_end = current_idx - config.consolidation_max_bars - 5;
    
    if (scan_start >= scan_end) return;
    
    // Scan for consolidations
    for (int i = scan_start; i < scan_end; ++i) {
        for (int base_len = config.consolidation_min_bars; 
             base_len <= config.consolidation_max_bars; ++base_len) {
            
            int base_end = i + base_len - 1;
            if (base_end >= scan_end) break;
            
            // V5.0: Use separated function for consolidation check
            if (!is_consolidation(bars, i, base_end, config.base_height_max_atr, atr)) {
                continue;
            }
            
            // Find impulse move
            int departure_end = base_end + 1;
            while (departure_end < current_idx && departure_end < base_end + 20) {
                // V5.0: Use separated function for impulse check
                if (is_impulse(bars, base_end, departure_end, config.min_impulse_atr, atr)) {
                    break;
                }
                departure_end++;
            }
            
            if (departure_end >= current_idx) continue;
            
            // Create zone using compiler-generated constructor
            Zone zone;  // V5.0: Simple default construction
            zone.formation_bar = i;
            zone.formation_datetime = bars[i].datetime;
            
            // Determine zone type based on impulse direction
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
            
            // Calculate basic strength
            double height = zone.base_high - zone.base_low;
            zone.strength = 50.0 + (height / atr) * 10.0;
            zone.strength = std::min(zone.strength, 100.0);
            
            // V5.0: Use separated functions for complex calculations
            calculate_elite_metrics(zone, bars, current_idx, config);
            calculate_swing_analysis(zone, bars, current_idx, config);
            
            // Add zone if valid
            if (zone.is_valid() && zone.strength >= config.min_zone_strength) {
                result.push_back(zone);  // V5.0: Uses compiler-generated copy
            }
        }
    }
    
    // Sort by strength
    std::sort(result.begin(), result.end(), 
        [](const Zone& a, const Zone& b) { return a.strength > b.strength; });
    
    // Limit results
    if (result.size() > 50) {
        result.resize(50);
    }
}

// ============================================================
// MARKET REGIME DETECTION
// ============================================================

MarketRegime determine_htf_regime(const std::vector<Bar>& bars, int current_idx, 
                                  const BacktestConfig& config) {
    if (!config.use_htf_regime_filter) {
        return MarketRegime::RANGING;
    }
    
    if (current_idx < config.htf_lookback_bars) {
        return MarketRegime::RANGING;
    }
    
    int lookback_start = current_idx - config.htf_lookback_bars;
    double ema_fast = calculate_ema(bars, config.ema_fast_period, current_idx);
    double ema_slow = calculate_ema(bars, config.ema_slow_period, current_idx);
    
    double diff_pct = ((ema_fast - ema_slow) / ema_slow) * 100.0;
    
    if (diff_pct > config.htf_trending_threshold) {
        return MarketRegime::BULL;
    } else if (diff_pct < -config.htf_trending_threshold) {
        return MarketRegime::BEAR;
    } else {
        return MarketRegime::RANGING;
    }
}

// ------------------------------------------------------------
// Helper: RSI calculation (simple Wilder-style)
// ------------------------------------------------------------
static double calculate_rsi(const std::vector<Bar>& bars, int period, int end_idx) {
    if (end_idx <= period) return 50.0;
    double gain = 0.0, loss = 0.0;
    for (int i = end_idx - period + 1; i <= end_idx; ++i) {
        double change = bars[i].close - bars[i - 1].close;
        if (change > 0) gain += change; else loss -= change;
    }
    if (loss == 0) return 70.0;
    double rs = (gain / period) / (loss / period);
    return 100.0 - (100.0 / (1.0 + rs));
}

// ------------------------------------------------------------
// Helper: Simple RSI divergence detection (lookback 10 bars)
// ------------------------------------------------------------
static std::pair<bool, bool> detect_rsi_divergence(const std::vector<Bar>& bars, int end_idx) {
    if (end_idx < 12) return {false, false};
    auto rsi = [&](int idx){ return calculate_rsi(bars, 14, idx); };

    double price_low1 = bars[end_idx - 6].low;
    double price_low2 = bars[end_idx].low;
    double rsi_low1 = rsi(end_idx - 6);
    double rsi_low2 = rsi(end_idx);

    double price_high1 = bars[end_idx - 6].high;
    double price_high2 = bars[end_idx].high;
    double rsi_high1 = rsi(end_idx - 6);
    double rsi_high2 = rsi(end_idx);

    bool bull_div = (price_low2 < price_low1) && (rsi_low2 > rsi_low1 + 1.0);
    bool bear_div = (price_high2 > price_high1) && (rsi_high2 < rsi_high1 - 1.0);
    return {bull_div, bear_div};
}

// ------------------------------------------------------------
// Helper: Fib position within recent swing (0-1)
// ------------------------------------------------------------
static double compute_fib_position(const std::vector<Bar>& bars, int end_idx, int lookback = 50) {
    int start = std::max(0, end_idx - lookback);
    double swing_high = bars[start].high, swing_low = bars[start].low;
    for (int i = start; i <= end_idx; ++i) {
        swing_high = std::max(swing_high, bars[i].high);
        swing_low = std::min(swing_low, bars[i].low);
    }
    double range = swing_high - swing_low;
    if (range < 1e-8) return 0.5;
    double pos = (bars[end_idx].close - swing_low) / range;
    return std::clamp(pos, 0.0, 1.0);
}

// ------------------------------------------------------------
// Helper: Elliott-style wave hint (very lightweight heuristic)
// ------------------------------------------------------------
static int detect_wave_hint(const std::vector<Bar>& bars, int end_idx) {
    if (end_idx < 8) return 0;
    bool higher_highs = bars[end_idx].high > bars[end_idx - 2].high &&
                        bars[end_idx - 2].high > bars[end_idx - 4].high;
    bool higher_lows = bars[end_idx].low > bars[end_idx - 2].low &&
                       bars[end_idx - 2].low > bars[end_idx - 4].low;
    bool lower_highs = bars[end_idx].high < bars[end_idx - 2].high &&
                       bars[end_idx - 2].high < bars[end_idx - 4].high;
    bool lower_lows = bars[end_idx].low < bars[end_idx - 2].low &&
                      bars[end_idx - 2].low < bars[end_idx - 4].low;
    if (higher_highs && higher_lows) return 1;   // Bullish impulse hint
    if (lower_highs && lower_lows) return -1;    // Bearish impulse hint
    return 0;
}

// ------------------------------------------------------------
// Build market context for adaptive scoring
// ------------------------------------------------------------
MarketContext build_market_context(const std::vector<Bar>& bars, int current_idx,
                                   const BacktestConfig& config, MarketRegime regime) {
    MarketContext ctx{};
    ctx.regime = regime;
    if (current_idx < 60) {
        ctx.trend_strength = 0.0;
        ctx.volatility_ratio = 1.0;
        ctx.fib_position = 0.5;
        ctx.rsi_bull_div = ctx.rsi_bear_div = false;
        ctx.wave_hint = 0;
        ctx.no_trade_zone = false;
        return ctx;
    }

    double ema_fast = calculate_ema(bars, config.ema_fast_period, current_idx);
    double ema_slow = calculate_ema(bars, config.ema_slow_period, current_idx);
    double diff_pct = (ema_slow == 0) ? 0.0 : ((ema_fast - ema_slow) / std::abs(ema_slow)) * 100.0;
    ctx.trend_strength = std::clamp(std::abs(diff_pct) / std::max(1.0, config.htf_trending_threshold * 1.5), 0.0, 1.0);

    double atr_fast = calculate_atr(bars, 14, current_idx);
    double atr_slow = calculate_atr(bars, 50, current_idx);
    ctx.volatility_ratio = (atr_slow < 1e-6) ? 1.0 : atr_fast / atr_slow;

    auto [bull_div, bear_div] = detect_rsi_divergence(bars, current_idx);
    ctx.rsi_bull_div = bull_div;
    ctx.rsi_bear_div = bear_div;

    ctx.fib_position = compute_fib_position(bars, current_idx, 50);
    ctx.wave_hint = detect_wave_hint(bars, current_idx);

    bool conflicting = bull_div && bear_div;
    bool extreme_vol = ctx.volatility_ratio > 1.8;
    bool flat_trend = ctx.trend_strength < 0.2 && regime == MarketRegime::RANGING;
    ctx.no_trade_zone = conflicting || (extreme_vol && flat_trend);

    return ctx;
}

// ============================================================
// ZONE SCORING - Simplified
// ============================================================

void ZoneScore::calculate_composite() {
    total_score = base_strength_score + elite_bonus_score + swing_position_score +
                 regime_alignment_score + state_freshness_score + 
                 rejection_confirmation_score;
}

ZoneScore calculate_zone_score(const Zone& zone, const MarketContext& context,
                               const std::vector<Bar>& bars, int current_idx,
                               const BacktestConfig& config) {
    ZoneScore score;
    
    // Base strength score
    score.base_strength_score = (zone.strength / 100.0) * 40.0;
    
    // Elite bonus
    score.elite_bonus_score = zone.is_elite ? 25.0 : 0.0;
    
    // Swing position
    score.swing_position_score = zone.swing_analysis.swing_score * 0.67;  // Scale to 20 max
    
    // Regime alignment with trend strength influence
    bool aligned = (zone.type == ZoneType::DEMAND && context.regime == MarketRegime::BULL) ||
                   (zone.type == ZoneType::SUPPLY && context.regime == MarketRegime::BEAR);
    double base_regime = aligned ? 20.0 : (context.regime == MarketRegime::RANGING ? 10.0 : 0.0);
    score.regime_alignment_score = base_regime * (0.6 + 0.4 * context.trend_strength);
    
    // State freshness
    score.state_freshness_score = (zone.state == ZoneState::FRESH) ? 10.0 : 
                                  (zone.state == ZoneState::TESTED) ? 5.0 : 0.0;
    
    // Rejection confirmation
    score.rejection_confirmation_score = (zone.touch_count > 0) ? 5.0 : 0.0;
    
    // Base composite
    score.calculate_composite();

    // Adaptive multiplier based on dynamic market context
    double multiplier = 1.0;

    // Trend-aware bias
    if (context.regime == MarketRegime::BULL) {
        multiplier += (zone.type == ZoneType::DEMAND ? 0.20 : -0.20) * context.trend_strength;
    } else if (context.regime == MarketRegime::BEAR) {
        multiplier += (zone.type == ZoneType::SUPPLY ? 0.20 : -0.20) * context.trend_strength;
    } else { // RANGING
        multiplier *= 0.95;  // Slightly conservative in chop
    }

    // Volatility adjustment
    if (context.volatility_ratio > 1.4) {
        multiplier *= 0.85;  // high vol: be cautious
    } else if (context.volatility_ratio < 0.9) {
        multiplier *= 1.05;  // low vol: slightly more confident
    }

    // RSI divergences
    if (zone.type == ZoneType::DEMAND && context.rsi_bull_div) {
        multiplier += 0.10;
    }
    if (zone.type == ZoneType::SUPPLY && context.rsi_bear_div) {
        multiplier += 0.10;
    }

    // Fibonacci alignment
    if (zone.type == ZoneType::DEMAND && context.fib_position >= 0.50 && context.fib_position <= 0.80) {
        multiplier += 0.05;
    }
    if (zone.type == ZoneType::SUPPLY && context.fib_position >= 0.20 && context.fib_position <= 0.50) {
        multiplier += 0.05;
    }

    // Elliott wave hint
    if (context.wave_hint == 1 && zone.type == ZoneType::DEMAND) {
        multiplier += 0.05;
    } else if (context.wave_hint == -1 && zone.type == ZoneType::SUPPLY) {
        multiplier += 0.05;
    }

    // No-trade filter
    if (context.no_trade_zone) {
        multiplier *= 0.50;
    }

    // Clamp multiplier and apply
    multiplier = std::clamp(multiplier, 0.50, 1.80);
    score.total_score = std::min(100.0, score.total_score * multiplier);
    score.entry_aggressiveness = score.total_score / 100.0;

    return score;
}

// ============================================================
// ENTRY DECISION
// ============================================================

EntryDecision evaluate_entry(const Zone& zone, const std::vector<Bar>& bars,
                             int current_idx, MarketRegime regime,
                             const BacktestConfig& config) {
    EntryDecision decision;
    decision.should_enter = false;
    
    if (!zone.is_valid()) {
        decision.rejection_reason = "Invalid zone";
        return decision;
    }
    
    // Build market context and calculate zone score
    MarketContext ctx = build_market_context(bars, current_idx, config, regime);
    decision.score = calculate_zone_score(zone, ctx, bars, current_idx, config);
    
    // Check minimum score threshold
    if (decision.score.total_score < config.scoring_config.entry_minimum_score) {
        decision.rejection_reason = "Score too low";
        return decision;
    }
    
    // Set entry parameters
    const Bar& current_bar = bars[current_idx];
    
    if (zone.type == ZoneType::DEMAND) {
        decision.entry_price = zone.proximal_line;
        decision.stop_loss = zone.distal_line - (zone.distal_line * 0.001);  // 0.1% buffer
        double risk = decision.entry_price - decision.stop_loss;
        decision.take_profit = decision.entry_price + (risk * config.scoring_config.rr_base_ratio);
    } else {
        decision.entry_price = zone.proximal_line;
        decision.stop_loss = zone.distal_line + (zone.distal_line * 0.001);
        double risk = decision.stop_loss - decision.entry_price;
        decision.take_profit = decision.entry_price - (risk * config.scoring_config.rr_base_ratio);
    }
    
    decision.should_enter = true;
    return decision;
}

// ============================================================
// POSITION SIZING
// ============================================================

int calculate_position_size(double entry_price, double stop_loss,
                           const BacktestConfig& config, double current_capital) {
    double price_risk = std::abs(entry_price - stop_loss);
    if (price_risk <= 0) return 0;

    // Risk-based sizing
    double risk_amount = current_capital * (config.risk_per_trade_pct / 100.0);
    double value_per_point = risk_amount / price_risk;
    int lots = static_cast<int>(value_per_point / config.lot_size);
    lots = std::max(1, lots);  // At least 1 lot

    // ⭐ FIX: Enforce max_loss_per_trade cap
    // Max affordable lots = max_loss / (lot_size × stop_distance)
    if (config.max_loss_per_trade > 0) {
        int max_lots = static_cast<int>(
            config.max_loss_per_trade / (config.lot_size * price_risk));
        if (max_lots < 1) {
            // Even 1 lot exceeds the loss cap — reject by returning 0
            // Caller must check for 0 and skip entry
            return 0;
        }
        lots = std::min(lots, max_lots);
    }

    return lots;
}

// ============================================================
// TRAILING STOP
// ============================================================

double calculate_trailing_stop(const std::vector<Bar>& bars, int entry_idx,
                               int current_idx, double entry_price,
                               const std::string& direction,
                               const BacktestConfig& config) {
    if (!config.use_trailing_stop) {
        return 0.0;  // No trailing stop
    }
    
    double ema = calculate_ema(bars, config.trail_ema_period, current_idx);
    double atr = calculate_atr(bars, config.atr_period, current_idx);
    
    if (direction == "LONG") {
        return ema - (atr * config.trail_atr_multiplier);
    } else {
        return ema + (atr * config.trail_atr_multiplier);
    }
}
