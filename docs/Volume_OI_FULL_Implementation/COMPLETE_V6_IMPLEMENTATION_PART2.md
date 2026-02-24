# SD ENGINE V6.0 - COMPLETE IMPLEMENTATION PART 2
## Entry/Exit Filters, Scoring Integration, Configuration

---

## FILE 4: Zone Scoring Integration (MODIFICATION)

**File:** `src/scoring/zone_quality_scorer.cpp`

**Modify the calculate() function (around line 14-52):**

```cpp
ZoneQualityScore ZoneQualityScorer::calculate(
    const Zone& zone, 
    const std::vector<Bar>& bars, 
    int current_index
) const {
    ZoneQualityScore score;
    
    if (current_index < 0 || current_index >= static_cast<int>(bars.size())) {
        return score;
    }
    
    // =================================================================
    // V4.0 TRADITIONAL METRICS (50% weight in V6.0)
    // =================================================================
    double base_strength = calculate_base_strength_score(zone);      // 0-30
    double age_score = calculate_age_score(zone, bars[current_index]); // 0-25
    double rejection_score = calculate_rejection_score(zone, bars, current_index); // 0-25
    double touch_penalty = calculate_touch_penalty(zone);            // -15 to 0
    double breakthrough_penalty = calculate_breakthrough_penalty(zone, bars, current_index);
    double elite_bonus = calculate_elite_bonus(zone, bars[current_index]);
    
    // Traditional score (max ~65 points)
    double traditional_score = base_strength + age_score + rejection_score + 
                               touch_penalty + breakthrough_penalty + elite_bonus;
    
    // =================================================================
    // V6.0 VOLUME/OI METRICS (50% weight combined)
    // =================================================================
    double volume_score = zone.volume_profile.volume_score;  // 0-40 points
    double oi_score = zone.oi_profile.oi_score;              // 0-30 points
    
    // =================================================================
    // V6.0 WEIGHTED SCORING FORMULA
    // =================================================================
    // Traditional: 50% weight
    // Volume: 30% weight
    // OI: 20% weight
    
    score.total = (0.50 * traditional_score) +
                  (0.30 * volume_score) +
                  (0.20 * oi_score);
    
    // =================================================================
    // INSTITUTIONAL INDEX BONUS
    // =================================================================
    if (zone.institutional_index >= 80.0) {
        score.total += 15.0;  // Elite institutional participation
    } else if (zone.institutional_index >= 60.0) {
        score.total += 10.0;  // Good institutional participation
    } else if (zone.institutional_index >= 40.0) {
        score.total += 5.0;   // Moderate institutional participation
    }
    
    // =================================================================
    // FALLBACK for insufficient data
    // =================================================================
    if (score.total < 10.0 && base_strength > 15.0) {
        // If V6.0 scores are zero (no data), fallback to traditional only
        score.total = traditional_score;
        LOG_DEBUG("ZoneQualityScore using traditional fallback: " + 
                 std::to_string(score.total));
    }
    
    // =================================================================
    // POPULATE BREAKDOWN FIELDS (for logging)
    // =================================================================
    score.zone_strength = base_strength;
    score.zone_age = age_score;
    score.touch_count = rejection_score;
    score.zone_height = touch_penalty;
    
    // Clamp to valid range [0, 100]
    score.total = std::max(0.0, std::min(100.0, score.total));
    
    LOG_INFO("Zone Score Breakdown: Traditional=" + std::to_string(traditional_score) +
             " Volume=" + std::to_string(volume_score) +
             " OI=" + std::to_string(oi_score) +
             " InstIndex=" + std::to_string(zone.institutional_index) +
             " TOTAL=" + std::to_string(score.total));
    
    return score;
}
```

---

## FILE 5: Entry Decision Filters (MODIFICATION)

**File:** `src/scoring/entry_decision_engine.h`

**Add to class:**
```cpp
class EntryDecisionEngine {
private:
    // ... existing members ...
    
    // NEW V6.0: Add volume baseline reference
    const Utils::VolumeBaseline* volume_baseline_;
    
    // NEW V6.0: Add OI scorer reference
    const Core::OIScorer* oi_scorer_;

public:
    // NEW V6.0: Set volume baseline
    void set_volume_baseline(const Utils::VolumeBaseline* baseline);
    
    // NEW V6.0: Set OI scorer
    void set_oi_scorer(const Core::OIScorer* scorer);

private:
    // NEW V6.0: Volume entry filter
    bool validate_entry_volume(
        const Bar& current_bar,
        std::string& rejection_reason
    ) const;
    
    // NEW V6.0: OI entry filter
    bool validate_entry_oi(
        const Zone& zone,
        const Bar& current_bar,
        std::string& rejection_reason
    ) const;
    
    // NEW V6.0: Dynamic position sizing
    int calculate_dynamic_lot_size(
        const Zone& zone,
        const Bar& current_bar
    ) const;
    
    // NEW V6.0: Extract time slot from datetime
    std::string extract_time_slot(const std::string& datetime) const;
};
```

**File:** `src/scoring/entry_decision_engine.cpp`

**Add these implementations:**

```cpp
void EntryDecisionEngine::set_volume_baseline(const Utils::VolumeBaseline* baseline) {
    volume_baseline_ = baseline;
    LOG_INFO("EntryDecisionEngine: Volume baseline set");
}

void EntryDecisionEngine::set_oi_scorer(const Core::OIScorer* scorer) {
    oi_scorer_ = scorer;
    LOG_INFO("EntryDecisionEngine: OI scorer set");
}

bool EntryDecisionEngine::validate_entry_volume(
    const Bar& current_bar,
    std::string& rejection_reason
) const {
    // Skip if volume baseline not available
    if (volume_baseline_ == nullptr || !volume_baseline_->is_loaded()) {
        LOG_DEBUG("Volume baseline not available - skipping volume filter");
        return true; // Allow entry (degraded mode)
    }
    
    // Check if V6.0 volume filters are enabled
    if (!config.enable_volume_entry_filters) {
        return true;
    }
    
    // Get time slot
    std::string time_slot = extract_time_slot(current_bar.datetime);
    
    // Get normalized volume ratio
    double norm_ratio = volume_baseline_->get_normalized_ratio(
        time_slot, 
        current_bar.volume
    );
    
    // Filter 1: Minimum volume threshold
    if (norm_ratio < config.min_entry_volume_ratio) {
        rejection_reason = "Insufficient volume (" + 
                          std::to_string(norm_ratio) + "x vs " +
                          std::to_string(config.min_entry_volume_ratio) + "x required)";
        LOG_INFO("❌ Entry REJECTED: " + rejection_reason);
        return false;
    }
    
    // Filter 2: Extreme volume spike (unless elite zone)
    // This actually helps - extreme volume often marks exhaustion
    // We skip this filter for now based on testing
    
    LOG_DEBUG("✅ Volume filter PASSED: " + std::to_string(norm_ratio) + "x");
    return true;
}

bool EntryDecisionEngine::validate_entry_oi(
    const Zone& zone,
    const Bar& current_bar,
    std::string& rejection_reason
) const {
    // Skip if OI scorer not available
    if (oi_scorer_ == nullptr) {
        LOG_DEBUG("OI scorer not available - skipping OI filter");
        return true; // Allow entry (degraded mode)
    }
    
    // Check if V6.0 OI filters are enabled
    if (!config.enable_oi_entry_filters) {
        return true;
    }
    
    // Only validate if OI data is fresh
    if (!current_bar.oi_fresh) {
        LOG_DEBUG("OI data not fresh - skipping OI filter");
        return true; // Allow entry
    }
    
    // Get current market phase from zone's OI profile
    // (Already calculated during zone creation)
    MarketPhase phase = zone.oi_profile.market_phase;
    
    // Check phase alignment
    if (zone.type == ZoneType::DEMAND) {
        // DEMAND zones: Favorable phases
        if (phase == MarketPhase::SHORT_BUILDUP || 
            phase == MarketPhase::LONG_UNWINDING) {
            LOG_DEBUG("✅ OI phase FAVORABLE for DEMAND: " + market_phase_to_string(phase));
            return true;
        }
        // Unfavorable phases
        else if (phase == MarketPhase::LONG_BUILDUP ||
                 phase == MarketPhase::SHORT_COVERING) {
            rejection_reason = "Unfavorable OI phase for DEMAND: " + 
                              market_phase_to_string(phase);
            LOG_INFO("❌ Entry REJECTED: " + rejection_reason);
            return false;
        }
    } else { // SUPPLY zone
        // SUPPLY zones: Favorable phases
        if (phase == MarketPhase::LONG_BUILDUP ||
            phase == MarketPhase::SHORT_COVERING) {
            LOG_DEBUG("✅ OI phase FAVORABLE for SUPPLY: " + market_phase_to_string(phase));
            return true;
        }
        // Unfavorable phases
        else if (phase == MarketPhase::SHORT_BUILDUP ||
                 phase == MarketPhase::LONG_UNWINDING) {
            rejection_reason = "Unfavorable OI phase for SUPPLY: " + 
                              market_phase_to_string(phase);
            LOG_INFO("❌ Entry REJECTED: " + rejection_reason);
            return false;
        }
    }
    
    // Neutral phase - allow entry
    LOG_DEBUG("OI phase NEUTRAL - allowing entry");
    return true;
}

int EntryDecisionEngine::calculate_dynamic_lot_size(
    const Zone& zone,
    const Bar& current_bar
) const {
    int base_lots = config.lot_size; // e.g., 65 lots for NIFTY
    double multiplier = 1.0;
    
    // Volume-based adjustment
    if (volume_baseline_ != nullptr && volume_baseline_->is_loaded()) {
        std::string time_slot = extract_time_slot(current_bar.datetime);
        double norm_ratio = volume_baseline_->get_normalized_ratio(
            time_slot, 
            current_bar.volume
        );
        
        // Reduce size in low volume
        if (norm_ratio < 0.8) {
            multiplier = config.low_volume_size_multiplier; // Default: 0.5
            LOG_INFO("🔻 Position size REDUCED (low volume): " + std::to_string(multiplier) + "x");
        }
        // Increase size for high institutional participation
        else if (norm_ratio > 2.0 && zone.institutional_index >= 80.0) {
            multiplier = config.high_institutional_size_mult; // Default: 1.5
            LOG_INFO("🔺 Position size INCREASED (high institutional): " + std::to_string(multiplier) + "x");
        }
    }
    
    int final_lots = static_cast<int>(base_lots * multiplier);
    
    // Enforce safety limits
    final_lots = std::max(1, std::min(final_lots, config.max_lot_size));
    
    LOG_INFO("Position sizing: Base=" + std::to_string(base_lots) + 
             " Multiplier=" + std::to_string(multiplier) +
             " Final=" + std::to_string(final_lots));
    
    return final_lots;
}

std::string EntryDecisionEngine::extract_time_slot(const std::string& datetime) const {
    // Expected format: "2024-02-08 09:15:00"
    if (datetime.length() >= 16) {
        return datetime.substr(11, 5); // Extract "09:15"
    }
    return "00:00"; // Fallback
}

// MODIFY calculate_entry() function:
EntryDecision EntryDecisionEngine::calculate_entry(
    const Zone& zone,
    const ZoneScore& score,
    double atr,
    MarketRegime regime,
    const Bar& current_bar  // ADD THIS PARAMETER
) const {
    EntryDecision decision;
    
    // ... existing validation (zone score, EMA alignment, etc.) ...
    
    // ✅ NEW V6.0: Volume validation
    std::string vol_rejection;
    if (!validate_entry_volume(current_bar, vol_rejection)) {
        decision.should_enter = false;
        decision.rejection_reason = vol_rejection;
        return decision;
    }
    
    // ✅ NEW V6.0: OI validation
    std::string oi_rejection;
    if (!validate_entry_oi(zone, current_bar, oi_rejection)) {
        decision.should_enter = false;
        decision.rejection_reason = oi_rejection;
        return decision;
    }
    
    // ✅ NEW V6.0: Dynamic position sizing
    decision.lot_size = calculate_dynamic_lot_size(zone, current_bar);
    
    // ... rest of existing entry calculation ...
    
    return decision;
}
```

---

## FILE 6: Exit Signal Detection (MODIFICATION)

**File:** `src/backtest/trade_manager.h`

**Add to class:**
```cpp
class TradeManager {
private:
    // NEW V6.0: Volume baseline for exit signals
    const Utils::VolumeBaseline* volume_baseline_;
    
    // NEW V6.0: OI scorer for exit signals
    const Core::OIScorer* oi_scorer_;

public:
    // NEW V6.0: Set dependencies
    void set_volume_baseline(const Utils::VolumeBaseline* baseline);
    void set_oi_scorer(const Core::OIScorer* scorer);

private:
    // NEW V6.0: Volume exit signals
    enum class VolumeExitSignal {
        NONE,
        VOLUME_CLIMAX,
        VOLUME_DRYING_UP,
        VOLUME_DIVERGENCE
    };
    
    VolumeExitSignal check_volume_exit_signals(
        const Trade& trade,
        const Bar& current_bar,
        const std::vector<Bar>& bars
    ) const;
    
    // NEW V6.0: OI exit signals
    enum class OIExitSignal {
        NONE,
        OI_UNWINDING,
        OI_REVERSAL,
        OI_STAGNATION
    };
    
    OIExitSignal check_oi_exit_signals(
        const Trade& trade,
        const Bar& current_bar,
        const std::vector<Bar>& bars,
        int current_index
    ) const;
};
```

**File:** `src/backtest/trade_manager.cpp`

**Add implementations:**

```cpp
void TradeManager::set_volume_baseline(const Utils::VolumeBaseline* baseline) {
    volume_baseline_ = baseline;
}

void TradeManager::set_oi_scorer(const Core::OIScorer* scorer) {
    oi_scorer_ = scorer;
}

TradeManager::VolumeExitSignal TradeManager::check_volume_exit_signals(
    const Trade& trade,
    const Bar& current_bar,
    const std::vector<Bar>& bars
) const {
    // Skip if volume baseline not available or not enabled
    if (volume_baseline_ == nullptr || !volume_baseline_->is_loaded()) {
        return VolumeExitSignal::NONE;
    }
    
    if (!config.enable_volume_exit_signals) {
        return VolumeExitSignal::NONE;
    }
    
    // Get normalized volume
    std::string time_slot = extract_time_slot(current_bar.datetime);
    double norm_ratio = volume_baseline_->get_normalized_ratio(
        time_slot,
        current_bar.volume
    );
    
    // Signal 1: Volume Climax (>3.0x average + in profit)
    if (norm_ratio > config.volume_climax_exit_threshold && trade.unrealized_pnl > 0) {
        LOG_INFO("🚨 VOLUME CLIMAX detected: " + std::to_string(norm_ratio) + "x");
        return VolumeExitSignal::VOLUME_CLIMAX;
    }
    
    // Signal 2: Volume Drying Up (<0.5x for 3+ bars)
    // This requires tracking consecutive low volume bars - implement as needed
    
    // Signal 3: Volume Divergence
    // This requires price trend analysis - implement as needed
    
    return VolumeExitSignal::NONE;
}

TradeManager::OIExitSignal TradeManager::check_oi_exit_signals(
    const Trade& trade,
    const Bar& current_bar,
    const std::vector<Bar>& bars,
    int current_index
) const {
    // Skip if OI scorer not available or not enabled
    if (oi_scorer_ == nullptr || !config.enable_oi_exit_signals) {
        return OIExitSignal::NONE;
    }
    
    // Only process if OI data is fresh
    if (!current_bar.oi_fresh) {
        return OIExitSignal::NONE;
    }
    
    // Need at least 10 bars for analysis
    if (current_index < 10) {
        return OIExitSignal::NONE;
    }
    
    // Calculate recent price and OI changes
    int lookback = 10;
    int start_index = current_index - lookback;
    
    double price_change = ((current_bar.close - bars[start_index].close) / 
                          bars[start_index].close) * 100.0;
    
    double oi_start = bars[start_index].oi;
    double oi_current = current_bar.oi;
    double oi_change = ((oi_current - oi_start) / oi_start) * 100.0;
    
    // Signal 1: OI Unwinding (CRITICAL - exit immediately)
    if (trade.direction == TradeDirection::LONG) {
        // LONG + price rising + OI falling = longs exiting (smart money out)
        if (price_change > 0.2 && oi_change < config.oi_unwinding_threshold) {
            LOG_INFO("🚨 OI UNWINDING detected (LONG): OI " + std::to_string(oi_change) + "%");
            return OIExitSignal::OI_UNWINDING;
        }
    } else { // SHORT
        // SHORT + price falling + OI falling = shorts covering
        if (price_change < -0.2 && oi_change < config.oi_unwinding_threshold) {
            LOG_INFO("🚨 OI UNWINDING detected (SHORT): OI " + std::to_string(oi_change) + "%");
            return OIExitSignal::OI_UNWINDING;
        }
    }
    
    // Signal 2: OI Reversal (new counterparties entering against us)
    if (std::abs(price_change) < 0.1 && oi_change > config.oi_reversal_threshold) {
        LOG_INFO("⚠️  OI REVERSAL detected: OI +" + std::to_string(oi_change) + "%");
        return OIExitSignal::OI_REVERSAL;
    }
    
    return OIExitSignal::NONE;
}

// MODIFY update_positions() or wherever exit logic lives:
void TradeManager::check_exits(Trade& trade, const Bar& current_bar, int current_index) {
    // ... existing exit checks (SL, TP, Trailing) ...
    
    // ✅ NEW V6.0: Volume exit signals
    VolumeExitSignal vol_signal = check_volume_exit_signals(trade, current_bar, bars_);
    
    if (vol_signal == VolumeExitSignal::VOLUME_CLIMAX) {
        // Exit 50-100% of position at market
        exit_trade(trade, current_bar, "VOLUME_CLIMAX");
        return;
    }
    
    // ✅ NEW V6.0: OI exit signals
    OIExitSignal oi_signal = check_oi_exit_signals(trade, current_bar, bars_, current_index);
    
    if (oi_signal == OIExitSignal::OI_UNWINDING) {
        // CRITICAL: Exit immediately
        exit_trade(trade, current_bar, "OI_UNWINDING");
        return;
    } else if (oi_signal == OIExitSignal::OI_REVERSAL) {
        // Consider tightening stop or partial exit
        // For now, just log and let trailing stop handle it
        LOG_WARN("OI reversal detected for trade - monitoring closely");
    }
    
    // ... rest of exit logic ...
}
```

---

*(Continued in Part 3...)*
