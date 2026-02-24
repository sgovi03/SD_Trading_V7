# SD_ENGINE_V6.0 - IMPLEMENTATION GUIDE
## Detailed Code Changes & Build Instructions

**Version:** 6.0  
**Target:** Stable & Profitable System (70%+ Win Rate)  
**Date:** February 14, 2026

---

## QUICK START - PRIORITY ORDER

### Phase 1: Data Foundation (DO THIS FIRST)

1. **Python Data Collector Enhancement** (CRITICAL PATH)
2. **Volume Baseline Generation** (CRITICAL PATH)
3. **C++ Bar Structure Enhancement**
4. **CSV Parser Updates**

**Estimated Time:** 1-2 weeks  
**Blocker for:** All subsequent phases

---

## SECTION 1: PYTHON DATA COLLECTOR ENHANCEMENTS

### File: `scripts/fyers_bridge.py` (or your data collection script)

#### Current Code (Assumed):
```python
import fyers_api
import csv
import time
from datetime import datetime

class FyersDataCollector:
    def __init__(self):
        self.client = fyers_api.connect()
        self.symbol = "NSE:NIFTY26FEBFUT"
        self.last_oi_fetch = None
        self.current_oi = 0
        
    def collect_bar(self):
        # Fetch 1-min OHLCV
        bar = self.client.get_latest_bar(self.symbol, resolution='1')
        
        # Write to CSV
        with open('data/live_data.csv', 'a') as f:
            f.write(f"{bar.timestamp},{bar.datetime},{self.symbol},"
                   f"{bar.open},{bar.high},{bar.low},{bar.close},"
                   f"{bar.volume},{bar.oi}\n")
```

#### NEW Code (V6.0):
```python
import fyers_api
import csv
import time
from datetime import datetime, timedelta

class FyersDataCollectorV6:
    def __init__(self):
        self.client = fyers_api.connect()
        self.symbol = "NSE:NIFTY26FEBFUT"
        
        # OI tracking
        self.last_oi_fetch_time = None
        self.current_oi = 0
        self.oi_fetch_interval = 180  # 3 minutes in seconds
        
    def should_fetch_oi(self):
        """Determine if we should fetch fresh OI data"""
        if self.last_oi_fetch_time is None:
            return True
        
        elapsed = (datetime.now() - self.last_oi_fetch_time).total_seconds()
        return elapsed >= self.oi_fetch_interval
    
    def collect_bar(self):
        """Collect 1-min bar with enhanced OI metadata"""
        # Fetch 1-min OHLCV
        bar = self.client.get_latest_bar(self.symbol, resolution='1')
        
        # Fetch OI (every 3 minutes)
        oi_fresh = False
        oi_age_seconds = 0
        
        if self.should_fetch_oi():
            try:
                # Fetch current OI from Fyers
                oi_data = self.client.get_open_interest(self.symbol)
                self.current_oi = oi_data['oi']
                self.last_oi_fetch_time = datetime.now()
                oi_fresh = True
                oi_age_seconds = 0
            except Exception as e:
                print(f"ERROR fetching OI: {e}")
                oi_fresh = False
                oi_age_seconds = self._calculate_oi_age()
        else:
            # Use cached OI
            oi_fresh = False
            oi_age_seconds = self._calculate_oi_age()
        
        # Write enhanced CSV row
        with open('data/live_data.csv', 'a') as f:
            csv_row = (f"{bar.timestamp},{bar.datetime},{self.symbol},"
                      f"{bar.open},{bar.high},{bar.low},{bar.close},"
                      f"{bar.volume},{self.current_oi},"
                      f"{1 if oi_fresh else 0},{oi_age_seconds}\n")
            f.write(csv_row)
        
        return bar
    
    def _calculate_oi_age(self):
        """Calculate how old the current OI value is"""
        if self.last_oi_fetch_time is None:
            return 9999  # Unknown/very stale
        
        elapsed = (datetime.now() - self.last_oi_fetch_time).total_seconds()
        return int(elapsed)
    
    def validate_bar(self, bar):
        """Validate data quality"""
        if bar.volume <= 0:
            print(f"WARNING: Zero volume at {bar.datetime}")
            return False
        
        if bar.high < bar.low:
            print(f"ERROR: High < Low at {bar.datetime}")
            return False
        
        if self.current_oi <= 0:
            print(f"WARNING: Zero/negative OI at {bar.datetime}")
        
        if self._calculate_oi_age() > 600:  # >10 minutes
            print(f"WARNING: Stale OI data ({self._calculate_oi_age()}s old)")
        
        return True
```

#### CSV Header Update:
```python
# Create CSV with new header if doesn't exist
def initialize_csv():
    header = "Timestamp,DateTime,Symbol,Open,High,Low,Close,Volume,OpenInterest,OI_Fresh,OI_Age_Seconds\n"
    
    if not os.path.exists('data/live_data.csv'):
        with open('data/live_data.csv', 'w') as f:
            f.write(header)
```

---

## SECTION 2: VOLUME BASELINE GENERATION

### New File: `scripts/build_volume_baseline.py`

```python
"""
Volume Baseline Generator for SD Engine V6.0

Generates time-of-day volume averages for normalization.
Run this script daily at 08:00 AM before market open.

Usage:
    python build_volume_baseline.py --lookback 20 --output data/baselines/volume_baseline.json
"""

import argparse
import json
from datetime import datetime, timedelta
from collections import defaultdict
import pandas as pd
import fyers_api

def build_volume_baseline(lookback_days=20, output_file='data/baselines/volume_baseline.json'):
    """
    Build time-of-day volume baseline
    
    Args:
        lookback_days: Number of days to average over (default: 20)
        output_file: Where to save the JSON baseline
    """
    print(f"Building volume baseline from last {lookback_days} days...")
    
    # Connect to Fyers
    client = fyers_api.connect()
    symbol = "NSE:NIFTY26FEBFUT"
    
    # Calculate date range
    end_date = datetime.now()
    start_date = end_date - timedelta(days=lookback_days)
    
    # Fetch historical data (1-min resolution)
    print(f"Fetching data from {start_date.date()} to {end_date.date()}...")
    historical_data = client.get_historical_data(
        symbol=symbol,
        resolution='1',
        start_date=start_date,
        end_date=end_date
    )
    
    # Convert to DataFrame
    df = pd.DataFrame(historical_data)
    df['datetime'] = pd.to_datetime(df['timestamp'], unit='s')
    df['time_slot'] = df['datetime'].dt.strftime('%H:%M')
    
    # Filter market hours only (09:15 - 15:30)
    df = df[df['time_slot'] >= '09:15']
    df = df[df['time_slot'] <= '15:30']
    
    # Aggregate to 5-min bars
    df.set_index('datetime', inplace=True)
    df_5min = df.resample('5T', origin='start').agg({
        'volume': 'sum',
        'time_slot': 'first'
    })
    df_5min.reset_index(inplace=True)
    
    # Calculate average volume per time slot
    baseline = {}
    time_slots = df_5min.groupby('time_slot')
    
    for time_slot, group in time_slots:
        avg_volume = group['volume'].mean()
        baseline[time_slot] = round(avg_volume, 2)
        print(f"  {time_slot}: {avg_volume:,.0f} avg volume")
    
    # Save to JSON
    with open(output_file, 'w') as f:
        json.dump(baseline, f, indent=2)
    
    print(f"\n✅ Volume baseline saved to {output_file}")
    print(f"   {len(baseline)} time slots with averages")
    
    return baseline

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Build volume baseline for SD Engine V6.0')
    parser.add_argument('--lookback', type=int, default=20, help='Days to look back (default: 20)')
    parser.add_argument('--output', type=str, default='data/baselines/volume_baseline.json',
                       help='Output file path')
    
    args = parser.parse_args()
    
    # Create output directory if needed
    import os
    os.makedirs(os.path.dirname(args.output), exist_ok=True)
    
    # Build baseline
    baseline = build_volume_baseline(args.lookback, args.output)
```

#### Example Output (`data/baselines/volume_baseline.json`):
```json
{
  "09:15": 125000.50,
  "09:20": 110000.25,
  "09:25": 95000.75,
  "09:30": 88000.00,
  ...
  "15:25": 140000.00,
  "15:30": 180000.50
}
```

---

## SECTION 3: C++ DATA STRUCTURE ENHANCEMENTS

### File: `include/common_types.h`

#### Addition 1: Enhanced Bar Structure

**Find this block:**
```cpp
struct Bar {
    std::string datetime;
    double open;
    double high;
    double low;
    double close;
    double volume;
    double oi;  // Open Interest
    
    Bar() 
        : datetime(""), open(0), high(0), low(0), 
          close(0), volume(0), oi(0) {}
};
```

**Replace with:**
```cpp
struct Bar {
    std::string datetime;
    double open;
    double high;
    double low;
    double close;
    double volume;
    double oi;  // Open Interest
    
    // NEW V6.0: OI metadata
    bool oi_fresh;              // Is this a fresh 3-min OI update?
    int oi_age_seconds;         // How old is the OI value?
    
    // NEW V6.0: Volume metadata (calculated in-memory)
    double norm_volume_ratio;   // volume / time-of-day baseline
    
    Bar() 
        : datetime(""), open(0), high(0), low(0), 
          close(0), volume(0), oi(0), oi_fresh(false),
          oi_age_seconds(0), norm_volume_ratio(0) {}
};
```

#### Addition 2: VolumeProfile Structure

**Add after Bar struct:**
```cpp
// ============================================================
// VOLUME PROFILE (NEW V6.0)
// ============================================================

struct VolumeProfile {
    double formation_volume;       // Volume when zone was created
    double avg_volume_baseline;    // Time-of-day normalized average
    double volume_ratio;           // formation_volume / avg_volume_baseline
    double peak_volume;            // Highest volume bar in zone formation
    int high_volume_bar_count;     // Number of bars with >1.5x avg volume
    double volume_score;           // 0-40 points contribution to zone score
    
    VolumeProfile() 
        : formation_volume(0), avg_volume_baseline(0),
          volume_ratio(0), peak_volume(0),
          high_volume_bar_count(0), volume_score(0) {}
    
    std::string to_string() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << "VolProfile[ratio=" << volume_ratio 
            << ", peak=" << peak_volume
            << ", high_vol_bars=" << high_volume_bar_count
            << ", score=" << volume_score << "]";
        return oss.str();
    }
};
```

#### Addition 3: OIProfile Structure

**Add after VolumeProfile:**
```cpp
// ============================================================
// OI PROFILE (NEW V6.0)
// ============================================================

enum class MarketPhase {
    LONG_BUILDUP,      // Price ↑ + OI ↑ (bullish)
    SHORT_COVERING,    // Price ↑ + OI ↓ (temporary bullish)
    SHORT_BUILDUP,     // Price ↓ + OI ↑ (bearish)
    LONG_UNWINDING,    // Price ↓ + OI ↓ (temporary bearish)
    NEUTRAL            // No clear phase
};

inline std::string market_phase_to_string(MarketPhase phase) {
    switch (phase) {
        case MarketPhase::LONG_BUILDUP: return "LONG_BUILDUP";
        case MarketPhase::SHORT_COVERING: return "SHORT_COVERING";
        case MarketPhase::SHORT_BUILDUP: return "SHORT_BUILDUP";
        case MarketPhase::LONG_UNWINDING: return "LONG_UNWINDING";
        case MarketPhase::NEUTRAL: return "NEUTRAL";
        default: return "UNKNOWN";
    }
}

struct OIProfile {
    long formation_oi;                 // OI when zone was created
    long oi_change_during_formation;   // Delta OI (end - start)
    double oi_change_percent;          // Percentage change in OI
    double price_oi_correlation;       // Correlation coefficient (-1 to +1)
    bool oi_data_quality;              // Were OI readings fresh during formation?
    MarketPhase market_phase;          // Detected market phase
    double oi_score;                   // 0-30 points contribution to zone score
    
    OIProfile()
        : formation_oi(0), oi_change_during_formation(0),
          oi_change_percent(0), price_oi_correlation(0),
          oi_data_quality(false), market_phase(MarketPhase::NEUTRAL),
          oi_score(0) {}
    
    std::string to_string() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << "OIProfile[oi=" << formation_oi
            << ", change=" << oi_change_percent << "%"
            << ", corr=" << price_oi_correlation
            << ", phase=" << market_phase_to_string(market_phase)
            << ", score=" << oi_score << "]";
        return oss.str();
    }
};
```

#### Addition 4: Enhanced Zone Structure

**Find the Zone struct, add these new fields:**
```cpp
struct Zone {
    // ... existing fields ...
    
    // NEW V6.0: Volume & OI Analytics
    VolumeProfile volume_profile;
    OIProfile oi_profile;
    double institutional_index;    // 0-100 composite institutional participation score
    
    Zone() 
        : /* existing initializers */,
          institutional_index(0) {}  // Add to constructor
};
```

#### Addition 5: VolumeOIConfig Structure

**Add at end of file before closing namespace:**
```cpp
// ============================================================
// VOLUME & OI CONFIGURATION (NEW V6.0)
// ============================================================

struct VolumeOIConfig {
    // Volume Configuration
    bool enable_volume_entry_filters;
    bool enable_volume_exit_signals;
    double min_entry_volume_ratio;           // Default: 0.8
    double institutional_volume_threshold;   // Default: 2.0
    double extreme_volume_threshold;         // Default: 3.0
    int volume_lookback_period;              // Default: 20
    std::string volume_normalization_method; // "time_of_day" or "session_avg"
    std::string volume_baseline_file;        // Path to baseline JSON
    
    // OI Configuration
    bool enable_oi_entry_filters;
    bool enable_oi_exit_signals;
    bool enable_market_phase_detection;
    double min_oi_change_threshold;          // Default: 0.01 (1%)
    double high_oi_buildup_threshold;        // Default: 0.05 (5%)
    int oi_lookback_period;                  // Default: 10 bars
    double price_oi_correlation_window;      // Default: 10 bars
    
    // Scoring Weights
    double base_score_weight;                // Default: 0.50
    double volume_score_weight;              // Default: 0.30
    double oi_score_weight;                  // Default: 0.20
    
    // Institutional Index Bonuses
    double institutional_volume_bonus;       // Default: 30 points
    double oi_alignment_bonus;               // Default: 25 points
    double low_volume_retest_bonus;          // Default: 20 points
    
    // Expiry Day Handling
    bool trade_on_expiry_day;                // Default: true
    bool expiry_day_disable_oi_filters;      // Default: true
    double expiry_day_position_multiplier;   // Default: 0.5
    double expiry_day_min_zone_score;        // Default: 75.0
    
    // Position Sizing
    double low_volume_size_multiplier;       // Default: 0.5
    double high_institutional_size_mult;     // Default: 1.5
    int max_lot_size;                        // Safety cap
    
    // Volume Exit Signals
    double volume_climax_exit_threshold;     // Default: 3.0
    double volume_drying_up_threshold;       // Default: 0.5
    int volume_drying_up_bar_count;          // Default: 3
    bool enable_volume_divergence_exit;      // Default: true
    
    // OI Exit Signals
    double oi_unwinding_threshold;           // Default: -0.01 (-1%)
    double oi_reversal_threshold;            // Default: 0.02 (2%)
    double oi_stagnation_threshold;          // Default: 0.005 (0.5%)
    int oi_stagnation_bar_count;             // Default: 10
    
    VolumeOIConfig()
        : enable_volume_entry_filters(true),
          enable_volume_exit_signals(true),
          min_entry_volume_ratio(0.8),
          institutional_volume_threshold(2.0),
          extreme_volume_threshold(3.0),
          volume_lookback_period(20),
          volume_normalization_method("time_of_day"),
          volume_baseline_file("data/baselines/volume_baseline.json"),
          enable_oi_entry_filters(true),
          enable_oi_exit_signals(true),
          enable_market_phase_detection(true),
          min_oi_change_threshold(0.01),
          high_oi_buildup_threshold(0.05),
          oi_lookback_period(10),
          price_oi_correlation_window(10),
          base_score_weight(0.50),
          volume_score_weight(0.30),
          oi_score_weight(0.20),
          institutional_volume_bonus(30.0),
          oi_alignment_bonus(25.0),
          low_volume_retest_bonus(20.0),
          trade_on_expiry_day(true),
          expiry_day_disable_oi_filters(true),
          expiry_day_position_multiplier(0.5),
          expiry_day_min_zone_score(75.0),
          low_volume_size_multiplier(0.5),
          high_institutional_size_mult(1.5),
          max_lot_size(150),
          volume_climax_exit_threshold(3.0),
          volume_drying_up_threshold(0.5),
          volume_drying_up_bar_count(3),
          enable_volume_divergence_exit(true),
          oi_unwinding_threshold(-0.01),
          oi_reversal_threshold(0.02),
          oi_stagnation_threshold(0.005),
          oi_stagnation_bar_count(10) {}
};
```

---

## SECTION 4: VOLUME BASELINE INFRASTRUCTURE

### New File: `src/utils/volume_baseline.h`

```cpp
#ifndef SD_TRADING_VOLUME_BASELINE_H
#define SD_TRADING_VOLUME_BASELINE_H

#include <string>
#include <map>

namespace SDTrading {
namespace Utils {

/**
 * @class VolumeBaseline
 * @brief Manages time-of-day volume averages for normalization
 * 
 * Loads baseline from JSON file (generated by Python script).
 * Provides fast lookup for volume normalization during trading.
 */
class VolumeBaseline {
public:
    VolumeBaseline();
    ~VolumeBaseline() = default;
    
    /**
     * Load baseline from JSON file
     * @param filepath Path to volume_baseline.json
     * @return true if successful
     */
    bool load_from_file(const std::string& filepath);
    
    /**
     * Get normalized volume ratio
     * @param time_slot Time slot string (e.g., "09:15")
     * @param current_volume Current bar's volume
     * @return Ratio (current / baseline), or 1.0 if slot not found
     */
    double get_normalized_ratio(const std::string& time_slot, double current_volume) const;
    
    /**
     * Get baseline average for a time slot
     * @param time_slot Time slot string
     * @return Average volume, or 0 if not found
     */
    double get_baseline(const std::string& time_slot) const;
    
    /**
     * Check if baseline is loaded
     */
    bool is_loaded() const { return loaded_; }
    
    /**
     * Get number of time slots in baseline
     */
    size_t size() const { return baseline_map_.size(); }
    
private:
    std::map<std::string, double> baseline_map_;  // time_slot -> avg_volume
    bool loaded_;
    
    // Helper to extract time slot from datetime
    std::string extract_time_slot(const std::string& datetime) const;
};

} // namespace Utils
} // namespace SDTrading

#endif // SD_TRADING_VOLUME_BASELINE_H
```

### New File: `src/utils/volume_baseline.cpp`

```cpp
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
        LOG_WARNING("Volume baseline not loaded - returning ratio 1.0");
        return 1.0;
    }
    
    auto it = baseline_map_.find(time_slot);
    if (it == baseline_map_.end()) {
        LOG_WARNING("Time slot not found in baseline: " + time_slot);
        return 1.0;  // Fallback
    }
    
    if (it->second == 0) {
        LOG_WARNING("Zero baseline volume for slot: " + time_slot);
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
```

---

## SECTION 5: CSV PARSER UPDATE

### File: `src/sd_engine_core.cpp` (or wherever CSV parsing happens)

**Find the CSV parsing code, likely in a function like `load_bars_from_csv()`:**

```cpp
// BEFORE (Current):
std::vector<Bar> load_bars_from_csv(const std::string& filepath) {
    std::vector<Bar> bars;
    std::ifstream file(filepath);
    std::string line;
    
    // Skip header
    std::getline(file, line);
    
    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string token;
        std::vector<std::string> tokens;
        
        while (std::getline(ss, token, ',')) {
            tokens.push_back(token);
        }
        
        if (tokens.size() < 9) continue;  // OLD: Expecting 9 columns
        
        Bar bar;
        bar.datetime = tokens[1];  // DateTime
        bar.open = std::stod(tokens[3]);
        bar.high = std::stod(tokens[4]);
        bar.low = std::stod(tokens[5]);
        bar.close = std::stod(tokens[6]);
        bar.volume = std::stod(tokens[7]);
        bar.oi = std::stod(tokens[8]);
        
        bars.push_back(bar);
    }
    
    return bars;
}
```

**AFTER (V6.0 Enhanced):**
```cpp
std::vector<Bar> load_bars_from_csv(const std::string& filepath) {
    std::vector<Bar> bars;
    std::ifstream file(filepath);
    std::string line;
    
    // Skip header
    std::getline(file, line);
    
    LOG_INFO("Loading bars from CSV: " + filepath);
    
    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string token;
        std::vector<std::string> tokens;
        
        while (std::getline(ss, token, ',')) {
            tokens.push_back(token);
        }
        
        // NEW: Expecting 11 columns (added OI_Fresh, OI_Age_Seconds)
        if (tokens.size() < 11) {
            LOG_WARNING("Skipping malformed CSV row (expected 11 columns, got " + 
                       std::to_string(tokens.size()) + ")");
            continue;
        }
        
        Bar bar;
        try {
            // Timestamp,DateTime,Symbol,Open,High,Low,Close,Volume,OI,OI_Fresh,OI_Age_Seconds
            bar.datetime = tokens[1];  // DateTime
            bar.open = std::stod(tokens[3]);
            bar.high = std::stod(tokens[4]);
            bar.low = std::stod(tokens[5]);
            bar.close = std::stod(tokens[6]);
            bar.volume = std::stod(tokens[7]);
            bar.oi = std::stod(tokens[8]);
            
            // NEW V6.0: Parse OI metadata
            bar.oi_fresh = (tokens[9] == "1");
            bar.oi_age_seconds = std::stoi(tokens[10]);
            
            // Validation
            if (bar.volume <= 0) {
                LOG_WARNING("Zero volume at " + bar.datetime + " - skipping bar");
                continue;
            }
            
            if (bar.high < bar.low) {
                LOG_ERROR("Invalid bar: High < Low at " + bar.datetime);
                continue;
            }
            
            bars.push_back(bar);
            
        } catch (const std::exception& e) {
            LOG_ERROR("Exception parsing CSV row: " + std::string(e.what()));
            continue;
        }
    }
    
    LOG_INFO("✅ Loaded " + std::to_string(bars.size()) + " bars from CSV");
    
    return bars;
}
```

---

## SECTION 6: BUILD SYSTEM UPDATES

### File: `CMakeLists.txt` (Root)

**Add volume_baseline to utils:**

Find the utils section and add:
```cmake
# Utils Library
add_library(utils
    src/utils/logger.h
    src/utils/system_initializer.cpp
    src/utils/system_initializer.h
    src/utils/volume_baseline.cpp        # NEW V6.0
    src/utils/volume_baseline.h          # NEW V6.0
)
target_include_directories(utils PUBLIC include)
```

**Link to nlohmann_json (needed for volume_baseline):**
```cmake
target_link_libraries(utils PUBLIC nlohmann_json::nlohmann_json)
```

---

## SECTION 7: CONFIGURATION FILE UPDATES

### File: `conf/phase1_enhanced_v3_1_config_FIXED_more_trades.txt`

**Add at end of file (before closing comments):**

```ini
# ============================================================
# SD ENGINE V6.0 - VOLUME & OI INTEGRATION
# ============================================================

# ====================
# VOLUME CONFIGURATION
# ====================
enable_volume_entry_filters = YES
enable_volume_exit_signals = YES
min_entry_volume_ratio = 0.8
institutional_volume_threshold = 2.0
extreme_volume_threshold = 3.0
volume_lookback_period = 20
volume_normalization_method = time_of_day
volume_baseline_file = data/baselines/volume_baseline.json

# ====================
# OI CONFIGURATION
# ====================
enable_oi_entry_filters = YES
enable_oi_exit_signals = YES
enable_market_phase_detection = YES
min_oi_change_threshold = 0.01
high_oi_buildup_threshold = 0.05
oi_lookback_period = 10
price_oi_correlation_window = 10

# ====================
# VOLUME/OI SCORING WEIGHTS
# ====================
base_score_weight = 0.50
volume_score_weight = 0.30
oi_score_weight = 0.20
institutional_volume_bonus = 30.0
oi_alignment_bonus = 25.0
low_volume_retest_bonus = 20.0

# ====================
# EXPIRY DAY HANDLING
# ====================
trade_on_expiry_day = YES
expiry_day_disable_oi_filters = YES
expiry_day_position_multiplier = 0.5
expiry_day_min_zone_score = 75.0

# ====================
# DYNAMIC POSITION SIZING
# ====================
low_volume_size_multiplier = 0.5
high_institutional_size_multiplier = 1.5
max_lot_size = 150

# ====================
# VOLUME EXIT SIGNALS
# ====================
volume_climax_exit_threshold = 3.0
volume_drying_up_threshold = 0.5
volume_drying_up_bar_count = 3
enable_volume_divergence_exit = YES

# ====================
# OI EXIT SIGNALS
# ====================
oi_unwinding_threshold = -0.01
oi_reversal_threshold = 0.02
oi_stagnation_threshold = 0.005
oi_stagnation_bar_count = 10
```

---

## SECTION 8: QUICK BUILD & TEST

### Step 1: Python Setup

```bash
# 1. Update Python data collector
cd scripts/
# Edit fyers_bridge.py with V6.0 changes

# 2. Generate volume baseline
python build_volume_baseline.py --lookback 20 --output ../data/baselines/volume_baseline.json

# 3. Test data collection
python fyers_bridge.py --test
```

### Step 2: C++ Build

```bash
# 1. Clean build
cd /path/to/SD_Trading_V4
rm -rf build/
mkdir build && cd build

# 2. Configure
cmake ..

# 3. Build
cmake --build . --config Release

# 4. Verify
./sd_backtest --config ../conf/phase1_enhanced_v3_1_config_FIXED_more_trades.txt --data ../data/test_data.csv
```

### Step 3: Validation Tests

```bash
# Test 1: Volume baseline loading
./test_volume_baseline

# Test 2: CSV parsing with new columns
./test_csv_parser --file ../data/test_data_v6.csv

# Test 3: Bar structure validation
./test_bar_enhancements
```

---

## SECTION 9: PHASE 1 COMPLETION CHECKLIST

- [ ] Python fyers_bridge.py enhanced with OI metadata
- [ ] build_volume_baseline.py script created and tested
- [ ] Volume baseline JSON generated (20 days minimum)
- [ ] Bar struct updated with oi_fresh, oi_age_seconds, norm_volume_ratio
- [ ] VolumeProfile struct added to common_types.h
- [ ] OIProfile struct added to common_types.h
- [ ] VolumeOIConfig struct added to common_types.h
- [ ] Enhanced Zone struct with volume_profile, oi_profile, institutional_index
- [ ] VolumeBaseline class created (header + cpp)
- [ ] CSV parser updated to read 11 columns
- [ ] CMakeLists.txt updated with volume_baseline
- [ ] Configuration file updated with V6.0 parameters
- [ ] Builds successfully without errors
- [ ] All existing tests still pass
- [ ] Can load historical CSV with new format
- [ ] Volume baseline loads correctly

**When all checkboxes are complete → Ready for Phase 2 (Volume/OI Scorers)**

---

## NEXT STEPS PREVIEW

**Phase 2 will create:**
1. `src/scoring/volume_scorer.h/cpp` - Calculate volume scores
2. `src/scoring/oi_scorer.h/cpp` - Calculate OI scores
3. Market phase detection functions
4. Institutional index calculation
5. Integration into zone_detector.cpp

**Estimated Timeline:**
- Phase 1 (This guide): 1-2 weeks
- Phase 2 (Scoring): 1-2 weeks
- Phase 3 (Entry/Exit): 1-2 weeks
- Phase 4 (Integration): 1 week
- Phase 5 (Testing): 2-4 weeks
- **Total: 6-11 weeks to stable v6.0**

---

## SUPPORT & TROUBLESHOOTING

### Common Issues

**Issue 1: Volume baseline not loading**
- Check file path in config
- Verify JSON is valid (use jsonlint)
- Check file permissions

**Issue 2: CSV parsing fails**
- Verify CSV has 11 columns
- Check for missing OI_Fresh/OI_Age_Seconds columns
- Validate CSV header matches expected format

**Issue 3: Build fails with nlohmann_json error**
- Ensure nlohmann_json is included in project
- Check CMakeLists.txt links correctly
- Install: `vcpkg install nlohmann-json` (Windows) or equivalent

**Issue 4: Python baseline generation fails**
- Check Fyers API credentials
- Verify historical data availability
- Check date range (must be within API limits)

---

END OF IMPLEMENTATION GUIDE - PHASE 1

**Start Here → Complete Phase 1 → Then request Phase 2 guide**
