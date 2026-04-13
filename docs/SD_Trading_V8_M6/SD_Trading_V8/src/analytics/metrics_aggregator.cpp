// ============================================================
// SD TRADING V8 - METRICS AGGREGATOR IMPLEMENTATION
// src/analytics/metrics_aggregator.cpp
// Milestone M6.2
// ============================================================

#include "metrics_aggregator.h"
#include <cmath>
#include <numeric>
#include <algorithm>
#include <sstream>
#include <iomanip>

#ifdef ERROR
#undef ERROR
#endif
#ifdef WARN
#undef WARN
#endif

namespace SDTrading {
namespace Analytics {

MetricsAggregator::MetricsAggregator(Persistence::SqliteConnection& conn)
    : conn_(conn) {}

// ============================================================
// compute_symbol
// ============================================================

SymbolMetrics MetricsAggregator::compute_symbol(
    const std::string& symbol,
    double starting_capital,
    const std::string& period_type)
{
    SymbolMetrics m;
    m.symbol          = symbol;
    m.period_type     = period_type;
    m.starting_capital = starting_capital;

    // ── Fetch all closed trades for this symbol ──────────────
    static const char* SQL =
        "SELECT net_pnl, gross_pnl, brokerage, bars_in_trade,"
        "       entry_time, exit_time"
        " FROM trades"
        " WHERE symbol=? AND trade_status='CLOSED'"
        "   AND net_pnl IS NOT NULL"
        " ORDER BY exit_time ASC";

    auto stmt = conn_.prepare(SQL);
    stmt.bind_text(1, symbol);

    std::vector<double> pnl_series;
    double gross_wins   = 0.0;
    double gross_losses = 0.0;
    std::vector<bool>   win_flags;
    double best  = 0.0, worst = 0.0;
    double total_bars = 0.0;

    bool first = true;
    while (stmt.step()) {
        double net_pnl  = stmt.column_double(0);
        double gross_pnl= stmt.column_double(1);
        double brokerage= stmt.column_double(2);
        int    bars     = stmt.column_int(3);

        if (first) {
            m.period_start = stmt.column_text(4);
            first = false;
        }
        m.period_end = stmt.column_text(5);

        ++m.total_trades;
        m.net_pnl          += net_pnl;
        m.gross_pnl        += gross_pnl;
        m.brokerage_total  += brokerage;
        total_bars         += bars;

        pnl_series.push_back(net_pnl);

        if (net_pnl > 0) {
            ++m.winning_trades;
            gross_wins += std::abs(gross_pnl);
            if (net_pnl > best) best = net_pnl;
        } else {
            ++m.losing_trades;
            gross_losses += std::abs(gross_pnl);
            if (net_pnl < worst) worst = net_pnl;
        }
        win_flags.push_back(net_pnl > 0);
    }

    if (m.total_trades == 0) {
        LOG_INFO("[MetricsAggregator] No closed trades for " << symbol);
        return m;
    }

    // ── Derived ratios ───────────────────────────────────────
    m.win_rate          = static_cast<double>(m.winning_trades) / m.total_trades;
    m.profit_factor     = compute_profit_factor(gross_wins, gross_losses);
    m.expectancy        = m.net_pnl / m.total_trades;
    m.best_trade_pnl    = best;
    m.worst_trade_pnl   = worst;
    m.avg_bars_in_trade = total_bars / m.total_trades;
    m.sharpe_ratio      = compute_sharpe(pnl_series);
    m.ending_capital    = starting_capital + m.net_pnl;
    m.total_return_pct  = starting_capital > 0
                          ? (m.net_pnl / starting_capital * 100.0) : 0.0;

    // Streaks
    m.max_consecutive_wins   = compute_max_streak(win_flags, true);
    m.max_consecutive_losses = compute_max_streak(win_flags, false);

    // ── Drawdown from equity_curve table ─────────────────────
    try {
        auto dd_stmt = conn_.prepare(
            "SELECT MIN(drawdown), MAX(drawdown_pct)"
            " FROM equity_curve WHERE symbol=?");
        dd_stmt.bind_text(1, symbol);
        if (dd_stmt.step()) {
            m.max_drawdown     = std::abs(dd_stmt.column_double(0));
            m.max_drawdown_pct = std::abs(dd_stmt.column_double(1));
        }
    } catch (...) {}

    LOG_INFO("[MetricsAggregator] " << symbol
             << ": trades=" << m.total_trades
             << " wr=" << std::fixed << std::setprecision(1)
             << m.win_rate * 100.0 << "%"
             << " pf=" << std::setprecision(2) << m.profit_factor
             << " pnl=" << m.net_pnl);

    return m;
}

// ============================================================
// compute_all_symbols
// ============================================================

std::vector<SymbolMetrics> MetricsAggregator::compute_all_symbols(
    double starting_capital,
    const std::string& period_type)
{
    std::vector<SymbolMetrics> result;

    // Get all distinct symbols from closed trades
    auto stmt = conn_.prepare(
        "SELECT DISTINCT symbol FROM trades"
        " WHERE trade_status='CLOSED' ORDER BY symbol");

    std::vector<std::string> symbols;
    while (stmt.step()) {
        symbols.push_back(stmt.column_text(0));
    }

    for (const auto& sym : symbols) {
        result.push_back(compute_symbol(sym, starting_capital, period_type));
    }
    return result;
}

// ============================================================
// compute_portfolio
// ============================================================

PortfolioMetrics MetricsAggregator::compute_portfolio(double starting_capital) {
    PortfolioMetrics p;

    // Timestamp
    auto ts_stmt = conn_.prepare("SELECT datetime('now','localtime')");
    if (ts_stmt.step()) p.computed_at = ts_stmt.column_text(0);

    // Per-symbol metrics
    p.by_symbol = compute_all_symbols(starting_capital, "ALL");

    if (p.by_symbol.empty()) {
        LOG_INFO("[MetricsAggregator] No closed trades for portfolio.");
        return p;
    }

    // Aggregate across symbols
    double best_pnl  = -1e18, worst_pnl = 1e18;
    double gross_wins = 0.0, gross_losses = 0.0;
    std::vector<double> all_pnl;

    for (const auto& s : p.by_symbol) {
        p.total_trades    += s.total_trades;
        p.winning_trades  += s.winning_trades;
        p.total_net_pnl   += s.net_pnl;
        p.total_gross_pnl += s.gross_pnl;
        p.total_brokerage += s.brokerage_total;

        if (s.net_pnl > best_pnl)  { best_pnl  = s.net_pnl; p.best_symbol  = s.symbol; }
        if (s.net_pnl < worst_pnl) { worst_pnl = s.net_pnl; p.worst_symbol = s.symbol; }

        if (s.winning_trades > 0) gross_wins   += s.gross_pnl > 0 ? s.gross_pnl : 0;
        if (s.losing_trades  > 0) gross_losses += s.gross_pnl < 0 ? std::abs(s.gross_pnl) : 0;
    }

    p.portfolio_win_rate      = p.total_trades > 0
                                ? static_cast<double>(p.winning_trades) / p.total_trades : 0.0;
    p.portfolio_profit_factor = compute_profit_factor(gross_wins, gross_losses);
    p.portfolio_expectancy    = p.total_trades > 0
                                ? p.total_net_pnl / p.total_trades : 0.0;
    p.total_return_pct        = starting_capital > 0
                                ? p.total_net_pnl / starting_capital * 100.0 : 0.0;

    // Portfolio drawdown from equity_curve_portfolio
    try {
        auto dd = conn_.prepare(
            "SELECT MIN(drawdown), MAX(drawdown_pct)"
            " FROM equity_curve_portfolio");
        if (dd.step()) {
            p.max_drawdown     = std::abs(dd.column_double(0));
            p.max_drawdown_pct = std::abs(dd.column_double(1));
        }
    } catch (...) {}

    // Portfolio Sharpe from equity_curve_portfolio net changes
    try {
        std::vector<double> daily_pnl;
        auto eq = conn_.prepare(
            "SELECT total_equity - LAG(total_equity,1,total_equity)"
            "       OVER (ORDER BY time)"
            " FROM equity_curve_portfolio ORDER BY time");
        while (eq.step()) {
            daily_pnl.push_back(eq.column_double(0));
        }
        p.portfolio_sharpe = compute_sharpe(daily_pnl);
    } catch (...) {}

    LOG_INFO("[MetricsAggregator] Portfolio:"
             << " symbols=" << p.by_symbol.size()
             << " trades=" << p.total_trades
             << " wr=" << std::fixed << std::setprecision(1)
             << p.portfolio_win_rate * 100.0 << "%"
             << " pnl=" << p.total_net_pnl
             << " best=" << p.best_symbol
             << " worst=" << p.worst_symbol);

    return p;
}

// ============================================================
// get_equity_curve
// ============================================================

std::vector<EquityCurvePoint> MetricsAggregator::get_equity_curve(
    const std::string& symbol, int limit)
{
    std::vector<EquityCurvePoint> result;

    bool is_portfolio = (symbol == "PORTFOLIO");
    std::string sql = is_portfolio
        ? "SELECT time, 'PORTFOLIO', total_equity, peak_equity,"
          " drawdown, drawdown_pct"
          " FROM equity_curve_portfolio ORDER BY time DESC LIMIT ?"
        : "SELECT time, symbol, equity, peak_equity,"
          " drawdown, drawdown_pct"
          " FROM equity_curve WHERE symbol=?"
          " ORDER BY time DESC LIMIT ?";

    auto stmt = conn_.prepare(sql);
    if (is_portfolio) {
        stmt.bind_int(1, limit);
    } else {
        stmt.bind_text(1, symbol);
        stmt.bind_int(2, limit);
    }

    while (stmt.step()) {
        EquityCurvePoint pt;
        pt.timestamp    = stmt.column_text(0);
        pt.symbol       = stmt.column_text(1);
        pt.equity       = stmt.column_double(2);
        pt.peak_equity  = stmt.column_double(3);
        pt.drawdown     = stmt.column_double(4);
        pt.drawdown_pct = stmt.column_double(5);
        result.push_back(pt);
    }

    // Return in chronological order
    std::reverse(result.begin(), result.end());
    return result;
}

// ============================================================
// persist
// ============================================================

void MetricsAggregator::persist(const SymbolMetrics& m) {
    static const char* SQL =
        "INSERT OR REPLACE INTO metrics"
        " (symbol, period_type, period_start, period_end,"
        "  total_trades, winning_trades, losing_trades,"
        "  win_rate, avg_rr, profit_factor, expectancy,"
        "  max_drawdown, max_drawdown_pct,"
        "  max_consecutive_wins, max_consecutive_losses,"
        "  gross_pnl, brokerage_total, net_pnl, total_return_pct,"
        "  sharpe_ratio, starting_capital, ending_capital)"
        " VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)";

    try {
        auto stmt = conn_.prepare(SQL);
        int i = 1;
        stmt.bind_text(i++,   m.symbol);
        stmt.bind_text(i++,   m.period_type);
        stmt.bind_text(i++,   m.period_start);
        stmt.bind_text(i++,   m.period_end);
        stmt.bind_int(i++,    m.total_trades);
        stmt.bind_int(i++,    m.winning_trades);
        stmt.bind_int(i++,    m.losing_trades);
        stmt.bind_double(i++, m.win_rate);
        stmt.bind_double(i++, m.avg_rr);
        stmt.bind_double(i++, m.profit_factor);
        stmt.bind_double(i++, m.expectancy);
        stmt.bind_double(i++, m.max_drawdown);
        stmt.bind_double(i++, m.max_drawdown_pct);
        stmt.bind_int(i++,    m.max_consecutive_wins);
        stmt.bind_int(i++,    m.max_consecutive_losses);
        stmt.bind_double(i++, m.gross_pnl);
        stmt.bind_double(i++, m.brokerage_total);
        stmt.bind_double(i++, m.net_pnl);
        stmt.bind_double(i++, m.total_return_pct);
        stmt.bind_double(i++, m.sharpe_ratio);
        stmt.bind_double(i++, m.starting_capital);
        stmt.bind_double(i++, m.ending_capital);
        stmt.execute();
        LOG_INFO("[MetricsAggregator] Persisted metrics for " << m.symbol);
    } catch (const std::exception& e) {
        LOG_ERROR("[MetricsAggregator] persist failed: " << e.what());
    }
}

void MetricsAggregator::persist_portfolio(const PortfolioMetrics& p) {
    for (const auto& s : p.by_symbol) {
        persist(s);
    }
    // Portfolio row stored as symbol='PORTFOLIO'
    SymbolMetrics portfolio_row;
    portfolio_row.symbol           = "PORTFOLIO";
    portfolio_row.period_type      = "ALL";
    portfolio_row.period_start     = p.period_start;
    portfolio_row.period_end       = p.period_end;
    portfolio_row.total_trades     = p.total_trades;
    portfolio_row.winning_trades   = p.winning_trades;
    portfolio_row.losing_trades    = p.total_trades - p.winning_trades;
    portfolio_row.win_rate         = p.portfolio_win_rate;
    portfolio_row.profit_factor    = p.portfolio_profit_factor;
    portfolio_row.expectancy       = p.portfolio_expectancy;
    portfolio_row.net_pnl          = p.total_net_pnl;
    portfolio_row.gross_pnl        = p.total_gross_pnl;
    portfolio_row.brokerage_total  = p.total_brokerage;
    portfolio_row.total_return_pct = p.total_return_pct;
    portfolio_row.max_drawdown     = p.max_drawdown;
    portfolio_row.max_drawdown_pct = p.max_drawdown_pct;
    portfolio_row.sharpe_ratio     = p.portfolio_sharpe;
    persist(portfolio_row);
}

// ============================================================
// Private helpers
// ============================================================

double MetricsAggregator::compute_sharpe(
    const std::vector<double>& pnl_series,
    double risk_free_rate) const
{
    if (pnl_series.size() < 2) return 0.0;

    double mean = std::accumulate(pnl_series.begin(),
                                   pnl_series.end(), 0.0)
                  / pnl_series.size();

    double variance = 0.0;
    for (double v : pnl_series) {
        variance += (v - mean) * (v - mean);
    }
    variance /= (pnl_series.size() - 1);
    double stddev = std::sqrt(variance);

    if (stddev < 1e-9) return 0.0;
    return (mean - risk_free_rate) / stddev;
}

double MetricsAggregator::compute_profit_factor(
    double gross_wins, double gross_losses) const
{
    if (gross_losses < 1e-9) return gross_wins > 0 ? 99.0 : 0.0;
    return gross_wins / gross_losses;
}

int MetricsAggregator::compute_max_streak(
    const std::vector<bool>& flags, bool target) const
{
    int max_streak = 0, current = 0;
    for (bool f : flags) {
        if (f == target) { ++current; max_streak = std::max(max_streak, current); }
        else              { current = 0; }
    }
    return max_streak;
}

} // namespace Analytics
} // namespace SDTrading
