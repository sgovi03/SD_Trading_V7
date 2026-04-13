// ============================================================
// SD TRADING V8 - DUCKDB ANALYTICS LAYER
// src/analytics/duckdb_analytics.h
// Milestone M6.1
//
// DuckDB attaches directly to the SQLite file using
// sqlite_scan() — no ETL, no data copy needed.
//
// Used for queries that are slow in SQLite but fast in DuckDB:
//   - Rolling Sharpe over sliding windows
//   - Zone win-rate percentile rankings
//   - Multi-period performance slicing (daily/weekly/monthly)
//   - Cross-symbol correlation
//
// If DUCKDB_AVAILABLE is not defined (DuckDB not installed),
// all methods return empty results with a log warning.
// The system continues to work using MetricsAggregator only.
// ============================================================

#ifndef SDTRADING_DUCKDB_ANALYTICS_H
#define SDTRADING_DUCKDB_ANALYTICS_H

#include "portfolio_metrics.h"
#include "../utils/logger.h"
#include <string>
#include <vector>

namespace SDTrading {
namespace Analytics {

// ============================================================
// ZONE PERFORMANCE RECORD  (DuckDB zone ranking query)
// ============================================================
struct ZonePerformance {
    int         zone_id         = 0;
    std::string symbol;
    std::string zone_type;      // DEMAND | SUPPLY
    int         total_trades    = 0;
    int         wins            = 0;
    double      win_rate        = 0.0;
    double      avg_pnl         = 0.0;
    double      total_pnl       = 0.0;
    double      avg_score       = 0.0;
    int         rank            = 0;   // 1=best
};

// ============================================================
// PERIOD PERFORMANCE  (daily / weekly / monthly slices)
// ============================================================
struct PeriodPerformance {
    std::string period;         // "2026-04-09" or "2026-W15" or "2026-04"
    std::string period_type;    // "DAILY" | "WEEKLY" | "MONTHLY"
    int         total_trades    = 0;
    int         wins            = 0;
    double      win_rate        = 0.0;
    double      net_pnl         = 0.0;
    double      profit_factor   = 0.0;
};

// ============================================================
// DUCKDB ANALYTICS
// ============================================================
class DuckDbAnalytics {
public:
    // db_path: path to the SQLite .db file
    explicit DuckDbAnalytics(const std::string& db_path);
    ~DuckDbAnalytics();

    // Returns false if DuckDB is not available
    bool is_available() const { return available_; }

    // ── Zone ranking ─────────────────────────────────────────
    // Top N zones by win-rate (minimum min_trades trades)
    std::vector<ZonePerformance> top_zones(
        const std::string& symbol,
        int top_n      = 10,
        int min_trades = 3);

    // ── Period performance ────────────────────────────────────
    std::vector<PeriodPerformance> daily_performance(
        const std::string& symbol,
        int lookback_days = 30);

    std::vector<PeriodPerformance> weekly_performance(
        const std::string& symbol,
        int lookback_weeks = 8);

    std::vector<PeriodPerformance> monthly_performance(
        const std::string& symbol);

    // ── Rolling Sharpe ────────────────────────────────────────
    // Returns {timestamp, rolling_sharpe} pairs
    std::vector<std::pair<std::string, double>> rolling_sharpe(
        const std::string& symbol,
        int window_trades = 20);

private:
    std::string db_path_;
    bool        available_ = false;

#ifdef DUCKDB_AVAILABLE
    // DuckDB connection — opaque pointer to avoid header dependency
    // when DuckDB is not installed.
    void* duckdb_conn_ = nullptr;
    void* duckdb_db_   = nullptr;

    bool  open();
    void  close();
    // Execute a DuckDB query, return rows as vector of string vectors
    std::vector<std::vector<std::string>> query(const std::string& sql);
#endif
};

} // namespace Analytics
} // namespace SDTrading

#endif // SDTRADING_DUCKDB_ANALYTICS_H
