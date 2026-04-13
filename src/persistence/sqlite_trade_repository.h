// ============================================================
// SD TRADING V8 - SQLITE TRADE REPOSITORY
// src/persistence/sqlite_trade_repository.h
// Milestone M3.3
//
// Replaces: CSVReporter (trades.csv, metrics.csv, equity_curve.csv)
//
// Writes to:
//   signals table   — on SignalEvent (before order placement)
//   trades table    — on TradeOpenEvent and TradeCloseEvent
//   equity_curve    — after each trade close
//   metrics         — computed on demand / session close
//
// All writes are async (called from DbWriter on EventBus worker).
// Signal → Trade linkage via order_tag (unique key across both tables).
// ============================================================

#ifndef SDTRADING_SQLITE_TRADE_REPOSITORY_H
#define SDTRADING_SQLITE_TRADE_REPOSITORY_H

#include "sqlite_connection.h"
#include "../events/event_types.h"
#include "common_types.h"
#include "../utils/logger.h"
#include <string>
#include <vector>
#include <optional>

namespace SDTrading {
namespace Persistence {

class SqliteTradeRepository {
public:
    explicit SqliteTradeRepository(SqliteConnection& conn);

    // ── Signals ────────────────────────────────────────────

    // Insert signal (called when engine emits a signal, before order).
    // Returns the new signal row id, or -1 on failure.
    int64_t insert_signal(const Events::SignalEvent& evt);

    // Mark signal as acted upon (order was placed successfully).
    bool mark_signal_acted(const std::string& order_tag);

    // ── Trades ─────────────────────────────────────────────

    // Insert open trade (called on TradeOpenEvent from Spring Boot).
    bool insert_trade_open(const Events::TradeOpenEvent& evt);

    // Update trade with close details (called on TradeCloseEvent).
    bool update_trade_close(const Events::TradeCloseEvent& evt);

    // ── Equity Curve ───────────────────────────────────────

    // Append equity curve point after a trade closes.
    // running_equity = prior equity + net_pnl
    bool append_equity_point(const std::string& symbol,
                              const std::string& timestamp,
                              double running_equity,
                              double peak_equity,
                              int64_t trade_id);

    // ── Read path (for analytics, dashboard) ───────────────

    // Fetch all closed trades for a symbol (for metrics computation).
    std::vector<Core::Trade> get_closed_trades(const std::string& symbol);

    // Fetch all closed trades across all symbols (portfolio view).
    std::vector<Core::Trade> get_all_closed_trades();

    // Count open positions for a symbol.
    int count_open_trades(const std::string& symbol);

    // Get the signal_id for a given order_tag (needed for trade insert).
    int64_t get_signal_id_by_tag(const std::string& order_tag);

    // ── CSV export (on-demand, backward compatibility) ──────

    // Export closed trades for a symbol to CSV string.
    // Matches the exact column order of the original trades.csv
    // so existing analysis scripts keep working.
    std::string export_trades_csv(const std::string& symbol);

    // Export all symbols (portfolio master).
    std::string export_trades_csv_all();

private:
    SqliteConnection& conn_;

    // Map Core::Trade fields from a SQL row
    Core::Trade row_to_trade(SqliteStatement& stmt) const;
};

} // namespace Persistence
} // namespace SDTrading

#endif // SDTRADING_SQLITE_TRADE_REPOSITORY_H
