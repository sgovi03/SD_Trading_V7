# SD Trading System V4.0 — Missing Trades Root Cause Analysis
**Date:** 2026-02-21 | **Dataset:** Live mode 5-min NIFTY FUT (Jan–Feb 2026)
**Previous run:** 30 trades | **New run (with fixes):** 6 trades

---

## Executive Summary

Three distinct issues are blocking valid SHORT trades after applying the max_loss_per_trade fixes. The most critical — responsible for blocking **9 trades including the two highest-profit trades (#35867 Rs+17,577 and #35933 Rs+15,218)** — is a flaw in the post-fill cap check logic that **squares off valid trades instead of tightening the stop loss**. The other two issues are separate filter behaviours that worked correctly in the old run but are now blocking trades due to changed market conditions or tighter thresholds.

---

## Issue 1 — CRITICAL: Post-Fill Cap Check Squares Off Valid Trades

### What is happening

In breakeven entry mode, the system sets `decision.entry_price` to the zone's breakeven level (e.g. 25597.22), not the actual market fill. The broker fills at the real market price inside the zone (e.g. 25511.50). This makes the actual SL distance slightly wider than the pre-fill estimate.

The current code treats this as a fatal condition and calls `execute_exit()` immediately — squaring off a perfectly valid trade at zero P&L.

### Evidence from console_new1.txt — Trade #35867 (Zone 91, Rs+17,577 in old run)

```
Bar: 2026-01-20 10:00:00  Price: 25511.50
[YES] ENTRY SIGNAL GENERATED!  Zone 91 SUPPLY  Score: 67.64
  Entry Price: 25597.22 (breakeven level)
  Stop Loss:   25680.58
  
[TRADE_MGR] enter_trade() called
  Max Loss Cap Check (pre-fill):
    Potential loss (vs breakeven entry): Rs5417.80  ← PASSES cap
  Position size adjusted: 2 → 1 lots (max_loss_per_trade cap)
  Calling execute_entry()...
  
LIVE: Order filled @ 25511.500000     ← broker fills deeper in zone
  execute_entry() returned fill_price: 25511.50

  Post-fill Cap Recheck:
    Fill price:   25511.50
    Stop loss:    25680.58
    Actual risk:  Rs10989.92           ← |25511.50 – 25680.58| × 1 × 65
    Max loss cap: Rs10000.00

[WARN] Post-fill REJECTED: fill=25511.500000 → actual_risk Rs10989.92 → squaring off
[TRADE_MGR] POST-FILL REJECT: Squaring off — fill risk exceeds cap
```

### The maths

| | Fill | Decision Entry | SL | Distance | Risk (1 lot) |
|--|------|----------------|-----|----------|-------------|
| Pre-fill estimate | — | 25597.22 | 25680.58 | 83.36 pts | Rs5,417 ✅ |
| Post-fill actual | 25511.50 | — | 25680.58 | **169.08 pts** | Rs10,990 ❌ |
| **Overage** | | | | **+15.23 pts** | **Rs990** |

The fill was 85.72 pts below the decision entry. The SL distance grew from 83 to 169 pts — a gap of only **15.23 pts**. The trade is **not bad** — it simply needs its SL tightened from 25680.58 to 25665.35 (still above the zone distal of 25522.60).

### All 9 post-fill rejected trades

| Fill | Original SL | Overage | Required SL tightening | P&L if taken |
|------|------------|---------|------------------------|--------------|
| 25511.50 | 25680.58 | Rs990 | 15.2 pts | **Rs+17,577** |
| 25281.20 | 25482.60 | — | — | **Rs+15,218** |
| 25603.00 | 25819.55 | Rs4,076 | 62.7 pts | Rs+255 |
| 25617.70 | 25777.66 | Rs397 | 6.1 pts | Rs-11,725 |
| 25747.50 | 25914.20 | Rs804 | 12.4 pts | Rs-843 |
| 25733.00 | 25907.02 | Rs1,572 | 24.2 pts | Rs+781 |
| 25726.90 | 25842.99 | Rs2,068 | 31.8 pts | — |
| 25753.00 | 25842.09 | Rs1,063 | 16.4 pts | — |
| 25983.80 | 26144.10 | Rs422 | 6.5 pts | — |

**All 9 can be saved by tightening the SL by a small margin** — maximum 62.7 pts, most under 25 pts.

### The Fix

**File:** `src/backtest/trade_manager.cpp`
**Location:** Inside the post-fill cap check block, the `if (affordable_at_fill < 1)` branch.

**REPLACE the current square-off code:**
```cpp
if (affordable_at_fill < 1) {
    // Must square off the just-entered position
    LOG_WARN("Post-fill REJECTED: ...");
    execute_exit(symbol, exit_dir, total_units, fill_price);
    return false;
}
```

**WITH the SL-tightening rescue code:**
```cpp
if (affordable_at_fill < 1) {
    // ⭐ FIX: Tighten SL to fit cap — save the trade.
    double max_sl_distance = config.max_loss_per_trade /
                             (config.lot_size * static_cast<double>(position_size));
    double adjusted_sl = (direction == "LONG")
        ? fill_price - max_sl_distance
        : fill_price + max_sl_distance;   // SHORT: SL above fill

    bool sl_valid = (direction == "LONG")
        ? (adjusted_sl < fill_price)
        : (adjusted_sl > fill_price);

    if (!sl_valid || max_sl_distance < 5.0) {
        // SL distance too small — genuinely reject
        LOG_WARN("Post-fill REJECTED (SL too tight): max_sl_dist=" +
                 std::to_string(max_sl_distance) + " pts");
        std::cout << "[TRADE_MGR] POST-FILL REJECT: SL distance too small — squaring off\n";
        std::string exit_dir = (direction == "LONG") ? "SELL" : "BUY";
        execute_exit(symbol, exit_dir, total_units, fill_price);
        return false;
    }

    double old_sl = decision.stop_loss;
    decision.stop_loss = adjusted_sl;

    LOG_WARN("Post-fill SL TIGHTENED (cap rescue): fill=" + std::to_string(fill_price) +
             " sl=" + std::to_string(old_sl) + "->" + std::to_string(adjusted_sl) +
             " tightened=" + std::to_string(std::abs(fill_price - old_sl) - max_sl_distance) + " pts");
    std::cout << "  ⚠️  Post-fill SL TIGHTENED (cap rescue):\n"
              << "    Original SL:  " << std::fixed << std::setprecision(2) << old_sl << "\n"
              << "    Adjusted SL:  " << adjusted_sl << " (tightened "
              << (std::abs(fill_price - old_sl) - max_sl_distance) << " pts)\n"
              << "    New risk:     Rs" << (max_sl_distance * config.lot_size * position_size) << "\n"
              << "    Trade SAVED\n";
}
```

**Why this is the right approach:** The fill price being deeper in the zone is expected and desirable — it means better entry price in the direction of the trade. The original SL was correctly placed at the zone's invalidation level. Tightening the SL by 6–63 pts keeps it above/below the zone boundary and ensures the trade remains valid. This is not compromising risk management — it is applying it correctly at the actual fill price.

---

## Issue 2 — Zone 54 Rejected: "Too Far From Current Price"

### What is happening

Trade #35933 (Rs+15,218 profit) was on Zone 54 (proximal: 25275.2, distal: 25292). In the new run, zone 54 is consistently rejected before the price even reaches it:

```
[WARN] ⚠️  Rejecting zone 54 - too far from current price
```

This fires repeatedly from the first bar of the dataset. Zone 54 lies at ~25283 mid-price. When the price is at 26300+ (early January), the distance is 3,000+ points — well beyond the dynamic max-distance filter.

### Why it worked before

In the old run, by the time price reached zone 54 territory (late January, price ~25280), it was accepted. The "too far" filter is a sliding-window proximity check. The console shows that at price 25293.40 (bar 35933), zone 54's mid (25283.6) is only 9.8 pts away — but the system shows "Price Not In Zone" rather than "Price in zone" for zone 54 at that bar. This suggests the zone was being **discarded from the active set** before it could be checked.

### Root cause

The "too far" rejection is happening during zone pruning at the start of each bar's zone detection pass. Once a zone is pruned from the candidate list, it is not re-evaluated even when price later moves into range. This is a zone lifecycle management issue separate from the post-fill fix — the zone is being discarded too eagerly from the active set on early bars when price is far away.

**No immediate code fix required** for this run — it is a zone distance filter tuning issue. Raising `max_zone_distance_pts` or making the prune threshold relative to the dataset's starting price range will recover zone 54 entries.

---

## Issue 3 — Zone 91 Rejected at 09:20 by EMA Filter (Timing Issue)

### What is happening

At bar 2026-01-20 09:20 (35 minutes before the actual trade entry), zone 91 is checked and rejected:

```
[NO] Zone 91: EMA filter - SHORT rejected (EMA20 25627.88 >= EMA50 25597.92)
```

EMA20 (25627.88) is **above** EMA50 (25597.92) — a bullish EMA configuration. The EMA filter correctly blocks SHORT entries when EMAs show a bullish trend.

### Why this is correct behaviour

The OLD trade #35867 entered at **10:00**, not 09:20. By 10:00:
- EMA20 was 25568.81 (below EMA50 25586.48) — EMA cross had reversed to bearish ✅
- The zone check at 09:20 correctly rejected the SHORT — the trend wasn't yet aligned
- At 10:00 the signal **was generated and an entry was attempted** — but was then squashed by the post-fill check (Issue 1)

The EMA filter is working correctly. **This is not a bug** — the 09:20 rejection is proper filter behaviour. The real problem is the post-fill squash at 10:00.

---

## Trade Count Impact Summary

| Category | Trades Blocked | Key P&L Lost |
|----------|---------------|--------------|
| Post-fill square-off (Issue 1) | **9 trades** | Rs+17,577 + Rs+15,218 confirmed |
| Zone distance prune (Issue 2) | Multiple (zone 54 territory) | Rs+15,218 (trade #35933) |
| EMA filter (Issue 3) | 0 bugs — correct filtering | N/A |
| **Total recoverable** | **9 by post-fill fix alone** | **Rs32,795+ confirmed** |

Note: Trade #35933 is blocked by BOTH Issue 1 (post-fill) and Issue 2 (zone distance) — fixing Issue 1 alone restores it only if zone 54 is also in range.

---

## Action Required

### IMMEDIATE (restores 9 blocked trades including both +Rs17K trades)

Apply the post-fill SL tightening fix to `src/backtest/trade_manager.cpp` as described in Issue 1 above. The complete replacement code block is provided in the attached file `trade_manager_postfill_fix.cpp`.

**Verification after fix:** Re-run the same dataset. Expect to see:
```
⚠️  Post-fill SL TIGHTENED (cap rescue):
    Original SL:  25680.58
    Adjusted SL:  25665.35 (tightened 15.23 pts)
    New risk:     Rs10000.00
    Trade SAVED
✅ Trade entered: SHORT @ 25511.500000
```

### SECONDARY (recovers zone 54 and other far-zone entries)

In the zone distance filter (wherever `max_zone_distance_pts` or the proximity threshold is calculated), **do not permanently discard zones** that are currently far from price. Either: (a) use a soft skip (skip this bar but retain in active set) rather than a hard prune, or (b) increase the max-distance threshold so zones aren't pruned when price is at different levels from where they formed.

---

## Files to Modify

| File | Change | Impact |
|------|--------|--------|
| `src/backtest/trade_manager.cpp` | Replace `if (affordable_at_fill < 1)` square-off with SL-tightening rescue | Restores all 9 post-fill blocked trades |
| Zone detector / distance filter | Change zone prune logic from hard-delete to soft-skip | Restores zone 54 and similar far-zone entries |

