// ============================================================
// SD TRADING V8 - SHADOW MODE
// src/data/shadow_mode.h
// Milestone M2.6
//
// Shadow mode runs AmiBroker pipe and old Fyers CSV feed
// in parallel for one week before cutting over.
// Discrepancies are logged to bar_rejections with reason
// "SHADOW_MISMATCH" for post-hoc analysis.
//
// Enabled via MASTER.config: shadow_mode_enabled = YES
//
// Architecture:
//   PipeListener (AmiBroker) → BarValidatedEvent (primary)
//   FyersAdapter CSV         → reads in parallel
//   ShadowModeComparator     → compares on each bar
//
// Comparison fields: O, H, L, C (within tolerance).
// Volume and OI not compared (different counting conventions).
//
// After 5 consecutive clean days → auto-disable shadow mode.
// ============================================================

#ifndef SDTRADING_SHADOW_MODE_H
#define SDTRADING_SHADOW_MODE_H

#include <string>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include "common_types.h"
#include "../events/event_types.h"
#include "../events/event_bus.h"
#include "../utils/logger.h"

namespace SDTrading {
namespace Data {

// ============================================================
// SHADOW COMPARISON RESULT
// ============================================================
struct ShadowComparison {
    bool        matches        = true;
    std::string symbol;
    std::string timestamp;
    double      ami_open, ami_high, ami_low, ami_close;
    double      fyers_open, fyers_high, fyers_low, fyers_close;
    double      max_diff_pct   = 0.0;   // max % difference across OHLC
    std::string mismatch_field;         // which field differed most
};

// ============================================================
// SHADOW MODE COMPARATOR
// One instance per symbol.
// ============================================================
class ShadowModeComparator {
public:
    ShadowModeComparator(const std::string& symbol,
                         double tolerance_pct,
                         Events::EventBus& bus);

    // Called by shadow pipeline when BOTH sources have a bar
    // for the same timestamp.
    void compare(const Core::Bar& amibroker_bar,
                 const Core::Bar& fyers_bar);

    // Statistics
    int   bars_compared()  const { return bars_compared_; }
    int   mismatches()     const { return mismatches_; }
    double mismatch_rate() const {
        return bars_compared_ > 0
               ? (double)mismatches_ / bars_compared_ * 100.0 : 0.0;
    }
    bool  is_clean() const { return mismatches_ == 0; }

private:
    std::string       symbol_;
    double            tolerance_pct_;
    Events::EventBus& bus_;

    int bars_compared_ = 0;
    int mismatches_    = 0;

    void log_mismatch(const ShadowComparison& cmp);
    void publish_mismatch_rejection(const ShadowComparison& cmp);
};

// ============================================================
// SHADOW MODE MANAGER
// Controls overall shadow mode lifecycle.
// ============================================================
class ShadowModeManager {
public:
    // tolerance_pct: max allowed % diff per OHLC field (default 0.1%)
    explicit ShadowModeManager(Events::EventBus& bus,
                                double tolerance_pct = 0.1);

    // Register a symbol for shadow comparison
    void register_symbol(const std::string& symbol);

    // Check if shadow mode is still active
    bool is_active() const { return active_.load(); }

    // Called at end of each trading day
    void on_day_end(const std::string& date);

    // Manual override
    void disable() { active_ = false; }

    // Print summary report
    void print_report() const;

    // Get per-symbol comparator
    ShadowModeComparator* get_comparator(const std::string& symbol);

private:
    Events::EventBus&  bus_;
    double             tolerance_pct_;
    std::atomic<bool>  active_ { true };
    int                clean_days_ = 0;
    static constexpr int AUTO_DISABLE_CLEAN_DAYS = 5;

    std::unordered_map<std::string,
        std::unique_ptr<ShadowModeComparator>> comparators_;
    mutable std::mutex comparators_mutex_;
};

} // namespace Data
} // namespace SDTrading

#endif // SDTRADING_SHADOW_MODE_H
