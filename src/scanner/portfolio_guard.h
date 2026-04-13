// ============================================================
// SD TRADING V8 - PORTFOLIO GUARD
// src/scanner/portfolio_guard.h
//
// PURPOSE:
//   Pre-filter for LiveEngine entry decisions.
//   Shared across all symbol LiveEngine instances.
//   Enforces portfolio-level constraints without touching
//   any LiveEngine trading logic.
//
// RESPONSIBILITIES:
//   • Per-symbol position tracking (max 1 per symbol)
//   • Capital pool management (shared, dynamic)
//   • Daily P&L tracking and loss-limit enforcement
//   • EV-weighted lot size calculation
//
// DESIGN:
//   - Singleton per portfolio (passed by reference)
//   - All methods thread-safe (mutex protected)
//   - LiveEngine calls is_entry_allowed() via PipeBroker
//   - PipeBroker calls notify_* after order events
//
// CAPITAL FORMULA (EV-weighted):
//   expected_value = (score/100) x rr_ratio
//   baseline_ev    = baseline_score x baseline_rr
//   score_mult     = expected_value / baseline_ev
//   risk_amount    = capital_available x risk_pct/100
//   raw_lots       = risk_amount / (sl_distance x lot_value)
//   final_lots     = clamp(raw_lots x score_mult, 1, max_lots)
// ============================================================

#ifndef SDTRADING_PORTFOLIO_GUARD_H
#define SDTRADING_PORTFOLIO_GUARD_H

#include <string>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include "utils/logger.h"

namespace SDTrading {
namespace Scanner {

// ============================================================
// PortfolioConfig — loaded from MASTER.config
// ============================================================
struct PortfolioConfig {
    double  starting_capital        = 300000.0; // ₹3,00,000
    double  risk_per_trade_pct      = 2.0;      // 2% per trade
    int     max_position_lots       = 4;        // per symbol ceiling
    double  daily_loss_limit_pct    = 5.0;      // halt if daily loss > 5%
    double  baseline_score          = 0.65;     // normalisation base
    double  baseline_rr             = 1.75;     // normalisation base
    int     max_positions_per_symbol = 1;       // max 1 open per symbol
};

// ============================================================
// PositionState — per-symbol runtime state
// ============================================================
struct PositionState {
    bool    is_open         = false;
    int     lots            = 0;
    double  entry_price     = 0.0;
    double  unrealised_pnl  = 0.0;
    std::string direction;          // "LONG" or "SHORT"
};

// ============================================================
// PortfolioGuard
// ============================================================
class PortfolioGuard {
public:
    explicit PortfolioGuard(const PortfolioConfig& cfg);

    // ----------------------------------------------------------
    // Pre-filter — called by PipeBroker.is_entry_allowed()
    // Returns true only if ALL conditions pass:
    //   1. Symbol has no open position
    //   2. Capital available (not exhausted)
    //   3. Daily loss limit not breached
    // ----------------------------------------------------------
    bool entry_allowed(const std::string& symbol) const;

    // ----------------------------------------------------------
    // Lot size calculation — called by PipeBroker to override
    // LiveEngine's lot size with portfolio-adjusted value.
    //
    // Parameters:
    //   symbol      : trading symbol
    //   score       : zone quality score (0-100)
    //   rr_ratio    : expected risk:reward ratio
    //   sl_distance : stop loss distance in points
    //   lot_value   : rupee value per lot per point
    //
    // Returns: final lot count (1 .. max_position_lots)
    // ----------------------------------------------------------
    int calculate_lots(
        const std::string& symbol,
        double score,
        double rr_ratio,
        double sl_distance,
        double lot_value) const;

    // ----------------------------------------------------------
    // Position lifecycle notifications
    // Called by PipeBroker after order events.
    // ----------------------------------------------------------
    void notify_position_opened(
        const std::string& symbol,
        const std::string& direction,
        int                lots,
        double             entry_price);

    void notify_position_closed(
        const std::string& symbol,
        double             exit_price,
        double             realised_pnl);

    // ----------------------------------------------------------
    // Daily reset — call at market open each day
    // ----------------------------------------------------------
    void reset_daily_pnl();

    // ----------------------------------------------------------
    // Accessors
    // ----------------------------------------------------------
    double  capital_available()  const;
    double  daily_pnl()         const;
    bool    daily_limit_hit()   const;
    int     open_position_count() const;
    bool    has_open_position(const std::string& symbol) const;

    // Human-readable status for logging
    std::string status_string() const;

private:
    PortfolioConfig                                 cfg_;
    mutable std::mutex                              mutex_;

    // Capital
    double                                          capital_available_;

    // Daily P&L
    double                                          daily_pnl_          { 0.0 };
    bool                                            daily_limit_hit_    { false };

    // Per-symbol position state
    std::unordered_map<std::string, PositionState>  positions_;

    // ── Private helpers ──────────────────────────────────────
    double baseline_ev() const {
        return cfg_.baseline_score * cfg_.baseline_rr;
    }
};

} // namespace Scanner
} // namespace SDTrading

#endif // SDTRADING_PORTFOLIO_GUARD_H
