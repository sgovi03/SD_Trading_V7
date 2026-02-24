# SD Trading System V4.0 — Volume Analysis Code Review
## Against: `entry_scoring_code_review.md` Specification
**Date:** February 17, 2026 | **Source:** `Volume_analysis_code.zip`

---

> ### ⚠️ OVERALL VERDICT: 7 OF 9 ITEMS COMPLETE — 2 CRITICAL GAPS REMAIN
> Structs, scoring, zone_manager touch tracking, and CSV header are done.
> The pullback filter function **exists but is never called**.
> The zone touch data source is **wrong** (`zone.bars` does not exist on `Zone` struct).

---

## Quick Status — All Review Items

| Review Item | File | Status | Finding |
|---|---|---|---|
| `volume_rising_on_retests` SET in `zone_manager.cpp` | `zone_manager.cpp` | ✅ DONE | Correctly set at line 168 inside `mark_zone_as_tested()` |
| `touch_volumes` populated on each zone touch | `zone_manager.cpp` | ⚠️ PARTIAL | Logic correct but reads `zone.bars` which does **NOT** exist on `Zone` struct — always zero |
| `calculate_entry_volume_metrics()` implemented | `entry_decision_engine.cpp` | ✅ DONE | Full implementation present with correct pullback scoring logic |
| `calculate_entry_volume_metrics()` **CALLED** in `calculate_entry()` | `entry_decision_engine.cpp` | 🔴 MISSING | Function **never called anywhere**. Pullback filter is completely dormant |
| `bar_history` passed into `calculate_entry()` | `backtest_engine.cpp` | 🔴 MISSING | Signature has no `bar_history` param. Declining trend check always skipped |
| `Trade` struct — all 12 new volume fields declared | `common_types.h` | ✅ DONE | All 12 fields present at lines 510–525 |
| `Trade` constructor — all fields initialised | `common_types.h` | ⚠️ PARTIAL | 10 of 15 fields in initialiser list; 5 rely on C++11 in-class defaults only |
| `enter_trade()` — zone volume fields populated | `trade_manager.cpp` | ⚠️ PARTIAL | Only 3 of 10 zone fields assigned. `zone_is_initiative`, `zone_has_volume_climax`, `zone_volume_score`, `zone_institutional_index`, `entry_position_lots` all missing |
| `close_trade()` — exit volume fields populated | `trade_manager.cpp` | ⚠️ PARTIAL | `exit_volume_ratio` set correctly. `exit_was_volume_climax` never set — always `false` in CSV |
| CSV reporter — all 12 columns in header + row | `csv_reporter.cpp` | ⚠️ PARTIAL | All 12 header columns present. Row **missing `zone_volume_score`** — header/row offset by 1 from column 32 onward |

---

## 1. Your Question: Is `volume_rising_on_retests` Actively SET in `volume_scorer.cpp`?

**NO — `volume_scorer.cpp` does NOT set it. It only READS it. This is correct architecture.**

`volume_scorer.cpp` line 155 reads the flag to apply the −30pt penalty:

```cpp
// volume_scorer.cpp line 155 — READS the flag (correct)
if (profile.volume_rising_on_retests) {
    score -= 30.0;   // penalty applied IF flag is true
}

// volume_scorer.cpp — does NOT set volume_rising_on_retests anywhere
// It is not volume_scorer's responsibility to set it
```

The flag **IS set** — but in `zone_manager.cpp`, not `volume_scorer.cpp`. This is the correct architectural separation: `zone_manager` tracks zone state across touches; `volume_scorer` scores a snapshot. The set location is:

```cpp
// zone_manager.cpp — mark_zone_as_tested() line 168
zone.volume_profile.volume_rising_on_retests = (rising_count >= 2);
```

---

### ⚠️ Critical Problem: The Data Feeding `touch_volumes` Is Always Zero

The implementation logic in `mark_zone_as_tested()` is correct but reads from `zone.bars` — **a member that does NOT exist on the `Zone` struct**. This means `touch_vol` is always `0.0`, `touch_vol_ratio` is always `0.0`, and `volume_rising_on_retests` can **never become `true`** despite the code looking correct.

```cpp
// zone_manager.cpp — BROKEN: zone.bars does not exist on Zone struct
std::string slot = volume_baseline_->extract_time_slot(
    zone.bars.empty() ? "" : zone.bars.back().datetime);  // ❌ zone.bars is undefined
double touch_vol = zone.bars.empty() ? 0.0 : zone.bars.back().volume; // ❌ always 0.0
```

**The `Zone` struct only has `bars_to_retest` (an `int`) and `bars_inside_after_sweep` (an `int`) — there is no `std::vector<Bar> bars` member.**

### Fix — Pass Current Bar into `mark_zone_as_tested()`

`backtest_engine.cpp::update_zone_states()` already has the current `bar` available where `touch_count++` happens.

**Step 1 — `zone_manager.h` update signature:**
```cpp
void mark_zone_as_tested(Zone& zone, double touch_price,
                          const Bar* touch_bar = nullptr);  // ADD
```

**Step 2 — `zone_manager.cpp` use `touch_bar` instead of `zone.bars`:**
```cpp
void ZoneManager::mark_zone_as_tested(Zone& zone, double touch_price,
                                       const Bar* touch_bar) {
    zone.state = ZoneState::TESTED;
    zone.touch_count++;

    // V6.0: Record normalised volume at this touch
    if (volume_baseline_ && volume_baseline_->is_loaded() && touch_bar != nullptr) {
        std::string slot = volume_baseline_->extract_time_slot(touch_bar->datetime);
        double ratio = volume_baseline_->get_normalized_ratio(slot, touch_bar->volume);
        zone.volume_profile.touch_volumes.push_back(ratio);
    }

    // Rising count check — unchanged
    if (zone.volume_profile.touch_volumes.size() >= 3) {
        const auto& tv = zone.volume_profile.touch_volumes;
        int n = tv.size();
        int rising_count = 0;
        for (int i = n - 2; i >= std::max(0, n - 3); --i)
            if (tv[i + 1] > tv[i]) rising_count++;
        zone.volume_profile.volume_rising_on_retests = (rising_count >= 2);
    }
    // ... rest unchanged
}
```

**Step 3 — `backtest_engine.cpp::update_zone_states()` inline fix at both `touch_count++` locations:**

```cpp
// backtest_engine.cpp — update_zone_states() — add after EACH zone.touch_count++
if (volume_baseline_ && volume_baseline_->is_loaded()) {
    std::string slot = extract_time_slot(bar.datetime);
    double ratio = volume_baseline_->get_normalized_ratio(slot, bar.volume);
    zone.volume_profile.touch_volumes.push_back(ratio);

    if (zone.volume_profile.touch_volumes.size() >= 3) {
        const auto& tv = zone.volume_profile.touch_volumes;
        int n = tv.size();
        int rising_count = 0;
        for (int i = n - 2; i >= std::max(0, n - 3); --i)
            if (tv[i + 1] > tv[i]) rising_count++;
        zone.volume_profile.volume_rising_on_retests = (rising_count >= 2);
    }
}
```

---

## 2. Critical Gap — `calculate_entry_volume_metrics()` Never Called

The function is correctly implemented (lines 23–78 of `entry_decision_engine.cpp`). But a codebase-wide grep finds **zero calls** to it:

```
grep result across all .cpp files:
  entry_decision_engine.h:74    — declaration only
  entry_decision_engine.cpp:23  — implementation only
  ZERO calls from calculate_entry() or anywhere else
```

`calculate_entry()` still only calls the old formation-time check:

```cpp
// Still using WRONG formation-time volume (unchanged since previous version):
bool vol_passed = validate_entry_volume(zone, *current_bar,
                                         zone_score_val, vol_rejection);
// ↑ checks zone.volume_profile.volume_ratio — set at zone CREATION time, not pullback time
```

**The Sam Seiden pullback filter is 100% dormant despite being correctly implemented.**

### Fix Part A — Add `bar_history` to `calculate_entry()` signature

**`entry_decision_engine.h`:**
```cpp
EntryDecision calculate_entry(
    const Zone& zone,
    const ZoneScore& score,
    double atr,
    MarketRegime regime = MarketRegime::RANGING,
    const ZoneQualityScore* zone_quality_score = nullptr,
    const Bar* current_bar = nullptr,
    const std::vector<Bar>* bar_history = nullptr  // ADD
) const;
```

**`backtest_engine.cpp` — both call sites:**
```cpp
// Two-stage path (line ~681):
decision = entry_engine.calculate_entry(zone, zone_score, atr, regime,
    config.enable_revamped_scoring ? &zone_quality_score : nullptr,
    &bar, &bars);   // ADD &bars

// Direct path (line ~685):
decision = entry_engine.calculate_entry(zone, zone_score, atr, regime,
    nullptr, &bar, &bars);  // ADD &bars
```

### Fix Part B — Call the function inside `calculate_entry()`

Add this block **after the existing `validate_entry_oi` block** inside the
`if (current_bar != nullptr && config.v6_fully_enabled)` section:

```cpp
// entry_decision_engine.cpp — inside calculate_entry()
// ADD after the validate_entry_oi block:

if (bar_history != nullptr && !bar_history->empty()) {
    EntryVolumeMetrics entry_vol = calculate_entry_volume_metrics(
        zone, *current_bar, *bar_history
    );

    // Store on decision for trade_manager to copy to Trade struct
    decision.entry_pullback_vol_ratio = entry_vol.pullback_volume_ratio;
    decision.entry_volume_score       = entry_vol.volume_score;

    if (entry_vol.volume_score < config.min_volume_entry_score) {
        decision.should_enter     = false;
        decision.rejection_reason = entry_vol.rejection_reason.empty()
            ? "Entry volume score too low (" +
              std::to_string(entry_vol.volume_score) + "/60)"
            : entry_vol.rejection_reason;
        LOG_INFO("❌ PULLBACK VOLUME REJECTED: " + decision.rejection_reason);
        return decision;
    }
    LOG_INFO("✅ Pullback volume passed: " +
             std::to_string(entry_vol.volume_score) + "/60 (" +
             std::to_string(entry_vol.pullback_volume_ratio) + "x)");
}
```

Also add `min_volume_entry_score` to your config file if not already present:
```ini
min_volume_entry_score = 0    # range -50 to 60; set to 10 to activate filter
```

---

## 3. Partial Gap — `enter_trade()` Missing 7 of 10 Volume Assignments

`trade_manager.cpp enter_trade()` currently assigns only **3 of 10** entry-time volume fields:

```cpp
// Currently populated (lines 376–380) — only 3 of 10:
current_trade.zone_departure_volume_ratio     = zone.volume_profile.departure_volume_ratio;  ✅
current_trade.zone_touch_volumes              = zone.volume_profile.touch_volumes;           ✅
current_trade.zone_volume_rising_on_retests   = zone.volume_profile.volume_rising_on_retests; ✅
```

These **7 are missing** — every one shows as `0` / `false` / `""` in the CSV:

| Missing Field | Source |
|---|---|
| `zone_is_initiative` | `zone.volume_profile.is_initiative` |
| `zone_has_volume_climax` | `zone.volume_profile.has_volume_climax` |
| `zone_volume_score` | `zone.volume_profile.volume_score` |
| `zone_institutional_index` | `zone.institutional_index` |
| `entry_position_lots` | `position_size` (local var in `enter_trade`) |
| `entry_pullback_vol_ratio` | `decision.entry_pullback_vol_ratio` (available after Fix #2) |
| `entry_volume_score` | `decision.entry_volume_score` (available after Fix #2) |

### Fix — Add after line 380 in `trade_manager.cpp`:

```cpp
current_trade.zone_is_initiative       = zone.volume_profile.is_initiative;
current_trade.zone_has_volume_climax   = zone.volume_profile.has_volume_climax;
current_trade.zone_volume_score        = zone.volume_profile.volume_score;
current_trade.zone_institutional_index = zone.institutional_index;
current_trade.entry_position_lots      = position_size;
// After Fix #2 is done, also add:
current_trade.entry_pullback_vol_ratio = decision.entry_pullback_vol_ratio;
current_trade.entry_volume_score       = decision.entry_volume_score;
```

---

## 4. Partial Gap — `close_trade()` Missing `exit_was_volume_climax`

`exit_volume_ratio` is populated correctly:
```cpp
current_trade.exit_volume_ratio = bar.norm_volume_ratio;  // ✅ already present
```

`exit_was_volume_climax` is **never assigned** in either overload of `close_trade()`.
It will always be `false` in the CSV regardless of how the trade actually closed.

### Fix — One line after `exit_volume_ratio` in `close_trade(const Bar& bar, ...)`:

```cpp
current_trade.exit_volume_ratio      = bar.norm_volume_ratio;         // already there ✅
current_trade.exit_was_volume_climax = (exit_reason == "VOL_CLIMAX"); // ADD THIS
```

---

## 5. Partial Gap — CSV Row Missing `zone_volume_score` Causing Column Offset

The **header** declares 5 new zone columns in this order:
```
Zone Departure Vol Ratio | Zone Initiative | Zone Vol Climax | Zone Vol Score | Zone Institutional Index
```

The **row writer** outputs only 4, silently skipping `zone_volume_score`:
```cpp
<< trade.zone_departure_volume_ratio << ","       // col 32: Zone Departure Vol Ratio  ✅
<< (trade.zone_is_initiative ? "YES":"NO") << "," // col 33: Zone Initiative            ✅
<< (trade.zone_has_volume_climax ? "YES":"NO") << "," // col 34: Zone Vol Climax       ✅
                                                  // col 35: Zone Vol Score — MISSING   ❌
<< trade.zone_institutional_index << ","          // col 35: appears under Zone Vol Score ❌
<< trade.entry_pullback_vol_ratio << ","          // col 36: appears under Zone Inst Index ❌
// every subsequent column is under the wrong heading
```

**From column 32 onward every value appears under the wrong heading** — corrupting all volume analysis queries.

### Fix — Add the missing field in `csv_reporter.cpp write_trade_row()`:

```cpp
<< trade.zone_departure_volume_ratio << ","
<< (trade.zone_is_initiative ? "YES" : "NO") << ","
<< (trade.zone_has_volume_climax ? "YES" : "NO") << ","
<< trade.zone_volume_score << ","           // ADD THIS LINE — was missing
<< trade.zone_institutional_index << ","
<< trade.entry_pullback_vol_ratio << ","
// ... rest unchanged
```

---

## 6. What Is Fully and Correctly Implemented

- `VolumeProfile` struct — all new `departure` / `initiative` / `climax` / `touch` fields present ✅
- `EntryVolumeMetrics` struct — correctly declared with all 6 fields ✅
- `volume_scorer.cpp` — all 5 scoring components correct (departure 25pts, initiative 15pts, formation 10pts, climax 10pts, multi-touch −30pts) ✅
- `calculate_entry_volume_metrics()` — function body complete with correct pullback logic ✅
- `Trade` struct — all 12 new fields declared with correct types ✅
- CSV header — all 12 new column names present in correct order ✅
- `exit_volume_ratio` — populated correctly from `bar.norm_volume_ratio` in `close_trade()` ✅
- `volume_rising_on_retests` set logic in `zone_manager.cpp` — architecturally correct (wrong data source is the only problem) ✅

---

## 7. Prioritised Fix Checklist

| # | Fix | File | Time | Impact |
|---|---|---|---|---|
| 1 | Fix `zone.bars` → pass `touch_bar` into `mark_zone_as_tested()` or inline in `update_zone_states()` | `zone_manager.cpp` `backtest_engine.cpp` | 20 min | Activates Component 5 (−30pt penalty). Without this `volume_rising_on_retests` is always `false` |
| 2 | Add `bar_history*` param to `calculate_entry()` + update both call sites | `entry_decision_engine.h/.cpp` `backtest_engine.cpp` | 15 min | Enables 3-bar declining trend check inside metrics function |
| 3 | **Call** `calculate_entry_volume_metrics()` inside `calculate_entry()` | `entry_decision_engine.cpp` | 20 min | Activates Sam Seiden pullback filter — currently 100% dormant despite being implemented |
| 4 | Add missing `zone_volume_score` to `write_trade_row()` | `csv_reporter.cpp` | 2 min | Fixes CSV column offset — without this every field from column 32 onward is under the wrong heading |
| 5 | Add 7 missing field assignments to `enter_trade()` | `trade_manager.cpp` | 10 min | Populates `zone_is_initiative`, `zone_has_volume_climax`, `zone_volume_score`, `zone_institutional_index`, `entry_position_lots` in CSV |
| 6 | Add `exit_was_volume_climax = (exit_reason == "VOL_CLIMAX")` | `trade_manager.cpp` | 2 min | CSV `Exit Was Climax` column correctly shows `YES` for volume-triggered exits |

> **Total: ~70 minutes to close all 9 review items.**

---

*SD Trading System V4.0 | Volume Analysis Code Review | Feb 2026*
