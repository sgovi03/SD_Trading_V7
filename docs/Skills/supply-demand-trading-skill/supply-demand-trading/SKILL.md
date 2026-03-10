---
name: supply-demand-trading
description: Expert guidance for developing, debugging, and optimizing supply & demand zone-based algorithmic trading systems. Use when users work on S&D zone detection, institutional order flow analysis, volume/OI profiling, multi-timeframe analysis, backtesting trading systems, C++ trading engines, or any supply-demand methodology questions. Covers Sam Seiden institutional S&D concepts, zone state machines, entry/exit logic, risk management, and performance optimization. Always trigger for S&D trading system questions, backtest analysis, zone detection bugs, or algorithmic trading development.
---

# Supply & Demand Trading System Skill

Comprehensive guidance for building institutional-grade supply & demand trading systems based on Sam Seiden's methodology, adapted for algorithmic execution.

## Quick Start Guide

**If user mentions any of these, use this skill:**
- Supply/demand zones, S&D zones, institutional levels
- Zone detection, zone scoring, zone states (FRESH/TESTED/VIOLATED/RECLAIMED)
- Volume profile, OI analysis, institutional order flow
- Backtest analysis, live trading bugs, entry/exit logic
- C++ trading engine, AmiBroker AFL, multi-timeframe analysis
- Sam Seiden methodology, Mark Douglas psychology
- NIFTY futures, NSE trading, Fyers broker integration

## Core Methodology

### Supply & Demand Zone Fundamentals

**Definition:** Price levels where institutional players create imbalances between buying and selling pressure, leaving unfilled orders ("left over" supply/demand).

**Key Principles (Sam Seiden):**
1. Zones form at turning points where price moves away with strength
2. Fresh zones (untested) have highest probability
3. Multiple touches degrade zone quality
4. Institutional participants leave footprints in volume/time patterns
5. Rally-Base-Rally (RBR) = Demand, Drop-Base-Drop (DBD) = Supply

**Zone Anatomy:**
```
DEMAND ZONE (RBR):
│
│    ▲ IMPULSE (departure)
│   ╱
│  ╱
│ ╱
├──────┐ BASE (consolidation - zone formation)
│      │
│      │ ← Proximal line (bottom of demand)
└──────┘ ← Distal line (top of demand)
   ▼ RALLY (prior leg)

Institutional Logic:
- Rally up exhausts buyers
- Price consolidates (institutions accumulate)
- Impulse departure = unfilled buy orders remain
- Return to zone = opportunity for fill
```

### Zone States (Critical for Algorithmic Trading)

```
State Machine:
FRESH → TESTED → VIOLATED
         ↓         ↓
    RECLAIMED ← ─ ┘

FRESH:     Never touched since creation (highest quality)
TESTED:    Price returned but held (weakened but valid)
VIOLATED:  Price broke through zone (invalidated)
RECLAIMED: Violated zone that price reclaimed (rare, very strong)
```

**State Transition Logic:**
```cpp
// Simplified state machine
if (price breaks distal line) {
    if (state == VIOLATED) {
        if (price reclaims proximal line) {
            state = RECLAIMED;  // Sweep & reclaim
        }
    } else {
        state = VIOLATED;  // First violation
    }
} else if (price touches proximal line) {
    if (state == FRESH) {
        state = TESTED;  // First test
    }
    // TESTED remains TESTED on subsequent touches
}
```

### Institutional Order Flow Signatures

**Volume Profile Analysis:**
1. **Departure Volume Ratio:** `zone_departure_volume / avg_baseline_volume`
   - Strong: >2.0× (institutional participation)
   - Moderate: 1.5-2.0×
   - Weak: <1.5× (retail zone)

2. **Initiative vs Response:**
   - Initiative: Price moves away on selling (supply) or buying (demand)
   - Response: Price reverses on opposite pressure
   - Initiative = institutional, Response = retail

3. **Pullback Volume:**
   - Low pullback volume = zone holding (unfilled orders present)
   - High pullback volume = absorption/counter-trend filling

**Open Interest (OI) Analysis:**
- Rising OI + Rising Price = Long buildup (demand)
- Rising OI + Falling Price = Short buildup (supply)
- Falling OI = Unwinding (both longs/shorts exiting)

## System Architecture

### Zone Detection Algorithm

**Step 1: Identify Impulse Moves**
```cpp
// Impulse = price move > impulse_threshold × ATR in N bars
bool is_impulse(bars, start_index, end_index) {
    double price_move = abs(bars[end_index].close - bars[start_index].close);
    double atr = calculate_atr(bars, end_index, 14);
    return (price_move / atr) >= config.min_impulse_atr;  // Typically 1.4-2.0
}
```

**Step 2: Identify Base (Consolidation)**
```cpp
// Base = consolidation zone before impulse
// Criteria:
// - Range < max_base_range × ATR
// - Duration: min_bars to max_bars
// - No strong directional bias

struct Base {
    int start_bar;
    int end_bar;
    double high;  // Distal line
    double low;   // Proximal line
    double avg_volume;
};
```

**Step 3: Classify Zone Type**
```cpp
// Rally-Base-Rally = DEMAND
// Drop-Base-Drop = SUPPLY

if (rally before base && impulse up after base) {
    type = DEMAND;
    proximal_line = base.low;
    distal_line = base.high;
}
else if (drop before base && impulse down after base) {
    type = SUPPLY;
    proximal_line = base.high;
    distal_line = base.low;
}
```

### Zone Scoring System

**Multi-Factor Scoring (0-100 scale):**

1. **Base Strength (0-25 points):**
   - Zone width (0.5-1.0 ATR optimal)
   - Consolidation tightness
   - Base duration (5-20 bars optimal)

2. **Volume Profile (0-25 points):**
   - Departure volume ratio
   - Initiative vs response
   - Volume climax presence

3. **Institutional Index (0-20 points):**
   - OI alignment
   - Volume concentration
   - Time-of-day formation (9:15-11:00 highest)

4. **State & Age (0-15 points):**
   - FRESH = 15, TESTED = 10, RECLAIMED = 12, VIOLATED = 0
   - Age penalty (zones >60 days degraded)

5. **Rejection History (0-15 points):**
   - Strong rejections = bonus
   - Weak tests = penalty
   - Break-through attempts = penalty

**Example Scoring:**
```
Zone Score Breakdown:
Base Strength:    18/25 (0.8 ATR width, 12 bars, tight)
Volume Profile:   22/25 (2.3× departure, initiative)
Institutional:    16/20 (OI aligned, AM formation)
State & Age:      15/15 (FRESH, 5 days old)
Rejection:        12/15 (2 strong rejections)
─────────────────────────
TOTAL:            83/100 ← Elite zone
```

### Entry Decision Logic

**Multi-Stage Filter System:**

```
Stage 1: Zone Quality Filters
├─ Zone score ≥ minimum (typically 30-40)
├─ Zone state valid (FRESH, TESTED, or RECLAIMED only)
├─ Touch count < maximum (typically 50-100)
└─ Age within range (0-60 days)

Stage 2: Volume/Institutional Filters
├─ Pullback volume < threshold (confirms holding)
├─ Volume score ≥ minimum
├─ Institutional index ≥ minimum
└─ Departure ratio ≥ minimum

Stage 3: Entry Bar Confirmation
├─ Approach velocity ≥ minimum (momentum check)
├─ Penetration depth < 50% (not too deep)
├─ Rejection wick present (confirmation)
└─ Entry bar body strength adequate

Stage 4: Market Context
├─ Time-of-day (avoid lunch/close hours)
├─ Trend alignment (optional)
├─ Higher timeframe alignment
└─ Liquidity sweep setup (bonus)

PASS ALL → ENTER TRADE
```

### Risk Management Framework

**Position Sizing:**
```cpp
// Kelly Criterion adapted for S&D zones
double calculate_position_size(
    double account_balance,
    double win_rate,
    double avg_win_ratio,
    double zone_score
) {
    // Base Kelly
    double kelly = (win_rate * avg_win_ratio - (1 - win_rate)) / avg_win_ratio;
    
    // Conservative: Use 1/4 Kelly
    double fraction = kelly * 0.25;
    
    // Scale by zone quality
    double quality_multiplier = zone_score / 100.0;
    
    // Risk per trade
    double risk_pct = fraction * quality_multiplier;
    
    return account_balance * risk_pct;
}
```

**Stop Loss Placement:**
```cpp
// Stop beyond zone distal line
if (zone.type == DEMAND) {
    stop_loss = zone.distal_line - (buffer_atr × ATR);
} else {
    stop_loss = zone.distal_line + (buffer_atr × ATR);
}

// Typical buffer: 0.3-0.5 ATR
// Ensures stop is beyond "noise"
```

**Target Placement:**
```cpp
// Risk-Reward based
double entry_to_stop = abs(entry_price - stop_loss);
double target = entry_price + (entry_to_stop * RR_ratio);

// Adaptive RR by zone quality:
// High quality (>70): RR = 2.0-3.0
// Medium (50-70): RR = 1.5-2.0
// Low quality (<50): Don't trade
```

## Common Issues & Solutions

### Issue 1: Too Many Zones Detected

**Symptoms:**
- 100+ zones in 1000 bars
- Zones overlapping excessively
- Low average zone quality

**Root Causes:**
```cpp
// Too lenient detection parameters:
min_zone_strength = 20;        // Too low (should be 35-50)
min_zone_width_atr = 0.3;      // Too narrow (should be 0.6-1.3)
consolidation_min_bars = 2;    // Too short (should be 5-8)
```

**Solution:** Tighten detection parameters progressively until zone count is reasonable (10-30 zones per 1000 bars).

---

### Issue 2: Volume/OI Profiles Never Update

**Symptoms:**
- Zones show same volume metrics after multiple touches
- Institutional index stays at formation values
- Dynamic updates not reflecting

**Root Cause:**
```cpp
// Profiles calculated once at creation, never updated
zone.volume_profile = calculate_initial_profile(zone);
// Missing: Update on each touch!
```

**Solution:**
```cpp
// On zone touch event:
if (zone_touched) {
    // Recalculate profiles with current market data
    zone.volume_profile = calculate_volume_profile(
        zone, 
        current_bars,
        volume_baseline
    );
    
    zone.institutional_index = calculate_institutional_index(
        zone,
        current_oi_data
    );
    
    LOG_INFO("Zone " + zone.id + " profiles UPDATED");
}
```

---

### Issue 3: Entry Filters Too Strict (Zero Trades)

**Symptoms:**
- Backtest shows 0 or very few trades
- Many "Entry rejected" messages
- High-quality zones blocked

**Diagnostic Approach:**
```cpp
// Add rejection tracking:
std::map<std::string, int> rejection_counts;

if (entry_rejected) {
    rejection_counts[rejection_reason]++;
    LOG_INFO("Entry rejected: " + rejection_reason);
}

// After run, print breakdown:
for (auto& [reason, count] : rejection_counts) {
    std::cout << reason << ": " << count << std::endl;
}
```

**Common Culprits:**
```cpp
// 1. Volume threshold too high
min_volume_entry_score = 20;  // If zones have scores 0-10, all blocked!

// 2. Institutional threshold unrealistic
institutional_index_min = 60;  // Real data rarely exceeds 40-50

// 3. Departure ratio too strict
min_departure_ratio = 2.0;     // Market data might be 0.5-1.5 range
```

**Solution:** Use data-driven thresholds:
```python
# Analyze actual zone distribution
import json

with open('zones_master.json') as f:
    zones = json.load(f)['zones']

vol_scores = [z['volume_profile']['volume_score'] for z in zones]
inst_indexes = [z['institutional_index'] for z in zones]

print(f"Volume scores: min={min(vol_scores)}, max={max(vol_scores)}, avg={sum(vol_scores)/len(vol_scores)}")
print(f"Inst indexes: min={min(inst_indexes)}, max={max(inst_indexes)}, avg={sum(inst_indexes)/len(inst_indexes)}")

# Set thresholds at 30th percentile to pass 70% of zones
```

---

### Issue 4: Backtest Shows Profits, Live Shows Losses

**The Notorious Backtest-Live Gap**

**Root Causes:**

1. **Timing Asymmetry:**
```cpp
// BACKTEST (hindsight bias):
if (current_bar.low <= zone.proximal_line) {
    enter_at_price = zone.proximal_line;  // Ideal fill!
}

// LIVE (reality):
// Receives bar AFTER close
// Entry decision made on NEXT bar
// Slippage: 10-40 points common
```

2. **Volume/OI Data Mismatch:**
```cpp
// BACKTEST: Using historical volume baseline
baseline_volume = precalculated_baseline[time_of_day];

// LIVE: Volume baseline not available OR 
// baseline calculated from different data source
baseline_volume = 0;  // BUG: Division by zero or invalid ratios!
```

3. **Exit Logic Triggering Too Early:**
```cpp
// Common bug: Exit checks run on ENTRY bar
if (check_volume_exhaustion(trade, current_bar)) {
    exit_trade();  // Exits immediately! (bars_in_trade = 0)
}

// Should be:
if (trade.bars_in_trade >= 3 && check_volume_exhaustion(...)) {
    exit_trade();
}
```

**Solutions:**

A. **Simulate Live Timing in Backtest:**
```cpp
// Backtest should NOT use intra-bar data
// Entry decision on bar N uses ONLY data through bar N-1 close

bool can_enter = evaluate_entry(
    zone,
    bars[i-1],        // Use PREVIOUS bar close
    i-1,
    historical_data
);

if (can_enter) {
    // Enter at NEXT bar open (realistic)
    entry_price = bars[i].open;
}
```

B. **Validate Data Consistency:**
```cpp
// Ensure volume baselines exist
assert(volume_baseline > 0 && "Volume baseline not loaded!");

// Use same data source for backtest and live
// If live uses Fyers API → backtest must use Fyers historical data
```

C. **Add Minimum Hold Period:**
```cpp
// Don't allow exit on first 3 bars
if (trade.bars_in_trade < 3) {
    return;  // Give trade time to develop
}
```

---

### Issue 5: Zones Become Exhausted (Touched 100+ Times)

**Symptoms:**
- Few zones, each touched 200-300+ times
- Decreasing win rate over time on same zones
- No fresh zones appearing

**Root Causes:**
- Zone detection too strict → only 4-5 zones total
- No touch count limit → zones traded indefinitely
- No zone refresh mechanism

**Solutions:**

1. **Relax Zone Detection:**
```cpp
// Before:
min_zone_strength = 60;
min_zone_width_atr = 1.5;

// After:
min_zone_strength = 40;
min_zone_width_atr = 0.8;

// Result: 5 zones → 20-30 zones
```

2. **Add Touch Limit:**
```cpp
if (zone.touch_count > config.max_zone_touch_count) {
    zone.is_active = false;  // Retire exhausted zone
    LOG_INFO("Zone " + zone.id + " retired after " + zone.touch_count + " touches");
}

// Typical limit: 50-100 touches
```

3. **Periodic Zone Re-Detection:**
```cpp
// Every N bars, re-run zone detection
if (current_bar_index % config.zone_refresh_interval == 0) {
    detect_new_zones(bars, current_bar_index);
    prune_old_zones(zones, current_bar_index);
}
```

---

## Multi-Timeframe Framework

### The MTF Advantage

**Problem:** 5-minute zones capture noise, miss major institutional levels.

**Solution:** Detect zones on multiple timeframes, use confluence for high-probability setups.

### Implementation Architecture

```
5-Minute Data (Input)
    ↓
Bar Aggregation
    ├─→ 15-Minute Bars
    ├─→ 30-Minute Bars
    ├─→ 1-Hour Bars
    └─→ 4-Hour Bars
    ↓
Zone Detection (Per Timeframe)
    ├─→ zones_5m.json   (tactical, short-term)
    ├─→ zones_15m.json  (intermediate)
    ├─→ zones_30m.json  (swing)
    ├─→ zones_1h.json   (positional)
    └─→ zones_4h.json   (strategic, major levels)
    ↓
Entry Alignment Check
    ├─→ Entry on 5m zone
    ├─→ Check 15m alignment
    ├─→ Check 30m alignment
    └─→ Check 1h alignment
    ↓
Confluence Scoring
    └─→ +10 pts (15m), +15 pts (30m), +20 pts (1h)
```

### Bar Aggregation Implementation

```cpp
class TimeframeAggregator {
public:
    std::vector<Bar> aggregate(
        const std::vector<Bar>& bars_5m,
        int target_minutes
    ) {
        std::vector<Bar> aggregated;
        int ratio = target_minutes / 5;  // 15min = 3 bars, 60min = 12 bars
        
        for (size_t i = 0; i < bars_5m.size(); i += ratio) {
            if (i + ratio > bars_5m.size()) break;
            
            Bar agg_bar;
            agg_bar.datetime = bars_5m[i].datetime;
            agg_bar.open = bars_5m[i].open;
            agg_bar.close = bars_5m[i + ratio - 1].close;
            
            // High/Low across all bars in period
            agg_bar.high = bars_5m[i].high;
            agg_bar.low = bars_5m[i].low;
            for (int j = 0; j < ratio; j++) {
                agg_bar.high = std::max(agg_bar.high, bars_5m[i+j].high);
                agg_bar.low = std::min(agg_bar.low, bars_5m[i+j].low);
            }
            
            // Sum volume and OI
            agg_bar.volume = 0;
            agg_bar.oi = 0;
            for (int j = 0; j < ratio; j++) {
                agg_bar.volume += bars_5m[i+j].volume;
                agg_bar.oi += bars_5m[i+j].oi;
            }
            
            aggregated.push_back(agg_bar);
        }
        
        return aggregated;
    }
};
```

### Confluence Scoring

```cpp
double calculate_mtf_confluence(
    const Zone& entry_zone_5m,
    double current_price,
    const MultiTimeframeZones& mtf_zones
) {
    double bonus = 0;
    
    // Check if current price is within any HTF zone of same type
    for (const auto& zone_15m : mtf_zones.zones_15m) {
        if (zone_15m.type == entry_zone_5m.type &&
            is_price_in_zone(current_price, zone_15m)) {
            bonus += 10;
            break;
        }
    }
    
    for (const auto& zone_30m : mtf_zones.zones_30m) {
        if (zone_30m.type == entry_zone_5m.type &&
            is_price_in_zone(current_price, zone_30m)) {
            bonus += 15;
            break;
        }
    }
    
    for (const auto& zone_1h : mtf_zones.zones_1h) {
        if (zone_1h.type == entry_zone_5m.type &&
            is_price_in_zone(current_price, zone_1h)) {
            bonus += 20;
            break;
        }
    }
    
    return bonus;
}
```

**Example:**
```
5m DEMAND zone at 24,500-24,520 (score: 35)
15m: No zone ❌
30m: DEMAND at 24,480-24,550 ✅ (+15 pts)
1h:  DEMAND at 24,450-24,600 ✅ (+20 pts)

Final score: 35 + 15 + 20 = 70 (strong setup!)
```

---

## Performance Optimization

### Backtesting Metrics That Matter

**Win Rate:** Should be 45-60% for quality S&D system  
**Profit Factor:** Target 1.8-2.5 (gross profit / gross loss)  
**Expectancy:** Positive per-trade average  
**Max Drawdown:** <20% of capital  
**Sharpe Ratio:** >1.5 for live trading viability

### The 80/20 Rule for Trading Systems

**80% of profits come from:**
- 20% of zones (elite institutional levels)
- 20% of setups (FRESH zones with strong volume)
- 20% of market conditions (trending, volatile)

**Focus effort on:**
1. Zone quality filters (eliminate bottom 50% of zones)
2. Entry timing (wait for confirmation, not hope)
3. Exit management (let winners run, cut losers fast)

### Data-Driven Parameter Optimization

**DON'T:**
- Optimize until backtest shows 90% win rate (overfitting!)
- Use curve-fitting to maximize single metric
- Ignore transaction costs, slippage, latency

**DO:**
- Optimize on training period, validate on separate test period
- Use walk-forward analysis (rolling optimization)
- Include realistic costs (₹100 commission + 3-5 pts slippage per trade)
- Optimize for robust performance across parameter ranges

**Example Optimization:**
```python
# Test min_zone_strength across range
for strength in range(30, 60, 5):
    config.min_zone_strength = strength
    results = run_backtest(config)
    
    # Track performance
    performance[strength] = {
        'win_rate': results.win_rate,
        'profit_factor': results.profit_factor,
        'drawdown': results.max_drawdown,
        'trades': results.total_trades
    }

# Choose value that's robust (not extreme)
# E.g., if 40-45 all perform well, use 42 (middle)
```

---

## Integration with Fyers Broker (NSE India)

### Order Submission via Spring Boot Microservices

**Architecture:**
```
C++ Trading Engine
    ↓ (HTTP REST)
Java Spring Boot Microservice
    ↓ (Fyers API)
Fyers Broker
    ↓
NSE Exchange
```

**Example Order Submission:**
```cpp
// C++ side - HTTP client
#include <curl/curl.h>

void submit_order(const Trade& trade) {
    CURL* curl = curl_easy_init();
    
    std::string url = "http://localhost:8080/api/orders/submit";
    std::string json_data = format_order_json(trade);
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data.c_str());
    
    CURLcode res = curl_easy_perform(curl);
    
    if (res != CURLE_OK) {
        LOG_ERROR("Order submission failed: " + std::string(curl_easy_strerror(res)));
    }
    
    curl_easy_cleanup(curl);
}

std::string format_order_json(const Trade& trade) {
    return R"({
        "symbol": "NIFTY25APR24FUT",
        "side": ")" + (trade.direction == LONG ? "BUY" : "SELL") + R"(",
        "quantity": )" + std::to_string(trade.lot_size * 50) + R"(,
        "price": )" + std::to_string(trade.entry_price) + R"(,
        "stopLoss": )" + std::to_string(trade.stop_loss) + R"(,
        "target": )" + std::to_string(trade.target) + R"(,
        "strategyId": 13
    })";
}
```

**Java Spring Boot Controller:**
```java
@RestController
@RequestMapping("/api/orders")
public class OrderController {
    
    @Autowired
    private FyersApiService fyersService;
    
    @PostMapping("/submit")
    public ResponseEntity<OrderResponse> submitOrder(@RequestBody OrderRequest request) {
        try {
            // Validate request
            validateOrder(request);
            
            // Submit to Fyers
            String orderId = fyersService.placeOrder(
                request.getSymbol(),
                request.getSide(),
                request.getQuantity(),
                request.getPrice()
            );
            
            // Place bracket orders (SL & Target)
            if (request.getStopLoss() != null) {
                fyersService.placeStopLoss(orderId, request.getStopLoss());
            }
            
            if (request.getTarget() != null) {
                fyersService.placeTarget(orderId, request.getTarget());
            }
            
            return ResponseEntity.ok(new OrderResponse(orderId, "SUCCESS"));
            
        } catch (Exception e) {
            log.error("Order submission error", e);
            return ResponseEntity.status(500).body(
                new OrderResponse(null, "FAILED: " + e.getMessage())
            );
        }
    }
}
```

### OI Data Retrieval

**Note:** Open Interest for intraday bars requires **Market Depth API**, not Quotes API.

```java
@Service
public class FyersDataService {
    
    public Map<String, OIData> getOpenInterest(String symbol, String timeframe) {
        // Use market depth API
        String url = "https://api.fyers.in/data-rest/v2/depth";
        
        // Fyers returns depth data including OI
        Response response = restTemplate.getForObject(
            url + "?symbol=" + symbol,
            Response.class
        );
        
        return parseOIData(response);
    }
}
```

---

## AmiBroker AFL Integration

### Zone Detection AFL (v8.0 - Latest)

**Key Features:**
- Rally-Base-Rally / Drop-Base-Drop detection
- State machine (FRESH/TESTED/VIOLATED/RECLAIMED)
- Export to CSV for C++ engine consumption

**Critical AFL Constraints:**
```c
// AFL limitations you MUST respect:

// 1. No multiple return statements
function GetZoneState(prices, zone) {
    // WRONG:
    if (condition1) return 1;
    if (condition2) return 2;
    
    // RIGHT:
    result = 0;
    if (condition1) result = 1;
    else if (condition2) result = 2;
    return result;
}

// 2. Array vs Scalar context
high_price = High;  // Array
max_high = LastValue(Highest(High));  // Scalar

// Can't mix: max_high + High  // ERROR!
// Must use: max_high + LastValue(High)  // OK

// 3. No object-oriented programming
// No classes, no inheritance, no polymorphism
// Use structs and procedural code only
```

**Zone Export Format:**
```c
// Export zones to CSV for C++ consumption
function ExportZones() {
    fh = fopen("zones_amibroker.csv", "w");
    
    fputs("zone_id,type,proximal,distal,formation_bar,state,strength\n", fh);
    
    for (i = 0; i < BarCount; i++) {
        if (IsZoneFormation[i]) {
            line = NumToStr(ZoneID[i]) + "," +
                   ZoneType[i] + "," +
                   NumToStr(Proximal[i]) + "," +
                   NumToStr(Distal[i]) + "," +
                   NumToStr(i) + "," +
                   GetZoneState(i) + "," +
                   NumToStr(Strength[i]) + "\n";
            fputs(line, fh);
        }
    }
    
    fclose(fh);
}
```

---

## Debugging Decision Trees

### When Backtest Returns Are Negative

```
Start: Negative Returns
    ↓
├─→ Check Trade Count
│   ├─→ 0 trades? → Entry filters too strict
│   │               → Review rejection logs
│   │               → Lower thresholds
│   │
│   └─→ Many trades? → Continue ↓
│
├─→ Check Win Rate
│   ├─→ <30%? → Entry logic fundamentally wrong
│   │           → Review zone quality
│   │           → Check for logic bugs
│   │
│   ├─→ 30-45%? → Entry slightly weak
│   │             → Add filters (velocity, wick confirmation)
│   │
│   └─→ >45%? → Win rate OK, check exit logic ↓
│
├─→ Check Exit Reason Breakdown
│   ├─→ All exits = STOP_LOSS?
│   │   → Stop too tight
│   │   → Increase stop buffer
│   │
│   ├─→ All exits = SESSION_END?
│   │   → Trades not reaching targets
│   │   → Check target placement
│   │   → Consider trailing stops
│   │
│   └─→ Exits = VOL_EXHAUSTION (immediate)?
│       → BUG: Exit triggering on entry bar
│       → Add bars_in_trade check
│
├─→ Check Avg Win vs Avg Loss
│   ├─→ Avg Win < Avg Loss?
│   │   → Risk:Reward ratio inverted
│   │   → Targets too close
│   │   → Stops too far
│   │
│   └─→ Avg Win > Avg Loss but still losing?
│       → Not enough winners (win rate issue)
│       → Back to entry logic
│
└─→ Check Transaction Costs
    └─→ Are you including commission + slippage?
        → Each trade costs ₹100-150 typically
        → Factor this into expectancy
```

### When Live Performance Differs from Backtest

**Systematic Diagnostic:**

1. **Log Everything in Live:**
```cpp
LOG_INFO("=== ENTRY DECISION ===");
LOG_INFO("Zone: " + zone.id + " | Score: " + zone.score);
LOG_INFO("Entry Price: " + entry_price + " | SL: " + sl + " | Target: " + target);
LOG_INFO("Volume Score: " + zone.volume_profile.volume_score);
LOG_INFO("Institutional Index: " + zone.institutional_index);
LOG_INFO("Approach Velocity: " + approach_velocity);
LOG_INFO("Time: " + current_bar.datetime);
```

2. **Compare Logs:**
- Backtest log for same date/time
- Live log for same entry attempt
- Look for differences in:
  - Entry price (backtest vs live)
  - Zone metrics (volume, OI, score)
  - Timing (bar index, datetime)

3. **Common Discrepancies:**
```
Backtest: Entry at 24,500.0 (zone proximal)
Live:     Entry at 24,505.5 (zone proximal + slippage)
          → Worse fill = lower profit

Backtest: Volume baseline = 50,000
Live:     Volume baseline = 0 (NOT LOADED!)
          → Volume ratios wrong = wrong decisions

Backtest: Bar processed at 10:30:00
Live:     Bar received at 10:31:05 (1 min delay)
          → Different bar data
```

---

## Best Practices Checklist

**Zone Detection:**
- [ ] Impulse threshold: 1.4-2.0× ATR
- [ ] Base duration: 5-20 bars
- [ ] Zone width: 0.6-1.3 ATR
- [ ] Minimum zone strength: 35-50
- [ ] Detection interval: Every 4-6 bars

**Zone Scoring:**
- [ ] Multi-factor (base + volume + institutional + state + rejection)
- [ ] Normalize to 0-100 scale
- [ ] Weight volume/institutional heavily (40-50% combined)
- [ ] Penalize age >60 days
- [ ] Bonus for FRESH and RECLAIMED states

**Entry Filters:**
- [ ] Zone score ≥ 30 minimum
- [ ] Zone state = FRESH, TESTED, or RECLAIMED only
- [ ] Touch count < 50-100
- [ ] Pullback volume < 1.5× baseline
- [ ] Approach velocity ≥ 0.5 ATR/bar
- [ ] Penetration depth < 50%
- [ ] Time-of-day: Avoid 13:00-15:00

**Risk Management:**
- [ ] Stop loss: Zone distal + 0.3-0.5 ATR buffer
- [ ] Position size: 0.5-2% risk per trade
- [ ] Risk:Reward: 1.5-3.0 based on zone quality
- [ ] Max concurrent trades: 3-5
- [ ] Max consecutive losses: 3 (pause system)

**Exit Management:**
- [ ] Trailing stop: Activate at 0.9-1.0R profit
- [ ] Volume exhaustion: Check only after 3+ bars
- [ ] Session end: Close 15 min before market close
- [ ] Don't exit on entry bar (bars_in_trade ≥ 3)

**Data Quality:**
- [ ] Volume baseline file exists and loads
- [ ] OI data available for all bars
- [ ] No missing bars in historical data
- [ ] ATR calculation uses ≥14 bars
- [ ] Timezone consistency (backtest vs live)

**Performance Tracking:**
- [ ] Log all entries with full context
- [ ] Track exit reason breakdown
- [ ] Calculate metrics daily/weekly
- [ ] Compare live vs backtest performance
- [ ] Maintain trade journal with screenshots

---

## References

**Methodology:**
- Sam Seiden: "Supply & Demand" (Online Trading Academy)
- Adam Grimes: "The Art and Science of Technical Analysis"
- Mark Douglas: "Trading in the Zone" (Psychology)

**Implementation Patterns:**
- See `/references/zone_detection_patterns.md` for code examples
- See `/references/state_machine_logic.md` for state transitions
- See `/references/volume_oi_calculations.md` for institutional metrics

**Historical Context:**
- See `/references/version_history.md` for V3.x → V6.0 evolution
- See `/references/bug_tracker.md` for known issues and fixes

---

## Quick Reference Card

**Zone Formation:**
```
RBR (DEMAND): Rally → Base → Rally up
DBD (SUPPLY): Drop → Base → Drop down
```

**Zone States:**
```
FRESH → TESTED → VIOLATED → RECLAIMED
  ↑__________________________|
```

**Scoring Formula:**
```
Total = Base(25) + Volume(25) + Institutional(20) + State(15) + Rejection(15)
```

**Entry Criteria:**
```
MUST PASS:
✓ Score ≥ 30
✓ State = FRESH/TESTED/RECLAIMED
✓ Touches < 100
✓ Volume score ≥ 0
✓ Approach velocity ≥ 0.5
```

**Risk Math:**
```
Stop = Distal ± (0.5 × ATR)
Target = Entry + (Entry-Stop) × RR
Size = (Account × Risk%) / (Entry-Stop)
```

---

**For additional help:**
- Consult `/references/` for detailed algorithms
- Check `/scripts/` for utility tools
- Review `/examples/` for working implementations
