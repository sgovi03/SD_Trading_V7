# Entry Decision Scores - Analysis

## Question
Are we using entry decision scores while arriving at the final trade decision?

## Answer: ✅ YES, BUT Limited Active Use

The **zone scores** from the `EntryDecision` struct **ARE being used** in the final trade decision, but primarily for:
1. **Initial Filtering** (gating mechanism)
2. **Logging/Reporting** (audit trail)
3. **Post-trade Analysis** (stored for forensics)

## Flow Diagram

```
┌─────────────────────────────────────────────────────────┐
│ 1. SCORE CALCULATION                                    │
│    ZoneScore = scorer.evaluate_zone(zone, regime, bar) │
└──────────────────────┬──────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────┐
│ 2. ENTRY DECISION CALCULATION                          │
│    EntryDecision = entry_engine.calculate_entry(       │
│      zone, zone_score, atr, bars, index, regime)       │
│                                                         │
│    ├─ decision.score = score (STORED)                 │
│    ├─ decision.entry_price (CALCULATED)               │
│    ├─ decision.stop_loss (CALCULATED)                 │
│    ├─ decision.take_profit (CALCULATED)               │
│    ├─ decision.expected_rr (CALCULATED)               │
│    └─ decision.should_enter (GATED)                   │
└──────────────────────┬──────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────┐
│ 3. SCORE FILTERING (entry_decision_engine.cpp)         │
│    ├─ Check: score.total_score >= entry_minimum_score │
│    ├─ Check: expected_rr >= target_min_rr             │
│    └─ Set: decision.should_enter = true/false         │
└──────────────────────┬──────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────┐
│ 4. TRADE RULES VALIDATION (trade_manager.cpp)          │
│    ├─ TradeRules::evaluate_entry(zone, score, regime) │
│    └─ Check: rule_result.passed                       │
└──────────────────────┬──────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────┐
│ 5. TRADE EXECUTION & SCORE STORAGE                     │
│    current_trade.zone_score = decision.score.total_score        │
│    current_trade.score_base_strength = ...                      │
│    current_trade.score_elite_bonus = ...                        │
│    current_trade.score_swing_position = ...                     │
│    current_trade.score_regime_alignment = ...                   │
│    current_trade.score_state_freshness = ...                    │
│    current_trade.score_rejection_confirmation = ...             │
│    current_trade.score_recommended_rr = ...                     │
└──────────────────────┬──────────────────────────────────┘
                       │
                    TRADE ENTERS
```

## Where Entry Decision Scores Are Used

### 1. **Initial Gating - entry_decision_engine.cpp (Lines 108-118)**

```cpp
// Check if score meets minimum threshold
if (score.total_score < config.scoring.entry_minimum_score) {
    decision.should_enter = false;
    decision.rejection_reason = "Score below minimum";
    return decision;
}
```

**Status:** ✅ ACTIVE - Filters out low-quality zones

### 2. **Adaptive Entry Logic - entry_decision_engine.cpp (Lines 120-129)**

```cpp
double aggressiveness = score.entry_aggressiveness;

if (zone.type == ZoneType::DEMAND) {
    // DEMAND: Use aggressiveness to place entry
    double conservative_factor = 1.0 - aggressiveness;
    decision.entry_price = zone.distal_line + 
        conservative_factor * (zone.proximal_line - zone.distal_line);
}
```

**Status:** ✅ ACTIVE - Adjusts entry placement based on score

### 3. **R:R Validation - entry_decision_engine.cpp (Lines 192-200)**

```cpp
// Reject if R:R too low
if (decision.expected_rr < config.target_min_rr) {
    decision.should_enter = false;
    decision.rejection_reason = "R:R too low";
    return decision;
}
```

**Status:** ✅ ACTIVE - Validates risk/reward ratio

### 4. **Trade Rules Validation - trade_manager.cpp (Lines 237-248)**

```cpp
if (config.trade_rules_config && bars != nullptr) {
    TradeRules trade_rules(*config.trade_rules_config);
    RuleCheckResult rule_result = trade_rules.evaluate_entry(
        zone,
        decision.score,           // ← SCORE USED HERE
        regime,
        *bars,
        bar_index
    );
    
    if (!rule_result.passed) {
        return false;
    }
}
```

**Status:** ✅ ACTIVE - Additional rule filtering using score

### 5. **Score Storage - trade_manager.cpp (Lines 308-320)**

```cpp
current_trade.zone_score = decision.score.total_score;
current_trade.entry_aggressiveness = decision.entry_location_pct;
current_trade.score_base_strength = decision.score.base_strength_score;
current_trade.score_elite_bonus = decision.score.elite_bonus_score;
current_trade.score_swing_position = decision.score.swing_position_score;
current_trade.score_regime_alignment = decision.score.regime_alignment_score;
current_trade.score_state_freshness = decision.score.state_freshness_score;
current_trade.score_rejection_confirmation = decision.score.rejection_confirmation_score;
current_trade.score_recommended_rr = decision.score.recommended_rr;
current_trade.score_rationale = decision.score.entry_rationale;
```

**Status:** ✅ STORED - For post-trade analysis and forensics

## What Score Fields Affect the Final Decision

| Field | Affects | Usage |
|-------|---------|-------|
| `total_score` | Entry Gating | ✅ Blocks if < threshold |
| `entry_aggressiveness` | Entry Price | ✅ Adjusts placement |
| `recommended_rr` | Entry Rationale | 📊 Stored only |
| `base_strength_score` | Trade Rules | ⚠️ Optional (if rules enabled) |
| `elite_bonus_score` | Trade Rules | ⚠️ Optional (if rules enabled) |
| `swing_position_score` | Trade Rules | ⚠️ Optional (if rules enabled) |
| `regime_alignment_score` | Trade Rules | ⚠️ Optional (if rules enabled) |
| `state_freshness_score` | Trade Rules | ⚠️ Optional (if rules enabled) |
| `rejection_confirmation_score` | Trade Rules | ⚠️ Optional (if rules enabled) |

## What NOT Using Score Fields After Entry

❌ **Position Sizing** - Uses fixed config value or capital percentage
❌ **Stop Loss Adjustment** - Uses ATR multiplier, not score
❌ **Take Profit Adjustment** - Uses target calculator strategy, not score
❌ **Trailing Stop Logic** - Uses price action and technical indicators
❌ **Exit Decisions** - Uses RSI, ADX, MACD, not entry score
❌ **Risk Management** - Uses original R calculation and capital rules

## Key Findings

### 1. **Score Used for Entry, Not Exit**
The entry decision scores are **only used at entry time**. Once the trade is active, it doesn't affect exits or adjustments—those use technical indicators (RSI, ADX, MACD, Bollinger Bands).

### 2. **Archived for Forensics**
All score components are **stored in Trade record** for later analysis:
- Backtest reports can correlate entry scores with win rates
- Trade dumps can show what scores led to winners/losers
- Performance analysis can identify optimal score ranges

### 3. **Trade Rules Optional Layer**
If `config.trade_rules_config` is enabled, TradeRules class can access the full score breakdown for additional validation:
```cpp
RuleCheckResult rule_result = trade_rules.evaluate_entry(
    zone,
    decision.score,  // ← TradeRules can inspect all components
    regime,
    *bars,
    bar_index
);
```
This is an **optional advanced filtering layer**.

### 4. **Adaptive Entry Pricing**
The `entry_aggressiveness` component directly affects where you enter within the zone:
- Higher aggressiveness = more aggressive entry (closer to distal for SUPPLY, closer to proximal for DEMAND)
- Lower aggressiveness = more conservative entry
- This IS actively used in final entry decision

## Recommendation: Enhancement Ideas

### Currently Not Used (But Could Be)
1. **Position Sizing Based on Score Quality**
   - Current: Fixed 1 lot
   - Proposed: Scale position with score (higher score → larger position)

2. **Risk Management Adjustment**
   - Current: Fixed SL distance (ATR multiplier)
   - Proposed: Tighter SL for low-score entries, wider for high-score zones

3. **Target Multiplier Adjustment**
   - Current: Fixed taken from target_calculator
   - Proposed: Adjust R:R multiplier based on entry score

4. **Real-Time Score Recalculation**
   - Current: Scored once at entry
   - Proposed: Rescore every bar, exit if score drops below threshold

5. **Multi-Level Exits**
   - Current: Single TP based on calculated target
   - Proposed: Partial exits at score-based levels

## Files Involved

| File | Lines | Purpose |
|------|-------|---------|
| `src/scoring/entry_decision_engine.cpp` | 108-200 | Score filtering & adaptive entry |
| `src/backtest/trade_manager.cpp` | 237-248 | Trade rules validation |
| `src/backtest/trade_manager.cpp` | 308-320 | Score storage in Trade |
| `src/live/live_engine.cpp` | 1304-1310 | Score calculation & decision |

## Conclusion

✅ **Entry Decision Scores ARE actively used** in the final trade decision for:
- Entry gating (minimum score threshold)
- Entry price placement (aggressiveness)
- R:R validation
- Optional Trade Rules filtering

❌ **Entry Scores NOT used** for:
- Position sizing
- Stop loss placement
- Take profit adjustment
- Exit timing
- Risk management

**Overall: Scores drive ENTRY decisions, not ongoing trade management.**

---

**Analysis Date:** February 9, 2026
**Scope:** Live Engine, Backtest Engine, Trade Manager
**Status:** ✅ COMPLETE
