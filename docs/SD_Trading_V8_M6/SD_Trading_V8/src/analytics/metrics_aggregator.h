// ============================================================
// SD TRADING V8 - METRICS AGGREGATOR
// src/analytics/metrics_aggregator.h
// Milestone M6.2
//
// Computes SymbolMetrics and PortfolioMetrics directly from
// the SQLite trades and equity_curve tables using standard
// SQLite queries (no DuckDB dependency for basic metrics).
//
// DuckDB is used only for the heavy analytical queries in
// DuckDbAnalytics (M6.1): rolling Sharpe, percentile zones,
// multi-period slicing. For session-end metrics the simple
// SQLite path is sufficient and always available.
// ============================================================

#ifndef SDTRADING_METRICS_AGGREGATOR_H
#define SDTRADING_METRICS_AGGREGATOR_H

#include "portfolio_metrics.h"
#include "../persistence/sqlite_connection.h"
#include "../utils/logger.h"
#include <string>
#include <vector>

namespace SDTrading {
namespace Analytics {

class MetricsAggregator {
public:
    explicit MetricsAggregator(Persistence::SqliteConnection& conn);

    // ── Per-symbol ───────────────────────────────────────────

    // Compute metrics for one symbol over all closed trades.
    SymbolMetrics compute_symbol(const std::string& symbol,
                                  double starting_capital,
                                  const std::string& period_type = "ALL");

    // Compute metrics for all active symbols.
    std::vector<SymbolMetrics> compute_all_symbols(
        double starting_capital,
        const std::string& period_type = "ALL");

    // ── Portfolio ────────────────────────────────────────────

    // Aggregate all per-symbol metrics into portfolio view.
    PortfolioMetrics compute_portfolio(double starting_capital);

    // ── Equity curve ─────────────────────────────────────────

    // Return equity curve for one symbol (or "PORTFOLIO").
    std::vector<EquityCurvePoint> get_equity_curve(
        const std::string& symbol,
        int limit = 500);

    // ── Persist metrics to DB ────────────────────────────────

    // Write computed metrics to the metrics table.
    void persist(const SymbolMetrics& m);
    void persist_portfolio(const PortfolioMetrics& p);

private:
    Persistence::SqliteConnection& conn_;

    // Internal helpers
    double compute_sharpe(const std::vector<double>& pnl_series,
                          double risk_free_rate = 0.0) const;
    double compute_profit_factor(double gross_wins,
                                 double gross_losses) const;
    int    compute_max_streak(const std::vector<bool>& wins,
                               bool target) const;
};

} // namespace Analytics
} // namespace SDTrading

#endif // SDTRADING_METRICS_AGGREGATOR_H
