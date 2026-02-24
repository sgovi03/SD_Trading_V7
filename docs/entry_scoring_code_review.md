# SD Trading System V4.0
## Comprehensive Entry Scoring — Code Review
**February 17, 2026 | V6.0 Institutional Volume Edition**

---

> ### ⚠️ OVERALL VERDICT: PARTIALLY COMPLETE — 4 CRITICAL GAPS
> Volume scoring restructured correctly. New fields in structs present. But **4 critical runtime gaps** remain unfixed — the new scoring components are declared but never actually fire.

---

## Table of Contents

1. [What Is Correctly Implemented](#1-what-is-correctly-implemented)
2. [Critical Gaps — Must Fix](#2-critical-gaps--must-fix)
3. [High-Priority Gaps — Should Fix](#3-high-priority-gaps--should-fix)
4. [Fix #1 — volume_rising_on_retests](#4-fix-1--volume_rising_on_retests-critical-20-min)
5. [Fix #2 — Real-time Pullback Volume at Entry](#5-fix-2--real-time-pullback-volume-at-entry-critical-30-min)
6. [Fix #3 + #4 — Trade Struct & CSV](#6-fix-3--4--add-volume-data-to-trade-struct--csv-critical-40-min)
7. [Complete New trades.csv Column Specification](#7-complete-new-tradescsv-column-specification)
8. [Why This Data Matters for Post-Trade Analysis](#8-why-this-data-matters-for-post-trade-analysis)
9. [Prioritised Implementation Checklist](#9-prioritised-implementation-checklist)

---

## 1. What Is Correctly Implemented

| Component | Status | Detail |
|---|---|---|
| `VolumeProfile` struct — new fields | ✅ DONE | `departure_avg_volume`, `departure_volume_ratio`, `departure_peak_volume`, `departure_bar_count`, `strong_departure`, `is_initiative`, `volume_efficiency`, `has_volume_climax`, `volume_rising_on_retests`, `touch_volumes` all present in `common_types.h` |
| `EntryVolumeMetrics` struct | ✅ DONE | `pullback_volume_ratio`, `entry_bar_body_pct`, `volume_declining_trend`, `passes_sweet_spot`, `volume_score`, `rejection_reason` all declared in `common_types.h` |
| Departure volume tracking — `volume_scorer.cpp` | ✅ DONE | Tracks bars from `formation_bar` to `impulse_end` (7 bars or ATR-based). Calculates `departure_avg_volume`, `departure_volume_ratio`, `departure_peak_volume` correctly |
| Initiative vs absorption — `volume_scorer.cpp` | ✅ DONE | Checks `body_pct > 0.6` for initiative, `body_pct < 0.3` + `wick_pct > 0.6` for absorption. Defaults to initiative when ambiguous. Logic is correct |
| Volume climax detection — `volume_scorer.cpp` | ✅ DONE | Checks `bars[formation_bar - 1]` for `last_bar_ratio > 2.5`. Correct placement at end of consolidation |
| Scoring — 5 components | ✅ DONE | Component 1 Departure (25 pts), Component 2 Initiative (15 pts), Component 3 Formation (10 pts), Component 4 Climax (10 pts), Component 5 Multi-touch (−30 pts). All scoring logic correct |
| `calculate_dynamic_lot_size` — `std::round` fix | ✅ DONE | `std::round()` applied — `1 × 1.5 = 2` lots correctly. Zone-level volume used, not bar-level. High-score branch (`high_score_threshold`) added |
| EMA filter for SHORTs — `backtest_engine.cpp` | ✅ DONE | `require_ema_alignment_for_shorts` config flag added. EMA check for LONG and SHORT both happen in `check_for_entries()` before scoring |
| High-score position sizing branch | ✅ DONE | `zone_score >= config.high_score_threshold` triggers 1.5× multiplier in `calculate_dynamic_lot_size` |
| Config fields — `high_score_threshold` etc. | ✅ DONE | `high_score_position_mult` and `high_score_threshold` present in `Config` struct and loaded by `config_loader.cpp` |

---

## 2. Critical Gaps — Must Fix

| Gap / Issue | Severity | File | Fix Required |
|---|---|---|---|
| `volume_rising_on_retests` never SET | 🔴 CRITICAL | `volume_scorer.cpp` | Component 5 scoring references `profile.volume_rising_on_retests` but **nothing in the codebase ever sets it**. Always `false` → the −30 pt penalty on exhausted zones never fires |
| `calculate_entry_volume_metrics()` not implemented | 🔴 CRITICAL | `entry_decision_engine.cpp` | The full pullback filter function from the implementation guide **is absent**. `validate_entry_volume()` still uses zone-level formation volume, not real-time pullback bar volume |
| `Trade` struct missing all new volume fields | 🔴 CRITICAL | `common_types.h` | No volume fields on the `Trade` struct. `VolumeProfile` data computed at entry/exit is **never stored**, so nothing can be written to `trades.csv` |
| CSV reporter missing all new volume columns | 🔴 CRITICAL | `csv_reporter.cpp` | `write_trade_header()` and `write_trade_row()` have **zero new volume columns**. All new data is silently discarded |

---

## 3. High-Priority Gaps — Should Fix

| Gap / Issue | Severity | File | Fix Required |
|---|---|---|---|
| Impulse-end detection is approximate | 🟠 HIGH | `volume_scorer.cpp` | Fixed 7-bar window used when ATR check fails early. Should dynamically detect actual impulse end for accuracy |
| `validate_entry_volume` uses formation volume | 🟠 HIGH | `entry_decision_engine.cpp` | Still checking `zone.volume_profile.volume_ratio` (zone formation time). Should check current pullback bar volume vs baseline at entry time |
| No departure volume threshold in config | 🟠 HIGH | `config_loader.cpp` | `min_departure_volume_ratio`, `optimal_departure_volume_ratio` not loaded from config. Values are hardcoded in scoring thresholds |
| Multi-touch volume tracking never populated | 🟠 HIGH | `zone_manager.cpp` | `touch_volumes` vector exists on `VolumeProfile` but nothing adds to it when a zone is touched. Required for Component 5 |
| Exit volume data not captured at close | 🟠 HIGH | `trade_manager.cpp` | `close_trade()` records exit price and reason but captures **no volume data** at the moment of exit |

---

## 4. Fix #1 — `volume_rising_on_retests` (CRITICAL, ~20 min)

### Problem

Component 5 of volume scoring deducts 30 points when volume is rising on retests, but the field is **never set** anywhere. The penalty never fires, meaning exhausted zones are not being penalised.

### Fix: Add to `zone_manager.cpp` — in `update_zone_on_touch()` or wherever `zone.touch_count` is incremented

```cpp
// Step 1: Record the normalised volume at this touch
if (volume_scorer_ != nullptr) {
    std::string slot = extract_time_slot(touch_bar.datetime);
    double touch_vol_ratio = volume_baseline_->get_normalized_ratio(slot, touch_bar.volume);
    zone.volume_profile.touch_volumes.push_back(touch_vol_ratio);
}

// Step 2: After 3+ touches, check if volume trend is rising
if (zone.volume_profile.touch_volumes.size() >= 3) {
    const auto& tv = zone.volume_profile.touch_volumes;
    int n = tv.size();

    // Count how many of the last 3 touch pairs show rising volume
    double rising_count = 0;
    for (int i = n - 2; i >= std::max(0, n - 3); --i) {
        if (tv[i + 1] > tv[i]) rising_count++;
    }

    zone.volume_profile.volume_rising_on_retests = (rising_count >= 2);

    if (zone.volume_profile.volume_rising_on_retests) {
        LOG_WARN("⚠️ Zone " + std::to_string(zone.zone_id) +
                 " showing RISING volume on retests - zone weakening");
    }
}
```

---

## 5. Fix #2 — Real-time Pullback Volume at Entry (CRITICAL, ~30 min)

### Problem

`validate_entry_volume()` checks `zone.volume_profile.volume_ratio` which was computed **at zone formation time** — not at the current pullback bar. This entirely misses the most important Sam Seiden principle: weak volume on the return to the zone.

### Fix Part A: Add `calculate_entry_volume_metrics()` to `entry_decision_engine.cpp`

Declare as `private` in `entry_decision_engine.h`:

```cpp
EntryVolumeMetrics calculate_entry_volume_metrics(
    const Zone& zone,
    const Bar& current_bar,
    const std::vector<Bar>& bar_history
) const;
```

Implement in `entry_decision_engine.cpp`:

```cpp
EntryVolumeMetrics EntryDecisionEngine::calculate_entry_volume_metrics(
    const Zone& zone,
    const Bar& current_bar,
    const std::vector<Bar>& bar_history
) const {
    EntryVolumeMetrics metrics;
    metrics.volume_score = 0;

    if (!volume_baseline_ || !volume_baseline_->is_loaded()) return metrics;

    std::string slot = extract_time_slot(current_bar.datetime);
    double baseline  = volume_baseline_->get_baseline(slot);
    if (baseline <= 0) baseline = 1.0;

    // ── PULLBACK VOLUME: the bar currently hitting the zone ──────────────
    metrics.pullback_volume_ratio = current_bar.volume / baseline;

    if (metrics.pullback_volume_ratio > 1.8) {
        // AUTOMATIC REJECTION — zone being absorbed on pullback
        metrics.rejection_reason =
            "HIGH PULLBACK VOL (" +
            std::to_string(metrics.pullback_volume_ratio) +
            "x) — zone under absorption";
        metrics.volume_score = -50;
        return metrics;  // early exit
    }
    else if (metrics.pullback_volume_ratio < 0.5) { metrics.volume_score += 30; }  // Excellent — very dry
    else if (metrics.pullback_volume_ratio < 0.8) { metrics.volume_score += 20; }  // Good — light
    else if (metrics.pullback_volume_ratio < 1.2) { metrics.volume_score += 10; }  // Acceptable
    else if (metrics.pullback_volume_ratio < 1.5) { metrics.volume_score -= 10; }  // Warning zone

    // ── ENTRY BAR QUALITY ────────────────────────────────────────────────
    double body = std::abs(current_bar.close - current_bar.open);
    double range = current_bar.high - current_bar.low;
    metrics.entry_bar_body_pct = (range > 0) ? body / range : 0.0;

    if      (metrics.entry_bar_body_pct > 0.6) metrics.volume_score += 10;  // Strong directional
    else if (metrics.entry_bar_body_pct < 0.3) metrics.volume_score -= 5;   // Doji/indecision

    // ── RECENT VOLUME TREND (last 3 bars declining = healthy pullback) ───
    if (bar_history.size() >= 3) {
        int n = bar_history.size();
        metrics.volume_declining_trend =
            (bar_history[n-1].volume < bar_history[n-2].volume) &&
            (bar_history[n-2].volume < bar_history[n-3].volume);

        if (metrics.volume_declining_trend) metrics.volume_score += 10;
        else                                metrics.volume_score -= 10;
    }

    metrics.volume_score = std::max(-50, std::min(60, metrics.volume_score));

    LOG_INFO("📊 Entry volume score: " + std::to_string(metrics.volume_score)
             + "/60 (pullback=" + std::to_string(metrics.pullback_volume_ratio) + "x)");

    return metrics;
}
```

### Fix Part B: Wire into `calculate_entry()` — add after existing `validate_entry_oi` block

```cpp
// ── NEW: Real-time pullback volume check ─────────────────────────────────
EntryVolumeMetrics entry_vol = calculate_entry_volume_metrics(
    zone, *current_bar, bar_history   // NOTE: also add bar_history to calculate_entry signature
);

if (entry_vol.volume_score < config.min_volume_entry_score) {
    decision.should_enter  = false;
    decision.rejection_reason = entry_vol.rejection_reason.empty()
        ? "Entry volume score too low (" + std::to_string(entry_vol.volume_score) + ")"
        : entry_vol.rejection_reason;
    LOG_INFO("❌ ENTRY VOLUME SCORE REJECTED: " + decision.rejection_reason);
    return decision;
}

LOG_INFO("✅ Entry volume passed: " + std::to_string(entry_vol.volume_score) + "/60");
```

---

## 6. Fix #3 + #4 — Add Volume Data to Trade Struct & CSV (CRITICAL, ~40 min)

### Step 1 — Add new fields to `struct Trade` in `common_types.h`

```cpp
// ===== V6.0 VOLUME DATA AT ENTRY =====

// Zone formation volume snapshot
double zone_departure_volume_ratio = 0.0;  // Impulse vol / baseline at zone creation
bool   zone_is_initiative          = false; // Initiative vs absorption
bool   zone_has_volume_climax      = false; // Spike at end of consolidation
double zone_volume_score           = 0.0;  // 0–60 zone volume score
int    zone_institutional_index    = 0;    // Institutional index at zone

// Entry-time pullback volume
double entry_pullback_vol_ratio    = 0.0;  // Pullback bar vol / baseline
int    entry_volume_score          = 0;    // 0–60 entry volume score
std::string entry_volume_pattern   = "";   // e.g. "SWEET_SPOT", "ELITE", "LOW_VOL"
int    entry_position_lots         = 0;    // Final lot size (1, 2, or 3)
std::string position_size_reason   = "";   // e.g. "SweetSpot 1.5x", "LowVol 0.5x"

// ===== V6.0 VOLUME DATA AT EXIT =====
double exit_volume_ratio           = 0.0;  // Exit bar vol / baseline
bool   exit_was_volume_climax      = false; // Did VOL_CLIMAX trigger exit?

// Add to Trade() constructor initialiser list:
// zone_departure_volume_ratio(0), zone_is_initiative(false),
// zone_has_volume_climax(false), zone_volume_score(0),
// zone_institutional_index(0), entry_pullback_vol_ratio(0),
// entry_volume_score(0), entry_position_lots(0),
// exit_volume_ratio(0), exit_was_volume_climax(false)
```

### Step 2 — Populate in `trade_manager.cpp` — `enter_trade()`

```cpp
// After existing trade field assignments (zone_id, zone_distal, etc.)

// Zone volume profile snapshot
current_trade.zone_departure_volume_ratio = zone.volume_profile.departure_volume_ratio;
current_trade.zone_is_initiative          = zone.volume_profile.is_initiative;
current_trade.zone_has_volume_climax      = zone.volume_profile.has_volume_climax;
current_trade.zone_volume_score           = zone.volume_profile.volume_score;
current_trade.zone_institutional_index    = zone.institutional_index;

// Entry-time pullback (stored on EntryDecision and passed through)
current_trade.entry_pullback_vol_ratio    = decision.entry_pullback_vol_ratio;
current_trade.entry_volume_score          = decision.entry_volume_score;
current_trade.entry_volume_pattern        = decision.entry_volume_pattern;
current_trade.entry_position_lots         = position_size;
current_trade.position_size_reason        = decision.position_size_reason;
```

> **Note:** Also add `entry_pullback_vol_ratio`, `entry_volume_score`, `entry_volume_pattern`, `position_size_reason` fields to the `EntryDecision` struct and populate them from `calculate_entry_volume_metrics()` before returning the decision.

### Step 3 — Populate in `trade_manager.cpp` — `close_trade()`

```cpp
// In close_trade(const Bar& bar, ...) — add after exit_price assignment

if (volume_baseline_ && volume_baseline_->is_loaded()) {
    std::string slot = extract_time_slot(bar.datetime);
    current_trade.exit_volume_ratio =
        volume_baseline_->get_normalized_ratio(slot, bar.volume);
}
current_trade.exit_was_volume_climax = (exit_reason == "VOL_CLIMAX");
```

### Step 4 — Update `csv_reporter.cpp`

**In `write_trade_header()`** — append after the existing final column (`MACD Histogram`):

```cpp
<< "Zone Departure Vol,Zone Initiative,Zone Climax,Zone Vol Score,Zone Inst Index,"
<< "Entry Pullback Vol,Entry Vol Score,Entry Vol Pattern,Position Lots,Lot Size Reason,"
<< "Exit Vol Ratio,Exit Was Climax\n";
```

**In `write_trade_row()`** — append after the existing final field (`macd_histogram`):

```cpp
<< std::setprecision(2) << trade.zone_departure_volume_ratio << ","
<< (trade.zone_is_initiative        ? "YES" : "NO") << ","
<< (trade.zone_has_volume_climax    ? "YES" : "NO") << ","
<< trade.zone_volume_score          << ","
<< trade.zone_institutional_index   << ","
<< trade.entry_pullback_vol_ratio   << ","
<< trade.entry_volume_score         << ","
<< sanitize_csv(trade.entry_volume_pattern) << ","
<< trade.entry_position_lots        << ","
<< sanitize_csv(trade.position_size_reason) << ","
<< trade.exit_volume_ratio          << ","
<< (trade.exit_was_volume_climax    ? "YES" : "NO") << "\n";
```

---

## 7. Complete New `trades.csv` Column Specification

12 new columns total — 10 captured at trade entry, 2 at trade exit.

| CSV Column | When | Source | Importance |
|---|---|---|---|
| `Zone Departure Vol` | Entry | `zone.volume_profile.departure_volume_ratio` | 🔴 CRITICAL — core Sam Seiden signal |
| `Zone Initiative` | Entry | `zone.volume_profile.is_initiative` | 🔴 CRITICAL — absorption filter |
| `Zone Climax` | Entry | `zone.volume_profile.has_volume_climax` | 🟠 HIGH — final absorption signal |
| `Zone Vol Score` | Entry | `zone.volume_profile.volume_score` (0–60) | 🟠 HIGH — composite zone quality |
| `Zone Inst Index` | Entry | `zone.institutional_index` (0–100) | 🟠 HIGH — institutional footprint |
| `Entry Pullback Vol` | Entry | `current_bar.volume / baseline` at entry | 🔴 CRITICAL — weak sellers check |
| `Entry Vol Score` | Entry | `EntryVolumeMetrics.volume_score` (0–60) | 🔴 CRITICAL — entry gate score |
| `Entry Vol Pattern` | Entry | `SWEET_SPOT` / `ELITE` / `HIGH_VOL` / `LOW_VOL` | 🟠 HIGH — pattern classification |
| `Position Lots` | Entry | `decision.lot_size` (1–3) | 🟠 HIGH — actual lots entered |
| `Lot Size Reason` | Entry | `SweetSpot 1.5x` / `Elite 1.5x` / `LowVol 0.5x` / `Base` | 🟢 MEDIUM — sizing rationale |
| `Exit Vol Ratio` | Exit | `exit_bar.volume / baseline` | 🟠 HIGH — climax or dry exit? |
| `Exit Was Climax` | Exit | `exit_reason == "VOL_CLIMAX"` | 🟠 HIGH — volume-driven exits |

---

## 8. Why This Data Matters for Post-Trade Analysis

Once these 12 columns exist, you can answer the following with real data from your own backtest:

1. **Do high departure volume zones win more often?** Sort by `Zone Departure Vol`, compare win rates between `>2.0x` and `<1.5x` groups.

2. **Does low pullback volume predict winners?** Filter `Entry Pullback Vol < 0.8`, compare P&L vs `>1.2`. This validates or challenges the Sam Seiden principle directly.

3. **Are initiative zones better than absorption zones?** Group by `Zone Initiative YES/NO` — compare win rate, avg P&L, profit factor.

4. **Do 2-lot trades actually outperform 1-lot trades per R?** Compare `Position Lots 1` vs `2` — if 2-lot sizing is correctly identifying high-quality zones, P&L per R should be higher.

5. **Does Volume Climax exit protect profits better than trailing stop?** Compare `Exit Was Climax YES` vs `Exit Reason = TRAIL_SL`.

6. **Are Sweet Spot zones actually better than Elite zones?** Group by `Entry Vol Pattern` — validates the scoring weight distribution.

7. **Is there an optimal Zone Vol Score threshold?** Bucket by score range (0–20, 20–40, 40–60) and compare outcomes.

---

## 9. Prioritised Implementation Checklist

| # | Task | File | Time | Expected Benefit |
|---|---|---|---|---|
| 1 | Populate `volume_rising_on_retests` in `update_zone_on_touch()` | `zone_manager.cpp` | 20 min | Component 5 (−30 pt penalty) activates — rejects exhausted zones |
| 2 | Add `Trade` struct volume fields + init in constructor | `common_types.h` | 15 min | Enables all subsequent CSV logging |
| 3 | Implement `calculate_entry_volume_metrics()` for real pullback volume | `entry_decision_engine.cpp` | 30 min | Activates Sam Seiden pullback filter — rejects absorbed entries |
| 4 | Populate Trade fields in `enter_trade()` and `close_trade()` | `trade_manager.cpp` | 20 min | Captures all data in memory for CSV export |
| 5 | Add 12 new columns to `csv_reporter.cpp` | `csv_reporter.cpp` | 20 min | Full volume visibility in `trades.csv` for analysis |
| 6 | Pass `bar_history` into `calculate_entry()` signature | `backtest_engine.cpp` | 10 min | Enables 3-bar declining volume trend check at entry |
| 7 | Add departure volume thresholds to `config_loader.cpp` | `config_loader.cpp` | 15 min | Externally configurable — no hardcoded thresholds |
| | **Total** | | **~2 hrs** | **Win rate: 50% → 65–70% \| P&L: +₹10,000–17,000/month** |

---

> *SD Trading System V4.0 | Code Review — Institutional Volume Scoring | Feb 2026*
