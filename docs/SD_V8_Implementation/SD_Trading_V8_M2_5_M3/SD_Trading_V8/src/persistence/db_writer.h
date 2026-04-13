// ============================================================
// SD TRADING V8 - DB WRITER
// src/persistence/db_writer.h
// Milestone M3.5
//
// Single component that owns all SQLite repositories and
// subscribes to the EventBus as ASYNC subscribers.
//
// All DB writes happen on the EventBus async worker thread.
// The engine's critical path (SYNC) is never blocked by
// database latency, network I/O, or disk flushes.
//
// Wiring (called at startup before bus.start()):
//   DbWriter writer(conn);
//   writer.subscribe_all(bus);
//   bus.start();
//
// The writer then handles all events automatically.
// ============================================================

#ifndef SDTRADING_DB_WRITER_H
#define SDTRADING_DB_WRITER_H

#include "sqlite_connection.h"
#include "sqlite_bar_repository.h"
#include "sqlite_trade_repository.h"
#include "../events/event_bus.h"
#include "../events/event_types.h"
#include "../utils/logger.h"
#include <memory>
#include <unordered_map>
#include <string>

namespace SDTrading {
namespace Persistence {

class DbWriter {
public:
    // conn must outlive DbWriter
    explicit DbWriter(SqliteConnection& conn);

    // Register all ASYNC subscribers on the EventBus.
    // Call BEFORE bus.start().
    void subscribe_all(Events::EventBus& bus);

    // ── Per-symbol equity tracking ──────────────────────────
    // Called internally when a trade closes to update running equity.
    void update_equity(const std::string& symbol, double net_pnl,
                       const std::string& timestamp, int64_t trade_id);

    // ── On-demand CSV export (backward compatibility) ───────
    std::string export_trades_csv(const std::string& symbol);
    std::string export_trades_csv_all();

    // ── Statistics ──────────────────────────────────────────
    int64_t bars_written()    const { return bars_written_; }
    int64_t bars_rejected()   const { return rejections_written_; }
    int64_t signals_written() const { return signals_written_; }
    int64_t trades_opened()   const { return trades_opened_; }
    int64_t trades_closed()   const { return trades_closed_; }

private:
    SqliteConnection&           conn_;
    SqliteBarRepository         bar_repo_;
    SqliteTradeRepository       trade_repo_;

    // Per-symbol running equity state
    struct EquityState {
        double running_equity = 0.0;
        double peak_equity    = 0.0;
    };
    std::unordered_map<std::string, EquityState> equity_state_;

    // Counters
    int64_t bars_written_       = 0;
    int64_t rejections_written_ = 0;
    int64_t signals_written_    = 0;
    int64_t trades_opened_      = 0;
    int64_t trades_closed_      = 0;

    // Handlers
    void on_bar_validated(const Events::BarValidatedEvent& evt);
    void on_bar_rejected(const Events::BarRejectedEvent& evt);
    void on_signal(const Events::SignalEvent& evt);
    void on_trade_open(const Events::TradeOpenEvent& evt);
    void on_trade_close(const Events::TradeCloseEvent& evt);
    void on_alert(const Events::SystemAlertEvent& evt);
};

} // namespace Persistence
} // namespace SDTrading

#endif // SDTRADING_DB_WRITER_H
