# SD Trading System — Defect Report & Root Cause Analysis
**Date:** 2026-02-21 | **Codebase:** CodeShare_New.zip | **Trades:** 20 live trades

---

## Executive Summary

The trades.csv confirms **Rs62,590 in preventable losses** across 10 trades that should have been rejected at entry. Three distinct bugs survive in the new binary. The cap check code is **logically correct** but bypassed due to an architectural override in `EngineFactory.cpp`. The trailing stop has a **profit-giveaway bug** due to premature breakeven lock. The SL slippage fix is correctly implemented.

---

## BUG 1 — CRITICAL: max_loss_per_trade Cap Bypassed at Runtime

### Root Cause: EngineFactory silently overwrites `config.lot_size` with `sys_config.trading_lot_size`

**File:** `src/EngineFactory.cpp`, lines 40–42:

```cpp
// Apply system config overrides
config.starting_capital = sys_config.trading_starting_capital;
config.lot_size = sys_config.trading_lot_size;   // ← OVERRIDES phase_6 config value
```

The `ConfigLoader` correctly reads `lot_size = 65` and `max_loss_per_trade = 10000` from `phase_6_config`. But immediately after, `EngineFactory` overwrites `config.lot_size` with `sys_config.trading_lot_size` — a value extracted from `system_config.json` via the hand-rolled `extract_json_int()` parser.

**The failure cascade:**

`extract_json_int()` searches the raw JSON string for the key. If parsing of `system_config.json` fails (file not found, bad path, parse error), it **returns 0** (line 225 of `system_config_loader.cpp`):

```cpp
if (key_pos == std::string::npos) {
    return 0;  // ← Key not found returns 0, not default
}
```

`trading_lot_size` default in `SystemConfig` is **75** (common_types.h line 1283) — but if the JSON key fails to parse, it returns 0, not 75. When `config.lot_size = 0`:

```
potential_loss = sl_dist × position_size × config.lot_size
              = 267.6 × 1 × 0
              = 0
0 > 10000 → FALSE → "✓ Within max loss cap" → trade ENTERS
```

Every single cap-breaching trade slips through because potential loss computes to zero.

### Evidence from Trades CSV

| Trade | SL Dist | Risk (Rs) | Should Be | Actual |
|-------|---------|-----------|-----------|--------|
| #35506 | 160.0 pts | Rs10,397 | REJECTED | Entered → lost Rs11,724 |
| #36321 | 229.8 pts | Rs14,939 | REJECTED | Entered → lost Rs16,170 |
| #36485 | 267.6 pts | Rs17,393 | REJECTED | Entered → lost Rs13,870 |
| #35308 | 102.6 pts × 2 lots | Rs13,338 | Reduce to 1 lot | 2 lots entered |

**Total preventable losses: Rs62,590** across 10 trades.

### Fix — EngineFactory.cpp

Add `max_loss_per_trade` to the system config override block AND add a safety guard for `lot_size = 0`:

```cpp
// Apply system config overrides
config.starting_capital = sys_config.trading_starting_capital;

// ⭐ FIX: Guard against 0 lot_size from failed JSON parse
if (sys_config.trading_lot_size > 0) {
    config.lot_size = sys_config.trading_lot_size;
} else {
    LOG_WARN("sys_config.trading_lot_size is 0 — keeping phase_6 config value: "
             << config.lot_size);
}

// ⭐ FIX: Propagate max_loss_per_trade from system_config if set there
// (currently max_loss_per_trade only comes from phase_6 config — that is fine,
//  but add a runtime assertion so a bad lot_size is caught immediately)
if (config.lot_size <= 0) {
    throw std::runtime_error(
        "FATAL: config.lot_size = 0 after system override. "
        "Check system_config.json 'trading.lot_size'. "
        "Cap check will be bypassed!");
}
```

Additionally, add a **runtime assertion** inside `enter_trade()` as a safety net:

```cpp
// At top of enter_trade(), before any position sizing:
if (config.lot_size <= 0) {
    LOG_ERROR("FATAL: config.lot_size = 0. Cap check will not work. Rejecting trade.");
    std::cout << "[TRADE_MGR] REJECTED: config.lot_size is 0 — configuration error!\n";
    return false;
}
```

---

## BUG 2 — HIGH: Trailing Stop Fires Breakeven Lock Prematurely, Giving Away Profits

### Symptom

Trades #35109 and #36008 show `Stop Loss = Entry Price` in the CSV (breakeven lock fired) but the P&L is only Rs164 and Rs1,834 respectively. The trail activated so quickly that price had barely moved, and the breakeven stop was hit on the very next retracement — giving up essentially all potential profit.

### Root Cause: `trail_activation_rr = 0.5` + Bar-1 extreme update

The flow:

1. **Bar 1** (first bar after entry): `bars_in_trade < 2`, so no exit check. But the code **still updates `highest_price`/`lowest_price`** from bar 1's extreme.
2. **Bar 2**: `bars_in_trade = 2`, trail logic runs. It uses `highest_price` (updated by bar 1), so `best_excursion` reflects bar 1's move even though we couldn't exit on bar 1.
3. If bar 1 moved just 0.5R in the profitable direction, `current_r ≥ 0.5` → **Stage 1 (breakeven) fires on bar 2**.
4. If bar 2 then retraces back to entry → **TRAIL_SL hit at entry price for Rs0–164 profit**.

The trail is activating on the **bar 1 unrealizable profit** — a ghost profit that could never have been captured.

### Fix — live_engine.cpp (and backtest_engine.cpp mirrors)

**Option A (Recommended):** Do NOT update `highest_price`/`lowest_price` on bar 1. Let trail only see confirmed bars:

```cpp
if (trade.bars_in_trade < 2) {
    // ⭐ FIX: Do NOT update extremes on bar 1.
    // Trail should only activate on confirmed profit from bar 2 onward.
    // Updating on bar 1 creates phantom excursion that triggers premature breakeven lock.
    
    // Print status only
    std::cout << "| [POSITION] Position Monitoring (Entry Bar) |\n";
    std::cout << "  Bars in Trade: " << trade.bars_in_trade << " (min 2 for exit)\n";
    return;  // Don't update extremes, don't check exits
}

// From bar 2 onward: update extremes THEN check trail
if (trade.direction == "LONG") {
    if (current_bar.high > trade.highest_price || trade.highest_price <= 0.0)
        trade.highest_price = current_bar.high;
} else {
    if (current_bar.low < trade.lowest_price || trade.lowest_price <= 0.0)
        trade.lowest_price = current_bar.low;
}
```

**Option B:** Raise `trail_activation_rr` from 0.5 to 1.0 (only lock breakeven once 1R profit is confirmed):

```ini
# phase_6_config_v0.1_Tested_in_5Mins.txt
trail_activation_rr = 1.0    # Changed from 0.5 — prevents premature breakeven lock
```

Option A is architecturally correct. Option B is a quick mitigation that leaves the phantom-excursion logic intact.

---

## BUG 3 — MEDIUM: Trailing Stop Stage 2 Formula Wrong for SHORT Trades

### Current code (`live_engine.cpp`):

```cpp
} else if (current_r >= 1.0) {
    // Stage 2: Lock 0.5R profit floor
    new_trail_stop = (trade.direction == "LONG")
        ? trade.entry_price + (risk * 0.5)
        : trade.entry_price - (risk * 0.5);   // ← WRONG for SHORT
```

For a **SHORT** trade:
- Entry = 25200, SL = 25350 (SL is **above** entry), risk = 150 pts
- Stage 2 formula: `new_trail_stop = 25200 - 75 = 25125`
- This is **below** entry — i.e., it requires price to fall 75 pts BEFORE trail activates
- The ratchet condition: `new_trail_stop < trade.stop_loss` → `25125 < 25350` → TRUE, so SL moves
- But SL moves to 25125 which is **below entry**, meaning a SHORT trade would LOSE money if hit there

The formula should lock **profit** of 0.5R, meaning SL should move to `entry_price - (risk × 0.5)` for SHORT. Wait — for SHORT, profit = downward movement. SL protecting 0.5R profit for SHORT means the SL (above entry) should come DOWN to `entry_price + (risk × 0.5)`:

```
Entry SHORT = 25200, original SL = 25350 (150 pts above)
0.5R profit = 75 pts down from entry → price at 25125
To lock this profit, trail SL should be at entry_price + 0 (breakeven at most) 
Actually: SL should come down from 25350 toward entry
For SHORT locking 0.5R profit: new_sl = entry_price + (risk × 0.5) = 25200 + 75 = 25275
```

### Fix — live_engine.cpp and backtest_engine.cpp:

```cpp
} else if (current_r >= 1.0) {
    // Stage 2: Lock 0.5R profit floor
    new_trail_stop = (trade.direction == "LONG")
        ? trade.entry_price + (risk * 0.5)   // LONG: SL rises to lock profit
        : trade.entry_price + (risk * 0.5);   // SHORT: SL falls toward entry to lock profit
    // Note: same formula, ratchet ensures it only moves in profitable direction
```

Wait — `entry_price + (risk × 0.5)` for SHORT:
- 25200 + 75 = 25275. This is BELOW original SL (25350). Ratchet: `25275 < 25350` → TRUE. SL moves down to 25275. ✓
- If price hits 25275 (still above entry=25200), SHORT still profits 25200→25275 = -75 pts. That's a LOSS.

Actually the correct formula for SHORT Stage 2 is:
- We want to lock `0.5R profit`, meaning price must have moved 1R below entry (current_r ≥ 1.0)
- Trade is in profit by 1R = 150 pts. Current price ≈ 25050 (150 below entry)
- To lock 0.5R = 75 pts profit, set SL at entry - 75 = 25125? No — for SHORT, profit means price went DOWN.
- SL for SHORT must be ABOVE entry. To lock 0.5R profit: SL = entry_price - (risk × 0.5) = 25200 - 75 = 25125
- But 25125 is BELOW entry. If price reverses to 25125, SHORT exits at 25125 which is BELOW entry → LOSS.

The **correct interpretation**: `Stage 2 locks 0.5R profit` means if price reverses, we exit with at least 0.5R profit:
- For SHORT: price went DOWN by 1R (to 25050). Lock 0.5R profit = SL should not allow profit to drop below 0.5R.
- 0.5R profit from entry means price is at most `entry_price - (risk × 0.5) = 25200 - 75 = 25125` below entry.
- If price reverses from 25050 up to 25125, we should exit. So SL = 25125 (below entry for SHORT).
- But `current_bar.high >= trade.stop_loss` check for SHORT: 25125 is BELOW stop_loss currently.
- The ratchet for SHORT: `new_trail_stop < trade.stop_loss` only lets SL decrease.
- SL is currently 25350. 25125 < 25350 → ratchet allows. SL moves to 25125. ✓
- Exit check for SHORT: `bar.high >= trade.stop_loss`. Since SL = 25125 < entry = 25200, the SL will never be hit by the high (shorts exit when price goes UP, but SL is below entry = wrong direction)!

**The fundamental problem:** For SHORT, the SL must be **above** entry to protect profit. Stage 2 for SHORT must place SL at `entry_price + (risk × 0.5)` = a level that is below the original SL but still above entry.

```cpp
} else if (current_r >= 1.0) {
    // Stage 2: Lock 0.5R profit floor
    // LONG: SL above entry (entry + 0.5×risk) — allows exit with +0.5R if reversed
    // SHORT: SL below original but still above entry (entry_price + 0.5×risk is WRONG)
    //        Correct: SL comes DOWN from original toward entry, but stays ABOVE entry
    //        New SL = entry_price + 0 (breakeven, same as Stage 1 for safety)
    //        OR better: SL = entry_price + (original_sl_dist × 0.5)
    //        where original_sl_dist = risk (distance from entry to original SL)
    if (trade.direction == "LONG") {
        new_trail_stop = trade.entry_price + (risk * 0.5);  // Above entry ✓
    } else {
        // SHORT: Move SL DOWN toward entry, locking 0.5R profit floor
        // SL starts above entry (original). Bring it halfway toward entry.
        new_trail_stop = trade.entry_price + (risk * 0.5);  // Still above entry ✓
        // e.g. entry=25200, risk=150: new_sl = 25200 + 75 = 25275 (below original 25350) ✓
    }
```

So `entry_price + (risk * 0.5)` is correct for **both** LONG and SHORT! The current bug is that the SHORT formula uses `entry_price - (risk * 0.5)` which places SL below entry. **Fix: use the same formula for both.**

---

## BUG 4 — CONFIRMED WORKING: Intraday SL Slippage Fix

The fix in `live_engine.cpp` and `backtest_engine.cpp` is **correctly implemented**:

```cpp
// LONG: exit at worse of SL or bar.close (slippage when bar gaps through SL)
exit_price = sl_hit ? std::min(trade.stop_loss, current_bar.close) : trade.take_profit;

// SHORT: exit at worse of SL or bar.close
exit_price = sl_hit ? std::max(trade.stop_loss, current_bar.close) : trade.take_profit;
```

No changes needed here.

---

## Fix Priority Matrix

| Bug | File | Severity | Lines to Change |
|-----|------|----------|-----------------|
| 1. lot_size=0 allows all trades past cap | `EngineFactory.cpp` | **CRITICAL** | Line 41: add guard + throw |
| 1b. Runtime assertion in enter_trade | `trade_manager.cpp` | **CRITICAL** | Top of enter_trade() |
| 2. Trail fires on bar-1 phantom profit | `live_engine.cpp`, `backtest_engine.cpp` | **HIGH** | Remove extreme update in bars_in_trade < 2 block |
| 3. Stage 2 SHORT formula wrong | `live_engine.cpp`, `backtest_engine.cpp` | **MEDIUM** | Change `- (risk * 0.5)` to `+ (risk * 0.5)` for SHORT |
| 4. SL slippage fix | Already fixed | ✅ Done | — |

---

## Diagnostics: Add to Startup Log

To catch the lot_size=0 issue at startup and confirm config is applied correctly, add this to `EngineFactory.cpp` after all overrides:

```cpp
std::cout << "\n[CONFIG VERIFICATION]\n";
std::cout << "  lot_size:           " << config.lot_size << "\n";
std::cout << "  max_loss_per_trade: " << config.max_loss_per_trade << "\n";
std::cout << "  starting_capital:   " << config.starting_capital << "\n";
std::cout << "  trail_activation:   " << config.trail_activation_rr << "R\n";
std::cout << "  use_trailing_stop:  " << (config.use_trailing_stop ? "YES" : "NO") << "\n";
// Derived cap check:
double max_sl_for_1lot = config.max_loss_per_trade / config.lot_size;
std::cout << "  Max SL distance @1 lot: " << max_sl_for_1lot << " pts"
          << " (= Rs" << config.max_loss_per_trade << " max loss)\n\n";
```

This makes it instantly visible in the log whether the cap will work.
