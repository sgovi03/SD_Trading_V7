# TWO-STAGE SCORING CONFIGURATION PARAMETERS

**Date:** February 8, 2026  
**System:** SD Trading System V4.0  
**Status:** FULLY CONFIGURABLE ✅

---

## 📝 OVERVIEW

All zone quality and entry validation scoring parameters are now **fully configurable** through the main `Config` structure. No hard-coded values!

Parameters can be configured via:
- Strategy config files (`.txt`)
- System config files (`.json`)
- Programmatically in code

---

## 🎯 ZONE QUALITY SCORER (STAGE 1)

### Minimum Score Threshold

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `zone_quality_minimum_score` | double | 70.0 | Minimum total score for zone to pass Stage 1 |

### Zone Age Thresholds (Days)

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `zone_quality_age_very_fresh` | int | 2 | Days threshold for "very fresh" (20 points) |
| `zone_quality_age_fresh` | int | 5 | Days threshold for "fresh" (16 points) |
| `zone_quality_age_recent` | int | 10 | Days threshold for "recent" (12 points) |
| `zone_quality_age_aging` | int | 20 | Days threshold for "aging" (8 points) |
| `zone_quality_age_old` | int | 30 | Days threshold for "old" (4 points) |

**Zone Age Scoring:**
- 0-2 days: 20 points (very fresh)
- 3-5 days: 16 points (fresh)
- 6-10 days: 12 points (recent)
- 11-20 days: 8 points (aging)
- 21-30 days: 4 points (old)
- 30+ days: 0 points (stale)

### Zone Height Thresholds (Ratio to ATR)

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `zone_quality_height_optimal_min` | double | 0.3 | Minimum optimal height/ATR ratio (10 points) |
| `zone_quality_height_optimal_max` | double | 0.7 | Maximum optimal height/ATR ratio (10 points) |
| `zone_quality_height_acceptable_min` | double | 0.2 | Minimum acceptable height/ATR ratio (7 points) |
| `zone_quality_height_acceptable_max` | double | 1.0 | Maximum acceptable height/ATR ratio (7 points) |

**Zone Height Scoring:**
- 0.3-0.7 × ATR: 10 points (optimal institutional level)
- 0.2-1.0 × ATR: 7 points (acceptable)
- <0.2 × ATR: 3 points (too small, may be noise)
- >1.0 × ATR: 0 points (too large, just a range)

---

## 🎯 ENTRY VALIDATION SCORER (STAGE 2)

### Minimum Score Threshold

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entry_validation_minimum_score` | double | 65.0 | Minimum total score for entry to pass Stage 2 |

### Trend Alignment (EMA) Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entry_validation_ema_fast_period` | int | 50 | Fast EMA period for trend detection |
| `entry_validation_ema_slow_period` | int | 200 | Slow EMA period for trend detection |
| `entry_validation_strong_trend_sep` | double | 1.0 | Strong trend separation % (35 points) |
| `entry_validation_moderate_trend_sep` | double | 0.5 | Moderate trend separation % (30 points) |
| `entry_validation_weak_trend_sep` | double | 0.2 | Weak trend separation % (25 points) |
| `entry_validation_ranging_threshold` | double | 0.3 | Ranging market threshold % (18 points) |

**Trend Alignment Scoring:**
- Aligned + Strong (>1.0% sep): 35 points
- Aligned + Moderate (>0.5% sep): 30 points
- Aligned + Weak (>0.2% sep): 25 points
- Ranging (<0.3% sep): 18 points
- Counter-trend: 5 points

### Momentum State (RSI) Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entry_validation_rsi_deeply_oversold` | double | 30.0 | Deeply oversold level (20 points for LONG) |
| `entry_validation_rsi_oversold` | double | 35.0 | Oversold level (17 points for LONG) |
| `entry_validation_rsi_slightly_oversold` | double | 40.0 | Slightly oversold level (14 points for LONG) |
| `entry_validation_rsi_pullback` | double | 45.0 | Pullback level (10 points for LONG) |
| `entry_validation_rsi_neutral` | double | 50.0 | Neutral level (5 points for LONG) |
| `entry_validation_rsi_slightly_overbought` | double | 55.0 | Slightly overbought level (10 points for SHORT) |
| `entry_validation_rsi_overbought` | double | 60.0 | Overbought level (14 points for SHORT) |
| `entry_validation_rsi_deeply_overbought` | double | 70.0 | Deeply overbought level (20 points for SHORT) |

**RSI Scoring for LONG Trades:**
- RSI < 30: 20 points (deeply oversold - BEST)
- RSI < 35: 17 points (oversold - VERY GOOD)
- RSI < 40: 14 points (slightly oversold - GOOD)
- RSI < 45: 10 points (pullback - OK)
- RSI < 50: 5 points (neutral - WEAK)
- RSI ≥ 50: 0 points (overbought - BAD)

**RSI Scoring for SHORT Trades:**
- RSI > 70: 20 points (deeply overbought - BEST)
- RSI > 65: 17 points (overbought - VERY GOOD)
- RSI > 60: 14 points (slightly overbought - GOOD)
- RSI > 55: 10 points (pullback - OK)
- RSI > 50: 5 points (neutral - WEAK)
- RSI ≤ 50: 0 points (oversold - BAD)

### Momentum State (MACD) Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entry_validation_macd_strong_threshold` | double | 2.0 | Strong MACD histogram threshold (10 points) |
| `entry_validation_macd_moderate_threshold` | double | 1.0 | Moderate MACD histogram threshold (7 points) |

**MACD Scoring for LONG Trades:**
- Histogram < -2.0: 10 points (strong decline)
- Histogram < -1.0: 7 points (moderate decline)
- Histogram < 0: 5 points (slight decline)
- Histogram ≥ 0: 0 points (already positive)

**MACD Scoring for SHORT Trades:**
- Histogram > 2.0: 10 points (strong rise)
- Histogram > 1.0: 7 points (moderate rise)
- Histogram > 0: 5 points (slight rise)
- Histogram ≤ 0: 0 points (already negative)

### Trend Strength (ADX) Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entry_validation_adx_very_strong` | double | 50.0 | Very strong trend threshold (25 points) |
| `entry_validation_adx_strong` | double | 40.0 | Strong trend threshold (21 points) |
| `entry_validation_adx_moderate` | double | 30.0 | Moderate trend threshold (17 points) |
| `entry_validation_adx_weak` | double | 25.0 | Weak trend threshold (12 points) |
| `entry_validation_adx_minimum` | double | 20.0 | Minimum trend threshold (6 points) |

**ADX Scoring:**
- ADX ≥ 50: 25 points (very strong trend)
- ADX ≥ 40: 21 points (strong trend)
- ADX ≥ 30: 17 points (moderate trend)
- ADX ≥ 25: 12 points (weak trend)
- ADX ≥ 20: 6 points (very weak trend)
- ADX < 20: 0 points (choppy market - avoid)

### Volatility Context (Stop Loss) Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entry_validation_optimal_stop_atr_min` | double | 1.5 | Minimum optimal stop/ATR ratio (10 points) |
| `entry_validation_optimal_stop_atr_max` | double | 2.5 | Maximum optimal stop/ATR ratio (10 points) |
| `entry_validation_acceptable_stop_atr_min` | double | 1.2 | Minimum acceptable stop/ATR ratio (7 points) |
| `entry_validation_acceptable_stop_atr_max` | double | 3.0 | Maximum acceptable stop/ATR ratio (7 points) |

**Volatility Scoring:**
- 1.5-2.5 × ATR: 10 points (perfect stop distance)
- 1.2-3.0 × ATR: 7 points (good stop distance)
- 1.0-3.5 × ATR: 4 points (acceptable)
- <1.0 × ATR: 2 points (too tight - will get hit)
- >3.5 × ATR: 0 points (too wide - risking too much)

---

## 📝 CONFIGURATION EXAMPLE

### In Config File (strategy_config.txt format):

```ini
# === TWO-STAGE SCORING CONFIGURATION ===

# Zone Quality (Stage 1)
zone_quality_minimum_score=70.0
zone_quality_age_very_fresh=2
zone_quality_age_fresh=5
zone_quality_age_recent=10
zone_quality_age_aging=20
zone_quality_age_old=30
zone_quality_height_optimal_min=0.3
zone_quality_height_optimal_max=0.7
zone_quality_height_acceptable_min=0.2
zone_quality_height_acceptable_max=1.0

# Entry Validation (Stage 2)
entry_validation_minimum_score=65.0
entry_validation_ema_fast_period=50
entry_validation_ema_slow_period=200
entry_validation_strong_trend_sep=1.0
entry_validation_moderate_trend_sep=0.5
entry_validation_weak_trend_sep=0.2
entry_validation_ranging_threshold=0.3

# RSI Thresholds
entry_validation_rsi_deeply_oversold=30.0
entry_validation_rsi_oversold=35.0
entry_validation_rsi_slightly_oversold=40.0
entry_validation_rsi_pullback=45.0
entry_validation_rsi_neutral=50.0
entry_validation_rsi_slightly_overbought=55.0
entry_validation_rsi_overbought=60.0
entry_validation_rsi_deeply_overbought=70.0

# MACD Thresholds
entry_validation_macd_strong_threshold=2.0
entry_validation_macd_moderate_threshold=1.0

# ADX Thresholds
entry_validation_adx_very_strong=50.0
entry_validation_adx_strong=40.0
entry_validation_adx_moderate=30.0
entry_validation_adx_weak=25.0
entry_validation_adx_minimum=20.0

# Volatility Thresholds
entry_validation_optimal_stop_atr_min=1.5
entry_validation_optimal_stop_atr_max=2.5
entry_validation_acceptable_stop_atr_min=1.2
entry_validation_acceptable_stop_atr_max=3.0
```

### In Code:

```cpp
Config config;

// Zone Quality Configuration
config.zone_quality_minimum_score = 70.0;
config.zone_quality_age_very_fresh = 2;
config.zone_quality_age_fresh = 5;
// ... etc

// Entry Validation Configuration
config.entry_validation_minimum_score = 65.0;
config.entry_validation_ema_fast_period = 50;
config.entry_validation_ema_slow_period = 200;
// ... etc

// Use the scorers with your config
ZoneQualityScorer zone_scorer(config);
EntryValidationScorer entry_scorer(config);
```

---

## 🔧 TUNING RECOMMENDATIONS

### If Win Rate < 50%:

1. **Raise zone quality threshold:**
   - Change `zone_quality_minimum_score` from 70 to 75
   - Effect: Fewer but higher quality zones

2. **Raise entry validation threshold:**
   - Change `entry_validation_minimum_score` from 65 to 70
   - Effect: More selective entry conditions

3. **Tighten RSI requirements for LONG:**
   - Lower `entry_validation_rsi_pullback` from 45 to 42
   - Effect: Only enter LONG when more oversold

4. **Raise ADX minimum:**
   - Change `entry_validation_adx_minimum` from 20 to 25
   - Effect: Only trade in trending markets

### If Too Few Trades (<15/month):

1. **Lower zone quality threshold:**
   - Change `zone_quality_minimum_score` from 70 to 65
   - Effect: Accept more zones

2. **Lower entry validation threshold:**
   - Change `entry_validation_minimum_score` from 65 to 60
   - Effect: More lenient entry conditions

3. **Widen zone age acceptance:**
   - Increase `zone_quality_age_old` from 30 to 45
   - Effect: Accept older zones

4. **Lower ADX requirements:**
   - Change `entry_validation_adx_minimum` from 20 to 15
   - Effect: Trade in weaker trends

### Based on Your 77-Trade Analysis:

1. **LONG trades need stricter RSI:**
   - Your data: LONG with RSI<45 = 45.2% WR
   - Your data: LONG with RSI≥45 = 13.3% WR
   - Recommendation: Keep `entry_validation_rsi_pullback` at 45 or lower to 42

2. **ADX filtering is critical:**
   - Your data: ADX>25 = 40.3% WR, ₹24K profit
   - Your data: ADX<25 = 30.0% WR, ₹-18K loss
   - Recommendation: Set `entry_validation_adx_weak` to 25 minimum

3. **Fresh zones perform better:**
   - Your data: <7 days = 66.7% WR, +₹950/trade
   - Your data: 30-60 days = 31.1% WR, -₹849/trade
   - Recommendation: Keep `zone_quality_age_old` at 30 days max

---

## ✅ BENEFITS OF CONFIGURABLE SYSTEM

1. **Easy Experimentation:** Test different thresholds without recompiling
2. **Market Adaptation:** Adjust parameters for different market conditions
3. **Instrument-Specific:** Different configs for different trading instruments
4. **A/B Testing:** Run parallel systems with different settings
5. **Optimization:** Use optimization tools to find best parameter sets
6. **Transparency:** All scoring logic visible in config files
7. **No Code Changes:** Traders can tune without touching C++ code

---

## 📊 PARAMETER IMPACT MATRIX

| Parameter | Impact on | Trade Volume | Win Rate | Risk |
|-----------|-----------|--------------|----------|------|
| Zone Quality Min Score | 🔴 HIGH | ⬇️ Reduces | ⬆️ Improves | ⬇️ Lower |
| Entry Validation Min Score | 🔴 HIGH | ⬇️ Reduces | ⬆️ Improves | ⬇️ Lower |
| RSI Thresholds | 🟡 MEDIUM | ⬇️⬆️ Varies | ⬆️ Improves | ⬇️ Lower |
| ADX Thresholds | 🔴 HIGH | ⬇️ Reduces | ⬆️ Improves | ⬇️ Lower |
| Zone Age Limits | 🟡 MEDIUM | ⬇️ Reduces | ⬆️ Improves | ⬇️ Lower |
| EMA Periods | 🟢 LOW | ⬇️⬆️ Varies | ⬇️⬆️ Varies | ⬇️⬆️ Varies |
| MACD Thresholds | 🟢 LOW | ⬇️⬆️ Varies | ⬇️⬆️ Varies | ⬇️⬆️ Varies |
| Stop ATR Ratios | 🟢 LOW | ~ Same | ~ Same | ⬇️⬆️ Varies |

---

## 🎯 NEXT STEPS

1. **Build the system:** `.\build_release.bat`
2. **Test with defaults:** Run with current parameter values
3. **Monitor results:** Track win rate, trade volume, P&L
4. **Optimize:** Adjust parameters based on performance
5. **Validate:** Re-test with new parameters on historical data

All parameters are now **fully configurable and ready for optimization!** 🚀

---

**Generated:** February 8, 2026  
**Status:** Configuration Complete ✅  
**Ready for:** Build and Test
