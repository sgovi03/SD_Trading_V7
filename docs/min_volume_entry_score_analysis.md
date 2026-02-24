This depends on your backtest data. Let me reason through it properly based on the scoring logic already in your code.Read full scoring logic to reason about threshold valuesRead full scoring logic to reason about threshold valuesScriptDoneGood — I have the complete scoring matrix. Now I can give you a precise answer with the exact score breakdown for every scenario.

Recommended Setting: Start at -50 (Off), Calibrate to 0 or 10
Here is the complete score map so you can make an informed decision:

Every Possible Score Combination
The function has 3 independent components that combine additively:
Component A — Pullback Volume Ratio (most important):
Pullback VolumeScoreMeaning> 1.8x baseline−50, instant rejectHeavy absorption — zone being eaten1.5x – 1.8x−10Warning — elevated interest1.2x – 1.5x0Neutral — average volume0.8x – 1.2x+10Acceptable — normal dry pullback0.5x – 0.8x+20Good — light pullback< 0.5x+30Excellent — very dry, institutional footprint
Component B — Entry Bar Body %:
Bar BodyScoreMeaning> 60% body+10Strong directional close at zone30–60% body0Normal< 30% body (doji)−5Indecision at zone
Component C — 3-Bar Volume Trend:
TrendScoreMeaningVolume declining 3 bars+10Healthy pullback — sellers drying upNot declining−10Momentum still in pullback

Realistic Score Distribution
ScenarioABCTotalQualityPerfect: dry pullback, strong close, declining vol+30+10+10+50EliteGood: light pullback, decent close, declining vol+20+10+10+40StrongTypical: normal pullback, decent close, declining+10+10+10+30AcceptableMarginal: normal pullback, decent close, not declining+10+10−10+10BorderlineWeak: elevated pullback vol, doji, not declining−10−5−10−25SkipBad: high pullback vol (>1.5x), doji, not declining−10−5−10−25SkipInstant reject: pullback > 1.8x———−50Hard block

Recommended Thresholds
ini# Phase 1 — Data Collection (use now, before any backtest data)
min_volume_entry_score = -50    # effectively OFF — all trades pass, scores recorded in CSV

# Phase 2 — Conservative Gate (activate after ~50 trades reviewed)
min_volume_entry_score = 0      # blocks: elevated vol + doji + no declining trend
                                 # allows: anything with at least one positive signal

# Phase 3 — Standard Gate (after 150+ trades, once you see score vs win-rate data)
min_volume_entry_score = 10     # requires at least: acceptable pullback + decent close
                                 # blocks ~20-30% of entries, eliminates most "weak" setups

# Phase 4 — Tight Gate (only after strong data confirmation)
min_volume_entry_score = 20     # requires light or dry pullback — high-conviction only
                                 # blocks ~40-50% of entries

The Staged Approach (Recommended for You)
Since your system is just transitioning from backtest to live, do this in sequence:
Right now: set min_volume_entry_score = -50. Run backtests, collect the Entry Volume Score column in CSV. After 100 trades, look at the average score of your winning vs losing trades. The crossover point is your calibrated threshold.
Rule of thumb: if your winning trades average +25 and losing trades average +5, set the threshold at +10 to cut the bottom half of losers while keeping most winners.
Never set above +20 without data — at +20 you are requiring light or dry pullbacks only, which could filter out legitimate high-score zone entries where the market returns with slightly above-average volume but still respects the zone.