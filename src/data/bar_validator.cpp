// ============================================================
// SD TRADING V8 - BAR VALIDATOR IMPLEMENTATION
// src/data/bar_validator.cpp
// Milestone M2.3
// ============================================================

#include "bar_validator.h"
#include <cmath>
#include <sstream>
#include <iomanip>
#include <numeric>

namespace SDTrading {
namespace Data {

// ============================================================
// Constructor
// ============================================================

BarValidator::BarValidator(const std::string& symbol,
                           const ValidatorConfig& cfg)
    : symbol_(symbol), cfg_(cfg) {
    LOG_INFO("[BarValidator:" << symbol_ << "] Initialized.");
    LOG_INFO("[BarValidator:" << symbol_ << "]"
             << "  max_gap_pct=" << cfg_.max_gap_pct
             << "  atr_spike=" << cfg_.atr_spike_multiplier
             << "  vol_spike=" << cfg_.max_volume_spike_mult
             << "  oi_spike=" << cfg_.max_oi_spike_pct << "%");
}

// ============================================================
// validate() — main entry point
// ============================================================

ValidationResult BarValidator::validate(const RawBar& bar) {
    // Tier 1 — structural (fast, no state needed)
    auto t1 = check_tier1_structural(bar);
    if (!t1.passed) {
        ++bars_rejected_;
        ++consecutive_rejects_;
        if (is_alert_threshold_hit()) {
            LOG_WARN("[BarValidator:" << symbol_ << "] "
                     << consecutive_rejects_ << " consecutive rejections! "
                     << "Last reason: " << t1.reason);
        }
        return t1;
    }

    // Enabled check: when bar_validation_enabled=NO in config, tier-2 and tier-3
    // are bypassed entirely. Tier-1 above always runs because structurally broken
    // bars (H<L, C outside range) would corrupt zone geometry regardless.
    if (!cfg_.enabled) {
        update_state(bar);
        ++bars_validated_;
        consecutive_rejects_ = 0;
        return pass();
    }

    // Tier 2 — anomaly (uses prev bar state)
    if (has_prev_bar_) {
        auto t2 = check_tier2_anomaly(bar);
        if (!t2.passed) {
            ++bars_rejected_;
            ++consecutive_rejects_;
            if (is_alert_threshold_hit()) {
                LOG_WARN("[BarValidator:" << symbol_ << "] "
                         << consecutive_rejects_ << " consecutive Tier-2 rejects! "
                         << "Reason: " << t2.reason);
            }
            return t2;
        }
    }

    // Tier 3 — sequence (cross-bar consistency)
    auto t3 = check_tier3_sequence(bar);
    if (!t3.passed) {
        ++bars_rejected_;
        ++consecutive_rejects_;
        return t3;
    }

    // ── PASS ──
    update_state(bar);
    ++bars_validated_;
    consecutive_rejects_ = 0;
    return pass();
}

// ============================================================
// TIER 1 — STRUCTURAL
// Hard failures. No previous-bar context required.
// ============================================================

ValidationResult BarValidator::check_tier1_structural(const RawBar& bar) const {
    if (bar.high < bar.low) {
        return fail(1, "H < L: high=" + std::to_string(bar.high)
                       + " low=" + std::to_string(bar.low));
    }
    if (bar.close > bar.high) {
        return fail(1, "C > H: close=" + std::to_string(bar.close)
                       + " high=" + std::to_string(bar.high));
    }
    if (bar.close < bar.low) {
        return fail(1, "C < L: close=" + std::to_string(bar.close)
                       + " low=" + std::to_string(bar.low));
    }
    if (bar.open > bar.high) {
        return fail(1, "O > H: open=" + std::to_string(bar.open)
                       + " high=" + std::to_string(bar.high));
    }
    if (bar.open < bar.low) {
        return fail(1, "O < L: open=" + std::to_string(bar.open)
                       + " low=" + std::to_string(bar.low));
    }
    if (bar.volume <= 0) {
        return fail(1, "Volume <= 0: " + std::to_string(bar.volume));
    }
    if (bar.timestamp.empty()) {
        return fail(1, "Empty timestamp");
    }
    if (bar.symbol.empty()) {
        return fail(1, "Empty symbol");
    }
    return pass();
}

// ============================================================
// TIER 2 — ANOMALY
// Requires previous bar state. Configurable thresholds.
// ============================================================

ValidationResult BarValidator::check_tier2_anomaly(const RawBar& bar) const {
    // Session boundary: skip ALL tier-2 on first LIVE bar after HISTORICAL.
    // Without this, last HISTORICAL close bleeds into LIVE session causing
    // thousands of false gap-spike rejections (e.g. 24694.80 vs Sep opens).
    if (bar.source != "HISTORICAL" && last_source_ == "HISTORICAL") {
        LOG_INFO("[BarValidator:" << symbol_ << "] "
                 "Session boundary (HISTORICAL->LIVE): skipping tier-2 for "
                 "first live bar " << bar.timestamp);
        return { true, 0, "" };
    }

    // First bar of day skip (09:15 opening auction bars have wide spread/spike)
    const bool is_first_bar_of_day = has_prev_bar_
        && bar.timestamp.substr(11, 5) == "09:15";
    if (cfg_.skip_first_bar_of_day && is_first_bar_of_day) {
        LOG_DEBUG("[BarValidator:" << symbol_ << "] "
                  "Skipping tier-2 for first bar of day: " << bar.timestamp);
        return { true, 0, "" };
    }

    const double prev_close = prev_bar_.close;

    // ── Gap check ─────────────────────────────────────────
    // Phantom gap candles (the exact problem that caused the ₹9,500 loss)
    // Skip gap check for HISTORICAL bars — contract rollovers produce
    // legitimate large gaps between old and new futures contracts.
    if (bar.source != "HISTORICAL" && prev_close > 0.0) {
        double gap_pct = std::abs(bar.open - prev_close) / prev_close * 100.0;
        if (gap_pct > cfg_.max_gap_pct) {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(2);
            oss << "Gap spike: |open " << bar.open
                << " - prev_close " << prev_close
                << "| = " << gap_pct << "% > " << cfg_.max_gap_pct << "% limit";
            return fail(2, oss.str());
        }
    }

    // ── ATR spike check ───────────────────────────────────
    // Catches phantom candles with abnormal range
    // Skip for HISTORICAL — price gaps across contract rollovers
    // can cause legitimate large range bars.
    if (bar.source != "HISTORICAL" && rolling_atr_ > 0.0) {
        double range = bar.high - bar.low;
        double atr_multiple = range / rolling_atr_;
        if (atr_multiple > cfg_.atr_spike_multiplier) {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(2);
            oss << "Range spike: (H-L)=" << range
                << " = " << atr_multiple << "x ATR(" << rolling_atr_
                << ") > " << cfg_.atr_spike_multiplier << "x limit";
            return fail(2, oss.str());
        }
    }

    // ── Volume spike check ────────────────────────────────
    // Skip for HISTORICAL — volume patterns differ across contracts.
    if (bar.source != "HISTORICAL" && avg_volume_ > 0.0
        && static_cast<int>(volume_history_.size()) >= cfg_.volume_min_bars) {
        double vol_ratio = bar.volume / avg_volume_;
        if (vol_ratio > cfg_.max_volume_spike_mult) {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(1);
            oss << "Volume spike: " << bar.volume
                << " = " << vol_ratio << "x avg(" << avg_volume_
                << ") > " << cfg_.max_volume_spike_mult << "x limit";
            return fail(2, oss.str());
        }
    }

    // ── OI spike check ────────────────────────────────────
    // Skip for HISTORICAL source — contract rollovers cause massive
    // legitimate OI jumps (e.g. old contract 3.8M → new contract 16M).
    if (bar.source != "HISTORICAL" && prev_oi_ > 0.0 && bar.open_interest >= 0.0) {
        double oi_delta_pct = std::abs(bar.open_interest - prev_oi_)
                              / prev_oi_ * 100.0;
        if (oi_delta_pct > cfg_.max_oi_spike_pct) {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(2);
            oss << "OI spike: " << bar.open_interest
                << " vs prev " << prev_oi_
                << " = " << oi_delta_pct << "% change > "
                << cfg_.max_oi_spike_pct << "% limit";
            return fail(2, oss.str());
        }
    }

    // ── Zero OI during session (data feed issue) ──────────
    // OI should never be zero during market hours for liquid index futures
    if (bar.open_interest == 0.0 && prev_oi_ > 0.0) {
        LOG_WARN("[BarValidator:" << symbol_ << "] OI=0 during session at "
                 << bar.timestamp << " (prev=" << prev_oi_
                 << "). Flagged as suspect — NOT rejected (may be expiry).");
        // Warning only, not rejection — OI can briefly drop on expiry
    }

    return pass();
}

// ============================================================
// TIER 3 — SEQUENCE
// Stateful cross-bar checks.
// ============================================================

ValidationResult BarValidator::check_tier3_sequence(const RawBar& bar) const {
    // ── Duplicate timestamp ───────────────────────────────
    if (bar.timestamp == last_timestamp_) {
        return fail(3, "Duplicate timestamp: " + bar.timestamp);
    }

    // ── Timestamp regression ─────────────────────────────
    // Timestamps must be monotonically increasing
    if (!last_timestamp_.empty() && bar.timestamp < last_timestamp_) {
        return fail(3, "Timestamp regression: " + bar.timestamp
                       + " < prev " + last_timestamp_);
    }

    // ── Session sanity ────────────────────────────────────
    // For HISTORICAL bulk load spanning months, session count grows
    // unbounded — suppress this warning entirely for historical source.
    if (bar.source != "HISTORICAL" && bar_count_in_session_ > 100) {
        LOG_WARN("[BarValidator:" << symbol_ << "] bar_count_in_session_="
                 << bar_count_in_session_ << " > 100 at " << bar.timestamp
                 << ". Possible feed duplication.");
    }

    return pass();
}

// ============================================================
// Update rolling state (only called on PASS)
// ============================================================

void BarValidator::update_state(const RawBar& bar) {
    last_source_ = bar.source;   // track for session boundary detection

    // Update prev bar
    prev_bar_.datetime  = bar.timestamp;
    prev_bar_.open      = bar.open;
    prev_bar_.high      = bar.high;
    prev_bar_.low       = bar.low;
    prev_bar_.close     = bar.close;
    prev_bar_.volume    = bar.volume;
    prev_bar_.oi        = bar.open_interest;
    has_prev_bar_       = true;

    // Update OI
    prev_oi_ = bar.open_interest;

    // Update last timestamp
    // Reset session bar count when date changes (new trading day)
    std::string bar_date = bar.timestamp.size() >= 10
                           ? bar.timestamp.substr(0, 10) : "";
    std::string prev_date = last_timestamp_.size() >= 10
                            ? last_timestamp_.substr(0, 10) : "";
    if (!bar_date.empty() && bar_date != prev_date) {
        bar_count_in_session_ = 0;
    }
    last_timestamp_ = bar.timestamp;

    // Update session bar count
    ++bar_count_in_session_;

    // ── Rolling ATR (True Range approximation) ────────────
    double tr = bar.high - bar.low;
    if (has_prev_bar_) {
        double tr1 = std::abs(bar.high  - prev_bar_.close);
        double tr2 = std::abs(bar.low   - prev_bar_.close);
        tr = std::max({tr, tr1, tr2});
    }
    atr_history_.push_back(tr);
    if ((int)atr_history_.size() > cfg_.atr_lookback) {
        atr_history_.pop_front();
    }
    if (!atr_history_.empty()) {
        rolling_atr_ = std::accumulate(atr_history_.begin(), atr_history_.end(), 0.0)
                       / atr_history_.size();
    }

    // ── Rolling average volume ────────────────────────────
    volume_history_.push_back(bar.volume);
    if ((int)volume_history_.size() > cfg_.volume_lookback) {
        volume_history_.pop_front();
    }
    if (!volume_history_.empty()) {
        avg_volume_ = std::accumulate(volume_history_.begin(), volume_history_.end(), 0.0)
                      / volume_history_.size();
    }
}

// ============================================================
// to_validated_event()
// ============================================================

Events::BarValidatedEvent BarValidator::to_validated_event(const RawBar& bar) const {
    Events::BarValidatedEvent evt;
    evt.symbol         = bar.symbol;
    evt.timeframe      = bar.timeframe;
    evt.timestamp      = bar.timestamp;
    evt.open           = bar.open;
    evt.high           = bar.high;
    evt.low            = bar.low;
    evt.close          = bar.close;
    evt.volume         = bar.volume;
    evt.open_interest  = bar.open_interest;
    evt.source         = bar.source;
    evt.prev_close     = has_prev_bar_ ? prev_bar_.close : 0.0;
    evt.prev_atr       = rolling_atr_;
    return evt;
}

// ============================================================
// to_rejected_event()
// ============================================================

Events::BarRejectedEvent BarValidator::to_rejected_event(
    const RawBar& bar,
    const ValidationResult& result) const
{
    Events::BarRejectedEvent evt;
    evt.symbol           = bar.symbol;
    evt.timeframe        = bar.timeframe;
    // Reconstruct wire data for audit log
    std::ostringstream raw;
    raw << bar.source << "|" << bar.symbol << "|" << bar.timeframe << "|"
        << bar.timestamp << "|" << bar.open << "|" << bar.high << "|"
        << bar.low << "|" << bar.close << "|" << bar.volume << "|"
        << bar.open_interest;
    evt.raw_wire_data    = raw.str();
    evt.rejection_tier   = result.tier;
    evt.rejection_reason = result.reason;
    evt.prev_close       = has_prev_bar_ ? prev_bar_.close : 0.0;
    evt.prev_atr         = rolling_atr_;
    evt.prev_oi          = prev_oi_;
    evt.raw_oi           = bar.open_interest;
    return evt;
}

// ============================================================
// reset()
// ============================================================

void BarValidator::reset() {
    has_prev_bar_         = false;
    rolling_atr_          = 0.0;
    avg_volume_           = 0.0;
    prev_oi_              = 0.0;
    last_timestamp_       = "";
    bar_count_in_session_ = 0;
    atr_history_.clear();
    volume_history_.clear();
    consecutive_rejects_  = 0;
    LOG_INFO("[BarValidator:" << symbol_ << "] State reset.");
}

// ============================================================
// Static helpers
// ============================================================

ValidationResult BarValidator::pass() {
    return { true, 0, "" };
}

ValidationResult BarValidator::fail(int tier, const std::string& reason) {
    return { false, tier, reason };
}

} // namespace Data
} // namespace SDTrading