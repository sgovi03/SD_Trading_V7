# Entry, Stop Loss, and Take Profit Price Calculations

## Question
Are we calculating the entry, take profit, or stop loss prices based on entry decision scores, and also based on zone scores struct objects?

## Answer: MIXED Approach

| Price | Uses ZoneScore | Uses Zone Struct | Uses Config | Uses ATR | Data Source |
|-------|-----------------|------------------|-------------|---------|-------------|
| **Entry** | ✅ YES | ✅ YES | ❌ NO | ❌ NO | Score + Zone geometry |
| **Stop Loss** | ❌ NO | ✅ YES | ✅ YES | ✅ YES | Zone + ATR + Config |
| **Take Profit** | ❌ NO | ✅ YES | ✅ YES | ✅ YES | Structure-based calculator |

---

## 1. ENTRY PRICE - Uses ZoneScore + Zone Struct

### Location
[src/scoring/entry_decision_engine.cpp](src/scoring/entry_decision_engine.cpp#L118-L131)

### Calculation Formula

```cpp
// DEMAND ZONES (Long entries) - Conservative approach
if (zone.type == ZoneType::DEMAND) {
    double conservative_factor = 1.0 - aggressiveness;
    entry_price = zone.distal_line + 
                  conservative_factor * (zone.proximal_line - zone.distal_line);
}

// SUPPLY ZONES (Short entries) - Aggressive approach
else {
    entry_price = zone.distal_line - 
                  aggressiveness * (zone.distal_line - zone.proximal_line);
}
```

### Data Inputs
| Input | Source | Value Type | Purpose |
|-------|--------|-----------|---------|
| `zone.distal_line` | Zone struct | double (price) | Far edge of zone |
| `zone.proximal_line` | Zone struct | double (price) | Near edge of zone |
| `aggressiveness` | ZoneScore | double (0.0-1.0) | Entry positioning |
| `zone.type` | Zone struct | enum (DEMAND/SUPPLY) | Determines direction |

### Aggressiveness Source - ZoneScore

```cpp
// From zone_scorer.cpp: calculate_composite()
double aggressiveness = 
    (base_strength * 0.30 +       // 30% base strength
     elite_bonus * 0.25 +         // 25% elite bonus
     swing_score * 0.15 +         // 15% swing analysis
     regime_align * 0.20 +        // 20% regime alignment
     freshness * 0.10) / 100.0;   // 10% state freshness

// Rejection confirmation REDUCES aggressiveness
double rejection_factor = 1.0 - (rejection_score / 100.0);
entry_aggressiveness = aggressiveness * rejection_factor;
```

### Entry Positioning Examples

**Scenario 1: DEMAND Zone (Long Entry)**
```
Zone: Distal 1000, Proximal 1050
Score Aggressiveness: 0.60 (Moderate)

Conservative Factor = 1.0 - 0.60 = 0.40
Entry Price = 1000 + 0.40 * (1050 - 1000)
           = 1000 + 0.40 * 50
           = 1000 + 20
           = 1020  (40% from the bottom of zone)
```

**Scenario 2: SUPPLY Zone (Short Entry)**
```
Zone: Distal 2000, Proximal 1950
Score Aggressiveness: 0.80 (Aggressive)

Entry Price = 2000 - 0.80 * (2000 - 1950)
           = 2000 - 0.80 * 50
           = 2000 - 40
           = 1960  (80% down from top of zone)
```

---

## 2. STOP LOSS - Uses Zone Struct + ATR + Config

### Location
[src/scoring/entry_decision_engine.cpp](src/scoring/entry_decision_engine.cpp#L21-L41)

### Calculation Formula

```cpp
double calculate_stop_loss(const Zone& zone, double entry_price, double atr) const {
    double zone_height = std::abs(zone.proximal_line - zone.distal_line);
    
    // Calculate buffer as the LARGER of:
    // 1. Percentage of zone height
    // 2. ATR multiplier
    double buffer = std::max<double>(
        zone_height * config.sl_buffer_zone_pct / 100.0,
        atr * config.sl_buffer_atr
    );
    
    // Place stop loss BEYOND zone with buffer
    if (zone.type == ZoneType::DEMAND) {
        return zone.distal_line - buffer;  // Below demand zone
    } else {
        return zone.distal_line + buffer;  // Above supply zone
    }
}
```

### Data Inputs
| Input | Source | Value Type | Purpose |
|-------|--------|---------|---------|
| `zone.proximal_line` | Zone struct | double | Near edge of zone |
| `zone.distal_line` | Zone struct | double | Far edge of zone |
| `atr` | Calculated from bars | double | Volatility measure |
| `config.sl_buffer_zone_pct` | Config file | % | Zone height percentage |
| `config.sl_buffer_atr` | Config file | multiplier | ATR multiplier (e.g., 1.5x) |
| `zone.type` | Zone struct | enum | DEMAND or SUPPLY |

### Stop Loss Positioning Examples

**Scenario 1: DEMAND Zone with ATR=10**
```
Zone Height: Distal 1000, Proximal 1050 = 50
ATR: 10
Config: sl_buffer_zone_pct = 20%, sl_buffer_atr = 1.5

Buffer Option 1: 50 * 20% = 10 (zone height method)
Buffer Option 2: 10 * 1.5 = 15 (ATR method)
Buffer Used: MAX(10, 15) = 15

Stop Loss = 1000 - 15 = 985  (Below demand zone with buffer)
```

**Scenario 2: SUPPLY Zone with Low ATR**
```
Zone Height: Distal 2000, Proximal 1950 = 50
ATR: 5
Config: sl_buffer_zone_pct = 20%, sl_buffer_atr = 1.5

Buffer Option 1: 50 * 20% = 10 (zone height method)
Buffer Option 2: 5 * 1.5 = 7.5 (ATR method)
Buffer Used: MAX(10, 7.5) = 10

Stop Loss = 2000 + 10 = 2010  (Above supply zone with buffer)
```

### ⚠️ BREAKEVEN MODE Modification

If `config.use_breakeven_stop_loss = true`, the entry price is moved to the SL location:

```cpp
if (config.use_breakeven_stop_loss) {
    decision.entry_price = original_sl;
    // SL is then placed at same distance below/above new entry price
    if (zone.type == ZoneType::DEMAND) {
        decision.stop_loss = decision.entry_price - sl_distance;
    } else {
        decision.stop_loss = decision.entry_price + sl_distance;
    }
}
```

---

## 3. TAKE PROFIT - Uses Structure-Based Calculator

### Location
[src/scoring/entry_decision_engine.cpp](src/scoring/entry_decision_engine.cpp#L164-L169)

### Calculation Process

```cpp
// Uses TargetCalculatorFactory to select calculator
// Current implementation: StructureBasedCalculator
auto target_result = target_calculator_->calculate_target(
    zone,                // Zone for context
    bars,                // Bar data for finding swing highs/lows
    zones_ref,           // All zones to find supply/demand above/below
    decision.entry_price,
    decision.stop_loss,
    current_index
);

decision.take_profit = target_result.target_price;
```

### Target Calculation Strategy (StructureBasedCalculator)

**For DEMAND Zones (Long) - Find Resistance Above:**

1. **Swing Highs** - Identify swing high peaks above entry price
2. **Supply Zones** - Find supply zone proximal lines (bottom of resistance) above entry
3. **Fibonacci Extension** - Calculate 1.618 Fibonacci extension (if enabled)
4. **Filter by R:R Minimum** - Reject targets that don't meet minimum R:R

```cpp
double min_target_distance = risk * config_.min_rr_required;
// min_rr_required = 2.0 (fixed in config, e.g., 2.0 = 1:2 risk reward minimum)

// For each candidate target:
if (buffered > entry_price + min_target_distance) {
    candidates.push_back(buffered);
}
```

**For SUPPLY Zones (Short) - Find Support Below:**

1. **Swing Lows** - Identify swing low troughs below entry price
2. **Demand Zones** - Find demand zone proximal lines (top of support) below entry
3. **Fibonacci Extension** - Calculate 1.618 Fibonacci extension (if enabled)
4. **Filter by R:R Minimum** - Reject targets that don't meet minimum R:R

### ATP (Adjusted Target Price) Selection

From multiple candidates, StructureBasedCalculator selects:

```cpp
// 1. If clear path to nearest target → Use it
// 2. Otherwise → Select strongest structural level
// 3. Fallback → Calculate R:R-based target

// Final selection also considers:
- Clear path check (obstacles allowed: max 1-2)
- Relative strength of structural level
- Failed tests if too many obstacles
```

### Data Inputs for Take Profit
| Input | Source | Value Type | Purpose |
|-------|--------|----------|---------|
| `entry_price` | EntryDecision | double | Reference for target distance |
| `stop_loss` | EntryDecision | double | Risk calculation |
| `bars` | Bar array | price data | Swing high/low identification |
| `all_zones` | Zone array | zone data | Supply/demand zone identification |
| `config.min_rr_required` | Config | 2.0 (default) | Minimum R:R filter |
| `config.buffer_atr_multiple` | Config | 0.3x ATR | Buffer before hitting level |
| `config.use_fibonacci` | Config | bool | Enable/disable fibonacci |

### Take Profit Examples

**Example 1: DEMAND Zone - Clean Resistance**
```
Entry: 1020
Stop Loss: 985
Risk: 35

Minimum Target: 1020 + (35 * 2.0) = 1090 (meet 2.0 R:R minimum)

Swing Highs Found: 1095, 1110, 1125
Supply Zones Found: 1100-1105

Selected Target: 1100 (strong supply zone, meets R:R)
Actual R:R = (1100 - 1020) / 35 = 2.29:1 ✅
```

**Example 2: SUPPLY Zone - Partial Resistance**
```
Entry: 1960
Stop Loss: 1970
Risk: 10

Minimum Target: 1960 - (10 * 2.0) = 1940 (meet 2.0 R:R minimum)

Swing Lows Found: 1935, 1920, 1900
Demand Zones Found: 1930-1940

Selected Target: 1930 (strong demand zone, meets R:R)
Actual R:R = (1960 - 1930) / 10 = 3.0:1 ✅
```

---

## 4. OLD vs NEW Implementation

### Previous Implementation (Backup File)
```cpp
// OLD: Used recommended_rr from ZoneScore
decision.take_profit = calculate_take_profit(
    zone, 
    decision.entry_price, 
    decision.stop_loss, 
    decision.score.recommended_rr  // ← Was used here
);

// Calculation:
double reward = risk * recommended_rr;
return entry_price + reward;  // For DEMAND zones
```

**Why Changed?**
- Fixed R:R multiplier ignored market structure
- Couldn't adapt to varying distances to next support/resistance
- Structure-based approach more flexible and market-aware

### Current Implementation (Active)
```cpp
// NEW: Uses structure-based calculator
auto target_result = target_calculator_->calculate_target(
    zone, bars, zones_ref, 
    decision.entry_price, 
    decision.stop_loss,
    current_index
);
decision.take_profit = target_result.target_price;
```

**Advantages:**
- ✅ Adapts to market structure (swing highs/lows, zones)
- ✅ Respects key technical levels
- ✅ More dynamic profit taking
- ✅ Fibonacci extensions for extended moves

---

## 5. What ZoneScore.recommended_rr IS Used For

Although NOT used in take profit calculation currently, `recommended_rr` IS used for:

### 1. Trade Rules Validation
```cpp
// In trade_manager.cpp enter_trade()
RuleCheckResult rule_result = trade_rules.evaluate_entry(
    zone,
    decision.score,  // ← decision.score.recommended_rr accessible here
    regime,
    *bars,
    bar_index
);
```

### 2. Historical Recording
```cpp
current_trade.score_recommended_rr = decision.score.recommended_rr;
```

### 3. Trade Analysis & Forensics
- Stored in backtest reports
- Available for post-trade analysis
- Can identify high/low R:R correlation with profitability

### Recommended R:R Calculation (zone_scorer.cpp)

```cpp
if (config.scoring.rr_scale_with_score) {
    double score_factor = score.total_score / 100.0;
    score.recommended_rr = config.scoring.rr_base_ratio +
        (score_factor * (config.scoring.rr_max_ratio - config.scoring.rr_base_ratio));
    score.recommended_rr = std::min<double>(score.recommended_rr, config.scoring.rr_max_ratio);
} else {
    score.recommended_rr = config.scoring.rr_base_ratio;
}
```

**Example:**
```
Config: rr_base_ratio = 2.0, rr_max_ratio = 5.0
Zone Score: 78/100

Score Factor = 0.78
Recommended R:R = 2.0 + (0.78 * (5.0 - 2.0))
                = 2.0 + (0.78 * 3.0)
                = 2.0 + 2.34
                = 4.34:1
```

---

## 6. Complete Data Flow Diagram

```
┌──────────────────────────────────────┐
│ 1. ZONE SCORING                      │
│    ZoneScorer.evaluate_zone()        │
└────────────┬─────────────────────────┘
             │
             ├─ 6 component scores calculated
             ├─ Composite score calculated  
             ├─ entry_aggressiveness derived from components
             ├─ recommended_rr calculated (scaled by score)
             └─ rejection_confirmation reduced aggressiveness
             ↓
┌──────────────────────────────────────┐
│ 2. ENTRY DECISION ENGINE             │
│    calculate_entry(zone, score, atr) │
└────────────┬─────────────────────────┘
             │
    ┌────────┼────────────────────────────┐
    │        │                            │
    ▼        ▼                            ▼
  ENTRY    STOP LOSS              TAKE PROFIT
    │        │                            │
    ├─ Uses: ├─ Uses:            ├─ Uses:
    │  Zone  │  Zone             │  Zone
    │  Score │  ATR              │  Bars
    │        │  Config           │  All Zones
    │        │  Geo calc         │  R:R filter
    │        │                   │  Fibonacci
    │        │  Formula:         │
    │        │  Buffer of        │  Strategy:
    │        │  MAX(zone%,       │  Find swings/
    │        │  ATR mult)        │  zones near
    │        │  placed beyond    │  min R:R
    │        │  zone             │
    ▼        ▼                    ▼
  ENTRY   STOP_LOSS           TAKE_PROFIT
  PRICE   PLACED              PLACED
  (Zone  (Beyond              (At swing,
   + Score zone w/            zone, or fib
   agg)    buffer)             level)
    │        │                 │
    └────────┴─────────────────┘
             │
             ▼
    ┌──────────────────────────┐
    │ 3. VALIDATION            │
    │ (R:R check, threshold)   │
    │ 4. TRADE RULES           │
    │ (Optional filtering)      │
    │ 5. STORE IN TRADE RECORD │
    └──────────────────────────┘
```

---

## 7. Config Parameters Involved

| Parameter | File | Default | Used By | Effect |
|-----------|------|---------|---------|--------|
| `sl_buffer_zone_pct` | system_config.json | 20% | SL calc | Zone height padding |
| `sl_buffer_atr` | system_config.json | 1.5x | SL calc | ATR padding |
| `use_breakeven_stop_loss` | system_config.json | false | Entry/SL | Moves entry to SL if true |
| `min_rr_required` | StructureConfig | 2.0 | Target calc | Minimum R:R for trades |
| `target_rr_ratio` | system_config.json | varies | R:R calc | Fixed R:R used as baseline |
| `rr_scale_with_score` | system_config.json | true | Score calc | Scale R:R with quality |
| `rr_base_ratio` | system_config.json | 2.0 | Score calc | Minimum recommended R:R |
| `rr_max_ratio` | system_config.json | 5.0 | Score calc | Maximum recommended R:R |

---

## 8. Summary Table

| Component | ZoneScore Used | Zone Struct Used | Config Used | ATR Used | Strategy |
|-----------|-----------------|-----------------|------------|---------|----------|
| **Entry Price** | ✅ (aggressiveness) | ✅ (geometry) | ❌ | ❌ | Adaptive within zone |
| **Stop Loss** | ❌ | ✅ (geometry) | ✅ (buffers) | ✅ | Beyond zone + buffer |
| **Take Profit** | ⚠️ (recorded only) | ✅ (for context) | ✅ (min R:R) | ✅ (buffering) | Structure-based |

**Legend:**
- ✅ = Actively used in calculation
- ⚠️ = Stored/recorded but not used in calculation
- ❌ = Not involved

---

## 9. Key Architectural Insights

### Entry Quality → Entry Position Mapping
```
Higher Zone Score → Higher Aggressiveness → More Aggressive Entry
  VERY HIGH (90+)  →  0.80-1.0  →  Deep in zone  (risky but better risk:reward)
  HIGH (75-89)     →  0.60-0.79 →  Middle area   (balanced)
  MEDIUM (60-74)   →  0.40-0.59 →  Conservative (safer but lower reward)
  LOW (40-59)      →  0.20-0.39 →  Very edge    (least risky)
```

### Market Structure → Take Profit Selection
```
Clean Structure (1 swing)    → Selected level is strong
Partial Structure (2 swings) → Most probable level chosen
Messy (3+ swings)           → Fibonacci fallback or rules-based
```

### Risk → Stop Loss Placement
```
Volatile Market (High ATR)  → Wider SL (larger absolute points)
Quiet Market (Low ATR)      → Tighter SL (fewer points)
```

---

## Files References

| Detail | File | Lines |
|--------|------|-------|
| Entry price calculation | [entry_decision_engine.cpp](src/scoring/entry_decision_engine.cpp) | 118-131 |
| Stop loss calculation | [entry_decision_engine.cpp](src/scoring/entry_decision_engine.cpp) | 21-41 |
| Take profit calculation | [entry_decision_engine.cpp](src/scoring/entry_decision_engine.cpp) | 164-169 |
| Target calculator factory | [target_calculator_factory.h](src/targeting/target_calculator_factory.h) | - |
| Structure-based calculator | [structure_based_calculator.cpp](src/targeting/structure_based_calculator.cpp) | 10-100 |
| Zone score calculation | [zone_scorer.cpp](src/scoring/zone_scorer.cpp) | 100-150 |
| Recommended R:R calculation | [zone_scorer.cpp](src/scoring/zone_scorer.cpp) | 120-130 |
| Main entry decision flow | [live_engine.cpp](src/live/live_engine.cpp) | 1304-1307 |
| Trade record storage | [trade_manager.cpp](src/backtest/trade_manager.cpp) | 308-320 |

---

**Last Updated:** February 9, 2026
**Status:** ✅ IMPLEMENTATION VERIFIED
