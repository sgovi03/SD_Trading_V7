// ============================================================
// SD TRADING V8 - SHADOW MODE IMPLEMENTATION
// src/data/shadow_mode.cpp
// Milestone M2.6
// ============================================================

#include "shadow_mode.h"
#include <cmath>
#include <sstream>
#include <iomanip>

namespace SDTrading {
namespace Data {

// ============================================================
// ShadowModeComparator
// ============================================================

ShadowModeComparator::ShadowModeComparator(
    const std::string& symbol,
    double tolerance_pct,
    Events::EventBus& bus)
    : symbol_(symbol)
    , tolerance_pct_(tolerance_pct)
    , bus_(bus)
{
    LOG_INFO("[ShadowMode:" << symbol_ << "]"
             << " Comparator created (tolerance=" << tolerance_pct_ << "%)");
}

void ShadowModeComparator::compare(const Core::Bar& ami,
                                   const Core::Bar& fyers) {
    ++bars_compared_;

    auto pct_diff = [](double a, double b) -> double {
        if (b == 0.0) return 0.0;
        return std::abs(a - b) / b * 100.0;
    };

    ShadowComparison cmp;
    cmp.symbol      = symbol_;
    cmp.timestamp   = ami.datetime;
    cmp.ami_open    = ami.open;  cmp.ami_high    = ami.high;
    cmp.ami_low     = ami.low;   cmp.ami_close   = ami.close;
    cmp.fyers_open  = fyers.open; cmp.fyers_high  = fyers.high;
    cmp.fyers_low   = fyers.low;  cmp.fyers_close = fyers.close;

    // Find largest discrepancy across OHLC
    struct Field { const char* name; double diff; };
    Field fields[] = {
        {"Open",  pct_diff(ami.open,  fyers.open)},
        {"High",  pct_diff(ami.high,  fyers.high)},
        {"Low",   pct_diff(ami.low,   fyers.low)},
        {"Close", pct_diff(ami.close, fyers.close)},
    };

    for (auto& f : fields) {
        if (f.diff > cmp.max_diff_pct) {
            cmp.max_diff_pct    = f.diff;
            cmp.mismatch_field  = f.name;
        }
    }

    if (cmp.max_diff_pct > tolerance_pct_) {
        cmp.matches = false;
        ++mismatches_;
        log_mismatch(cmp);
        publish_mismatch_rejection(cmp);
    } else {
        LOG_DEBUG("[ShadowMode:" << symbol_ << "]"
                  << " Match @ " << ami.datetime
                  << " max_diff=" << std::fixed << std::setprecision(4)
                  << cmp.max_diff_pct << "%");
    }
}

void ShadowModeComparator::log_mismatch(const ShadowComparison& cmp) {
    LOG_WARN("[ShadowMode:" << symbol_ << "] MISMATCH @ " << cmp.timestamp);
    LOG_WARN("  Field:      " << cmp.mismatch_field
             << " diff=" << std::fixed << std::setprecision(4)
             << cmp.max_diff_pct << "%");
    LOG_WARN("  AmiBroker:  O=" << cmp.ami_open   << " H=" << cmp.ami_high
             << " L=" << cmp.ami_low    << " C=" << cmp.ami_close);
    LOG_WARN("  Fyers:      O=" << cmp.fyers_open << " H=" << cmp.fyers_high
             << " L=" << cmp.fyers_low  << " C=" << cmp.fyers_close);
    LOG_WARN("  Verdict: Use AmiBroker (matches TradingView reference data)");
}

void ShadowModeComparator::publish_mismatch_rejection(
    const ShadowComparison& cmp)
{
    // Log mismatch as bar rejection for DB audit trail
    std::ostringstream raw;
    raw << std::fixed << std::setprecision(2);
    raw << "FYERS|" << cmp.symbol << "|5min|" << cmp.timestamp
        << "|" << cmp.fyers_open  << "|" << cmp.fyers_high
        << "|" << cmp.fyers_low   << "|" << cmp.fyers_close;

    Events::BarRejectedEvent evt;
    evt.symbol           = cmp.symbol;
    evt.timeframe        = "5min";
    evt.raw_wire_data    = raw.str();
    evt.rejection_tier   = 2;
    evt.rejection_reason = "SHADOW_MISMATCH: " + cmp.mismatch_field
                           + " diff=" + std::to_string(cmp.max_diff_pct) + "%"
                           + " (Fyers vs AmiBroker). Use AmiBroker value.";
    evt.prev_close       = cmp.ami_close;  // AmiBroker is the reference
    bus_.publish(evt);
}

// ============================================================
// ShadowModeManager
// ============================================================

ShadowModeManager::ShadowModeManager(Events::EventBus& bus,
                                     double tolerance_pct)
    : bus_(bus)
    , tolerance_pct_(tolerance_pct)
{
    LOG_INFO("[ShadowModeManager] Initialized. Tolerance=" << tolerance_pct_ << "%");
    LOG_INFO("[ShadowModeManager] Will auto-disable after "
             << AUTO_DISABLE_CLEAN_DAYS << " consecutive clean days.");
}

void ShadowModeManager::register_symbol(const std::string& symbol) {
    std::lock_guard<std::mutex> lock(comparators_mutex_);
    comparators_[symbol] = std::make_unique<ShadowModeComparator>(
        symbol, tolerance_pct_, bus_);
    LOG_INFO("[ShadowModeManager] Registered symbol: " << symbol);
}

ShadowModeComparator* ShadowModeManager::get_comparator(
    const std::string& symbol)
{
    std::lock_guard<std::mutex> lock(comparators_mutex_);
    auto it = comparators_.find(symbol);
    return it != comparators_.end() ? it->second.get() : nullptr;
}

void ShadowModeManager::on_day_end(const std::string& date) {
    if (!active_.load()) return;

    // Check if all symbols were clean today
    bool all_clean = true;
    {
        std::lock_guard<std::mutex> lock(comparators_mutex_);
        for (auto& [sym, cmp] : comparators_) {
            if (!cmp->is_clean()) {
                all_clean = false;
                break;
            }
        }
    }

    if (all_clean) {
        ++clean_days_;
        LOG_INFO("[ShadowModeManager] Clean day #" << clean_days_
                 << "/" << AUTO_DISABLE_CLEAN_DAYS
                 << " on " << date);
        if (clean_days_ >= AUTO_DISABLE_CLEAN_DAYS) {
            active_ = false;
            LOG_INFO("[ShadowModeManager] AUTO-DISABLED after "
                     << AUTO_DISABLE_CLEAN_DAYS
                     << " consecutive clean days. AmiBroker feed confirmed reliable.");
        }
    } else {
        // Reset clean streak on any mismatch day
        clean_days_ = 0;
        LOG_WARN("[ShadowModeManager] Mismatch detected on "
                 << date << ". Clean streak reset.");
        print_report();
    }
}

void ShadowModeManager::print_report() const {
    std::lock_guard<std::mutex> lock(comparators_mutex_);
    LOG_INFO("[ShadowModeManager] ─── Shadow Mode Report ───");
    LOG_INFO("  Active: " << (active_.load() ? "YES" : "NO"));
    LOG_INFO("  Clean days: " << clean_days_ << "/" << AUTO_DISABLE_CLEAN_DAYS);
    for (auto& [sym, cmp] : comparators_) {
        LOG_INFO("  " << sym
                 << " bars_compared=" << cmp->bars_compared()
                 << " mismatches=" << cmp->mismatches()
                 << " rate=" << std::fixed << std::setprecision(2)
                 << cmp->mismatch_rate() << "%");
    }
    LOG_INFO("[ShadowModeManager] ─────────────────────────");
}

} // namespace Data
} // namespace SDTrading
