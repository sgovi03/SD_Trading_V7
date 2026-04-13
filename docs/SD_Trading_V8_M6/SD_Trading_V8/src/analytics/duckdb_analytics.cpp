// ============================================================
// SD TRADING V8 - DUCKDB ANALYTICS IMPLEMENTATION
// src/analytics/duckdb_analytics.cpp
// Milestone M6.1
// ============================================================

#include "duckdb_analytics.h"
#include <sstream>

#ifdef ERROR
#undef ERROR
#endif
#ifdef WARN
#undef WARN
#endif

#ifdef DUCKDB_AVAILABLE
#include <duckdb.h>
#endif

namespace SDTrading {
namespace Analytics {

// ============================================================
// Constructor / Destructor
// ============================================================

DuckDbAnalytics::DuckDbAnalytics(const std::string& db_path)
    : db_path_(db_path)
{
#ifdef DUCKDB_AVAILABLE
    available_ = open();
    if (available_) {
        LOG_INFO("[DuckDbAnalytics] Connected. SQLite DB: " << db_path_);
    } else {
        LOG_WARN("[DuckDbAnalytics] Failed to connect — analytics disabled.");
    }
#else
    available_ = false;
    LOG_INFO("[DuckDbAnalytics] DuckDB not compiled in (DUCKDB_AVAILABLE not set)."
             " Heavy analytics disabled. MetricsAggregator still works.");
#endif
}

DuckDbAnalytics::~DuckDbAnalytics() {
#ifdef DUCKDB_AVAILABLE
    close();
#endif
}

// ============================================================
// top_zones
// ============================================================

std::vector<ZonePerformance> DuckDbAnalytics::top_zones(
    const std::string& symbol, int top_n, int min_trades)
{
    std::vector<ZonePerformance> result;

#ifdef DUCKDB_AVAILABLE
    if (!available_) return result;

    std::ostringstream sql;
    sql << "SELECT"
        << "  t.zone_id,"
        << "  t.symbol,"
        << "  z.zone_type,"
        << "  COUNT(*) AS total_trades,"
        << "  SUM(CASE WHEN t.net_pnl > 0 THEN 1 ELSE 0 END) AS wins,"
        << "  ROUND(AVG(CASE WHEN t.net_pnl > 0 THEN 1.0 ELSE 0.0 END), 4) AS win_rate,"
        << "  ROUND(AVG(t.net_pnl), 2) AS avg_pnl,"
        << "  ROUND(SUM(t.net_pnl), 2) AS total_pnl,"
        << "  ROUND(AVG(s.zone_score), 2) AS avg_score,"
        << "  RANK() OVER (ORDER BY"
        << "    AVG(CASE WHEN t.net_pnl > 0 THEN 1.0 ELSE 0.0 END) DESC) AS rank"
        << " FROM sqlite_scan('" << db_path_ << "', 'trades') t"
        << " LEFT JOIN sqlite_scan('" << db_path_ << "', 'signals') s"
        << "   ON t.signal_id = s.id"
        << " LEFT JOIN sqlite_scan('" << db_path_ << "', 'zones') z"
        << "   ON t.zone_id = z.zone_id AND t.symbol = z.symbol"
        << " WHERE t.symbol = '" << symbol << "'"
        << "   AND t.trade_status = 'CLOSED'"
        << "   AND t.net_pnl IS NOT NULL"
        << " GROUP BY t.zone_id, t.symbol, z.zone_type"
        << " HAVING COUNT(*) >= " << min_trades
        << " ORDER BY win_rate DESC, total_pnl DESC"
        << " LIMIT " << top_n;

    auto rows = query(sql.str());
    int rank = 1;
    for (auto& row : rows) {
        if (row.size() < 10) continue;
        ZonePerformance zp;
        try {
            zp.zone_id      = std::stoi(row[0]);
            zp.symbol       = row[1];
            zp.zone_type    = row[2];
            zp.total_trades = std::stoi(row[3]);
            zp.wins         = std::stoi(row[4]);
            zp.win_rate     = std::stod(row[5]);
            zp.avg_pnl      = std::stod(row[6]);
            zp.total_pnl    = std::stod(row[7]);
            zp.avg_score    = std::stod(row[8]);
            zp.rank         = rank++;
        } catch (...) { continue; }
        result.push_back(zp);
    }
#else
    LOG_INFO("[DuckDbAnalytics] top_zones() skipped — DuckDB not available.");
#endif
    return result;
}

// ============================================================
// daily_performance
// ============================================================

std::vector<PeriodPerformance> DuckDbAnalytics::daily_performance(
    const std::string& symbol, int lookback_days)
{
    std::vector<PeriodPerformance> result;

#ifdef DUCKDB_AVAILABLE
    if (!available_) return result;

    std::ostringstream sql;
    sql << "SELECT"
        << "  strftime(exit_time, '%Y-%m-%d') AS day,"
        << "  COUNT(*) AS trades,"
        << "  SUM(CASE WHEN net_pnl > 0 THEN 1 ELSE 0 END) AS wins,"
        << "  ROUND(AVG(CASE WHEN net_pnl > 0 THEN 1.0 ELSE 0.0 END), 4) AS win_rate,"
        << "  ROUND(SUM(net_pnl), 2) AS net_pnl,"
        << "  ROUND(SUM(CASE WHEN net_pnl > 0 THEN ABS(gross_pnl) ELSE 0 END) /"
        << "    NULLIF(SUM(CASE WHEN net_pnl <= 0 THEN ABS(gross_pnl) ELSE 0 END), 0), 2)"
        << "  AS profit_factor"
        << " FROM sqlite_scan('" << db_path_ << "', 'trades')"
        << " WHERE symbol = '" << symbol << "'"
        << "   AND trade_status = 'CLOSED'"
        << "   AND exit_time >= date('now', '-" << lookback_days << " days')"
        << " GROUP BY day"
        << " ORDER BY day ASC";

    auto rows = query(sql.str());
    for (auto& row : rows) {
        if (row.size() < 6) continue;
        PeriodPerformance pp;
        try {
            pp.period       = row[0];
            pp.period_type  = "DAILY";
            pp.total_trades = std::stoi(row[1]);
            pp.wins         = std::stoi(row[2]);
            pp.win_rate     = std::stod(row[3]);
            pp.net_pnl      = std::stod(row[4]);
            pp.profit_factor= row[5].empty() ? 0.0 : std::stod(row[5]);
        } catch (...) { continue; }
        result.push_back(pp);
    }
#endif
    return result;
}

// ============================================================
// weekly_performance
// ============================================================

std::vector<PeriodPerformance> DuckDbAnalytics::weekly_performance(
    const std::string& symbol, int lookback_weeks)
{
    std::vector<PeriodPerformance> result;

#ifdef DUCKDB_AVAILABLE
    if (!available_) return result;

    std::ostringstream sql;
    sql << "SELECT"
        << "  strftime(exit_time, '%Y-W%W') AS week,"
        << "  COUNT(*) AS trades,"
        << "  SUM(CASE WHEN net_pnl > 0 THEN 1 ELSE 0 END) AS wins,"
        << "  ROUND(AVG(CASE WHEN net_pnl > 0 THEN 1.0 ELSE 0.0 END), 4) AS win_rate,"
        << "  ROUND(SUM(net_pnl), 2) AS net_pnl,"
        << "  ROUND(SUM(CASE WHEN net_pnl > 0 THEN ABS(gross_pnl) ELSE 0 END) /"
        << "    NULLIF(SUM(CASE WHEN net_pnl <= 0 THEN ABS(gross_pnl) ELSE 0 END), 0), 2)"
        << "  AS profit_factor"
        << " FROM sqlite_scan('" << db_path_ << "', 'trades')"
        << " WHERE symbol = '" << symbol << "'"
        << "   AND trade_status = 'CLOSED'"
        << "   AND exit_time >= date('now', '-" << lookback_weeks * 7 << " days')"
        << " GROUP BY week"
        << " ORDER BY week ASC";

    auto rows = query(sql.str());
    for (auto& row : rows) {
        if (row.size() < 6) continue;
        PeriodPerformance pp;
        try {
            pp.period       = row[0];
            pp.period_type  = "WEEKLY";
            pp.total_trades = std::stoi(row[1]);
            pp.wins         = std::stoi(row[2]);
            pp.win_rate     = std::stod(row[3]);
            pp.net_pnl      = std::stod(row[4]);
            pp.profit_factor= row[5].empty() ? 0.0 : std::stod(row[5]);
        } catch (...) { continue; }
        result.push_back(pp);
    }
#endif
    return result;
}

// ============================================================
// monthly_performance
// ============================================================

std::vector<PeriodPerformance> DuckDbAnalytics::monthly_performance(
    const std::string& symbol)
{
    std::vector<PeriodPerformance> result;

#ifdef DUCKDB_AVAILABLE
    if (!available_) return result;

    std::ostringstream sql;
    sql << "SELECT"
        << "  strftime(exit_time, '%Y-%m') AS month,"
        << "  COUNT(*) AS trades,"
        << "  SUM(CASE WHEN net_pnl > 0 THEN 1 ELSE 0 END) AS wins,"
        << "  ROUND(AVG(CASE WHEN net_pnl > 0 THEN 1.0 ELSE 0.0 END), 4) AS win_rate,"
        << "  ROUND(SUM(net_pnl), 2) AS net_pnl,"
        << "  ROUND(SUM(CASE WHEN net_pnl > 0 THEN ABS(gross_pnl) ELSE 0 END) /"
        << "    NULLIF(SUM(CASE WHEN net_pnl <= 0 THEN ABS(gross_pnl) ELSE 0 END), 0), 2)"
        << "  AS profit_factor"
        << " FROM sqlite_scan('" << db_path_ << "', 'trades')"
        << " WHERE symbol = '" << symbol << "'"
        << "   AND trade_status = 'CLOSED'"
        << " GROUP BY month"
        << " ORDER BY month ASC";

    auto rows = query(sql.str());
    for (auto& row : rows) {
        if (row.size() < 6) continue;
        PeriodPerformance pp;
        try {
            pp.period       = row[0];
            pp.period_type  = "MONTHLY";
            pp.total_trades = std::stoi(row[1]);
            pp.wins         = std::stoi(row[2]);
            pp.win_rate     = std::stod(row[3]);
            pp.net_pnl      = std::stod(row[4]);
            pp.profit_factor= row[5].empty() ? 0.0 : std::stod(row[5]);
        } catch (...) { continue; }
        result.push_back(pp);
    }
#endif
    return result;
}

// ============================================================
// rolling_sharpe
// ============================================================

std::vector<std::pair<std::string, double>> DuckDbAnalytics::rolling_sharpe(
    const std::string& symbol, int window)
{
    std::vector<std::pair<std::string, double>> result;

#ifdef DUCKDB_AVAILABLE
    if (!available_) return result;

    std::ostringstream sql;
    sql << "SELECT exit_time,"
        << "  AVG(net_pnl) OVER w / NULLIF(STDDEV(net_pnl) OVER w, 0)"
        << "  AS rolling_sharpe"
        << " FROM sqlite_scan('" << db_path_ << "', 'trades')"
        << " WHERE symbol = '" << symbol << "'"
        << "   AND trade_status = 'CLOSED'"
        << " WINDOW w AS ("
        << "   ORDER BY exit_time ROWS BETWEEN " << (window-1) << " PRECEDING AND CURRENT ROW)"
        << " ORDER BY exit_time ASC";

    auto rows = query(sql.str());
    for (auto& row : rows) {
        if (row.size() < 2 || row[1].empty()) continue;
        try {
            result.emplace_back(row[0], std::stod(row[1]));
        } catch (...) { continue; }
    }
#endif
    return result;
}

// ============================================================
// DuckDB internal helpers (compiled only when DUCKDB_AVAILABLE)
// ============================================================

#ifdef DUCKDB_AVAILABLE

bool DuckDbAnalytics::open() {
    duckdb_database db;
    duckdb_connection conn;

    if (duckdb_open(nullptr, &db) == DuckDBError) {
        LOG_WARN("[DuckDbAnalytics] duckdb_open failed.");
        return false;
    }
    if (duckdb_connect(db, &conn) == DuckDBError) {
        duckdb_close(&db);
        LOG_WARN("[DuckDbAnalytics] duckdb_connect failed.");
        return false;
    }

    duckdb_db_   = db;
    duckdb_conn_ = conn;

    // Load sqlite extension
    duckdb_result res;
    if (duckdb_query(static_cast<duckdb_connection>(duckdb_conn_),
                     "INSTALL sqlite; LOAD sqlite;",
                     &res) == DuckDBError) {
        LOG_WARN("[DuckDbAnalytics] Failed to load sqlite extension.");
        duckdb_destroy_result(&res);
        return false;
    }
    duckdb_destroy_result(&res);
    return true;
}

void DuckDbAnalytics::close() {
    if (duckdb_conn_) {
        duckdb_disconnect(static_cast<duckdb_connection*>(&duckdb_conn_));
        duckdb_conn_ = nullptr;
    }
    if (duckdb_db_) {
        duckdb_close(static_cast<duckdb_database*>(&duckdb_db_));
        duckdb_db_ = nullptr;
    }
}

std::vector<std::vector<std::string>> DuckDbAnalytics::query(
    const std::string& sql)
{
    std::vector<std::vector<std::string>> rows;
    duckdb_result res;

    if (duckdb_query(static_cast<duckdb_connection>(duckdb_conn_),
                     sql.c_str(), &res) == DuckDBError) {
        LOG_WARN("[DuckDbAnalytics] Query failed: "
                 << duckdb_result_error(&res));
        duckdb_destroy_result(&res);
        return rows;
    }

    idx_t row_count = duckdb_row_count(&res);
    idx_t col_count = duckdb_column_count(&res);

    for (idx_t r = 0; r < row_count; ++r) {
        std::vector<std::string> row;
        for (idx_t c = 0; c < col_count; ++c) {
            char* val = duckdb_value_varchar(&res, c, r);
            row.push_back(val ? std::string(val) : "");
            if (val) duckdb_free(val);
        }
        rows.push_back(row);
    }

    duckdb_destroy_result(&res);
    return rows;
}

#endif // DUCKDB_AVAILABLE

} // namespace Analytics
} // namespace SDTrading
