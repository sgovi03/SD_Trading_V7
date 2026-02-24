# Code Review: finalReview.zip — Fix Verification Report
**Date:** 2026-02-21 | **Files Reviewed:** EngineFactory.cpp, trade_manager.cpp, live_engine.cpp, backtest_engine.cpp

---

## OVERALL VERDICT: 3 of 4 Fixes Correct — Stage 2 Trail Fix Has a Critical Error

---

## ✅ DEFECT #4 — EngineFactory `lot_size` Override Guard: CORRECT

All required elements are present and syntactically valid.

| Check | Status |
|-------|--------|
| Guard: `if (sys_config.trading_lot_size > 0)` before override | ✅ |
| `LOG_WARN` when lot_size is 0 (keeps phase_6 value) | ✅ |
| `throw std::runtime_error("FATAL...")` if lot_size still 0 after all checks | ✅ |
| Config verification printout (`[CONFIG VERIFICATION]`) | ✅ |
| `max_sl_1lot` diagnostic output | ✅ |

**No changes needed.**

---

## ✅ DEFECT #2 — Post-Fill Cap Check in `trade_manager.cpp`: CORRECT

The post-fill block is correctly placed immediately after `execute_entry()` returns, uses `fill_price` (not `decision.entry_price`), and handles both the rejection path (square-off) and the reduction path (affordable lots).

| Check | Status |
|-------|--------|
| Placed after `fill_price` is known (after `execute_entry()`) | ✅ |
| Computes `actual_fill_risk = |fill_price - decision.stop_loss| × pos × lot_size` | ✅ |
| Rejects with square-off when `affordable_at_fill < 1` | ✅ |
| Reduces position to `affordable_at_fill` lots when 1 lot is still viable | ✅ |
| Updates `total_units` after reduction | ✅ |
| Logs all decisions clearly | ✅ |

**No changes needed.**

---

## ❌ DEFECT #3 — Stage 2 Trail Formula for SHORT: INCORRECT

### The Bug in the Submitted Fix

Both `live_engine.cpp` and `backtest_engine.cpp` contain:

```cpp
// Stage 2: Lock 0.5R profit floor
// FIX: For both LONG and SHORT, trail_stop = entry + (risk × 0.5)
new_trail_stop = trade.entry_price + (risk * 0.5);
```

This is **mathematically correct for LONG but wrong for SHORT.** Here is the proof:

#### SHORT direction mechanics
- SHORT profits when price **falls**. Stop loss must be **above** current price to be protective.
- After Stage 1 breakeven: `trade.stop_loss = entry_price` (e.g. 25354.80)
- Stage 2 aims to lock 0.5R of profit: the SL should trail **downward** to lock in gains.
- The ratchet for SHORT moves SL **down**: fires when `new_trail_stop < trade.stop_loss`

#### With the submitted fix (`entry + risk × 0.5`):
```
entry = 25354.80, risk = 245.55
new_trail_stop = 25354.80 + 122.78 = 25477.58

Ratchet check: 25477.58 < trade.stop_loss (25354.80)?  → FALSE
```
**The ratchet NEVER fires.** Stage 2 is silently skipped for every SHORT trade. Profits above 1R are not protected.

If the SL somehow were placed at 25477.58 for a SHORT, it would also guarantee a **loss** of 0.5R (price must rise above 25477 to trigger it, which is above the entry of 25354).

#### With the correct fix (`entry - risk × 0.5` for SHORT):
```
entry = 25354.80, risk = 245.55
new_trail_stop = 25354.80 - 122.78 = 25232.02

Ratchet check: 25232.02 < trade.stop_loss (25354.80)? → TRUE ✓
```
SL moves down from 25354.80 to 25232.02. If price reverses upward and hits 25232.02:
- `check_stop_loss` triggers: `bar.high >= 25232.02` → exits SHORT
- P&L = (25354.80 - 25232.02) × lots = +122.78 pts = **+0.5R profit locked** ✓

### Correct Fix for Both Files

**Find and replace in both `live_engine.cpp` and `backtest_engine.cpp`:**

```cpp
// WRONG — submitted:
} else if (current_r >= 1.0) {
    // Stage 2: Lock 0.5R profit floor
    // FIX: For both LONG and SHORT, trail_stop = entry + (risk × 0.5)
    new_trail_stop = trade.entry_price + (risk * 0.5);
```

```cpp
// CORRECT — replace with:
} else if (current_r >= 1.0) {
    // Stage 2: Lock 0.5R profit floor
    // LONG:  entry + risk×0.5  (SL above entry, ratchet moves UP)
    // SHORT: entry - risk×0.5  (SL below entry, ratchet moves DOWN)
    // Both lock 0.5R of realised profit if price reverses to this level.
    new_trail_stop = (trade.direction == "LONG")
        ? trade.entry_price + (risk * 0.5)
        : trade.entry_price - (risk * 0.5);
```

### Why this is correct for both directions

| Direction | Stage 2 formula | new_trail (example) | Ratchet fires? | P&L if triggered |
|-----------|----------------|---------------------|----------------|-----------------|
| LONG (entry=25500, risk=200) | entry + risk×0.5 = 25600 | 25600 | 25600 > 25500 ✅ | +100pts = +0.5R ✅ |
| SHORT (entry=25354, risk=245) | entry - risk×0.5 = 25232 | 25232 | 25232 < 25354 ✅ | +122pts = +0.5R ✅ |

---

## ✅ DEFECT #1 — Stale Binary

This is a build-process issue, not a code issue. The report correctly identified it. **After applying the Stage 2 correction above, rebuild the binary:**

```bash
cmake --build . --target sd_trading_system

# Confirm binary is newer than all modified source files:
ls -la sd_trading_system
ls -la src/backtest/trade_manager.cpp
ls -la src/live/live_engine.cpp
ls -la src/backtest/backtest_engine.cpp
ls -la src/EngineFactory.cpp
```

---

## Action Required

1. In **`live_engine.cpp`** — replace the Stage 2 block (approx. line 2068–2071):
   ```cpp
   // WRONG:
   new_trail_stop = trade.entry_price + (risk * 0.5);
   
   // CORRECT:
   new_trail_stop = (trade.direction == "LONG")
       ? trade.entry_price + (risk * 0.5)
       : trade.entry_price - (risk * 0.5);
   ```

2. In **`backtest_engine.cpp`** — apply the identical replacement (approx. line 837–839).

3. Rebuild the binary.

4. Run a backtest and verify:
   - No trades appear with `Risk_Amount > max_loss_per_trade` in the output CSV.
   - SHORT trades with exit reason `TRAIL_SL` show `Stop_Loss` **below** their `Entry_Price` at close (not at or above it).
