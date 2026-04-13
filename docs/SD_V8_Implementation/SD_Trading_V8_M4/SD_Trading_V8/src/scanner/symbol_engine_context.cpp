// ============================================================
// SD TRADING V8 - SYMBOL ENGINE CONTEXT IMPLEMENTATION
// src/scanner/symbol_engine_context.cpp
// Milestone M4.1
// ============================================================

#include "symbol_engine_context.h"
#include "../utils/logger.h"
#include "../include/order_tag_generator.h"
#include <algorithm>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace SDTrading {
namespace Scanner {

// ============================================================
// Constructor
// ============================================================

SymbolEngineContext::SymbolEngineContext(const Core::ResolvedConfig& resolved)
    : config_(resolved.config)
    , profile_(resolved.profile)
    , detector_(resolved.config)
    , scorer_(resolved.config)
    , entry_engine_(resolved.config)
    , zone_state_mgr_(resolved.config)
{
    LOG_INFO("[SymbolEngineContext:" << profile_.symbol << "] Created.");
    LOG_INFO("[SymbolEngineContext:" << profile_.symbol << "]"
             << " lot_size=" << config_.lot_size
             << " min_zone_strength=" << config_.min_zone_strength
             << " config_hash=" << resolved.report.config_hash);
}

// ============================================================
// load_zones — replay historical bars to build zone state
// ============================================================

bool SymbolEngineContext::load_zones(const std::vector<Core::Bar>& historical_bars) {
    if (historical_bars.empty()) {
        LOG_WARN("[SymbolEngineContext:" << profile_.symbol << "]"
                 << " No historical bars — starting with empty zone state.");
        state_ = EngineState::READY;
        return true;
    }

    LOG_INFO("[SymbolEngineContext:" << profile_.symbol << "]"
             << " Replaying " << historical_bars.size() << " historical bars...");

    for (const auto& bar : historical_bars) {
        detector_.add_bar(bar);
    }

    // Full zone detection pass over historical data
    active_zones_ = detector_.detect_zones_full();

    // Assign zone IDs and initialise states
    for (auto& z : active_zones_) {
        if (z.zone_id <= 0) z.zone_id = next_zone_id_++;
    }
    zone_state_mgr_.initialize_zone_states(active_zones_);

    // Update state against the last historical bar
    if (!historical_bars.empty()) {
        const auto& last = historical_bars.back();
        zone_state_mgr_.update_zone_states(
            active_zones_, last,
            static_cast<int>(historical_bars.size()) - 1);
        last_bar_timestamp_ = last.datetime;
    }

    LOG_INFO("[SymbolEngineContext:" << profile_.symbol << "]"
             << " Zones loaded: " << active_zones_.size()
             << " | Last bar: " << last_bar_timestamp_);

    state_ = EngineState::READY;
    return true;
}

// ============================================================
// on_bar — process one validated bar, return signal result
// Thread-safe: all state accessed through this object only,
// and the scanner ensures one call per symbol at a time.
// ============================================================

SignalResult SymbolEngineContext::on_bar(const Core::Bar& bar) {
    SignalResult result;
    result.symbol        = profile_.symbol;
    result.bar_timestamp = bar.datetime;

    if (state_ == EngineState::SUSPENDED || state_ == EngineState::STOPPED) {
        LOG_DEBUG("[SymbolEngineContext:" << profile_.symbol << "]"
                  << " Skipped (state=" << engine_state_str(state_) << ")");
        return result;
    }

    // ── Add bar to detector ──────────────────────────────
    detector_.add_bar(bar);
    ++bar_count_;
    last_bar_timestamp_ = bar.datetime;

    // ── Update zone states against current bar ───────────
    zone_state_mgr_.update_zone_states(active_zones_, bar, bar_count_);

    // ── Periodic zone detection to keep pipeline fresh ───
    bool zones_changed = false;
    if (bar_count_ % config_.live_zone_detection_interval_bars == 0) {
        auto new_zones = detector_.detect_zones_no_duplicates(active_zones_);
        for (auto& z : new_zones) {
            if (z.zone_id <= 0) z.zone_id = next_zone_id_++;
            active_zones_.push_back(z);
            zones_changed = true;
        }

        // Remove violated zones beyond age limit
        active_zones_.erase(
            std::remove_if(active_zones_.begin(), active_zones_.end(),
                [this](const Core::Zone& z) {
                    return z.state == Core::ZoneState::VIOLATED
                        || z.state == Core::ZoneState::EXHAUSTED;
                }),
            active_zones_.end()
        );
    }

    result.zones_changed  = zones_changed;
    result.updated_zones  = active_zones_;

    // ── Determine market regime ──────────────────────────
    Core::MarketRegime regime = determine_regime();

    // ── Entry check against all fresh/tested zones ───────
    // Find the highest-scoring zone that fires a valid entry
    double      best_score  = 0.0;
    Core::Zone* best_zone   = nullptr;
    Core::EntryDecision best_decision;

    for (auto& zone : active_zones_) {
        // Skip violated / exhausted
        if (config_.skip_violated_zones &&
            zone.state == Core::ZoneState::VIOLATED) continue;
        if (zone.state == Core::ZoneState::EXHAUSTED) continue;

        // Check if price is in zone
        double ltp = bar.close;
        bool in_demand_zone = zone.type == Core::ZoneType::DEMAND
                              && ltp >= zone.proximal_line
                              && ltp <= zone.distal_line;
        bool in_supply_zone = zone.type == Core::ZoneType::SUPPLY
                              && ltp <= zone.proximal_line
                              && ltp >= zone.distal_line;

        if (!in_demand_zone && !in_supply_zone) continue;

        // Score the zone
        Core::ZoneScore zs = scorer_.calculate_zone_score(
            zone, bar, bar_count_, regime, config_);

        if (zs.total_score < config_.min_zone_strength) continue;

        // Evaluate entry
        Core::EntryDecision decision = entry_engine_.calculate_entry(
            zone, zs, 0.0, regime, nullptr, &bar, nullptr, false);

        if (!decision.should_enter) continue;

        // Keep best score
        if (zs.total_score > best_score) {
            best_score    = zs.total_score;
            best_zone     = &zone;
            best_decision = decision;
        }
    }

    // ── Emit signal if entry found ───────────────────────
    if (best_zone != nullptr && best_score >= config_.min_zone_strength) {
        result.has_signal   = true;
        result.direction    = (best_zone->type == Core::ZoneType::DEMAND)
                              ? "LONG" : "SHORT";
        result.score        = best_score;
        result.zone_id      = best_zone->zone_id;
        result.zone_type    = (best_zone->type == Core::ZoneType::DEMAND)
                              ? "DEMAND" : "SUPPLY";
        result.entry_price  = best_decision.entry_price;
        result.stop_loss    = best_decision.stop_loss;
        result.target_1     = best_decision.take_profit;
        result.rr_ratio     = best_decision.expected_rr;
        result.lot_size     = config_.lot_size;
        result.order_tag    = generate_order_tag(result.direction, bar.datetime);
        result.regime       = Core::market_regime_to_string(regime);

        ++signals_emitted_;

        LOG_INFO("[SymbolEngineContext:" << profile_.symbol << "]"
                 << " SIGNAL: " << result.direction
                 << " @" << result.entry_price
                 << " SL=" << result.stop_loss
                 << " TP=" << result.target_1
                 << " score=" << result.score
                 << " zone=" << result.zone_id
                 << " tag=" << result.order_tag);
    }

    return result;
}

// ============================================================
// Private helpers
// ============================================================

Core::MarketRegime SymbolEngineContext::determine_regime() const {
    // Delegate to existing engine core logic
    // (uses the bar buffer inside detector_)
    if (detector_.bar_count() < 50) return Core::MarketRegime::RANGING;

    const auto& bars = detector_.get_bars();
    int idx = static_cast<int>(bars.size()) - 1;

    // Simple EMA-based regime (same logic as V7 live engine)
    // Full HTF regime uses: determine_htf_regime() from sd_engine_core
    // For scanner context, use fast EMA crossover
    int fast_p = config_.ema_fast_period;
    int slow_p = config_.ema_slow_period;

    if (idx < slow_p) return Core::MarketRegime::RANGING;

    // Calculate EMAs
    double fast_ema = 0.0, slow_ema = 0.0;
    double fast_k = 2.0 / (fast_p + 1);
    double slow_k = 2.0 / (slow_p + 1);

    fast_ema = bars[idx - fast_p].close;
    slow_ema = bars[idx - slow_p].close;

    for (int i = idx - fast_p + 1; i <= idx; ++i)
        fast_ema = bars[i].close * fast_k + fast_ema * (1 - fast_k);
    for (int i = idx - slow_p + 1; i <= idx; ++i)
        slow_ema = bars[i].close * slow_k + slow_ema * (1 - slow_k);

    double sep_pct = std::abs(fast_ema - slow_ema) / slow_ema * 100.0;

    if (sep_pct < config_.ema_ranging_threshold_pct)
        return Core::MarketRegime::RANGING;

    return (fast_ema > slow_ema) ? Core::MarketRegime::BULL
                                 : Core::MarketRegime::BEAR;
}

std::string SymbolEngineContext::generate_order_tag(
    const std::string& direction,
    const std::string& timestamp) const
{
    // Format: SDT + MMDDHHMMSS + [L/S] + CC (16 chars)
    // Parse timestamp: "2026-04-09T09:30:00+05:30"
    std::string tag = "SDT";
    if (timestamp.size() >= 19) {
        tag += timestamp.substr(5, 2);   // MM
        tag += timestamp.substr(8, 2);   // DD
        tag += timestamp.substr(11, 2);  // HH
        tag += timestamp.substr(14, 2);  // MM
        tag += timestamp.substr(17, 2);  // SS
    } else {
        tag += "0000000000";
    }
    tag += (direction == "LONG") ? "L" : "S";
    tag += "CC";  // 2-char symbol code (future: NIFTY→NF, BANKNIFTY→BN)
    return tag;
}

} // namespace Scanner
} // namespace SDTrading
