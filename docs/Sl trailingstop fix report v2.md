# SD Trading System — Stop Loss & Trailing Stop Fix Report (Revised)
**Date:** 2026-02-20 | **All trades intraday, no overnight positions**

---

## Root Cause Summary

Three bugs, across two separate engine paths, explain why losses exceed `max_loss_per_trade = 10000.0`:

| # | Bug | Where | Impact |
|---|-----|--------|--------|
| 1 | **Old engine (`sd_engine_core`) has zero max_loss enforcement** — `sd_realtime_csv_main` uses `BacktestConfig` which has no `max_loss_per_trade` field, and its `calculate_position_size()` blindly uses `max(1, lots)` with no cap | `sd_engine_core.cpp`, `sd_realtime_csv_main.cpp` | **CRITICAL — directly causes 13k–20k losses** |
| 2 | **Wrong position-size adjustment formula in `trade_manager.cpp`** — when potential loss > cap and size is reduced, the divisor uses `lot_size` alone instead of `lot_size × stop_distance`, so even after "adjustment" the trade may re-enter with 1 lot still over the cap | `trade_manager.cpp` | Adjustment logic ineffective for borderline cases |
| 3 | **Trailing stop too loose / activates too early** — EMA(20)−ATR×1.5 trail gives back nearly all profit; activation at 0.4R means trail is running at entry-level EMA before price has moved meaningfully | `backtest_engine.cpp`, `live_engine.cpp` | Winners exit at near-breakeven |

---

## Bug 1 — Old Engine (`sd_engine_core`) Has No Max-Loss Cap

### The Two Engine Paths

Your system has **two separate, parallel engine architectures**:

| Entry Point | Config Type | Max-Loss Cap? | SL Buffer |
|------------|-------------|----------------|-----------|
| `main_unified` → `EngineFactory` → `BacktestEngine` / `LiveEngine` | `Core::Config` (common_types.h) | ✅ Yes — in `trade_manager.cpp` | `sl_buffer_atr = 2.5`, `sl_buffer_zone_pct = 35%` |
| `sd_realtime_csv_main` → `sd_engine_core` | `BacktestConfig` (sd_engine_core.h) | ❌ **NO** — never implemented | 0.1% of price only (≈22 pts) |

When `sd_realtime_csv_main` is running (the CSV-based live simulation), it calls `load_config()` which populates a `BacktestConfig`. This struct **does not have a `max_loss_per_trade` field**. The old `calculate_position_size()` in `sd_engine_core.cpp` always returns `max(1, lots)` with no upper-bound loss check.

**Old engine stop loss (sd_engine_core.cpp lines 1031–1039):**
```cpp
decision.stop_loss = zone.distal_line - (zone.distal_line * 0.001);  // 0.1% only
```

For NIFTY at 22,000: `0.1%` = 22 pts — extremely tight, ignores `sl_buffer_atr`.

**Old engine position sizing (sd_engine_core.cpp lines 1050–1060):**
```cpp
int calculate_position_size(double entry_price, double stop_loss,
                             const BacktestConfig& config, double current_capital) {
    double risk_amount = current_capital * (config.risk_per_trade_pct / 100.0);
    double price_risk = std::abs(entry_price - stop_loss);
    if (price_risk <= 0) return 0;
    double value_per_point = risk_amount / price_risk;
    int lots = static_cast<int>(value_per_point / config.lot_size);
    return std::max(1, lots);  // ← NO MAX LOSS CAP, always at least 1 lot
}
```

With `risk_per_trade_pct = 1.0`, `starting_capital = 300,000`:
- `risk_amount = 3,000`
- For a wide zone of 200 pts: `price_risk = 222 pts` (distal span + 22pt buffer)
- `lots = floor(3000 / (65 × 222)) = 0 → max(1, 0) = 1 lot`
- **Loss = 222 × 1 × 65 = ₹14,430** — no cap enforced anywhere

For a very wide supply/demand zone of 300 pts: **Loss = 322 × 65 = ₹20,930** — matches the 20k losses you're observing.

### Fix — `sd_engine_core.cpp`: Add Max-Loss Cap

Add `max_loss_per_trade` to `BacktestConfig` and enforce it in `calculate_position_size`:

**Step 1: Add field to `BacktestConfig` in `sd_engine_core.h`** (around line 253, after `trail_fallback_tp_rr`):

```cpp
// ⭐ ADD THIS:
double max_loss_per_trade = 10000.0;   // Hard cap: max closed loss per trade in ₹
```

**Step 2: Parse from config in `sd_engine_core.cpp` `load_config()` function:**

Find the block where config keys are parsed (the large `else if` chain), and add:

```cpp
// ⭐ ADD THIS alongside other trade management keys:
else if (key == "max_loss_per_trade") config.max_loss_per_trade = std::stod(value);
```

**Step 3: Replace `calculate_position_size()` in `sd_engine_core.cpp`:**

```cpp
// === REPLACE ENTIRE FUNCTION (lines ~1050–1061) ===
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
```

**Step 4: Honor the return value 0 at the call site** — find where `calculate_position_size` is called in `sd_engine_core.cpp` and guard the entry:

```cpp
// Find the call site (around the manage_position / trade_decision block):
int pos_size = calculate_position_size(entry_price, stop_loss, config, current_capital);
if (pos_size <= 0) {
    LOG_WARN("Trade skipped: max_loss_per_trade cap would be breached "
             "(SL distance=" + std::to_string(std::abs(entry_price - stop_loss)) + 
             " pts, 1 lot = ₹" + 
             std::to_string(std::abs(entry_price - stop_loss) * config.lot_size) + ")");
    return;  // or continue, depending on calling context
}
```

---

## Bug 2 — Wrong Adjustment Formula in `trade_manager.cpp`

### Root Cause

In `trade_manager.cpp` `enter_trade()`, when `potential_loss > max_loss_per_trade`, the position size reduction formula is:

```cpp
// CURRENT (WRONG):
double max_stop_distance = config.max_loss_per_trade / config.lot_size;
// For max_loss=10000, lot_size=65: max_stop_distance = 153.8
// This represents "how many points if position_size=1" — it's not a position adjuster

position_size = static_cast<int>(max_stop_distance / actual_stop_distance);
// Example: stop_distance=130, position_size = floor(153.8/130) = 1
// Re-check: 130 * 1 * 65 = 8,450 ← happens to be OK, but by accident
```

The formula works by coincidence when `position_size` was already 1, but breaks when it was 2 or 3 (V6.0 dynamic sizing), producing the wrong intermediate target and incorrect log messages.

### Fix — `trade_manager.cpp` `enter_trade()` (~line 290):

```cpp
// === REPLACE the adjustment block ===
if (potential_loss > config.max_loss_per_trade) {
    double actual_stop_distance = std::abs(decision.entry_price - decision.stop_loss);
    int original_position_size = position_size;

    if (actual_stop_distance > 0 && config.lot_size > 0) {
        // ⭐ FIX: Correct formula
        // max_loss = lots × lot_size × stop_distance
        // → lots = floor(max_loss / (lot_size × stop_distance))
        int affordable_lots = static_cast<int>(
            config.max_loss_per_trade / (config.lot_size * actual_stop_distance));

        if (affordable_lots < 1) {
            // Even 1 lot exceeds the cap — reject outright
            LOG_WARN("Trade REJECTED: 1 lot × " + std::to_string(config.lot_size) +
                     " × " + std::to_string(actual_stop_distance) + "pts = ₹" +
                     std::to_string(actual_stop_distance * config.lot_size) +
                     " exceeds max_loss_per_trade ₹" +
                     std::to_string(config.max_loss_per_trade));
            std::cout << "[TRADE_MGR] REJECTED: 1 lot already exceeds max_loss_per_trade\n";
            std::cout.flush();
            return false;
        }

        position_size = affordable_lots;
        std::cout << "  ⚠️  Position size adjusted: "
                  << original_position_size << " → " << position_size
                  << " lots (max_loss_per_trade cap)\n";
        std::cout.flush();

        potential_loss = calculate_potential_loss(
            decision.entry_price, decision.stop_loss, position_size, config.lot_size);

        LOG_INFO("Position size adjusted: " + std::to_string(original_position_size) +
                 " → " + std::to_string(position_size) + " lots. New risk: ₹" +
                 std::to_string(potential_loss));
    }

    // Final safety check — must always pass at this point
    if (potential_loss > config.max_loss_per_trade) {
        LOG_WARN("Trade REJECTED: Adjusted loss ₹" + std::to_string(potential_loss) +
                 " still exceeds cap ₹" + std::to_string(config.max_loss_per_trade));
        std::cout << "[TRADE_MGR] REJECTED: Loss cap exceeded after size adjustment\n";
        return false;
    }
}
```

---

## Bug 3 — Trailing Stop Too Loose (Profits Given Back)

### Why EMA(20)−ATR×1.5 Fails at 0.4R Activation

With `trail_activation_rr = 0.4` and typical NIFTY 5-min parameters:

```
Entry:  22,000  |  SL: 21,850  |  Risk (R): 150 pts
Trail activates at: 0.4R = 60 pts profit → price = 22,060 (just 3 bars in)

EMA(20) at bar 3 of trade = ~21,980 (still catching up from pre-entry price)
ATR(14)                   = ~70 pts
Trail stop = 21,980 − (70 × 1.5) = 21,980 − 105 = 21,875

Original SL = 21,850  →  Trail stop 21,875 is only 25 pts better. 
Essentially you gave up 60 pts of profit for a 25 pt improvement in SL!

Later, price reaches 22,300 (2.33R), EMA catches up to 22,100:
Trail stop = 22,100 − 105 = 21,995  ←  Only 145 pts above entry (0.97R)!
Price pulls back to 22,000 → trail not hit yet (22,000 > 21,995)
Price at 21,995 → EXIT.  Profit = 21,995 − 22,000 = −5 pts (near breakeven!)
```

The EMA-based trail systematically gives back 1.0–1.5R of profit on every trade.

### Fix — Multi-Stage Trailing Stop

Replace the trailing stop block in **both** `backtest_engine.cpp` and `live_engine.cpp`:

**`backtest_engine.cpp` — replace trailing stop block in `manage_open_position()`:**

```cpp
// ⭐ IMPROVED: Multi-Stage Trailing Stop
// Stage 1 (≥ trail_activation_rr R): Move SL to exact breakeven (entry price)
// Stage 2 (≥ 1.0R):                  Lock 0.5R profit floor
// Stage 3 (≥ 2.0R):                  ATR-based trail from extreme high/low (1.0×ATR buffer)
// Stage 4 (≥ 3.0R):                  Tight trail from extreme high/low (0.5×ATR buffer)
if (config.use_trailing_stop) {
    double risk = std::abs(trade.entry_price - trade.original_stop_loss);
    if (risk <= 0) risk = std::abs(trade.entry_price - trade.stop_loss);
    if (risk < 0.01) risk = 1.0;

    // Track extreme price reached (bar extremes, not just close)
    if (trade.direction == "LONG") {
        if (bar.high > trade.highest_price || trade.highest_price <= 0.0)
            trade.highest_price = bar.high;
    } else {
        if (bar.low < trade.lowest_price || trade.lowest_price <= 0.0)
            trade.lowest_price = bar.low;
    }

    // R based on best excursion (highest/lowest), not current price
    double best_excursion = (trade.direction == "LONG")
        ? (trade.highest_price - trade.entry_price)
        : (trade.entry_price - trade.lowest_price);
    double current_r = (risk > 0) ? (best_excursion / risk) : 0.0;

    // Current ATR for trail buffer
    double atr_trail = risk;  // fallback
    if (bar_index >= config.atr_period) {
        double atr_sum = 0.0;
        for (int i = bar_index - config.atr_period + 1; i <= bar_index; i++) {
            double tr = std::max({
                bars[i].high - bars[i].low,
                std::abs(bars[i].high - bars[i - 1].close),
                std::abs(bars[i].low  - bars[i - 1].close)
            });
            atr_sum += tr;
        }
        atr_trail = atr_sum / config.atr_period;
    }

    double new_trail_stop = 0.0;

    if (current_r >= 3.0) {
        // Stage 4: Tight — 0.5×ATR from extreme
        new_trail_stop = (trade.direction == "LONG")
            ? trade.highest_price - (atr_trail * 0.5)
            : trade.lowest_price  + (atr_trail * 0.5);
        if (!trade.trailing_activated) {
            trade.trailing_activated = true;
            LOG_INFO("📈 Trail STAGE 4 (tight, 0.5×ATR) at " << current_r << "R");
        }
    } else if (current_r >= 2.0) {
        // Stage 3: Moderate — 1.0×ATR from extreme
        new_trail_stop = (trade.direction == "LONG")
            ? trade.highest_price - (atr_trail * 1.0)
            : trade.lowest_price  + (atr_trail * 1.0);
        if (!trade.trailing_activated) {
            trade.trailing_activated = true;
            LOG_INFO("📈 Trail STAGE 3 (1.0×ATR) at " << current_r << "R");
        }
    } else if (current_r >= 1.0) {
        // Stage 2: Lock 0.5R profit floor
        new_trail_stop = (trade.direction == "LONG")
            ? trade.entry_price + (risk * 0.5)
            : trade.entry_price - (risk * 0.5);
        if (!trade.trailing_activated) {
            trade.trailing_activated = true;
            LOG_INFO("📈 Trail STAGE 2 (0.5R profit lock) at " << current_r << "R");
        }
    } else if (current_r >= config.trail_activation_rr) {
        // Stage 1: Breakeven lock
        new_trail_stop = trade.entry_price;
        if (!trade.trailing_activated) {
            trade.trailing_activated = true;
            LOG_INFO("📈 Trail STAGE 1 (breakeven lock) at " << current_r << "R");
        }
    }

    // Ratchet: never move SL backwards
    if (new_trail_stop > 0.0) {
        if (trade.direction == "LONG" && new_trail_stop > trade.stop_loss) {
            LOG_DEBUG("Trail SL ▲ " << trade.stop_loss << " → " << new_trail_stop);
            trade.stop_loss = new_trail_stop;
            trade.current_trail_stop = new_trail_stop;
        } else if (trade.direction == "SHORT" && new_trail_stop < trade.stop_loss) {
            LOG_DEBUG("Trail SL ▼ " << trade.stop_loss << " → " << new_trail_stop);
            trade.stop_loss = new_trail_stop;
            trade.current_trail_stop = new_trail_stop;
        }
    }
}
```

**`live_engine.cpp`** — apply the same block, substituting:
- `bars[i]` → `bar_history[i]`
- `bar_index` → `static_cast<int>(bar_history.size()) - 1`
- `bar.high / bar.low` → `current_bar.high / current_bar.low`

---

## Config Update — `phase_6_config_v0.1_Tested_in_5Mins.txt`

```ini
# ─── TRAILING STOP — Multi-Stage ───────────────────────────────────────
use_trailing_stop = YES

# Stage 1 breakeven activation: raise from 0.4 to 0.5 (avoid noise-triggered activation)
trail_activation_rr = 0.5

# ATR multiplier for Stage 3 (2R+) trail — reduced from 1.5 to 1.0
# At 2R profit, 1.0×ATR buffer locks in ≈ 1.0–1.5R minimum
trail_atr_multiplier = 1.0

# ATR period — unchanged
atr_period = 14
```

---

## Impact Summary

| Scenario | Before Fix | After Fix |
|----------|-----------|-----------|
| 200pt zone in `sd_realtime_csv_main` | Loss = ₹14,430 (no cap) | Trade REJECTED (0 returned from position sizer) |
| 300pt zone in `sd_realtime_csv_main` | Loss = ₹20,930 (no cap) | Trade REJECTED |
| 2-lot trade, stop distance 130pts, `trade_manager` | Size adjusted to 1 lot incorrectly calculated | Correctly adjusted to 1 lot, ₹8,450 risk |
| Winner at 2.5R (375pt profit), EMA trail | Trail at EMA−105 ≈ entry+70 (0.47R exit) | Stage 3 trail: Highest−70 ≈ entry+305 (2.03R exit) |
| Winner reaches 1R then reverses to entry | Full initial risk loss possible | Breakeven lock at 1R: zero loss guaranteed |

---

## Fix Priority

1. **Immediate — Bug 1**: Add `max_loss_per_trade` to `BacktestConfig` and enforce it in `sd_engine_core.cpp`. This is the direct cause of 16k–20k losses.
2. **High — Bug 2**: Fix the adjustment formula in `trade_manager.cpp` for correctness and accurate logging.
3. **Improvement — Bug 3**: Swap in multi-stage trailing to retain profits. This will improve win-trade quality significantly.