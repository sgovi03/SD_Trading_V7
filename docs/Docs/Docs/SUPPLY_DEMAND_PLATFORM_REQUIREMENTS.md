# SUPPLY & DEMAND TRADING PLATFORM
## Comprehensive Requirements Analysis

**Vision:** A universal, institutional-grade trading platform based on supply and demand principles that works across all markets (Equities, Forex, Commodities, Crypto) and all instruments with minimal configuration changes.

**Author:** Requirements Analysis based on 25+ years trading experience + Sam Seiden institutional methodology
**Date:** February 8, 2026

---

## 📋 **TABLE OF CONTENTS**

1. [Core Philosophy](#core-philosophy)
2. [Functional Requirements](#functional-requirements)
3. [Zone Detection Requirements](#zone-detection-requirements)
4. [Zone Quality Scoring Requirements](#zone-quality-scoring-requirements)
5. [Entry Validation Requirements](#entry-validation-requirements)
6. [Target & Risk Management Requirements](#target-risk-management-requirements)
7. [Market Structure Requirements](#market-structure-requirements)
8. [Multi-Market Support Requirements](#multi-market-support-requirements)
9. [Data Requirements](#data-requirements)
10. [Performance Requirements](#performance-requirements)
11. [Extensibility Requirements](#extensibility-requirements)

---

## 🎯 **CORE PHILOSOPHY**

### **Supply & Demand Foundation (Sam Seiden Principles)**

The platform must be built on these institutional trading concepts:

1. **Price moves due to imbalance between supply and demand**
   - When demand exceeds supply → Price rises
   - When supply exceeds demand → Price falls
   - These imbalances leave "footprints" in price action

2. **Zones represent institutional order flow**
   - Banks/institutions accumulate/distribute in tight ranges
   - These zones act as support/resistance when price returns
   - The tighter the zone, the stronger the institutional interest

3. **Fresh zones have higher probability**
   - First touch has highest probability (institutions are still there)
   - Each subsequent touch reduces probability (orders get filled)
   - After 3-4 touches, zone becomes weak

4. **Context matters more than the zone itself**
   - A strong zone in the wrong market context will fail
   - A mediocre zone in perfect context can work
   - Trend, momentum, and structure determine context

5. **Probability decreases with distance**
   - The further price travels from a zone, the less relevant it becomes
   - Time decay: Zones older than 30 days lose relevance
   - Distance decay: Zones more than 10 ATR away are less important

---

## 📊 **FUNCTIONAL REQUIREMENTS**

### **FR-001: Universal Market Support**

**Requirement:** Platform must support ANY market with standardized interface

**Details:**
- **Equities:** Stocks, Indices (NIFTY, BANKNIFTY, S&P 500)
- **Forex:** All currency pairs (EUR/USD, GBP/JPY, etc.)
- **Commodities:** Gold, Silver, Oil, Natural Gas
- **Crypto:** BTC, ETH, altcoins
- **Derivatives:** Futures, Options (on any underlying)

**Implementation:**
```cpp
// Market-agnostic instrument specification
struct Instrument {
    string symbol;              // "NIFTY", "EURUSD", "BTCUSDT"
    MarketType market;          // EQUITY, FOREX, COMMODITY, CRYPTO
    InstrumentType type;        // SPOT, FUTURE, OPTION
    
    // Market-specific properties
    double tick_size;           // Minimum price movement
    double lot_size;            // Contract size
    string currency;            // "INR", "USD", "BTC"
    TradingHours hours;         // Market hours
    
    // Normalization for cross-market comparison
    double get_normalized_price_move(double move) const;
    double get_normalized_volatility() const;
};
```

**Acceptance Criteria:**
- [ ] Can load NIFTY futures data and detect zones
- [ ] Can load EURUSD forex data and detect zones
- [ ] Can load BTC/USD crypto data and detect zones
- [ ] Zone detection logic identical across all markets
- [ ] Only configuration needed: tick_size, lot_size, hours

---

### **FR-002: Multi-Timeframe Analysis**

**Requirement:** Platform must analyze zones across multiple timeframes simultaneously

**Details:**
- **Higher Timeframe (HTF) Zones:** Daily, Weekly zones for major levels
- **Trading Timeframe (TF) Zones:** 4H, 1H zones for entries
- **Lower Timeframe (LTF) Zones:** 15m, 5m zones for precision entries

**Hierarchy:**
```
Weekly Zone (HTF)
  ↓ Contains →
Daily Zone (HTF)
  ↓ Contains →
4H Zone (TF) ← Trade from this
  ↓ Contains →
1H Zone (LTF) ← Entry refinement
  ↓ Contains →
15m Zone (LTF) ← Precise entry
```

**Rules:**
1. HTF zones override LTF zones (bigger timeframe = stronger)
2. Only trade LTF zones aligned with HTF zones
3. Zone must be "in fresh" on HTF (not violated on higher timeframe)

**Implementation:**
```cpp
struct MultiTimeframeZone {
    Zone primary_zone;              // The zone we're trading
    Timeframe primary_tf;           // e.g., 1H
    
    optional<Zone> htf_parent_zone; // Daily/Weekly zone containing this
    vector<Zone> ltf_child_zones;   // 15m/5m zones within this
    
    bool is_aligned_with_htf;       // Does HTF support this direction?
    bool htf_zone_fresh;            // Is HTF zone still fresh?
};
```

**Acceptance Criteria:**
- [ ] Can detect zones on Daily, 4H, 1H, 15m, 5m simultaneously
- [ ] Can identify parent-child relationships between zones
- [ ] Can filter trades: only enter LTF zones with HTF alignment
- [ ] Can track HTF zone state (fresh, tested, violated)

---

### **FR-003: Real-Time Zone Tracking**

**Requirement:** Platform must track ALL zones in real-time, not just at entry

**Details:**

**Zone Lifecycle States:**
```cpp
enum ZoneState {
    FORMING,        // Consolidation pattern detected, not confirmed yet
    FRESH,          // Confirmed but never tested
    TESTED,         // Price returned and bounced (zone held)
    WEAKENING,      // Multiple tests, losing strength
    VIOLATED,       // Price broke through zone
    EXPIRED         // Too old or too far away
};
```

**Real-Time Updates:**
```cpp
class ZoneTracker {
public:
    // Called on every new bar
    void update_all_zones(const Bar& new_bar) {
        for (auto& zone : active_zones_) {
            // Check if zone was tested
            if (is_price_in_zone(new_bar, zone)) {
                handle_zone_test(zone, new_bar);
            }
            
            // Check if zone was violated
            if (is_zone_violated(new_bar, zone)) {
                zone.state = ZoneState::VIOLATED;
                move_to_inactive(zone);
            }
            
            // Update age
            zone.age_bars++;
            
            // Update departure metrics
            if (zone.state == ZoneState::FORMING) {
                update_departure_metrics(zone, new_bar);
            }
            
            // Recalculate quality score (if components changed)
            if (zone.touches_changed || zone.state_changed) {
                recalculate_zone_quality(zone);
            }
        }
    }
};
```

**Acceptance Criteria:**
- [ ] All zones updated on every bar (not just at entry check)
- [ ] Zone state transitions tracked accurately
- [ ] Zone age increments every bar/day
- [ ] Touch count updates when price tests zone
- [ ] Violated zones moved to inactive list

---

## 🔍 **ZONE DETECTION REQUIREMENTS**

### **ZD-001: Consolidation-Based Detection**

**Requirement:** Detect zones where price consolidates before explosive move

**Method:** Identify tight ranges followed by strong directional moves

**Algorithm:**
```cpp
struct ConsolidationPattern {
    int start_bar;              // Where consolidation began
    int end_bar;                // Where consolidation ended
    double high;                // Highest high in range
    double low;                 // Lowest low in range
    double range_atr_ratio;     // Range size / ATR
    
    // Quality metrics
    int num_bars;               // Consolidation duration
    double tightness;           // How tight (0-100%)
    bool clean;                 // No false breakouts
};

// Detection logic
ConsolidationPattern detect_consolidation(
    const vector<Bar>& bars,
    int index,
    int min_bars,           // Minimum bars to qualify (default: 3)
    int max_bars,           // Maximum bars (default: 20)
    double max_range_atr    // Max range vs ATR (default: 0.5)
) {
    // Find tight consolidation
    for (int window = min_bars; window <= max_bars; window++) {
        if (index < window) continue;
        
        // Calculate range
        double high = find_high(bars, index - window, index);
        double low = find_low(bars, index - window, index);
        double range = high - low;
        
        // Check if tight enough
        double atr = calculate_atr(bars, 14, index);
        if (range / atr <= max_range_atr) {
            // Check for explosive move after
            double move_after = calculate_move(bars, index, index + 5);
            if (move_after > range * 2.0) {
                // Found valid consolidation pattern
                return ConsolidationPattern{
                    index - window, index, high, low, range / atr,
                    window, calculate_tightness(bars, index - window, index),
                    is_clean_consolidation(bars, index - window, index)
                };
            }
        }
    }
    
    return {}; // Not found
}
```

**Criteria:**
- Range must be < 0.5 ATR (tight consolidation)
- Minimum 3 bars, maximum 20 bars
- Must be followed by 2x range move (explosive)
- No false breakouts during consolidation

**Acceptance Criteria:**
- [ ] Detects consolidation patterns automatically
- [ ] Filters out wide ranges (> 0.5 ATR)
- [ ] Requires explosive move for confirmation
- [ ] Works on all timeframes (5m to Daily)

---

### **ZD-002: Rally/Drop Base Detection**

**Requirement:** Detect zones at the base of strong rallies or drops (institutional accumulation/distribution)

**Types:**
1. **Rally-Base-Rally (RBR):** Demand zone
2. **Drop-Base-Drop (DBD):** Supply zone
3. **Rally-Base-Drop (RBD):** Supply zone (distribution)
4. **Drop-Base-Rally (DBR):** Demand zone (accumulation)

**Algorithm:**
```cpp
enum PatternType { RBR, DBD, RBD, DBR };

struct BasePattern {
    PatternType type;
    
    // Three phases
    struct Phase {
        int start_bar;
        int end_bar;
        double magnitude;       // Size of move
        double momentum;        // Speed of move
    };
    
    Phase leg1;                 // Initial move (Rally or Drop)
    Phase base;                 // Consolidation (tight range)
    Phase leg2;                 // Second move (direction determines type)
    
    // Quality
    double leg1_strength;       // How strong was first leg
    double base_tightness;      // How tight was base
    double leg2_strength;       // How strong was second leg
    bool valid;                 // Meets all criteria
};

BasePattern detect_base_pattern(
    const vector<Bar>& bars,
    int index
) {
    BasePattern pattern;
    
    // Detect first leg (look back 10-30 bars)
    auto leg1 = find_strong_move_before(bars, index, /*lookback=*/30);
    if (!leg1.valid) return {};
    
    // Detect base (after leg1, before current)
    auto base = find_consolidation(bars, leg1.end_bar, index);
    if (!base.valid || base.range_atr_ratio > 0.4) return {};
    
    // Detect second leg (after base)
    auto leg2 = find_strong_move_after(bars, base.end_bar, /*lookahead=*/10);
    if (!leg2.valid) return {};
    
    // Determine pattern type
    bool leg1_up = leg1.magnitude > 0;
    bool leg2_up = leg2.magnitude > 0;
    
    if (leg1_up && leg2_up) {
        pattern.type = PatternType::RBR;  // Demand zone
    } else if (!leg1_up && !leg2_up) {
        pattern.type = PatternType::DBD;  // Supply zone
    } else if (leg1_up && !leg2_up) {
        pattern.type = PatternType::RBD;  // Supply (distribution)
    } else {
        pattern.type = PatternType::DBR;  // Demand (accumulation)
    }
    
    pattern.leg1 = leg1;
    pattern.base = base;
    pattern.leg2 = leg2;
    pattern.valid = true;
    
    return pattern;
}
```

**Criteria:**
- Leg1 must be > 2 ATR (strong move)
- Base must be < 0.4 ATR (tight)
- Leg2 must be > 1.5 ATR (confirmation)
- Base duration: 3-15 bars

**Acceptance Criteria:**
- [ ] Detects RBR, DBD, RBD, DBR patterns
- [ ] Requires strong legs before/after base
- [ ] Base must be tight consolidation
- [ ] Pattern scoring based on leg strength + base tightness

---

### **ZD-003: Flip Zone Detection**

**Requirement:** Detect when previous resistance becomes support (or vice versa)

**Concept:**
- Old Supply zone breaks up → Becomes Demand zone (flip)
- Old Demand zone breaks down → Becomes Supply zone (flip)
- These are high-probability zones (tested from both sides)

**Algorithm:**
```cpp
struct FlipZone {
    Zone original_zone;             // The zone that flipped
    ZoneType original_type;         // SUPPLY or DEMAND
    ZoneType new_type;              // Opposite type after flip
    
    int flip_bar;                   // When flip occurred
    double flip_strength;           // How strong was the break
    bool confirmed;                 // Has price retested flip zone?
    
    // Quality
    int touches_before_flip;        // More touches = stronger flip
    double break_magnitude;         // Size of break (ATR normalized)
};

FlipZone detect_flip_zone(
    const Zone& existing_zone,
    const vector<Bar>& bars,
    int current_index
) {
    // Check if zone was broken
    bool broken = false;
    int break_bar = -1;
    
    for (int i = existing_zone.formation_bar; i <= current_index; i++) {
        if (existing_zone.type == ZoneType::SUPPLY) {
            // Supply zone: check if price broke above
            if (bars[i].close > existing_zone.proximal) {
                broken = true;
                break_bar = i;
                break;
            }
        } else {
            // Demand zone: check if price broke below
            if (bars[i].close < existing_zone.distal) {
                broken = true;
                break_bar = i;
                break;
            }
        }
    }
    
    if (!broken) return {};
    
    // Calculate break strength
    double atr = calculate_atr(bars, 14, break_bar);
    double break_distance = (existing_zone.type == ZoneType::SUPPLY) ?
        (bars[break_bar].close - existing_zone.proximal) :
        (existing_zone.distal - bars[break_bar].close);
    
    double break_strength = break_distance / atr;
    
    // Only valid flip if strong break (> 1.5 ATR)
    if (break_strength < 1.5) return {};
    
    // Create flip zone
    FlipZone flip;
    flip.original_zone = existing_zone;
    flip.original_type = existing_zone.type;
    flip.new_type = (existing_zone.type == ZoneType::SUPPLY) ? 
        ZoneType::DEMAND : ZoneType::SUPPLY;
    flip.flip_bar = break_bar;
    flip.flip_strength = break_strength;
    flip.touches_before_flip = existing_zone.touches;
    flip.break_magnitude = break_distance;
    
    // Check if confirmed (price returned and bounced)
    flip.confirmed = check_flip_confirmation(bars, flip, current_index);
    
    return flip;
}
```

**Criteria:**
- Zone must have been tested 2+ times before flip
- Break must be > 1.5 ATR (decisive break)
- Flip zone is the SAME price level as original zone
- Confirmation: Price returns and bounces within 10 bars

**Acceptance Criteria:**
- [ ] Detects when Supply becomes Demand (and vice versa)
- [ ] Requires strong break to qualify as flip
- [ ] Tracks flip confirmation (retest)
- [ ] Flip zones get bonus quality score

---

### **ZD-004: Order Block Detection**

**Requirement:** Detect the last opposing candle before explosive move (institutional order block)

**Concept:**
- Before strong rally: Last bearish candle = Demand order block
- Before strong drop: Last bullish candle = Supply order block
- Institutions use these candles to accumulate/distribute

**Algorithm:**
```cpp
struct OrderBlock {
    int candle_bar;                 // Index of order block candle
    double high;                    // Order block high
    double low;                     // Order block low
    ZoneType type;                  // DEMAND or SUPPLY
    
    // Quality metrics
    double candle_body_size;        // Size of candle body
    double wick_ratio;              // Wick size vs body
    double move_after_size;         // How strong was move after
    double volume_ratio;            // Volume vs average (if available)
    
    bool valid;                     // Meets criteria
};

OrderBlock detect_order_block(
    const vector<Bar>& bars,
    int explosive_move_start,      // Bar where explosive move began
    ZoneType expected_type          // DEMAND or SUPPLY
) {
    OrderBlock ob;
    
    // Look for last opposing candle before move
    for (int i = explosive_move_start - 1; i >= explosive_move_start - 10; i--) {
        if (i < 0) break;
        
        bool is_opposing = false;
        
        if (expected_type == ZoneType::DEMAND) {
            // For demand, look for last bearish candle
            is_opposing = (bars[i].close < bars[i].open);
        } else {
            // For supply, look for last bullish candle
            is_opposing = (bars[i].close > bars[i].open);
        }
        
        if (is_opposing) {
            // Found the order block
            ob.candle_bar = i;
            ob.high = bars[i].high;
            ob.low = bars[i].low;
            ob.type = expected_type;
            
            // Calculate quality metrics
            double body = abs(bars[i].close - bars[i].open);
            double range = bars[i].high - bars[i].low;
            ob.candle_body_size = body;
            ob.wick_ratio = (range - body) / body;
            
            // Measure move after
            double move_after = 0;
            for (int j = i + 1; j < min(i + 10, (int)bars.size()); j++) {
                if (expected_type == ZoneType::DEMAND) {
                    move_after = max(move_after, bars[j].high - bars[i].low);
                } else {
                    move_after = max(move_after, bars[i].high - bars[j].low);
                }
            }
            ob.move_after_size = move_after;
            
            // Validate
            double atr = calculate_atr(bars, 14, i);
            ob.valid = (move_after > 1.5 * atr) && (body > 0.3 * atr);
            
            break;
        }
    }
    
    return ob;
}
```

**Criteria:**
- Must be last opposing candle before move
- Move after must be > 1.5 ATR (strong move)
- Candle body must be > 0.3 ATR (meaningful size)
- Order block = entire candle range (high to low)

**Acceptance Criteria:**
- [ ] Detects order blocks before explosive moves
- [ ] Uses entire candle range as zone
- [ ] Validates with move strength after
- [ ] Integrates with other zone types (can coexist)

---

## 📊 **ZONE QUALITY SCORING REQUIREMENTS**

### **ZQS-001: Zone Formation Quality (0-100 points)**

**Requirement:** Score based on HOW the zone was formed

**Components:**

#### **1. Consolidation Tightness (0-30 points)**

```cpp
double calculate_tightness_score(const Zone& zone) {
    // Measure range vs ATR
    double range = zone.proximal - zone.distal;
    double atr = zone.atr_at_formation;
    double range_atr_ratio = range / atr;
    
    // Scoring
    if (range_atr_ratio <= 0.2) {
        return 30.0;  // Extremely tight (institutional)
    } else if (range_atr_ratio <= 0.3) {
        return 25.0;  // Very tight
    } else if (range_atr_ratio <= 0.4) {
        return 20.0;  // Tight
    } else if (range_atr_ratio <= 0.5) {
        return 15.0;  // Acceptable
    } else if (range_atr_ratio <= 0.7) {
        return 10.0;  // Loose
    } else {
        return 0.0;   // Too wide (not a zone)
    }
}
```

**Why:** Tighter consolidation = stronger institutional presence

---

#### **2. Pattern Type (0-20 points)**

```cpp
double calculate_pattern_score(const Zone& zone) {
    switch (zone.pattern_type) {
        case PatternType::RBR:
        case PatternType::DBD:
            return 20.0;  // Classic rally/drop base patterns
        
        case PatternType::RBD:
        case PatternType::DBR:
            return 18.0;  // Reversal patterns (slightly weaker)
        
        case PatternType::FLIP_ZONE:
            return 25.0;  // Flip zones (tested both sides) - BONUS
        
        case PatternType::ORDER_BLOCK:
            return 18.0;  // Single candle zones
        
        case PatternType::CONSOLIDATION:
            return 15.0;  // Generic consolidation
        
        default:
            return 10.0;
    }
}
```

**Why:** Some patterns have higher probability than others

---

#### **3. Departure Strength (0-25 points)**

```cpp
double calculate_departure_score(const Zone& zone) {
    double score = 0.0;
    
    // Speed of departure (0-10 points)
    double speed_atr = zone.departure.initial_speed / zone.atr_at_formation;
    if (speed_atr > 2.0) {
        score += 10.0;  // Explosive (>2 ATR per bar)
    } else if (speed_atr > 1.5) {
        score += 8.0;
    } else if (speed_atr > 1.0) {
        score += 5.0;
    } else {
        score += 2.0;   // Weak departure
    }
    
    // Clean departure (0-8 points)
    if (zone.departure.clean_move) {
        score += 8.0;   // No whipsaw
    } else {
        score += 2.0;   // Choppy
    }
    
    // Distance traveled (0-7 points)
    double excursion_atr = zone.departure.max_excursion / zone.atr_at_formation;
    if (excursion_atr > 5.0) {
        score += 7.0;   // Huge move (>5 ATR)
    } else if (excursion_atr > 3.0) {
        score += 5.0;
    } else if (excursion_atr > 2.0) {
        score += 3.0;
    }
    
    return score;
}
```

**Why:** Strong, clean departure indicates institutional participation

---

#### **4. Volume Profile (0-15 points) - Optional**

```cpp
double calculate_volume_score(const Zone& zone) {
    // Only if volume data available
    if (!zone.volume_data.available) return 7.5;  // Neutral score
    
    double score = 0.0;
    
    // High volume during formation (0-8 points)
    double vol_ratio = zone.volume_data.formation_volume / 
                      zone.volume_data.avg_volume;
    if (vol_ratio > 2.0) {
        score += 8.0;   // 2x normal volume
    } else if (vol_ratio > 1.5) {
        score += 6.0;
    } else if (vol_ratio > 1.2) {
        score += 4.0;
    } else {
        score += 1.0;   // Low volume (weak zone)
    }
    
    // Volume at departure (0-7 points)
    double departure_vol_ratio = zone.volume_data.departure_volume /
                                zone.volume_data.avg_volume;
    if (departure_vol_ratio > 2.5) {
        score += 7.0;   // Institutional exit
    } else if (departure_vol_ratio > 1.8) {
        score += 5.0;
    } else if (departure_vol_ratio > 1.3) {
        score += 3.0;
    }
    
    return score;
}
```

**Why:** High volume confirms institutional activity

---

#### **5. Multi-Timeframe Confluence (0-10 points)**

```cpp
double calculate_mtf_score(const Zone& zone) {
    int confluent_timeframes = 0;
    
    // Check if zone aligns with higher timeframe zones
    if (zone.mtf_data.daily_zone_nearby) {
        confluent_timeframes++;
    }
    if (zone.mtf_data.weekly_zone_nearby) {
        confluent_timeframes += 2;  // Weekly = stronger
    }
    if (zone.mtf_data.monthly_zone_nearby) {
        confluent_timeframes += 3;  // Monthly = strongest
    }
    
    // Scoring
    return min(10.0, confluent_timeframes * 2.5);
}
```

**Why:** Multiple timeframe confluence = stronger level

---

**Total Zone Formation Quality: 0-100 points**

```
Tightness:    0-30 points
Pattern Type: 0-20 points  
Departure:    0-25 points
Volume:       0-15 points
MTF:          0-10 points
────────────────────────────
TOTAL:        0-100 points
```

**Threshold:**
- ≥ 70: Trade this zone
- 60-69: Marginal (need perfect entry conditions)
- < 60: Skip zone

---

### **ZQS-002: Zone Historical Performance (0-100 points)**

**Requirement:** Score based on zone's track record

**Components:**

#### **1. Touch History (0-35 points)**

```cpp
double calculate_touch_history_score(const Zone& zone) {
    if (zone.touches == 0) {
        return 35.0;  // Fresh zone (untested) = highest probability
    }
    
    // Calculate success rate of previous touches
    double success_rate = (double)zone.successful_bounces / zone.touches;
    
    // Decay score with each touch
    double base_score = 35.0;
    double decay_factor = 0.75;  // 25% decay per touch
    double touch_penalty = base_score * pow(decay_factor, zone.touches - 1);
    
    // Adjust by success rate
    double score = touch_penalty * success_rate;
    
    // Minimum score
    return max(5.0, score);
}
```

**Logic:**
- First touch: 35 points (fresh)
- Second touch (if held): 26 points (75% of original)
- Third touch (if held): 20 points
- After 4-5 touches: Zone becomes weak (< 10 points)

---

#### **2. Zone Age (0-25 points)**

```cpp
double calculate_age_score(const Zone& zone, int current_bar) {
    int bars_since_formation = current_bar - zone.formation_bar;
    
    // For intraday trading (bars = minutes/hours)
    if (zone.timeframe <= Timeframe::H4) {
        int days_old = bars_since_formation / bars_per_day(zone.timeframe);
        
        if (days_old <= 3) {
            return 25.0;  // Very fresh
        } else if (days_old <= 7) {
            return 20.0;  // Fresh
        } else if (days_old <= 14) {
            return 15.0;  // Recent
        } else if (days_old <= 30) {
            return 8.0;   // Aging
        } else {
            return 0.0;   // Stale
        }
    }
    
    // For swing trading (bars = days/weeks)
    else {
        if (bars_since_formation <= 20) {
            return 25.0;  // Fresh (< 20 bars)
        } else if (bars_since_formation <= 50) {
            return 18.0;
        } else if (bars_since_formation <= 100) {
            return 10.0;
        } else {
            return 0.0;   // Too old
        }
    }
}
```

**Why:** Recent zones reflect current market structure

---

#### **3. Distance from Current Price (0-20 points)**

```cpp
double calculate_distance_score(const Zone& zone, double current_price) {
    // Calculate distance in ATR terms
    double distance = abs(current_price - 
        ((zone.type == ZoneType::DEMAND) ? zone.proximal : zone.distal));
    
    double atr = zone.current_atr;
    double distance_atr = distance / atr;
    
    // Scoring
    if (distance_atr <= 1.0) {
        return 20.0;  // Very close (< 1 ATR)
    } else if (distance_atr <= 2.0) {
        return 17.0;  // Close
    } else if (distance_atr <= 3.0) {
        return 14.0;  // Moderate distance
    } else if (distance_atr <= 5.0) {
        return 10.0;  // Far
    } else if (distance_atr <= 8.0) {
        return 5.0;   // Very far
    } else {
        return 0.0;   // Too far (irrelevant)
    }
}
```

**Why:** Closer zones are more immediately relevant

---

#### **4. Zone State (0-20 points)**

```cpp
double calculate_state_score(const Zone& zone) {
    switch (zone.state) {
        case ZoneState::FRESH:
            return 20.0;  // Never tested (highest probability)
        
        case ZoneState::TESTED:
            return 17.0;  // Tested once and held (proven)
        
        case ZoneState::WEAKENING:
            return 10.0;  // Multiple tests (losing strength)
        
        case ZoneState::VIOLATED:
            return 0.0;   // Broken (invalid)
        
        case ZoneState::EXPIRED:
            return 0.0;   // Too old/far (irrelevant)
        
        default:
            return 5.0;
    }
}
```

---

**Total Zone Historical Score: 0-100 points**

```
Touch History: 0-35 points
Age:           0-25 points
Distance:      0-20 points
State:         0-20 points
──────────────────────────
TOTAL:         0-100 points
```

---

### **ZQS-003: Final Zone Quality Score**

**Combination:**

```cpp
double calculate_final_zone_quality(const Zone& zone) {
    double formation_score = calculate_formation_quality(zone);  // 0-100
    double historical_score = calculate_historical_quality(zone); // 0-100
    
    // Weighted average (60% formation, 40% historical)
    double final_score = (formation_score * 0.60) + 
                        (historical_score * 0.40);
    
    return final_score;  // 0-100
}
```

**Why this weighting:**
- Formation quality (60%): HOW zone formed matters most
- Historical performance (40%): Track record provides context

**Threshold:**
```
≥ 75: EXCELLENT zone (trade aggressively)
≥ 70: GOOD zone (standard trade)
≥ 60: FAIR zone (need perfect entry context)
< 60: SKIP (poor quality)
```

---

## 🎯 **ENTRY VALIDATION REQUIREMENTS**

### **EV-001: Trend Alignment (0-100 points)**

**Requirement:** Entry must align with prevailing trend

**Components:**

#### **1. EMA Alignment (0-40 points)**

```cpp
double calculate_ema_alignment_score(
    const Zone& zone,
    const MarketContext& context
) {
    // Calculate EMA trend
    bool ema_uptrend = (context.ema_50 > context.ema_200);
    double ema_separation = abs(context.ema_50 - context.ema_200) / 
                           context.ema_200 * 100.0;
    
    // Check alignment
    bool aligned = (zone.type == ZoneType::DEMAND && ema_uptrend) ||
                   (zone.type == ZoneType::SUPPLY && !ema_uptrend);
    
    if (aligned) {
        // Perfect alignment - score by trend strength
        if (ema_separation > 2.0) {
            return 40.0;  // Very strong trend
        } else if (ema_separation > 1.0) {
            return 35.0;  // Strong trend
        } else if (ema_separation > 0.5) {
            return 30.0;  // Moderate trend
        } else if (ema_separation > 0.2) {
            return 25.0;  // Weak trend
        } else {
            return 20.0;  // Very weak (ranging)
        }
    } else if (ema_separation < 0.3) {
        // Ranging market (both directions OK)
        return 20.0;
    } else {
        // Counter-trend
        return 5.0;  // Heavy penalty
    }
}
```

---

#### **2. Price Structure (0-30 points)**

```cpp
double calculate_structure_score(
    const Zone& zone,
    const MarketContext& context
) {
    double score = 0.0;
    
    // Higher highs and higher lows (uptrend)
    if (zone.type == ZoneType::DEMAND) {
        if (context.making_higher_highs && context.making_higher_lows) {
            score += 30.0;  // Perfect uptrend structure
        } else if (context.making_higher_lows) {
            score += 20.0;  // Accumulation phase
        } else {
            score += 5.0;   // Weak structure
        }
    }
    
    // Lower highs and lower lows (downtrend)
    else {
        if (context.making_lower_highs && context.making_lower_lows) {
            score += 30.0;  // Perfect downtrend structure
        } else if (context.making_lower_highs) {
            score += 20.0;  // Distribution phase
        } else {
            score += 5.0;   // Weak structure
        }
    }
    
    return score;
}
```

---

#### **3. Multi-Timeframe Trend (0-30 points)**

```cpp
double calculate_mtf_trend_score(
    const Zone& zone,
    const MultiTimeframeContext& mtf
) {
    int aligned_timeframes = 0;
    
    // Check each higher timeframe
    if (zone.type == ZoneType::DEMAND) {
        if (mtf.h4_uptrend) aligned_timeframes++;
        if (mtf.daily_uptrend) aligned_timeframes += 2;
        if (mtf.weekly_uptrend) aligned_timeframes += 3;
    } else {
        if (mtf.h4_downtrend) aligned_timeframes++;
        if (mtf.daily_downtrend) aligned_timeframes += 2;
        if (mtf.weekly_downtrend) aligned_timeframes += 3;
    }
    
    // Score based on alignment
    return min(30.0, aligned_timeframes * 5.0);
}
```

---

**Total Trend Alignment: 0-100 points**

```
EMA Alignment:  0-40 points
Price Structure: 0-30 points
MTF Trend:      0-30 points
─────────────────────────────
TOTAL:          0-100 points
```

**Threshold:**
- ≥ 70: Strong trend alignment (take trade)
- 60-69: Moderate alignment (acceptable)
- < 60: Weak/counter-trend (skip)

---

### **EV-002: Momentum State (0-100 points)**

**Requirement:** Enter when momentum favors reversal

#### **1. RSI Position (0-40 points)**

```cpp
double calculate_rsi_score(
    const Zone& zone,
    double current_rsi
) {
    if (zone.type == ZoneType::DEMAND) {
        // For LONG: Want RSI low (oversold)
        if (current_rsi < 25) {
            return 40.0;  // Deeply oversold
        } else if (current_rsi < 30) {
            return 36.0;  // Oversold
        } else if (current_rsi < 35) {
            return 32.0;  // Slightly oversold
        } else if (current_rsi < 40) {
            return 25.0;  // Pullback
        } else if (current_rsi < 45) {
            return 15.0;  // Weak
        } else if (current_rsi < 50) {
            return 5.0;   // Neutral
        } else {
            return 0.0;   // Overbought (bad for LONG)
        }
    } else {
        // For SHORT: Want RSI high (overbought)
        if (current_rsi > 75) {
            return 40.0;  // Deeply overbought
        } else if (current_rsi > 70) {
            return 36.0;  // Overbought
        } else if (current_rsi > 65) {
            return 32.0;  // Slightly overbought
        } else if (current_rsi > 60) {
            return 25.0;  // Pullback
        } else if (current_rsi > 55) {
            return 15.0;  // Weak
        } else if (current_rsi > 50) {
            return 5.0;   // Neutral
        } else {
            return 0.0;   // Oversold (bad for SHORT)
        }
    }
}
```

---

#### **2. Stochastic Position (0-30 points)**

```cpp
double calculate_stochastic_score(
    const Zone& zone,
    double stoch_k,
    double stoch_d
) {
    if (zone.type == ZoneType::DEMAND) {
        // For LONG: Want stochastic low + turning up
        bool oversold = (stoch_k < 20);
        bool turning_up = (stoch_k > stoch_d);
        
        if (oversold && turning_up) {
            return 30.0;  // Perfect (oversold + bullish cross)
        } else if (oversold) {
            return 20.0;  // Oversold but not turning yet
        } else if (turning_up && stoch_k < 40) {
            return 15.0;  // Turning up from moderate level
        } else {
            return 5.0;   // Not favorable
        }
    } else {
        // For SHORT: Want stochastic high + turning down
        bool overbought = (stoch_k > 80);
        bool turning_down = (stoch_k < stoch_d);
        
        if (overbought && turning_down) {
            return 30.0;  // Perfect
        } else if (overbought) {
            return 20.0;
        } else if (turning_down && stoch_k > 60) {
            return 15.0;
        } else {
            return 5.0;
        }
    }
}
```

---

#### **3. MACD State (0-30 points)**

```cpp
double calculate_macd_score(
    const Zone& zone,
    const MACDData& macd
) {
    if (zone.type == ZoneType::DEMAND) {
        // For LONG: Want negative histogram (decline) + turning up
        bool negative = (macd.histogram < 0);
        bool deep_negative = (macd.histogram < -2.0);
        bool turning_up = (macd.histogram > macd.prev_histogram);
        bool bullish_cross = (macd.line > macd.signal);
        
        if (deep_negative && turning_up && bullish_cross) {
            return 30.0;  // Perfect setup
        } else if (negative && turning_up) {
            return 24.0;
        } else if (negative) {
            return 18.0;
        } else if (turning_up) {
            return 12.0;
        } else {
            return 5.0;
        }
    } else {
        // Similar for SHORT (positive histogram, turning down)
        // ...
    }
}
```

---

**Total Momentum Score: 0-100 points**

```
RSI:        0-40 points
Stochastic: 0-30 points
MACD:       0-30 points
────────────────────────
TOTAL:      0-100 points
```

---

### **EV-003: Entry Precision (0-100 points)**

**Requirement:** Enter at optimal location within zone

#### **1. Price Position in Zone (0-40 points)**

```cpp
double calculate_price_position_score(
    const Zone& zone,
    double current_price
) {
    double zone_height = zone.proximal - zone.distal;
    double price_in_zone = 0.0;
    
    if (zone.type == ZoneType::DEMAND) {
        // For LONG: Want price at bottom of zone (near distal)
        price_in_zone = (current_price - zone.distal) / zone_height;
        
        if (price_in_zone <= 0.2) {
            return 40.0;  // Bottom 20% of zone (best)
        } else if (price_in_zone <= 0.4) {
            return 32.0;  // Bottom 40%
        } else if (price_in_zone <= 0.6) {
            return 24.0;  // Middle
        } else if (price_in_zone <= 0.8) {
            return 16.0;  // Upper zone
        } else {
            return 8.0;   // Top of zone (worst for LONG)
        }
    } else {
        // For SHORT: Want price at top of zone (near proximal)
        price_in_zone = (zone.proximal - current_price) / zone_height;
        
        if (price_in_zone <= 0.2) {
            return 40.0;  // Top 20% of zone (best)
        } else if (price_in_zone <= 0.4) {
            return 32.0;
        } else if (price_in_zone <= 0.6) {
            return 24.0;
        } else if (price_in_zone <= 0.8) {
            return 16.0;
        } else {
            return 8.0;   // Bottom of zone (worst for SHORT)
        }
    }
}
```

---

#### **2. Rejection Candle (0-30 points)**

```cpp
double calculate_rejection_score(
    const Zone& zone,
    const Bar& current_bar
) {
    double range = current_bar.high - current_bar.low;
    double body = abs(current_bar.close - current_bar.open);
    
    if (zone.type == ZoneType::DEMAND) {
        // For LONG: Want bullish candle with long lower wick
        double lower_wick = current_bar.close - current_bar.low;
        double wick_ratio = lower_wick / range;
        
        bool bullish = (current_bar.close > current_bar.open);
        bool touched_zone = (current_bar.low <= zone.distal + 5);
        
        if (bullish && touched_zone && wick_ratio > 0.6) {
            return 30.0;  // Strong rejection (60%+ wick)
        } else if (bullish && touched_zone && wick_ratio > 0.4) {
            return 22.0;  // Moderate rejection
        } else if (bullish && touched_zone) {
            return 15.0;  // Weak rejection
        } else {
            return 5.0;   // No clear rejection
        }
    } else {
        // Similar for SHORT (bearish candle, upper wick)
        // ...
    }
}
```

---

#### **3. Volume Confirmation (0-30 points)**

```cpp
double calculate_volume_confirmation_score(
    const Bar& current_bar,
    double avg_volume
) {
    if (avg_volume == 0) return 15.0;  // Neutral if no volume data
    
    double vol_ratio = current_bar.volume / avg_volume;
    
    if (vol_ratio > 2.0) {
        return 30.0;  // High volume (2x average)
    } else if (vol_ratio > 1.5) {
        return 24.0;
    } else if (vol_ratio > 1.2) {
        return 18.0;
    } else if (vol_ratio > 0.8) {
        return 12.0;  // Normal volume
    } else {
        return 5.0;   // Low volume (weak)
    }
}
```

---

**Total Entry Precision: 0-100 points**

```
Price Position: 0-40 points
Rejection:      0-30 points
Volume:         0-30 points
────────────────────────────
TOTAL:          0-100 points
```

---

### **EV-004: Final Entry Validation Score**

**Combination:**

```cpp
double calculate_final_entry_score(
    const Zone& zone,
    const MarketContext& context,
    const Bar& current_bar
) {
    double trend_score = calculate_trend_alignment_score(zone, context);
    double momentum_score = calculate_momentum_score(zone, context);
    double precision_score = calculate_entry_precision_score(zone, current_bar);
    
    // Weighted average
    double final_score = (trend_score * 0.40) +      // 40% weight
                        (momentum_score * 0.35) +    // 35% weight
                        (precision_score * 0.25);    // 25% weight
    
    return final_score;  // 0-100
}
```

**Thresholds:**
```
≥ 75: EXCELLENT entry (take trade aggressively)
≥ 65: GOOD entry (standard trade)
≥ 55: FAIR entry (reduce position size)
< 55: SKIP (poor entry conditions)
```

---

## 🎯 **TARGET & RISK MANAGEMENT REQUIREMENTS**

### **TRM-001: Structure-Based Target Calculation**

**Requirement:** Targets must be based on market structure, not fixed R:R

**Algorithm:**

```cpp
struct TargetCalculation {
    double target_price;
    double target_distance;
    double structure_rr;            // R:R to structure target
    
    // Obstacles
    vector<double> resistance_levels;   // For LONG
    vector<double> support_levels;      // For SHORT
    double nearest_obstacle;
    int obstacles_to_target;
    
    // Quality
    bool clear_path;                // No major obstacles
    bool high_probability_target;   // R:R ≥ 2.5 + clear path
};

TargetCalculation calculate_structure_target(
    const Zone& zone,
    const vector<Bar>& bars,
    const vector<Zone>& all_zones,
    int current_index
) {
    TargetCalculation target;
    
    if (zone.type == ZoneType::DEMAND) {
        // For LONG: Find resistance above
        
        // 1. Find swing highs
        vector<double> swing_highs = find_swing_highs_above(
            zone.proximal, bars, current_index, lookback=100);
        
        // 2. Find supply zones above
        vector<double> supply_zones = find_supply_zones_above(
            zone.proximal, all_zones);
        
        // 3. Calculate Fibonacci extension
        double fib_target = calculate_fib_extension(
            zone, bars, current_index, level=1.618);
        
        // 4. Combine all potential targets
        vector<double> candidates = swing_highs;
        candidates.insert(candidates.end(), 
            supply_zones.begin(), supply_zones.end());
        if (fib_target > 0) candidates.push_back(fib_target);
        
        // 5. Filter: Must be at least 2:1 R:R
        double stop_distance = zone.proximal - zone.distal;
        double min_target = zone.proximal + (2.0 * stop_distance);
        
        candidates.erase(
            std::remove_if(candidates.begin(), candidates.end(),
                [min_target](double t) { return t < min_target; }),
            candidates.end()
        );
        
        if (candidates.empty()) {
            // No valid structure target - use fixed 2:1
            target.target_price = min_target;
            target.structure_rr = 2.0;
            target.high_probability_target = false;
        } else {
            // Take NEAREST valid target
            std::sort(candidates.begin(), candidates.end());
            target.target_price = candidates[0];
            
            // Add buffer (don't target exact level)
            double atr = calculate_atr(bars, 14, current_index);
            target.target_price -= (0.3 * atr);
            
            // Calculate R:R
            target.target_distance = target.target_price - zone.proximal;
            target.structure_rr = target.target_distance / stop_distance;
            
            // Check for clear path
            target.resistance_levels = find_resistance_between(
                zone.proximal, target.target_price, bars, all_zones);
            target.obstacles_to_target = target.resistance_levels.size();
            target.clear_path = (target.obstacles_to_target <= 1);
            
            // High probability target
            target.high_probability_target = 
                (target.structure_rr >= 2.5) && target.clear_path;
        }
    } else {
        // Similar for SHORT (find support below)
        // ...
    }
    
    return target;
}
```

**Requirements:**
- [ ] Identifies all swing highs/lows in path
- [ ] Identifies all opposing zones in path
- [ ] Calculates Fibonacci extensions
- [ ] Takes nearest obstacle as target
- [ ] Buffers before exact level (0.3 ATR)
- [ ] Minimum 2:1 R:R required
- [ ] Flags "clear path" if ≤1 obstacle

---

### **TRM-002: Dynamic Stop Loss Calculation**

**Requirement:** Stop loss based on zone structure + volatility

```cpp
double calculate_stop_loss(
    const Zone& zone,
    const vector<Bar>& bars,
    int current_index
) {
    double atr = calculate_atr(bars, 14, current_index);
    double base_stop;
    
    if (zone.type == ZoneType::DEMAND) {
        // For LONG: Stop below zone distal
        base_stop = zone.distal;
        
        // Add buffer based on zone height and ATR
        double zone_height = zone.proximal - zone.distal;
        double buffer = max(zone_height * 0.1, atr * 0.3);
        
        base_stop -= buffer;
        
        // Find recent swing low (don't put stop above it)
        double swing_low = find_recent_swing_low(bars, current_index, lookback=20);
        if (swing_low > 0 && base_stop > swing_low) {
            base_stop = swing_low - (atr * 0.2);
        }
    } else {
        // For SHORT: Stop above zone proximal
        base_stop = zone.proximal;
        
        double zone_height = zone.proximal - zone.distal;
        double buffer = max(zone_height * 0.1, atr * 0.3);
        
        base_stop += buffer;
        
        // Find recent swing high
        double swing_high = find_recent_swing_high(bars, current_index, lookback=20);
        if (swing_high > 0 && base_stop < swing_high) {
            base_stop = swing_high + (atr * 0.2);
        }
    }
    
    return base_stop;
}
```

**Requirements:**
- [ ] Stop beyond zone boundary (distal for LONG, proximal for SHORT)
- [ ] Buffer of 10% zone height OR 0.3 ATR (whichever larger)
- [ ] Respects recent swing levels
- [ ] Typical stop: 1.5-2.5 ATR from entry

---

### **TRM-003: Position Sizing Based on Zone Quality**

**Requirement:** Risk more on high-quality setups, less on marginal ones

```cpp
double calculate_position_size(
    double account_balance,
    double risk_per_trade_pct,      // e.g., 1.0% or 2.0%
    double entry_price,
    double stop_loss,
    const Zone& zone,
    double entry_score
) {
    // Base risk amount
    double base_risk = account_balance * (risk_per_trade_pct / 100.0);
    
    // Adjust based on setup quality
    double quality_multiplier = 1.0;
    
    // Zone quality
    if (zone.quality_score >= 85) {
        quality_multiplier *= 1.5;  // Excellent zone
    } else if (zone.quality_score >= 75) {
        quality_multiplier *= 1.2;  // Good zone
    } else if (zone.quality_score < 65) {
        quality_multiplier *= 0.7;  // Marginal zone
    }
    
    // Entry quality
    if (entry_score >= 80) {
        quality_multiplier *= 1.3;  // Excellent entry
    } else if (entry_score >= 70) {
        quality_multiplier *= 1.1;  // Good entry
    } else if (entry_score < 60) {
        quality_multiplier *= 0.8;  // Marginal entry
    }
    
    // Combined zone + entry (both excellent)
    if (zone.quality_score >= 85 && entry_score >= 80) {
        quality_multiplier *= 1.2;  // Bonus for perfect setup
    }
    
    // Cap multiplier
    quality_multiplier = min(quality_multiplier, 2.0);  // Max 2x risk
    quality_multiplier = max(quality_multiplier, 0.5);  // Min 0.5x risk
    
    // Calculate position size
    double adjusted_risk = base_risk * quality_multiplier;
    double stop_distance = abs(entry_price - stop_loss);
    double position_size = adjusted_risk / stop_distance;
    
    return position_size;
}
```

**Requirements:**
- [ ] Base risk: 1-2% of account per trade
- [ ] Multiply risk by zone quality (0.7x to 1.5x)
- [ ] Multiply risk by entry quality (0.8x to 1.3x)
- [ ] Bonus multiplier for perfect setups (1.2x)
- [ ] Cap at 2x base risk, floor at 0.5x base risk

---

### **TRM-004: Trailing Stop Management**

**Requirement:** Trail stops to protect profits, based on market structure

```cpp
struct TrailingStopConfig {
    double activation_rr;           // Start trailing at X:1 (e.g., 0.6)
    double trail_structure;         // Trail to swing lows/highs
    double trail_atr_multiple;      // Or trail by X * ATR (e.g., 1.5)
    bool use_structure;             // Prefer structure over ATR
};

double calculate_trailing_stop(
    const Position& position,
    const vector<Bar>& bars,
    int current_index,
    const TrailingStopConfig& config
) {
    double current_price = bars[current_index].close;
    double entry_price = position.entry_price;
    double stop_loss = position.stop_loss;
    double risk = abs(entry_price - stop_loss);
    
    // Check if profit reached activation level
    double unrealized_pl = (position.direction == Direction::LONG) ?
        (current_price - entry_price) : (entry_price - current_price);
    
    double rr = unrealized_pl / risk;
    
    if (rr < config.activation_rr) {
        // Not profitable enough to trail yet
        return stop_loss;  // Keep original stop
    }
    
    // Trail the stop
    double new_stop = stop_loss;
    
    if (config.use_structure) {
        // Trail to recent swing level
        if (position.direction == Direction::LONG) {
            double swing_low = find_recent_swing_low(
                bars, current_index, lookback=10);
            
            if (swing_low > new_stop) {
                double atr = calculate_atr(bars, 14, current_index);
                new_stop = swing_low - (atr * 0.2);  // Slightly below swing
            }
        } else {
            double swing_high = find_recent_swing_high(
                bars, current_index, lookback=10);
            
            if (swing_high < new_stop) {
                double atr = calculate_atr(bars, 14, current_index);
                new_stop = swing_high + (atr * 0.2);  // Slightly above swing
            }
        }
    } else {
        // Trail by ATR multiple
        double atr = calculate_atr(bars, 14, current_index);
        double trail_distance = atr * config.trail_atr_multiple;
        
        if (position.direction == Direction::LONG) {
            new_stop = current_price - trail_distance;
        } else {
            new_stop = current_price + trail_distance;
        }
    }
    
    // Never move stop against position
    if (position.direction == Direction::LONG) {
        new_stop = max(new_stop, stop_loss);
    } else {
        new_stop = min(new_stop, stop_loss);
    }
    
    return new_stop;
}
```

**Requirements:**
- [ ] Activate at 0.5-0.7 R:R (configurable)
- [ ] Prefer structure-based trailing (swing levels)
- [ ] Fallback to ATR-based (1.5-2.0 ATR)
- [ ] Never move stop against position
- [ ] Update on every bar

---

## 📊 **MARKET STRUCTURE REQUIREMENTS**

### **MS-001: Swing Point Detection**

**Requirement:** Identify swing highs and swing lows accurately

```cpp
struct SwingPoint {
    int bar_index;
    double price;
    SwingType type;                 // HIGH or LOW
    int strength;                   // How many bars each side
    int touches;                    // How many times tested
    bool broken;                    // Has price broken through?
};

vector<SwingPoint> detect_swing_points(
    const vector<Bar>& bars,
    int current_index,
    int min_strength = 5            // Bars each side to qualify
) {
    vector<SwingPoint> swings;
    
    for (int i = min_strength; i <= current_index - min_strength; i++) {
        // Check for swing high
        bool is_swing_high = true;
        for (int j = i - min_strength; j <= i + min_strength; j++) {
            if (j != i && bars[j].high >= bars[i].high) {
                is_swing_high = false;
                break;
            }
        }
        
        if (is_swing_high) {
            SwingPoint swing;
            swing.bar_index = i;
            swing.price = bars[i].high;
            swing.type = SwingType::HIGH;
            swing.strength = min_strength;
            swing.touches = count_touches(bars, i, current_index, swing.price);
            swing.broken = is_level_broken(bars, i, current_index, swing.price, true);
            
            swings.push_back(swing);
        }
        
        // Check for swing low (similar)
        // ...
    }
    
    return swings;
}
```

**Requirements:**
- [ ] Detects swing highs: highest point with N bars each side lower
- [ ] Detects swing lows: lowest point with N bars each side higher
- [ ] Configurable strength (default: 5 bars)
- [ ] Tracks touches (how many times level was tested)
- [ ] Tracks broken status

---

### **MS-002: Break of Structure (BOS) Detection**

**Requirement:** Identify when market structure breaks (trend change signal)

```cpp
struct BreakOfStructure {
    int break_bar;
    double broken_level;
    SwingType broken_swing;         // Was HIGH or LOW broken?
    Direction new_direction;        // BULLISH or BEARISH
    double break_magnitude;         // Size of break (ATR normalized)
    bool confirmed;                 // Has price held above/below?
};

BreakOfStructure detect_bos(
    const vector<Bar>& bars,
    const vector<SwingPoint>& swings,
    int current_index
) {
    // For bullish BOS: Price breaks above recent swing high
    // For bearish BOS: Price breaks below recent swing low
    
    BreakOfStructure bos;
    
    // Find most recent unbroken swing high
    for (auto it = swings.rbegin(); it != swings.rend(); ++it) {
        if (it->type == SwingType::HIGH && !it->broken) {
            // Check if current price broke above
            if (bars[current_index].close > it->price) {
                bos.break_bar = current_index;
                bos.broken_level = it->price;
                bos.broken_swing = SwingType::HIGH;
                bos.new_direction = Direction::BULLISH;
                
                double atr = calculate_atr(bars, 14, current_index);
                bos.break_magnitude = (bars[current_index].close - it->price) / atr;
                
                // Confirm: Next 3 bars stay above level
                bos.confirmed = check_bos_confirmation(
                    bars, current_index, it->price, Direction::BULLISH);
                
                return bos;
            }
        }
    }
    
    // Similar for bearish BOS (break below swing low)
    // ...
    
    return {};  // No BOS detected
}
```

**Requirements:**
- [ ] Detects when price breaks recent swing high/low
- [ ] Requires > 0.5 ATR break (decisive)
- [ ] Confirms break with 2-3 bars holding beyond level
- [ ] Signals potential trend change

---

### **MS-003: Higher Highs / Higher Lows Tracking**

**Requirement:** Track market structure to determine trend

```cpp
struct MarketStructure {
    bool making_higher_highs;
    bool making_higher_lows;
    bool making_lower_highs;
    bool making_lower_lows;
    
    Direction structure_trend;      // BULLISH, BEARISH, RANGING
    
    vector<double> recent_highs;    // Last 3-5 swing highs
    vector<double> recent_lows;     // Last 3-5 swing lows
};

MarketStructure analyze_market_structure(
    const vector<SwingPoint>& swings,
    int lookback_swings = 5
) {
    MarketStructure structure;
    
    // Get recent swing highs
    vector<SwingPoint> recent_highs_swings;
    for (auto it = swings.rbegin(); it != swings.rend(); ++it) {
        if (it->type == SwingType::HIGH) {
            recent_highs_swings.push_back(*it);
            if (recent_highs_swings.size() >= lookback_swings) break;
        }
    }
    
    // Check if making higher highs
    if (recent_highs_swings.size() >= 2) {
        structure.making_higher_highs = true;
        for (size_t i = 0; i < recent_highs_swings.size() - 1; i++) {
            if (recent_highs_swings[i].price <= recent_highs_swings[i+1].price) {
                structure.making_higher_highs = false;
                break;
            }
        }
    }
    
    // Similar for higher lows, lower highs, lower lows
    // ...
    
    // Determine overall structure
    if (structure.making_higher_highs && structure.making_higher_lows) {
        structure.structure_trend = Direction::BULLISH;
    } else if (structure.making_lower_highs && structure.making_lower_lows) {
        structure.structure_trend = Direction::BEARISH;
    } else {
        structure.structure_trend = Direction::RANGING;
    }
    
    return structure;
}
```

**Requirements:**
- [ ] Tracks last 3-5 swing highs and lows
- [ ] Identifies higher highs / higher lows (uptrend)
- [ ] Identifies lower highs / lower lows (downtrend)
- [ ] Determines structure trend: BULLISH, BEARISH, or RANGING

---

## 🌍 **MULTI-MARKET SUPPORT REQUIREMENTS**

### **MMS-001: Market-Specific Parameters**

**Requirement:** Each market has unique characteristics that must be handled

```cpp
struct MarketParameters {
    // Trading hours
    struct TradingHours {
        TimeOfDay market_open;
        TimeOfDay market_close;
        vector<TimeRange> trading_sessions;  // e.g., Asian, European, US
        vector<DayOfWeek> trading_days;
    };
    TradingHours hours;
    
    // Price specifications
    double tick_size;               // Minimum price movement
    double lot_size;                // Contract/lot size
    int price_decimals;             // Decimal places for display
    string currency;                // Base currency
    
    // Volatility characteristics
    double typical_daily_range_pct; // Expected daily move (%)
    double typical_atr_pct;         // Average ATR as % of price
    
    // Liquidity
    LiquidityLevel liquidity;       // HIGH, MEDIUM, LOW
    double typical_spread_pips;     // Bid-ask spread
    
    // Zone detection parameters (market-adjusted)
    double consolidation_threshold; // Max range for zone (ATR multiple)
    int min_consolidation_bars;
    int max_consolidation_bars;
    
    // Scoring adjustments
    double zone_age_decay_rate;     // How fast zones expire (varies by market)
    double distance_decay_rate;     // How fast zones lose relevance with distance
};

// Example configurations
MarketParameters get_market_params(const Instrument& instrument) {
    if (instrument.market == MarketType::EQUITY && 
        instrument.symbol == "NIFTY") {
        return {
            .hours = {
                .market_open = {9, 15},
                .market_close = {15, 30},
                .trading_sessions = {{9,15, 15,30}},  // Single session
                .trading_days = {MON, TUE, WED, THU, FRI}
            },
            .tick_size = 0.05,
            .lot_size = 75,
            .price_decimals = 2,
            .currency = "INR",
            .typical_daily_range_pct = 1.5,
            .typical_atr_pct = 0.8,
            .liquidity = LiquidityLevel::HIGH,
            .typical_spread_pips = 1,
            .consolidation_threshold = 0.5,
            .min_consolidation_bars = 3,
            .max_consolidation_bars = 20,
            .zone_age_decay_rate = 0.05,  // 5% per day
            .distance_decay_rate = 0.1    // 10% per ATR
        };
    }
    else if (instrument.market == MarketType::FOREX &&
             instrument.symbol == "EURUSD") {
        return {
            .hours = {
                .market_open = {0, 0},   // 24-hour market
                .market_close = {23, 59},
                .trading_sessions = {
                    {2,0, 11,0},   // Asian session
                    {7,0, 16,0},   // European session
                    {12,0, 21,0}   // US session
                },
                .trading_days = {MON, TUE, WED, THU, FRI}
            },
            .tick_size = 0.00001,  // 0.1 pip
            .lot_size = 100000,    // 1 standard lot
            .price_decimals = 5,
            .currency = "USD",
            .typical_daily_range_pct = 0.7,
            .typical_atr_pct = 0.5,
            .liquidity = LiquidityLevel::VERY_HIGH,
            .typical_spread_pips = 0.8,
            .consolidation_threshold = 0.4,  // Tighter for forex
            .min_consolidation_bars = 4,
            .max_consolidation_bars = 30,
            .zone_age_decay_rate = 0.03,  // Slower decay (24hr market)
            .distance_decay_rate = 0.08
        };
    }
    // ... other markets
}
```

**Requirements:**
- [ ] Define parameters for each supported market
- [ ] Adjust zone detection thresholds per market
- [ ] Adjust scoring decay rates per market
- [ ] Handle different trading hours
- [ ] Handle different tick sizes and lot sizes

---

### **MMS-002: Normalized Volatility**

**Requirement:** Compare volatility across markets with different price scales

```cpp
double calculate_normalized_volatility(
    const Instrument& instrument,
    const vector<Bar>& bars,
    int index
) {
    double raw_atr = calculate_atr(bars, 14, index);
    double current_price = bars[index].close;
    
    // Normalize to percentage
    double atr_pct = (raw_atr / current_price) * 100.0;
    
    // Adjust for market's typical volatility
    auto params = get_market_params(instrument);
    double normalized = atr_pct / params.typical_atr_pct;
    
    // Returns: 
    // 1.0 = normal volatility for this market
    // >1.5 = high volatility
    // <0.5 = low volatility
    
    return normalized;
}
```

**Why:** Allows cross-market comparison and consistent zone quality scoring

---

### **MMS-003: Session-Aware Zone Detection**

**Requirement:** Zones formed during high-liquidity sessions are stronger

```cpp
enum TradingSession { ASIAN, EUROPEAN, US, OVERLAP };

TradingSession get_current_session(
    const Instrument& instrument,
    DateTime timestamp
) {
    auto params = get_market_params(instrument);
    TimeOfDay time = timestamp.time_of_day();
    
    for (const auto& session : params.hours.trading_sessions) {
        if (time >= session.start && time < session.end) {
            // Check if overlap (multiple sessions active)
            int active_sessions = count_active_sessions(time, params);
            if (active_sessions > 1) {
                return TradingSession::OVERLAP;
            }
            
            // Determine which session
            if (session.start.hour < 12) {
                return TradingSession::ASIAN;
            } else if (session.start.hour < 16) {
                return TradingSession::EUROPEAN;
            } else {
                return TradingSession::US;
            }
        }
    }
    
    return TradingSession::ASIAN;  // Default
}

double calculate_session_bonus(
    const Zone& zone,
    const Instrument& instrument
) {
    auto session = get_current_session(instrument, zone.formation_time);
    
    // Bonus points for zones formed during high-liquidity periods
    switch (session) {
        case TradingSession::OVERLAP:
            return 10.0;  // Highest volume (session overlap)
        case TradingSession::EUROPEAN:
        case TradingSession::US:
            return 7.0;   // High volume sessions
        case TradingSession::ASIAN:
            return 3.0;   // Lower volume (for forex)
        default:
            return 0.0;
    }
}
```

**Requirements:**
- [ ] Identify which session zone was formed in
- [ ] Bonus score for overlap sessions (highest liquidity)
- [ ] Adjust expectations for low-liquidity sessions
- [ ] Works for 24-hour markets (forex) and single-session (equity)

---

## 📈 **DATA REQUIREMENTS**

### **DR-001: Required Data Fields**

**Minimum Required:**
```cpp
struct Bar {
    DateTime timestamp;
    double open;
    double high;
    double low;
    double close;
    Timeframe timeframe;      // 1m, 5m, 15m, 1h, 4h, 1d, etc.
};
```

**Highly Recommended:**
```cpp
struct BarWithVolume {
    // ... all Bar fields ...
    uint64_t volume;          // Trading volume
    uint64_t tick_count;      // Number of ticks (if available)
};
```

**Optional (for advanced features):**
```cpp
struct BarExtended {
    // ... all BarWithVolume fields ...
    
    double bid;               // Bid price
    double ask;               // Ask price
    double spread;            // Bid-ask spread
    
    uint64_t buy_volume;      // Volume at ask (buying pressure)
    uint64_t sell_volume;     // Volume at bid (selling pressure)
    
    double vwap;              // Volume-weighted average price
    double poc;               // Point of control (max volume price level)
};
```

---

### **DR-002: Data Quality Requirements**

**Requirements:**
- [ ] No gaps in data (all bars present)
- [ ] No invalid prices (high < low, zero prices, etc.)
- [ ] Consistent timeframe (no missing bars)
- [ ] Properly adjusted for splits/dividends (equities)
- [ ] Timezone consistency (all timestamps in same timezone)

**Data Validation:**
```cpp
struct DataValidation {
    bool has_gaps;
    bool has_invalid_bars;
    bool has_incorrect_sequence;
    double data_quality_score;  // 0-100
};

DataValidation validate_data(const vector<Bar>& bars) {
    DataValidation result;
    result.has_gaps = false;
    result.has_invalid_bars = false;
    result.has_incorrect_sequence = false;
    
    for (size_t i = 0; i < bars.size(); i++) {
        // Check for invalid bar
        if (bars[i].high < bars[i].low ||
            bars[i].open <= 0 || bars[i].close <= 0) {
            result.has_invalid_bars = true;
        }
        
        // Check sequence
        if (i > 0) {
            auto expected_time = bars[i-1].timestamp + 
                get_timeframe_duration(bars[i].timeframe);
            
            if (bars[i].timestamp != expected_time) {
                result.has_gaps = true;
            }
            
            if (bars[i].timestamp <= bars[i-1].timestamp) {
                result.has_incorrect_sequence = true;
            }
        }
    }
    
    // Calculate quality score
    result.data_quality_score = 100.0;
    if (result.has_gaps) result.data_quality_score -= 30.0;
    if (result.has_invalid_bars) result.data_quality_score -= 40.0;
    if (result.has_incorrect_sequence) result.data_quality_score -= 30.0;
    
    return result;
}
```

**Requirements:**
- [ ] Validate data before running detection
- [ ] Reject data with quality score < 70
- [ ] Log data quality issues
- [ ] Provide data cleaning utilities

---

## ⚡ **PERFORMANCE REQUIREMENTS**

### **PR-001: Real-Time Processing**

**Requirements:**
- [ ] Process new bar in < 100ms (single instrument)
- [ ] Support up to 50 instruments simultaneously
- [ ] Update all zones in < 500ms per instrument
- [ ] Entry decision in < 50ms

**Optimization Strategies:**
```cpp
// 1. Incremental calculations (don't recalculate everything)
// 2. Cache indicator values
// 3. Use efficient data structures
// 4. Parallel processing for multiple instruments
// 5. Lazy evaluation (calculate only when needed)
```

---

### **PR-002: Memory Management**

**Requirements:**
- [ ] Keep only necessary historical data in memory
- [ ] Archive old zones (>60 days) to disk
- [ ] Limit active zones per instrument (e.g., top 50 by quality)
- [ ] Total memory < 1GB for 50 instruments

---

### **PR-003: Backtesting Performance**

**Requirements:**
- [ ] Backtest 1 year of daily data in < 10 seconds
- [ ] Support parallel backtesting (multiple parameters)
- [ ] Progress reporting during long backtests

---

## 🔧 **EXTENSIBILITY REQUIREMENTS**

### **ER-001: Plugin Architecture**

**Requirement:** Allow custom zone detection algorithms, scoring components, and indicators

```cpp
// Base class for zone detectors
class ZoneDetector {
public:
    virtual vector<Zone> detect_zones(
        const vector<Bar>& bars,
        int start_index,
        int end_index
    ) = 0;
    
    virtual string get_name() const = 0;
    virtual string get_description() const = 0;
};

// User can implement custom detector
class MyCustomDetector : public ZoneDetector {
public:
    vector<Zone> detect_zones(
        const vector<Bar>& bars,
        int start_index,
        int end_index
    ) override {
        // Custom logic here
        // ...
    }
    
    string get_name() const override { return "MyCustom"; }
    string get_description() const override { 
        return "My custom zone detection algorithm"; 
    }
};

// Platform registers and uses it
platform.register_zone_detector(make_unique<MyCustomDetector>());
```

**Requirements:**
- [ ] Plugin interface for zone detectors
- [ ] Plugin interface for scoring components
- [ ] Plugin interface for indicators
- [ ] Plugin interface for entry rules
- [ ] Hot-reload plugins without restart

---

### **ER-002: Configuration System**

**Requirement:** All parameters configurable via files (no hardcoding)

```json
{
  "platform": {
    "version": "1.0.0",
    "default_market": "EQUITY"
  },
  
  "markets": {
    "NIFTY": {
      "market_type": "EQUITY",
      "tick_size": 0.05,
      "lot_size": 75,
      "trading_hours": {
        "open": "09:15",
        "close": "15:30"
      },
      "zone_detection": {
        "consolidation_max_atr": 0.5,
        "min_bars": 3,
        "max_bars": 20
      }
    }
  },
  
  "zone_quality_scoring": {
    "weights": {
      "formation_quality": 0.60,
      "historical_performance": 0.40
    },
    "formation_components": {
      "tightness": 30,
      "pattern_type": 20,
      "departure_strength": 25,
      "volume": 15,
      "mtf_confluence": 10
    },
    "thresholds": {
      "minimum_score": 70,
      "excellent_score": 85
    }
  },
  
  "entry_validation": {
    "weights": {
      "trend_alignment": 0.40,
      "momentum_state": 0.35,
      "entry_precision": 0.25
    },
    "thresholds": {
      "minimum_score": 65,
      "excellent_score": 80
    }
  },
  
  "risk_management": {
    "default_risk_pct": 1.0,
    "max_risk_pct": 2.0,
    "trailing_stop": {
      "activation_rr": 0.6,
      "use_structure": true,
      "atr_multiple": 1.5
    }
  }
}
```

**Requirements:**
- [ ] JSON/YAML configuration files
- [ ] Per-market configuration
- [ ] Live config reload (no restart)
- [ ] Config validation on load
- [ ] Default values for all parameters

---

## 📋 **SUMMARY OF REQUIREMENTS**

### **Critical (Must Have):**

1. ✅ **Universal Market Support** - Works on any market with minimal config
2. ✅ **Multi-Timeframe Analysis** - HTF zones guide LTF entries
3. ✅ **Real-Time Zone Tracking** - All zones updated every bar
4. ✅ **Consolidation Detection** - Core zone detection method
5. ✅ **Two-Stage Scoring** - Zone Quality + Entry Validation (separate)
6. ✅ **Structure-Based Targets** - Not fixed R:R, based on swing levels
7. ✅ **Dynamic Stop Loss** - Based on zone + volatility
8. ✅ **Trailing Stops** - Protect profits with structure

### **Important (Should Have):**

9. ✅ **Rally/Drop Base Detection** - RBR, DBD, RBD, DBR patterns
10. ✅ **Flip Zone Detection** - Previous resistance becomes support
11. ✅ **Order Block Detection** - Last opposing candle method
12. ✅ **Volume Profile** - If data available
13. ✅ **BOS Detection** - Break of structure signals
14. ✅ **Market Structure Tracking** - HH/HL, LH/LL
15. ✅ **Session-Aware Scoring** - Forex sessions matter
16. ✅ **Position Sizing** - Risk more on quality setups

### **Nice to Have (Enhancement):**

17. ✅ **Plugin Architecture** - Custom detectors/scorers
18. ✅ **Configuration System** - All parameters in files
19. ✅ **Data Validation** - Quality checks
20. ✅ **Performance Monitoring** - Metrics and logs

---

## 🎯 **SUCCESS CRITERIA**

### **Platform considered successful if:**

1. **Works across markets** - Same code for NIFTY, EURUSD, BTCUSD
2. **Consistent scoring** - Zone quality score means same thing across markets
3. **Structure-based targets** - No fixed R:R, uses actual market levels
4. **High win rate** - 55-65% win rate achievable
5. **Good R:R** - Average R:R 2.5:1 or better on winning trades
6. **Extensible** - Easy to add new detectors/scorers
7. **Fast** - Real-time processing without lag
8. **Reliable** - No false zones, accurate tracking

---

## 📚 **REFERENCE: SAM SEIDEN'S INSTITUTIONAL APPROACH**

These requirements are based on Sam Seiden's institutional supply/demand methodology:

**Core Principles:**
1. **Banks/institutions move markets** - Their order flow creates supply/demand zones
2. **Tight consolidation = accumulation/distribution** - Institutions build positions
3. **Fresh zones have highest probability** - First touch before orders filled
4. **Context determines success** - Same zone works or fails based on trend/momentum
5. **Multiple timeframes required** - HTF zones are stronger
6. **Structure over indicators** - Market structure (HH/HL) more important than RSI/MACD

**What Makes a Zone "Institutional":**
- Tight range (< 0.5 ATR) during consolidation
- Explosive departure (> 2 ATR move away)
- High volume during formation (if data available)
- Located at swing highs/lows
- Multiple timeframe confluence

**This platform implements these concepts systematically.** 🎯

---

**END OF REQUIREMENTS DOCUMENT**

This is a comprehensive blueprint for building an institutional-grade Supply & Demand trading platform. Ready for implementation! 🚀
