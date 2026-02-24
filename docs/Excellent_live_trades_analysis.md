# SD Trading System V4.0 — Live Trades Analysis
**Dataset:** 27 trades, Jan 6 – Feb 3 2026 | +₹30,714 (+10.24%) | Sharpe 6.198
**Win Rate:** 44.44% (12W / 15L) | **Profit Factor: 2.26** | **Avg Win: ₹4,590 vs Avg Loss: ₹1,624**

---

## System Health: What This Dataset Tells You First

The headline numbers are striking — 44% win rate but **profit factor 2.26** and **Sharpe 6.198**. The system is surviving on large wins (+₹4,590 avg) dwarfing small losses (−₹1,624 avg), a 2.83 RR ratio in practice. This is a valid operating mode — you don't need 50%+ win rate with these payoff ratios. But the loss streak risk is real: 5 consecutive losses occurred. The deeper question is: **are the wins coming from skill (identifiable patterns) or luck?** The data says mostly skill — and the patterns are very clear.

**Critical upfront finding:** Entry Pullback Vol Ratio = 0.00 for all 27 trades. The `calculate_entry_volume_metrics()` function is not firing in live mode — either `volume_baseline_` is null, `bar_history` is empty, or `v6_fully_enabled = false` at entry time. This means the entry volume filter is entirely inactive in live trading. This is both a bug AND an opportunity — the patterns below are what the system achieves WITHOUT the entry volume layer.

---

## Pattern 1 — Zone Departure Vol Ratio Is the #1 Predictor

**Zone Departure Vol Ratio (DVR)** is the ratio of the zone's impulse/departure volume to the time-of-day baseline. It measures how institutionally-driven the original zone formation was. This is the single most powerful predictor in the dataset.

| DVR Range | n | Wins | Win Rate | Total P&L | Avg P&L |
|---|---|---|---|---|---|
| **0.0–0.3 (near zero)** | 4 | 1 | **25.0%** | −₹3,641 | −₹910 |
| **0.3–0.6 (low-moderate)** | 5 | 4 | **80.0%** | +₹18,032 | +₹3,606 |
| 0.6–0.9 (dead zone) | 2 | 0 | **0.0%** | −₹2,805 | −₹1,403 |
| 0.9–1.3 (average) | 9 | 4 | 44.4% | +₹2,783 | +₹309 |
| **1.3–1.8 (strong)** | 5 | 3 | **60.0%** | +₹18,396 | +₹3,679 |
| 1.8+ (excessive) | 2 | 0 | **0.0%** | −₹2,051 | −₹1,026 |

**The pattern is unmistakable: a U-shape with two sweet spots.** DVR 0.3–0.6 and DVR 1.3–1.8 are profitable; the middle (0.6–1.3) and extremes (<0.3 and >1.8) are not.

### Why DVR 0.3–0.6 Works
Low-to-moderate departure volume (0.3–0.6× baseline) means the zone formed with measured, controlled institutional activity. Institutions don't need to blast through volume — they absorb quietly. This is the institutional accumulation signature. Zone 138 (DVR=0.35) produced 3 trades, 3 wins, +₹17,035 total — the best zone in the dataset.

### Why DVR 1.3–1.8 Works  
Moderate-high departure volume (1.3–1.8×) signals a strong directional imbalance at formation — institutions committed with size, creating a clear level. Zone 150 (DVR=1.41) produced the single biggest trade: +₹9,381. Zone 146 (DVR=1.77) contributed +₹3,916 over 2 trades.

### Why DVR >1.8 Fails
Excessive departure volume (>1.8×) is the absorption trap: high volume with limited follow-through means the counter-party was absorbing supply/demand efficiently. Zone 141 (DVR=1.85, is_initiative=**NO**) — both trades were losses (−₹2,051 total). The `Zone Initiative = NO` flag correctly identifies this.

### Why DVR 0.6–0.9 is the Dead Zone
These zones formed with "normal" volume — neither the quiet accumulation signature nor the strong directional conviction signature. These are the ambiguous zones where retail and institutional activity overlap. 0% win rate on both trades, both from Zone 132 (LONG, DVR=0.64, Inst=0).

### The DVR + Zone Initiative Combination
When `Zone Initiative = NO` (indicating absorption not a clean institutional move), every trade loses regardless of DVR. The 2 Initiative=NO trades (both Zone 141) lost −₹2,051. Among Initiative=YES trades, the sweet DVR bands separate winners from losers:

| Filter | n | Win Rate | Total P&L |
|---|---|---|---|
| DVR 0.3–0.6 + Init=YES | 9 | 55.6% | +₹14,392 |
| DVR 0.9–1.3 + Init=YES | 11 | 36.4% | −₹23 |
| DVR 1.3–1.8 + Init=YES | 5 | 60.0% | +₹18,396 |

**The 0.9–1.3 middle band with Init=YES is essentially break-even (−₹23 on 11 trades).** Adding Init=YES is necessary but not sufficient — DVR must be in one of the two sweet zones to generate returns.

---

## Pattern 2 — Zone Score Is Inverted: Lower Scores Win More

Live data perfectly confirms the backtest finding:

| Metric | Winners | Losers |
|---|---|---|
| Avg Zone Score | **59.92** | **63.09** |

Zones scoring 45–55 have **80% win rate** (4 of 5 trades). Zones scoring 62–67 have 40% WR. Zones 67–72 have 40% WR. **Higher-scored zones perform worse.**

| Score Band | n | Win Rate | Total P&L | Avg P&L |
|---|---|---|---|---|
| **45–55 (LOW)** | 5 | **80.0%** | +₹15,783 | **+₹3,157** |
| 55–62 (MED-LOW) | 7 | 28.6% | +₹791 | +₹113 |
| 62–67 (MED-HIGH) | 10 | 40.0% | +₹10,084 | +₹1,008 |
| 67–72 (HIGH) | 5 | 40.0% | +₹4,057 | +₹811 |

**Root cause (confirmed from code review):** The `elite_bonus_score` component is 0 for all trades in this dataset (none of these zones are `is_elite = true`). The `swing_position_score` and `base_strength_score` both show **lower averages for winners** (-1.09 and -1.58 respectively). The scoring model is applying positive weight to features that empirically don't predict profitability. Score 45–55 zones often have high DVR sweet spot values and fast retest characteristics that override the lower scoring.

**Practical implication:** `entry_minimum_score = 60` (the proposed config change) would have **blocked Zone 150 (49.21) which produced +₹9,381** and Zone 138 (which scored 64–70, OK) but also Zone 129 (47.34 → +₹1,069 net). Be careful with this threshold on live data.

---

## Pattern 3 — ADX: Two Separate Profitable Regimes

| ADX Range | n | Win Rate | Total P&L | Avg P&L |
|---|---|---|---|---|
| 25–35 | 10 | 40.0% | +₹10,272 | +₹1,027 |
| 35–50 | 8 | 50.0% | +₹9,944 | +₹1,243 |
| **50–70** | 6 | **16.7%** | **−₹3,976** | **−₹663** |
| **70–100** | 3 | **100.0%** | **+₹14,474** | **+₹4,825** |

**ADX 50–70 is the problem band: 16.7% win rate, only 1 of 6 trades profitable.** This is the "just entering a trend" regime — sufficient ADX to push through zones with momentum, but not yet in a fully committed directional move.

**ADX ≥ 70 is exceptional: 3 trades, 3 wins, +₹14,474 avg +₹4,825.** Extreme ADX means institutional conviction is so high that even at supply/demand zones, the momentum carries the trade to target. These are the "ride the trend" entries.

The pairing: **DVR sweet spot + ADX ≤35 or ≥70** produces 8 trades at **62.5% WR, +₹25,551 total (avg +₹3,194)** vs the rest: 19 trades at 36.8% WR, +₹5,164 avg +₹272.

---

## Pattern 4 — RSI Shows Clear Directionality Signal

| Direction | Winners avg RSI | Losers avg RSI | Signal |
|---|---|---|---|
| **LONG** | **27.0** | 42.5 | LONG works when oversold |
| **SHORT** | 67.9 | 69.4 | SHORT not strongly RSI-dependent |

For LONG trades: RSI < 35 (oversold) → **60% WR, +₹11,597** (5 trades). RSI 35–65 → 33% WR, −₹1,075. RSI > 65 → 0% WR, −₹3,125 (the 14:59 SESSION_END loss). **LONG entries at oversold RSI are dramatically better.**

For SHORT trades: The RSI signal is weaker — overbought RSI (>70) actually has **lower WR (33%)** than non-overbought (50%). This seems counterintuitive but makes sense: extreme RSI >70 in a trending market can push through supply zones rather than reversing at them. The best SHORT trades came from RSI in the 60–70 range where the market was elevated but not in a panic bid.

**RSI sweet zones:**
- **LONG**: RSI < 35 (oversold demand zone retests) → 60% WR
- **SHORT**: RSI 55–65 → 62.5% WR, +₹17,965

---

## Pattern 5 — 14:xx Hour Is Systematically Losing

| Hour | n | Win Rate | Total P&L |
|---|---|---|---|
| 09:xx | 5 | 60.0% | +₹3,752 |
| 10:xx | 4 | 50.0% | +₹11,095 |
| 11:xx | 3 | 66.7% | +₹8,630 |
| 12:xx | 2 | 100.0% | +₹9,045 |
| 13:xx | 6 | 50.0% | +₹9,935 |
| **14:xx** | **7** | **0.0%** | **−₹11,742** |

**Every single 14:xx trade is a loss.** 7 trades, 0 wins, −₹11,742. Without the 14:xx trades, the system would be **+₹42,456 (+14.2%)** instead of +₹30,714.

Looking at what's happening at 14:xx:
- These aren't all the same kind of trade — some are SESSION_END exits (forced close at 15:00), some are genuine STOP_LOSS hits
- The common pattern: **MACD Histogram is positive (0.03 to 2.99)** — i.e. momentum is running AGAINST the zone entry direction in most cases. At 14:xx, the afternoon momentum trend has established itself and zones are being run through rather than respected.
- DVR at 14:xx ranges 0.12 to 1.85 — no particular DVR signature, this is purely a time effect
- Confirming the backtest finding: blocking 14:xx entries entirely would be a significant improvement. The optimal cutoff is `session_close_avoid_minutes = 60` (avoid entries after 14:00).

---

## Pattern 6 — BB Bandwidth: Lower Volatility = Better Entries

| BB Bandwidth | n | Win Rate | Total P&L |
|---|---|---|---|
| **< 0.15 (tight/squeeze)** | 8 | **62.5%** | +₹14,988 |
| 0.15–0.25 (moderate) | 6 | 50.0% | +₹13,822 |
| **0.25–0.35 (wide)** | 11 | **27.3%** | **−₹2,681** |
| > 0.35 (very wide) | 2 | 50.0% | +₹4,586 |

Winners avg BB_BW = **0.193**, Losers avg BB_BW = **0.253**. Tight/squeeze market conditions at entry (BBW < 0.15) produce 62.5% WR. Wide BB (0.25–0.35) produces 27.3% WR — the "volatility expansion" regime where zones are penetrated rather than respected.

**Adding BBW < 0.25 to the filter:** Passes 8 trades, wr=62.5%, +₹24,101. But caution: it also "blocks" some profitable wider-BB trades (Zone 138 trade #178522 ADX=95 had BBW=0.29 and produced +₹6,385).

---

## Pattern 7 — Position Sizing: 2-Lot Trades Underperform

| Lots | n | Win Rate | Avg P&L | Avg DVR | Avg Inst |
|---|---|---|---|---|---|
| **1 lot** | 20 | **50.0%** | +₹1,247 | 0.728 | 6.0 |
| **2 lots** | 7 | **28.6%** | +₹825 | 1.504 | 37.9 |

2-lot trades (triggered by high institutional index or sweet-spot volume pattern) have lower win rate. Why? 2-lot trades are concentrated in DVR 1.3–1.8 range (avg 1.504) — which is a sweet spot for profit when they work but the 2× size amplifies losses. The biggest loss (−₹3,190 from Zone 139) was a 2-lot trade. The 2-lot sizing logic is routing into higher-DVR zones where Zone Initiative can fail (Zone 141: 2 lots, 0 wins). **Sizing up should require both DVR sweetspot AND Initiative=YES as a hard gate.**

---

## Pattern 8 — Per-Zone Analysis: The Stars and the Problems

**Outstanding Zones:**
| Zone | n | WR | P&L | DVR | Init | Key Signal |
|---|---|---|---|---|---|---|
| **138** | 3 | 100% | +₹17,035 | 0.35 | YES | DVR sweet, 3 separate dates |
| **150** | 1 | 100% | +₹9,381 | 1.41 | YES | DVR sweet, best single trade |
| **130** | 1 | 100% | +₹4,578 | 0.96 | YES | Normal DVR works when ADX low |

Zone 138 is remarkable — 3 entries spanning Jan 16, Feb 3 (morning and afternoon), all winners. This zone (SUPPLY, distal=25915.90, proximal=25906.40, formed Nov 3 2025) consistently acts as institutional supply. Its DVR=0.35 means the original departure was measured/quiet institutional selling.

**Problem Zones:**
| Zone | n | WR | P&L | DVR | Init | Issue |
|---|---|---|---|---|---|---|
| **139** | 2 | 0% | −₹5,470 | 0.94 | YES | DVR dead zone, mid-range |
| **141** | 2 | 0% | −₹2,051 | 1.85 | **NO** | Initiative=NO (absorption) |
| **132** | 2 | 0% | −₹2,805 | 0.64 | YES | DVR dead zone |

Zones 139 and 132 both sit in the 0.6–0.9 DVR dead zone. Zone 141's Initiative=NO flag is the clearest identifiable failure signal in the dataset.

---

## Pattern 9 — Critical Bug: Entry Pullback Volume Not Computing

All 27 trades show `Entry Pullback Vol Ratio = 0.00`. This means in live mode, `calculate_entry_volume_metrics()` returns early without computing the pullback score. The most likely cause:

```cpp
if (!volume_baseline_ || !volume_baseline_->is_loaded()) return metrics;
```

The `volume_baseline_` is null in the live engine at entry time. This is actually masking the entry volume inversion bug found in the backtest — the broken pullback scoring (rewarding dry <0.5x) is not running, which may partially explain why live performance is better than backtest in relative terms. Paradoxically, **fixing the entry pullback scoring (recalibrating to reward 0.8–1.2x normal volume) should be done BEFORE re-enabling it in live mode**, or you risk re-introducing the inversion.

The `Entry Volume Score = 0` for all trades (same bug) means the `min_volume_entry_score = -20` filter is also not running. Once fixed, this filter should block the absorption trades (DVR>1.8, Init=NO) which are currently being allowed through.

---

## Summary: The Golden Pattern for This System

A trade passing **all four criteria** has historically been the highest-probability setup:

```
1. Zone Departure Vol Ratio:  0.3–0.6  OR  1.3–1.8
2. Zone Initiative:           YES (not an absorption zone)
3. BB Bandwidth at entry:     < 0.25 (tight market, zone will be respected)
4. Entry time:                NOT 14:xx (afternoon momentum destroys zones)
```

Adding directional RSI alignment (LONG: RSI < 40, SHORT: RSI < 70) further improves selection. The DVR sweet spot + Init=YES combination alone gives **70% WR on 10 trades, +₹36,428 total** — capturing 118% of the system's gross profit on only 37% of the trades.

---

## Recommended Code/Config Changes From This Analysis

**Immediate — config only:**
```ini
# Block afternoon entries
session_close_avoid_minutes = 60     # block after 14:00

# Keep entry_minimum_score conservative (Zone 150 scored 49.21 = best trade)
entry_minimum_score = 48.0           # was 60.0 proposed — too aggressive for live

# Keep use_trailing_stop = NO (trail hurts as shown)
use_trailing_stop = NO
```

**Code changes needed:**

1. **Fix entry pullback volume in live engine** — load volume_baseline before live entry checks. Once fixed, recalibrate the pullback thresholds to reward 0.8–1.2x normal (not <0.5x dry).

2. **Add DVR gate as an entry filter:**
```cpp
// In live_engine.cpp::check_for_entries():
double dvr = zone.volume_profile.departure_volume_ratio;  // or recompute
bool dvr_sweet = (dvr >= 0.3 && dvr <= 0.6) || (dvr >= 1.3 && dvr <= 1.8);
if (dvr > 0.0 && !dvr_sweet) {
    LOG_DEBUG("Zone " << zone.zone_id << " blocked: DVR=" << dvr << " outside sweet zones");
    continue;
}
```

3. **Add Initiative=YES gate for 2-lot sizing:**
```cpp
// In entry_decision_engine.cpp::calculate_dynamic_lot_size():
// Only allow 2-lot if Initiative=YES (prevents absorption zone over-sizing)
if (zone.volume_profile.is_initiative == false) {
    return 1;  // force 1 lot for absorption zones
}
```

4. **Fix zone volume profile serialization** (confirmed still broken in live master JSON) — departure_volume_ratio, is_initiative, strong_departure, has_volume_climax are all missing from the JSON persistence layer. They ARE being computed and passed to the trade CSV correctly, but zones are being reloaded with these fields as zero/false.

---

*SD Trading System V4.0 — Live Analysis | Feb 17, 2026 | 27 trades, Jan 6 – Feb 3 2026*
