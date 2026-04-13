// ============================================================
// SD TRADING V8 - PORTFOLIO METRICS
// src/analytics/portfolio_metrics.h
// Milestone M6.2
//
// Plain structs for all computed metrics.
// Produced by MetricsAggregator, consumed by REST endpoints.
// ============================================================

#ifndef SDTRADING_PORTFOLIO_METRICS_H
#define SDTRADING_PORTFOLIO_METRICS_H

#include <string>
#include <vector>
#include <cmath>

namespace SDTrading {
namespace Analytics {

// ============================================================
// PER-SYMBOL METRICS
// ============================================================
struct SymbolMetrics {
    std::string symbol;
    std::string period_type;       // "SESSION" | "ALL"
    std::string period_start;
    std::string period_end;

    // Trade counts
    int    total_trades         = 0;
    int    winning_trades       = 0;
    int    losing_trades        = 0;

    // Ratios
    double win_rate             = 0.0;   // 0.0–1.0
    double avg_rr               = 0.0;
    double profit_factor        = 0.0;
    double expectancy           = 0.0;   // avg P&L per trade

    // Streaks
    int    max_consecutive_wins   = 0;
    int    max_consecutive_losses = 0;

    // P&L
    double gross_pnl            = 0.0;
    double brokerage_total      = 0.0;
    double net_pnl              = 0.0;
    double total_return_pct     = 0.0;

    // Drawdown
    double max_drawdown         = 0.0;
    double max_drawdown_pct     = 0.0;

    // Risk
    double sharpe_ratio         = 0.0;
    double starting_capital     = 0.0;
    double ending_capital       = 0.0;

    // Best / worst trade
    double best_trade_pnl       = 0.0;
    double worst_trade_pnl      = 0.0;
    double avg_bars_in_trade    = 0.0;
};

// ============================================================
// PORTFOLIO METRICS  (all symbols combined)
// ============================================================
struct PortfolioMetrics {
    std::string computed_at;
    std::string period_start;
    std::string period_end;

    // Aggregate P&L
    double total_net_pnl        = 0.0;
    double total_gross_pnl      = 0.0;
    double total_brokerage      = 0.0;
    double total_return_pct     = 0.0;

    // Combined trade stats
    int    total_trades         = 0;
    int    winning_trades       = 0;
    double portfolio_win_rate   = 0.0;
    double portfolio_profit_factor = 0.0;
    double portfolio_expectancy = 0.0;
    double portfolio_sharpe     = 0.0;

    // Portfolio-level drawdown
    double max_drawdown         = 0.0;
    double max_drawdown_pct     = 0.0;

    // Per-symbol breakdown
    std::vector<SymbolMetrics> by_symbol;

    // Best / worst symbol by net P&L
    std::string best_symbol;
    std::string worst_symbol;
};

// ============================================================
// EQUITY CURVE POINT  (for REST export)
// ============================================================
struct EquityCurvePoint {
    std::string timestamp;
    std::string symbol;        // or "PORTFOLIO"
    double      equity         = 0.0;
    double      peak_equity    = 0.0;
    double      drawdown       = 0.0;
    double      drawdown_pct   = 0.0;
};

} // namespace Analytics
} // namespace SDTrading

#endif // SDTRADING_PORTFOLIO_METRICS_H
