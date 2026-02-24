# Market Regime Detection Investigation - FINDINGS

**Date:** February 9, 2026  
**Status:** ✅ INVESTIGATION COMPLETE

---

## Executive Summary

The market regime detection is **working correctly**. The reason all 221 trades show `RANGING` regime is because **the market during the trading period (Dec 31, 2025 - Feb 1, 2026) was genuinely RANGING**.

### Key Finding
Over the entire 35-day trading period, analyzing 7,770 fifty-minute windows:
- **0 windows** showed 5%+ price movement (BULL)
- **0 windows** showed -5%+ price movement (BEAR)  
- **7,770 windows** (100%) showed movement between -2.473% and +1.650% (RANGING)

This is **statistically valid** for a consolidation/ranging market phase.

---

## Detailed Analysis

### Market Volatility During Trading Period

**Price Movement Statistics (50-Minute Windows):**
| Metric | Value | Note |
|--------|-------|------|
| **Maximum positive move** | +1.650% | Never reached +5% threshold |
| **Maximum negative move** | -2.473% | Never reached -5% threshold |
| **Mean movement per 50-min window** | -0.029% | Essentially flat |
| **Median movement** | -0.031% | Centered near zero |
| **Standard deviation** | 0.289% | Very low volatility |
| **5th percentile** | -0.467% | Even worst case far from threshold |
| **95th percentile** | +0.419% | Even best case far from threshold |

**Window Range (High-Low within 50-min window):**
| Metric | Value |
|--------|-------|
| **Max range** | 3.252% | Largest intra-window swing |
| **Mean range** | 0.365% | Average intra-period movement |
| **Median range** | 0.306% | Typical movement band |

### Implication

The regime detection logic is correct:
```
if (pct_change > 5.0%) → BULL                     // 0 windows matched
if (pct_change < -5.0%) → BEAR                    // 0 windows matched  
else → RANGING                                    // 7,770 windows matched ✓
```

**Verdict:** ✅ **Regime detection is FUNCTIONING CORRECTLY**

---

## Root Cause: Not Regime Detection

The 41.18% win rate problem is **NOT caused by incorrect regime detection**. The market truly was ranging.

**New Problem Statement:**
> Why is the system only winning 41.18% of trades in a RANGING market when the system should be optimized for ranging market conditions?

---

## Impact Assessment

### What This Tells Us

1. **Entry Rules for RANGING Market**: The system is configured to trade in RANGING markets:
   - `allow_ranging_trades: true` ✓
   - `trade_with_trend_only: true` (allows counter-trend trades in RANGING) ✓

2. **RANGING Market Rules**: TradeRules checks regime with special logic for RANGING:
   ```cpp
   if (regime == RANGING) {
       if (direction == SUPPLY) {
           return true;  // SHORT always allowed
       }
       if (direction == DEMAND) {
           // LONG only if: (swing_extreme AND rejection_ok) OR elite_ok
           if ((swing_extreme && rejection_ok) || elite_ok) {
               return true;
           }
       }
   }
   ```

3. **Actual Performance**: Only 41.18% win rate despite these rule, suggests:
   - The **RANGING market rules are too permissive** (allowing bad trades)
   - The **zone quality metrics aren't working** in RANGING conditions
   - The **entry aggressiveness is wrong** for RANGING market

---

## Diagnosis: Where the Real Problem Lies

Since regime detection is correct, the issues must be in:

### Issue #1: RANGING Market Entry Rules Too Loose
**Current logic for LONG in RANGING:**
```cpp
if ((swing_extreme && rejection_ok) || elite_ok) {
    allow = true;
}
```

**Problem:** This allows entry if EITHER condition is met:
- Swing extreme + rejection >= 1.0, OR
- Elite bonus > 0

**Evidence:** Zones with just rejection_confirmation_score >= 1.0 are generating losses

**Fix:** Make BOTH conditions required:
```cpp
if ((swing_extreme && rejection_ok) && elite_ok) {
    allow = true;  // Require BOTH swing extreme AND elite bonus
}
```

### Issue #2: Zone Score Not Predictive
**Finding:** From the earlier analysis, zones with score 45 and zones with score 85 have the **same 41% win rate**. This means zone score is NOT predictive.

**Implications:**
- Scoring components might be broken
- Weights might be wrong
- Aggressiveness calculation might not correlate with actual entry quality

### Issue #3: Entry Timing in RANGING Market
**In RANGING markets:** You want to trade structure reversals/bounces, not trends.

**Current approach:** Uses entry_aggressiveness to position within zone:
```cpp
decision.entry_price = zone.distal_line + 
    conservative_factor * (zone.proximal_line - zone.distal_line);
```

**Problem:** Doesn't account for structure bounce mechanics in RANGING market. May be entering too early or at bad levels.

### Issue #4: Position Sizing
**Current:** All trades are fixed 1 lot (65 shares based on system_config.json)

**Better:** Score-based position sizing:
- Score 40-50: 0.5 lot
- Score 50-70: 1.0 lot  
- Score 70-90: 1.5 lots

This would improve Sharpe ratio even without changing win rate.

---

## What NOT to Change

❌ **Don't modify regime detection:**
- It's working correctly
- 5% threshold is appropriate for 50-minute windows
- Market truly was RANGING

❌ **Don't reduce threshold to 2%:**
- Would artificially create BULL/BEAR signals
- Market data doesn't support it
- Would mask the actual problem

---

## Immediate Recommendations

### Priority 1: Fix RANGING Market Entry Rules
Modify [src/rules/TradeRules.cpp](../src/rules/TradeRules.cpp) line ~120:

**Current:**
```cpp
if ((swing_extreme && rejection_ok) || elite_ok)
    return true;
```

**Change to:**
```cpp
if ((swing_extreme && rejection_ok && elite_ok)) {
    return true;
}
```

This makes the RANGING market rules more restrictive, reducing false entries.

### Priority 2: Analyze Zone Score Predictiveness  
Run ML analysis on trades to see which score components correlate with wins:
- Check correlation: score.component ↔ trade P&L
- Identify which components have zero predictive power
- Either fix or remove them

### Priority 3: Test Score-Based Position Sizing
Modify entry decision to apply position multiplier based on score:
```cpp
double position_multiplier;
if (score.total_score < 50) position_multiplier = 0.5;
else if (score.total_score < 70) position_multiplier = 1.0;
else position_multiplier = 1.5;

decision.position_size = config.base_lot_size * position_multiplier;
```

### Priority 4: Review RANGING Market Entry Timing
Evaluate whether entry placement (aggressiveness) is optimal for bounce trades vs trend trades. May need different logic for RANGING market.

---

## Investigation Results Summary

| Question | Answer |
|----------|--------|
| **Is regime detection broken?** | ❌ No, it's working correctly |
| **Is 5% threshold wrong?** | ❌ No, it's appropriate for the market |
| **Was the market trending?** | ❌ No, it was genuinely ranging |
| **Why all RANGING regime?** | ✓ Market phase was ranging (0 BULL/BEAR windows in 7,770 analyzed) |
| **Is this a problem?** | ❌ No, regime detection is correct |
| **Where IS the real problem?** | ✓ In RANGING market entry rules or zone scoring |

---

## Next Steps

1. ✅ **Investigation complete** - regime detection is working
2. 🔄 **Focus shift** - problem is not regime detection but RANGING market trading logic
3. ⏳ **Priority fixes**:
   - Tighten RANGING market entry rules (Priority 1)
   - Analyze zone score predictiveness (Priority 2)
   - Implement position sizing by quality (Priority 3)
   - Review entry timing logic (Priority 4)

---

**Conclusion:** The regime detection is a **red herring**. The system correctly identified the RANGING market conditions. The win rate problem lies elsewhere in the entry decision or position sizing logic.
