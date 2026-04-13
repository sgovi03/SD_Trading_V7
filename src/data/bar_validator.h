// ============================================================
// SD TRADING V8 - BAR VALIDATOR
// src/data/bar_validator.h
// Milestone M2.3
//
// Three-tier validation on every bar from AmiBroker pipe.
// Designed specifically to catch the class of bad candle data
// that caused a ₹9,500 P&L loss (wrong Fyers candle on a
// single trade, compared to AmiBroker + TradingView data).
//
// Tier 1 — Structural (hard failure, no recovery):
//   H >= L, C in [L,H], O in [L,H], V > 0, timestamp monotonic
//
// Tier 2 — Anomaly (configurable thresholds):
//   Gap%, ATR spike, volume spike, OI spike, zero OI in session
//
// Tier 3 — Sequence (stateful, cross-bar):
//   Duplicate timestamp, session gap, bar count bounds
//
// Each validator instance is per-symbol.
// Maintains rolling state (prev bar, ATR estimate, avg volume).
// ============================================================

#ifndef SDTRADING_BAR_VALIDATOR_H
#define SDTRADING_BAR_VALIDATOR_H

#include <string>
#include <deque>
#include <cmath>
#include <sstream>
#include "common_types.h"
#include "../events/event_types.h"
#include "../utils/logger.h"

namespace SDTrading {
namespace Data {

// ============================================================
// VALIDATOR CONFIG
// Loaded from MASTER.config at startup via Config struct.
// ============================================================
struct ValidatorConfig {
    // ── Master switch ─────────────────────────────────────────────────────────
    // Set bar_validation_enabled = NO in MASTER.config to bypass ALL tier-2
    // checks and store nothing to bar_rejections. Tier-1 structural checks
    // (H<L, C outside H-L range) are always enforced regardless of this flag
    // because structurally broken bars corrupt zone geometry.
    bool   enabled                   = true;

    // ── Tier-2 thresholds ─────────────────────────────────────────────────────
    // Defaults are tuned for NSE NIFTY/BANKNIFTY 5-min futures:
    //   - gap_pct=5%  : covers overnight events; 3% wrongly rejects Sep opens
    //   - atr_mult=6x : opening auction bars routinely show 5-10x ATR
    //   - vol_mult=20x: new contract starts with tiny avg; 10x fires immediately
    //   - oi_pct=50%  : contract rollover causes 100-350% OI jump legitimately
    //   - skip_09:15  : first bar of day skips tier-2 (auction wide spread)
    //   - vol_min_bars: require N bars of history before volume spike check
    double max_gap_pct               = 5.0;   // was 3.0 — raised for overnight gaps
    double atr_spike_multiplier      = 6.0;   // was 4.0 — raised for opening auction
    double max_volume_spike_mult     = 20.0;  // was 10.0 — raised for new contracts
    double max_oi_spike_pct          = 50.0;  // was 20.0 — raised for rollover days
    bool   skip_first_bar_of_day     = true;  // skip tier-2 for 09:15 bar
    int    volume_min_bars           = 50;    // min history before vol spike check

    int    consecutive_reject_alert  = 3;     // Alert threshold for consecutive rejects
    int    atr_lookback              = 14;    // Bars to compute rolling ATR estimate
    int    volume_lookback           = 20;    // Bars for rolling volume average
};

// ============================================================
// VALIDATION RESULT
// ============================================================
struct ValidationResult {
    bool        passed;
    int         tier;           // 0=passed, 1-3=failed tier
    std::string reason;         // Human-readable rejection reason
};

// ============================================================
// RAW BAR (from pipe, before validation)
// ============================================================
struct RawBar {
    std::string source;         // "AMIBROKER"
    std::string symbol;
    std::string timeframe;
    std::string timestamp;      // ISO8601
    double      open;
    double      high;
    double      low;
    double      close;
    double      volume;
    double      open_interest;

    bool is_structurally_complete() const {
        return !symbol.empty() && !timestamp.empty()
            && open > 0 && high > 0 && low > 0 && close > 0;
    }
};

// ============================================================
// BAR VALIDATOR — one instance per symbol
// ============================================================
class BarValidator {
public:
    explicit BarValidator(const std::string& symbol,
                          const ValidatorConfig& cfg);

    // --------------------------------------------------------
    // validate()
    // Main entry point. Returns ValidationResult.
    // If passed, caller should publish BarValidatedEvent.
    // If failed, caller should publish BarRejectedEvent.
    //
    // Side effects: updates internal rolling state on PASS only.
    // A rejected bar does NOT update prev_bar state — the engine
    // will continue from the last known-good bar.
    // --------------------------------------------------------
    ValidationResult validate(const RawBar& bar);

    // --------------------------------------------------------
    // to_validated_event()
    // Converts a passing RawBar to BarValidatedEvent.
    // Only call after validate() returns passed=true.
    // --------------------------------------------------------
    Events::BarValidatedEvent to_validated_event(const RawBar& bar) const;

    // --------------------------------------------------------
    // to_rejected_event()
    // Converts a failing RawBar + ValidationResult to event.
    // --------------------------------------------------------
    Events::BarRejectedEvent  to_rejected_event(
        const RawBar& bar,
        const ValidationResult& result) const;

    // Statistics
    int  get_bars_validated()  const { return bars_validated_; }
    int  get_bars_rejected()   const { return bars_rejected_; }
    int  get_consecutive_rejects() const { return consecutive_rejects_; }
    bool is_alert_threshold_hit() const {
        return consecutive_rejects_ >= cfg_.consecutive_reject_alert;
    }

    // Reset state (used when restarting a session)
    void reset();

private:
    std::string     symbol_;
    ValidatorConfig cfg_;

    // Rolling state (updated only on PASS)
    Core::Bar       prev_bar_;
    bool            has_prev_bar_ = false;
    std::string     last_source_;   // tracks source for session boundary detection
    double          rolling_atr_  = 0.0;
    double          avg_volume_   = 0.0;
    double          prev_oi_      = 0.0;
    std::string     last_timestamp_;
    int             bar_count_in_session_ = 0;

    // ATR and volume history for rolling estimates
    std::deque<double> atr_history_;
    std::deque<double> volume_history_;

    // Counters
    int bars_validated_      = 0;
    int bars_rejected_       = 0;
    int consecutive_rejects_ = 0;

    // ── Tier 1: Structural ──────────────────────────────────
    ValidationResult check_tier1_structural(const RawBar& bar) const;

    // ── Tier 2: Anomaly ─────────────────────────────────────
    ValidationResult check_tier2_anomaly(const RawBar& bar) const;

    // ── Tier 3: Sequence ────────────────────────────────────
    ValidationResult check_tier3_sequence(const RawBar& bar) const;

    // Update rolling state after a passing bar
    void update_state(const RawBar& bar);

    // Compute ATR estimate from H, L, prev_close
    double compute_atr_contribution(const RawBar& bar) const;

    // Passed result
    static ValidationResult pass();

    // Failed result
    static ValidationResult fail(int tier, const std::string& reason);
};

} // namespace Data
} // namespace SDTrading

#endif // SDTRADING_BAR_VALIDATOR_H