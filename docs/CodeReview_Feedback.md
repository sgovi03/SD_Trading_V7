# Code Review — ReVerify Submission
**Date:** 2026-02-20

---

## Overall Assessment

Good progress — **2 of 3 fixes are correctly implemented**. One fix has a critical gap that will prevent it from working at runtime, and one fix (intraday SL slippage) was not attempted. Details below.

---

## Fix 1 — `sd_engine_core`: max_loss_per_trade Enforcement

### ✅ CORRECT: `calculate_position_size()` formula fixed
`sd_engine_core.cpp` — the formula is now correct:
```cpp
int max_lots = static_cast<int>(
    config.max_loss_per_trade / (config.lot_size * price_risk));
if (max_lots < 1) return 0;  // reject
lots = std::min(lots, max_lots);
```
This will properly reject trade #36477 (sl_dist=277pts → affordable_lots=0 → rejected) and cap trade #35340 at 1 lot instead of 2.

### ✅ CORRECT: `max_loss_per_trade` field added to `BacktestConfig`
`sd_engine_core.h` line 262: field added with correct default of `10000.0`.

### ❌ CRITICAL GAP: `max_loss_per_trade` is never parsed from config file
`sd_engine_core.cpp` `load_config()` has only 9 keys in its `else if` chain, and `max_loss_per_trade` is **not one of them**. The comment `// ... add more config parsing as needed` marks the spot but the line was never added:

```cpp
// CURRENT — max_loss_per_trade is MISSING from this chain:
if (key == "starting_capital") config.starting_capital = std::stod(value);
else if (key == "risk_per_trade_pct") config.risk_per_trade_pct = std::stod(value);
else if (key == "lot_size") config.lot_size = std::stoi(value);
// ... 6 more keys ...
// <- max_loss_per_trade is NEVER parsed here
```

**Consequence:** The struct default (10000.0) is used, which happens to match the current config value — so it accidentally works today. But if you change `max_loss_per_trade` in the config file to any other value (e.g., `8000.0`), the change will be **silently ignored** and the old engine will always enforce 10000.0 regardless.

There is also a second issue: the config line contains an inline comment:
```
max_loss_per_trade = 10000.0          # was 10500 — cut max loss in half
```
The old engine's parser does **not** strip inline `#` comments. If the parse line were added as-is, `std::stod("10000.0          # was 10500")` would throw `std::invalid_argument` and crash — there is no try-catch around the parse loop.

### ❌ CRITICAL GAP: `calculate_position_size()` is dead code for the live path
The function was fixed but is never called during live trading. `sd_realtime_csv_main.cpp` calls `evaluate_entry()` which generates a signal but does not call `calculate_position_size()` and does not execute any trade. Your actual live execution goes through `main_unified → LiveEngine → trade_mgr.enter_trade()` which uses `Core::Config`, not `BacktestConfig`. The `BacktestConfig` fix therefore only matters for the CSV simulation path.

**The core architectural fact:** your live trades go through `trade_manager.cpp` `enter_trade()` using `Core::Config`. That is where Fix 2 (the formula fix) applies — and it is correctly implemented there.

### Required Addition to `sd_engine_core.cpp` `load_config()`:

```cpp
// ADD to the else-if chain, with inline comment stripping:
else if (key == "max_loss_per_trade") {
    std::string clean_val = value.substr(0, value.find('#'));
    clean_val.erase(clean_val.find_last_not_of(" \t") + 1);
    if (!clean_val.empty()) config.max_loss_per_trade = std::stod(clean_val);
}
```

---

## Fix 2 — `trade_manager.cpp`: Correct Position Size Adjustment Formula

### ✅ FULLY CORRECT — This is the critical live trading fix

The formula in `enter_trade()` is exactly right:

```cpp
int affordable_lots = static_cast<int>(
    config.max_loss_per_trade / (config.lot_size * actual_stop_distance));
if (affordable_lots < 1) return false;  // reject outright
position_size = affordable_lots;
```

Verified against the actual problem trades from the CSV:

| Trade | SL Dist | Old Result | New Result |
|-------|---------|------------|------------|
| #36477 | 277.88 pts | Entered, lost Rs20,109 | REJECTED (affordable_lots = 0) |
| #36321 | 229.83 pts | Entered, lost Rs16,170 | REJECTED (affordable_lots = 0) |
| #35506 | 159.96 pts | Entered, lost Rs11,724 | REJECTED (affordable_lots = 0) |
| #35340 | 136.55 pts x 2 lots | 2 lots, risk Rs17,751 | Capped to 1 lot, risk Rs8,876 |

This fix is in the correct code path and will work for live trading.

---

## Fix 3 — Multi-Stage Trailing Stop

### ✅ CORRECTLY IMPLEMENTED in both engines

Both `live_engine.cpp` and `backtest_engine.cpp` have the correct 4-stage logic:
- Stage 1 (>=0.5R): Breakeven lock
- Stage 2 (>=1.0R): +0.5R profit floor locked
- Stage 3 (>=2.0R): 1.0xATR trail from bar extreme
- Stage 4 (>=3.0R): 0.5xATR tight trail from bar extreme

Ratchet logic is correct — stop only moves in profitable direction. Config updated correctly: `trail_activation_rr = 0.5`, `trail_atr_multiplier = 1.0`.

### Minor: Duplicate config entry
`trail_activation_rr` appears at line 380 (active, `0.5`) and line 597 (commented `# trail_activation_rr = 1.2`). The commented one is inactive so no functional problem, but clean it up to avoid confusion.

---

## Fix 4 — Intraday SL Slippage (Not Implemented)

### ❌ NOT ADDRESSED

The trades CSV showed Rs9,446 in extra losses from 6 trades where exit price was worse than the stop loss level within the same 5-minute bar. Both engines still use `trade.stop_loss` as the exit price:

```cpp
// live_engine.cpp — UNCHANGED:
exit_price = sl_hit ? trade.stop_loss : trade.take_profit;

// backtest_engine.cpp — UNCHANGED:
trade_manager.close_trade(bar, exit_reason, trade.stop_loss);
```

When a SHORT trade's SL is `25112.88` but the bar closes at `25141.30`, your actual market fill is at `25141.30`, not `25112.88`. The backtest reports a better number than what actually happens live — this is the source of the backtest/live P&L discrepancy on SL exits.

### Required Fix for `live_engine.cpp`:

```cpp
// REPLACE the exit_price assignment:
if (trade.direction == "LONG") {
    sl_hit = (current_bar.low <= trade.stop_loss);
    tp_hit = (current_bar.high >= trade.take_profit);
    if (sl_hit && tp_hit) { sl_hit = true; tp_hit = false; }
    exit_price = sl_hit
        ? std::min(trade.stop_loss, current_bar.close)  // worse of SL vs close
        : trade.take_profit;
} else { // SHORT
    sl_hit = (current_bar.high >= trade.stop_loss);
    tp_hit = (current_bar.low <= trade.take_profit);
    if (sl_hit && tp_hit) { sl_hit = true; tp_hit = false; }
    exit_price = sl_hit
        ? std::max(trade.stop_loss, current_bar.close)  // worse of SL vs close
        : trade.take_profit;
}
```

### Required Fix for `backtest_engine.cpp`:

```cpp
// REPLACE the SL close_trade call:
if (trade_manager.check_stop_loss(bar)) {
    std::string exit_reason = trade.trailing_activated ? "TRAIL_SL" : "SL";
    double sl_fill = (trade.direction == "LONG")
        ? std::min(trade.stop_loss, bar.close)
        : std::max(trade.stop_loss, bar.close);
    Trade closed_trade = trade_manager.close_trade(bar, exit_reason, sl_fill);
    // ... rest unchanged
```

---

## Summary

| # | Fix | File | Status | Action |
|---|-----|------|--------|--------|
| 1a | `max_loss_per_trade` field in BacktestConfig | `sd_engine_core.h` | Done | None |
| 1b | `calculate_position_size()` formula | `sd_engine_core.cpp` | Done | None |
| 1c | Parse `max_loss_per_trade` from config | `sd_engine_core.cpp` load_config() | **MISSING** | Add else-if with comment stripping |
| 2 | Correct adjustment formula in trade_manager | `trade_manager.cpp` | Done | None — this is the live fix |
| 3 | Multi-stage trailing stop | `live_engine.cpp`, `backtest_engine.cpp` | Done | None |
| 4 | Intraday SL slippage fill price | `live_engine.cpp`, `backtest_engine.cpp` | **Not done** | Implement bar.close fill for SL hits |
