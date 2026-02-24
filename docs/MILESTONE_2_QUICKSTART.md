# MILESTONE 2 - QUICK START GUIDE
## SD Trading System V4.0 - Zone Detection & Market Analysis

---

## 🚀 **GETTING STARTED (3 STEPS)**

### Step 1: Extract Package
```cmd
tar -xzf SD_Trading_V4_Milestone2.tar.gz
cd SD_Trading_V4
```

### Step 2: Build
```cmd
build_debug.bat
```

### Step 3: Run Verification
```cmd
cd build\bin\Debug
verify_milestone2.exe
```

**That's it!** You should see zone detection results! ✅

---

## 📁 **WHAT'S NEW IN MILESTONE 2**

### New Components:
1. **ZoneDetector** - Detects institutional supply/demand zones
2. **MarketAnalyzer** - Calculates ATR, EMA, market regime
3. **Verification Program** - Demonstrates zone detection

### New Files:
```
src/zones/zone_detector.h        Zone detection interface
src/zones/zone_detector.cpp      Zone detection logic
src/analysis/market_analyzer.h   Market analysis interface  
src/analysis/market_analyzer.cpp Market analysis logic
src/verify_milestone2.cpp        Verification program
examples/sample_data.csv          Sample price data (50 bars)
```

---

## 🔧 **USING THE ZONE DETECTOR**

### Basic Usage:
```cpp
#include "zones/zone_detector.h"
#include "analysis/market_analyzer.h"

// Load configuration
Config config = ConfigLoader().load_from_file("config.txt");

// Create detector
ZoneDetector detector(config);

// Add bar data
for (const auto& bar : bars) {
    detector.add_bar(bar);
}

// Detect zones
std::vector<Zone> zones = detector.detect_zones();

// Process zones
for (const auto& zone : zones) {
    if (zone.is_elite && zone.strength > 80.0) {
        // High-quality zone found!
        std::cout << "Elite " << (zone.type == SUPPLY ? "SUPPLY" : "DEMAND") 
                  << " zone at " << zone.distal_line << std::endl;
    }
}
```

---

## 📊 **USING THE MARKET ANALYZER**

### Calculate Technical Indicators:
```cpp
#include "analysis/market_analyzer.h"

// Calculate ATR
double atr = MarketAnalyzer::calculate_atr(bars, 14);

// Calculate EMA
double ema_fast = MarketAnalyzer::calculate_ema(bars, 20);
double ema_slow = MarketAnalyzer::calculate_ema(bars, 50);

// Detect market regime
MarketRegime regime = MarketAnalyzer::detect_regime(bars, 50, 5.0);
if (regime == BULL) {
    std::cout << "Market is bullish" << std::endl;
}

// Analyze HTF trend
MarketRegime htf = MarketAnalyzer::analyze_htf_trend(bars, 100, 3.0);
```

---

## 🎯 **ZONE DETECTOR ALGORITHM**

### Detection Process:
```
1. CONSOLIDATION CHECK
   → Is price range tight? (< max_consolidation_range * ATR)
   
2. IMPULSE VALIDATION
   → Strong move BEFORE consolidation?
   → Strong move AFTER consolidation?
   
3. DIRECTIONAL PATTERN
   → SUPPLY: Rally in → Reject out
   → DEMAND: Drop in → Bounce out
   
4. QUALITY ANALYSIS
   → Calculate strength (0-100)
   → Check elite criteria
   → Analyze swing position
```

### Elite Zone Criteria:
```
✓ Departure imbalance >= min_departure_imbalance (default: 2.5 ATR)
✓ Retest speed < max_retest_speed_atr (default: 0.5 pts/bar)
✓ Bars to retest >= min_bars_to_retest (default: 10 bars)
```

---

## 📋 **VERIFICATION PROGRAM OUTPUT**

### What You'll See:
```
=== MARKET ANALYSIS ===
ATR(14):       45.23          ← Volatility measurement
EMA(20):       21756.80       ← Fast moving average
EMA(50):       21680.50       ← Slow moving average
Regime:        BULLISH        ← Current market state
HTF Trend:     BULLISH        ← Higher timeframe bias
Last Close:    22060.00       ← Most recent price
Total Bars:    50             ← Data points loaded

=== ZONE 1 ===
Type:           DEMAND         ← Support zone
Formation:      Bar 25         ← When zone formed
Distal Line:    21470.00       ← Entry/origin line
Proximal Line:  21520.00       ← Top of base
Strength:       85.50/100      ← Quality score
Elite:          YES            ← Premium quality
  Departure:    3.45 ATR       ← Strong move away
  Speed:        12.30 pts/bar  ← Fast retest
  Patience:     15 bars        ← Institutional patience
Swing Score:    25.00/30       ← Position bonus
Swing %ile:     15%            ← Bottom 15% of range
At Swing Low:   YES            ← At swing low point

=== SUMMARY ===
Supply zones:   1              ← Resistance zones
Demand zones:   2              ← Support zones
Elite zones:    2              ← Premium quality zones
```

---

## 🔍 **ZONE TYPES EXPLAINED**

### SUPPLY Zone (Resistance):
```
Price Action:
  └─→ Rally into consolidation (accumulation)
  └─→ Price REJECTS (distribution)
  └─→ Price drops (selling pressure)

Institutional Logic:
  → Smart money sells into demand
  → Retail buyers trapped
  → Zone acts as resistance
```

### DEMAND Zone (Support):
```
Price Action:
  └─→ Drop into consolidation (distribution)
  └─→ Price BOUNCES (accumulation)
  └─→ Price rallies (buying pressure)

Institutional Logic:
  → Smart money buys the dip
  → Retail sellers trapped
  → Zone acts as support
```

---

## ⚙️ **CONFIGURATION PARAMETERS**

### Key Zone Detection Settings:
```ini
# Consolidation Detection
consolidation_min_bars=3           # Minimum base length
consolidation_max_bars=7           # Maximum base length
max_consolidation_range=0.3        # Max range (ATR multiplier)

# Impulse Validation
min_impulse_atr=1.5                # Minimum move size

# Zone Quality
min_zone_strength=60.0             # Minimum strength to keep
lookback_for_zones=500             # How far back to scan

# Elite Qualification
min_departure_imbalance=2.5        # Minimum departure (ATR)
max_retest_speed_atr=0.5           # Maximum speed (pts/bar)
min_bars_to_retest=10              # Minimum patience (bars)
```

---

## 🐛 **TROUBLESHOOTING**

### Build Fails:
```cmd
# Clean and rebuild
clean.bat
build_debug.bat
```

### No Zones Detected:
- Check data file has enough bars (need 50+)
- Verify config parameters aren't too strict
- Lower min_zone_strength to 50.0
- Increase max_consolidation_range to 0.5

### Program Crashes:
- Check data file format (CSV with header)
- Verify all required fields present
- Check for invalid price values

---

## 📚 **API REFERENCE**

### ZoneDetector Class:
```cpp
class ZoneDetector {
public:
    // Constructor
    ZoneDetector(const Config& config);
    
    // Add bar data
    void add_bar(const Bar& bar);
    
    // Detect all zones
    std::vector<Zone> detect_zones();
    
    // Utilities
    const std::vector<Bar>& get_bars() const;
    void clear();
    size_t bar_count() const;
};
```

### MarketAnalyzer Class:
```cpp
class MarketAnalyzer {
public:
    // Technical indicators
    static double calculate_atr(
        const std::vector<Bar>& bars,
        int period,
        int end_index = -1
    );
    
    static double calculate_ema(
        const std::vector<Bar>& bars,
        int period,
        int end_index = -1
    );
    
    // Market state
    static MarketRegime detect_regime(
        const std::vector<Bar>& bars,
        int lookback = 50,
        double threshold = 5.0
    );
    
    static MarketRegime analyze_htf_trend(
        const std::vector<Bar>& bars,
        int htf_period = 100,
        double threshold = 3.0
    );
};
```

---

## ✅ **SUCCESS CRITERIA**

You'll know Milestone 2 is working when:
- ✅ Build completes without errors
- ✅ `verify_milestone2.exe` runs successfully
- ✅ Program displays market analysis
- ✅ Program detects at least 1 zone
- ✅ Zone details shown (type, strength, elite status)
- ✅ Summary statistics displayed

---

## 🎯 **NEXT STEPS**

Once Milestone 2 is verified:
1. Review zone detection results
2. Experiment with different config settings
3. Try your own CSV data files
4. **Proceed to Milestone 3** - Zone Scorer & Entry Logic! 🚀

---

**Questions? Check the full report: `docs/MILESTONE_2_REPORT.md`**
