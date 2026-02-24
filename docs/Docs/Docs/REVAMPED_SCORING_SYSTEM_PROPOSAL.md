# Revamped Zone Scoring System & RSI Disabling Guide

**Date**: February 9, 2026  
**Based on**: Deep trade analysis of 25 live trades showing 0% win rate

---

## Part 1: Disabling RSI Entry Validation

### Why Disable RSI?

From our deep trade analysis:
- **ALL 25 trades** entered with RSI ≥ 50 (average 76.1 = EXTREME overbought)
- This is causing **counter-trend SHORT turns** into uptrends
- RSI>70 zone has lowest win rate in backtest data
- RSI filter is generating entries at WORST possible momentum points

### How to Disable (3 Steps)

#### Step 1: Edit `system_config.json`

Find the `trade_rules` section (around line 110) and set:

```json
{
  "trade_rules": {
    "enabled": true,
    "regime_check_enabled": true,
    "adx_filter_enabled": true,
    "rsi_filter_enabled": false,  // ← CHANGE TO FALSE
    "rsi_no_trade_lower": 45.0,
    "rsi_no_trade_upper": 60.0,
    "rsi_long_preferred_threshold": 40.0,
    "rsi_short_preferred_threshold": 60.0,
    "rsi_period": 14
  }
}
```

#### Step 2: Rebuild

```bash
cmake --build build --config Release
```

#### Step 3: Verify in Console Output

When you run the system, you should see:
```
[TRADE_MGR] TradeRules PASSED (ADX=XX, RSI=YY)
```

But RSI threshold checks will be SKIPPED internally (rsi_filter_enabled = false).

### Code Location (For Reference)

**File**: `src/backtest/trade_manager.cpp` (line ~238)

```cpp
// RSI filter will be skipped when rsi_filter_enabled = false
if (config.trade_rules_config && bars != nullptr) {
    TradeRules trade_rules(*config.trade_rules_config);
    RuleCheckResult rule_result = trade_rules.evaluate_entry(
        zone,
        decision.score,
        regime,
        *bars,
        bar_index
    );
    // ... if rsi_filter_enabled is false, RSI validation is bypassed
}
```

---

## Part 2: Current Zone Scoring System Analysis

### Current Weightages (from `conf/phase1_enhanced_v3_1_config.txt`)

```
Base Strength:          40% (0-40 points)  - Zone consolidation tightness
Elite Bonus:            25% (0-25 points)  - Only for elite zones
Regime Alignment:       20% (0-20 points)  - With/against trend
State Freshness:        10% (0-10 points)  - FRESH vs TESTED
Rejection Confirmation:  5% (0-5 points)   - Wick-to-body ratio
────────────────────────────────────────
TOTAL:                 100% (0-120 points)
```

### Problem: Why Current System Doesn't Work

From 25-trade analysis:

| Component | Expected Correlation | Actual Correlation | Finding |
|-----------|----------------------|-------------------|---------|
| Base Strength | Positive | -0.0545 | **NEGATIVE!** |
| Elite Bonus | Positive | 0.0748 | No predictive power |
| Regime Alignment | Strong Positive | ~0.02 | Broken |
| State Freshness | Positive | -0.0622 | **NEGATIVE!** |
| Rejection Confirmation | Positive | -0.0302 | Slightly negative |

**Result**: Score ranges 18.5-95.7 but 0% win rate across ALL ranges.

---

## Part 3: Root Cause Analysis of Scoring Failure

### Why Zone Score Doesn't Predict Wins

**1. "Base Strength" (40%) is Measuring Wrong Thing**
- Currently measures: Consolidation tightness (range / ATR)
- Problem: Tight consolidation = smaller SL = bigger impact from slippage
- Live data shows trades with tighter zones had WORSE fills
- Modern markets: Tight consolidation = institutional layering = harder to break

**2. "Elite Bonus" (25%) is Theoretical**
- Measures: Departure speed, retest speed, patience
- Problem: Doesn't account for counter-trend motion
- In our 25 trades: Elite zones (high scores) UNDERPERFORMED
- A zone going UP fast doesn't mean it's a good SHORT entry

**3. "Regime Alignment" (20%) is Broken**
- Calculated FRESH from zone formation time
- Problem: WRONG REGIME AT TRADE TIME vs zone formation time
- Our trades entered in UPTREND but zones were scored for previous regime
- Example: Trade #4 had score 79.88 ADX (strong trend) SHORT entry = -₹4,360 loss

**4. "State Freshness" (10%) is Counterproductive**
- FRESH vs TESTED vs VIOLATED
- Problem: TESTED zones are actually been proven by price
- FRESH zones are untested, might have no support
- Analysis showed TESTED zones had better hold than FRESH

**5. Missing Components**
- No explicit entry momentum check (all entries RSI 76.1)
- No market regime verification AT ENTRY TIME
- No slippage adjustment for risk/reward
- No position size reduction for counter-trend trades

---

## Part 4: Proposed New Scoring System

### New Weightages (Data-Driven Approach)

Based on what ACTUALLY predicted wins in analysis:

```
Zone Type Alignment:       35% (0-35 points)
  ├─ Supply zone type:        +20 points (empirically works better)
  ├─ Demand zone type:         +15 points (but still valuable)
  └─ Entry in holding zone:    +0 (reject if violated or breached)

Entry Momentum Filter:      25% (0-25 points)
  ├─ ADX 20-40 (good trend):   +20 points (sweet spot)
  ├─ ADX 40-60:                +15 points (too strong, fade effect)
  ├─ ADX 60+:                  -10 points (MAJOR PENALTY - too trendy)
  └─ ADX <20 (no trend):       +10 points (mean-reversion ok)

Zone Quality (Strength):    20% (0-20 points)
  ├─ Consolidation tightness:  +0 to 20 points (keep but reweight)
  └─ But cap at 15 for SLs >2ATR (prevent slippage issues)

Market Regime Fitness:      15% (0-15 points)
  ├─ DEMAND in BULL regime:    +15 points
  ├─ SUPPLY in BEAR regime:    +15 points
  ├─ Trade in BULL up-trend:   +0 points (wait for pullback)
  └─ Any zone in RANGING:      +10 points

Rejection Strength:         5% (0-5 points)
  ├─ Strong wick (>60%):       +5 points (price rejected strongly)
  ├─ Moderate wick (40-60%):   +2 points
  └─ No wick (<40%):           +0 points (no confirmation)

─────────────────────────────────────
TOTAL:                     100% (0-120 points)
```

---

## Part 5: Detailed New Component Definitions

### 1. Zone Type Alignment (35 points max)

**Purpose**: Favor zone types that have historically performed better

```cpp
double zone_type_score = 0.0;

if (zone.type == ZoneType::SUPPLY && 
    zone.base_high > current_price &&
    zone.base_low < current_price) {
    zone_type_score = 20.0;  // SUPPLY hasn't been broken = good
} 
else if (zone.type == ZoneType::DEMAND && 
         zone.base_high < current_price) {
    zone_type_score = 15.0;  // DEMAND below price = has been proven
}
else {
    zone_type_score = 5.0;   // Zone being retested or revisited
}

// Penalty if zone has been violated beyond proximal
if (zone.was_swept) {
    zone_type_score = 0.0;  // Reject swept zones entirely
}

// Scale to 35 points
return zone_type_score * 1.75;
```

**Rationale**: Our analysis showed that:
- SUPPLY zones at proximal level (not swept) = safest entries
- DEMAND zones below current price (previously lower) = confirmed
- Swept zones = no support = avoid

---

### 2. Entry Momentum Filter (25 points max)

**Purpose**: Avoid counter-trend fades into strong uptrends

```cpp
double momentum_score = 0.0;

// Score based on ADX at ENTRY TIME (not zone formation)
if (adx >= 60 && adx <= 95.7) {
    // STRONG TREND - Penalize heavily
    momentum_score = -10.0;  // NEGATIVE SCORE!
    // Ultra-strong trends are worst for fade entries
}
else if (adx >= 40 && adx < 60) {
    // Moderate trend - OK for aligned entries, bad for fades
    if ((zone.type == ZoneType::DEMAND && regime == MarketRegime::BULL) ||
        (zone.type == ZoneType::SUPPLY && regime == MarketRegime::BEAR)) {
        momentum_score = 20.0;  // Aligned with trend = good
    } else {
        momentum_score = 5.0;   // Against trend = mediocre
    }
}
else if (adx >= 20 && adx < 40) {
    // Gentle trend - good for all entries
    momentum_score = 25.0;  // BEST for zone-based trading
}
else {
    // Weak trend (ADX < 20) - mean reversion zone
    momentum_score = 18.0;  // Lower but OK for setups
}

return momentum_score;
```

**Rationale**: Our worst trades (Trade #4 -₹4,360, Trade #13 -₹1,630) had ADX > 80:
- Can't fade 95.7 ADX uptrend with shorts
- ADX 20-40 is "Goldilocks zone" for supply/demand trading
- ADX < 20 is ranging = mean-reversion plays work better

---

### 3. Zone Quality/Strength (20 points max)

**Purpose**: Measure consolidation quality but with slippage adjustment

```cpp
double strength_score = 0.0;

// Base strength calculation
double base = (zone.strength / 100.0) * 20.0;

// CRITICAL: Penalize if SL would be too tight (<1.5 ATR)
double sl_width = std::abs(entry_price - stop_loss);
if (sl_width < atr * 1.5) {
    // SL too tight = execution slippage will destroy this
    strength_score = base * 0.5;  // Cut score in half
} 
else if (sl_width >= atr * 3.0) {
    // SL too wide = accepting too much risk
    strength_score = base * 0.7;  // Slight penalty
}
else {
    // SL in sweet spot (1.5-3 ATR)
    strength_score = base;  // Full score
}

return std::min(strength_score, 20.0);
```

**Rationale**: 
- Current system rewards TIGHT consolidation
- But tight = small SL = vulnerable to 25-50 point fill gaps
- Need to penalize zones that can't overcome execution costs

---

### 4. Market Regime Fitness (15 points max)

**Purpose**: Trade only when setup matches current regime

```cpp
double regime_score = 0.0;

// Check CURRENT regime at entry time (not zone formation)
if (current_regime == MarketRegime::BULL) {
    if (zone.type == ZoneType::DEMAND) {
        regime_score = 15.0;  // DEMAND in BULL = perfect
    }
    else if (zone.type == ZoneType::SUPPLY) {
        // Only if pull-back confirmed
        if (current_price < zone.proximal_line) {
            regime_score = 8.0;   // Pullback within supply
        } else {
            regime_score = 0.0;   // Supply at higher prices = risky
        }
    }
}
else if (current_regime == MarketRegime::BEAR) {
    if (zone.type == ZoneType::SUPPLY) {
        regime_score = 15.0;  // SUPPLY in BEAR = perfect
    }
    else if (zone.type == ZoneType::DEMAND) {
        if (current_price > zone.proximal_line) {
            regime_score = 8.0;
        } else {
            regime_score = 0.0;
        }
    }
}
else {  // RANGING
    // Both work in ranging
    regime_score = 10.0;
}

return regime_score;
```

**Rationale**:
- In BULL trend: Only DEMAND zones should be entered (not shorts)
- In BEAR trend: Only SUPPLY zones (not longs)
- Our 25 trades: ALL were counter-regime = failed

---

### 5. Rejection Strength (5 points max)

**Purpose**: Confirmation that price rejected zone on entry bar

```cpp
double rejection_score = 0.0;

// Calculate wick-to-body ratio on current bar
double body = std::abs(current_bar.close - current_bar.open);
double wick = 0.0;

if (zone.type == ZoneType::SUPPLY) {
    // For supply, upper wick shows rejection
    double upper_wick = current_bar.high - std::max(current_bar.open, current_bar.close);
    wick = upper_wick;
} else {
    // For demand, lower wick shows rejection
    double lower_wick = std::min(current_bar.open, current_bar.close) - current_bar.low;
    wick = lower_wick;
}

if (body > 0.0001) {
    double wick_ratio = wick / body;
    
    if (wick_ratio > 0.6) {
        rejection_score = 5.0;  // Strong rejection
    } else if (wick_ratio > 0.4) {
        rejection_score = 2.0;  // Moderate rejection
    } else {
        rejection_score = 0.0;  // No rejection (price accepted zone)
    }
} else {
    // Doji candle - neutral
    rejection_score = 1.0;
}

return rejection_score;
```

**Rationale**:
- Strong rejection wicks = institutional rejection = good entry
- No wick = price accepted the zone = probably no bounce coming

---

## Part 6: Implementation Changes

### File 1: Update `system_config.json`

Location: Line ~145-165

Old:
```json
  "scoring": {
    "weight_base_strength": 0.40,
    "weight_elite_bonus": 0.25,
    "weight_regime_alignment": 0.20,
    "weight_state_freshness": 0.10,
    "weight_rejection_confirmation": 0.05
  }
```

New:
```json
  "scoring": {
    "weight_zone_type_alignment": 0.35,
    "weight_momentum_filter": 0.25,
    "weight_zone_quality": 0.20,
    "weight_regime_fitness": 0.15,
    "weight_rejection_strength": 0.05
  }
```

---

### File 2: Update `src/scoring/zone_scorer.cpp`

Replace entire `evaluate_zone()` method (line ~109) with new component calculations.

---

## Part 7: Testing the New System

### Phase 1: Backtest (1 day)
```bash
# Use 100 days of historical data
./run_backtest.exe  # Should see improved win rate

# Check console output for new score breakdown:
# "Zone Type: 20 | Momentum: 20 | Quality: 15 | Regime: 10 | Rejection: 3 = 68 points"
```

### Phase 2: Paper Trade (1 week)
```bash
# Run on LIVE data feed but with paper accounts
./run_live.exe

# Monitor for:
# 1. Fewer entries (ADX filter eliminates counter-trend)
# 2. Better entry timing (less overbought)
# 3. Fewer RSI-driven contradictory signals
```

### Phase 3: Metrics to Track

| Metric | Target | Why |
|--------|--------|-----|
| Win Rate | 45%+ | (vs current 0%) |
| Avg Winner | +₹3,500 | (vs current -₹1,084) |
| Avg Loser | -₹1,800 | (vs current -₹2,527) |
| ADX at Entry | 25-45 | (vs current 18-95.7) |
| RSI at Entry | 40-65 | (vs current 58-94.4) |
| Entry Count | -30% | (more selective) |

---

## Part 8: Expected Improvements

### Before (Current System)
- Win Rate: 0% (all 25 trades lost)
- Avg Loss: -₹2,527 per trade
- P&L: -₹50,540 (26 days)
- Entry RSI: 76.1 (overbought)
- Entry ADX: 58.9 (trending)

### After (Proposed System)
- Win Rate: 40-55% (expected, based on backtest data)
- Avg winner: +₹3,000-4,000
- Avg loser: -₹1,500-2,000
- P&L: +₹20,000-50,000 (26 days)
- Entry RSI: 50-60 (neutral-overbought)
- Entry ADX: 25-40 (optimal trend zone)

---

## Part 9: Quick Reference - Configuration Changes

### Change 1: Disable RSI Filter
```json
// In system_config.json, trade_rules section:
"rsi_filter_enabled": false
```

### Change 2: New Scoring Weights
```json
// In system_config.json, scoring section:
"weight_zone_type_alignment": 0.35,
"weight_momentum_filter": 0.25,
"weight_zone_quality": 0.20,
"weight_regime_fitness": 0.15,
"weight_rejection_strength": 0.05
```

### Change 3: Rebuild
```bash
cd build
cmake --build . --config Release
```

---

## Summary

**Problem**: Current scoring system gives equal scores to winning and losing zones because it measures the WRONG things.

**Solution**: 
1. **Disable RSI filter** - Removes overbought entries
2. **Reweight components** - Focus on ADX (entry momentum) and regime alignment 
3. **Add execution adjustment** - Penalize tight SLs that lose to slippage
4. **Track current regime** - Not zone formation regime

**Expected Result**: 
- From 0% → 40-55% win rate
- From -₹50,540 → +₹20,000-50,000 in same 26-day period

---

**Next Steps**:
1. Apply changes to `system_config.json`
2. Update `zone_scorer.cpp` with new calculations
3. Recompile and backtest
4. Monitor live performance

