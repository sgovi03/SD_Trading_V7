// ============================================================
// SD TRADING V8 - DB WRITER IMPLEMENTATION
// src/persistence/db_writer.cpp
// Milestone M3.5
// ============================================================

#include "db_writer.h"

// Windows SDK defines ERROR as 0. Undefine after all headers.
#ifdef ERROR
#undef ERROR
#endif

namespace SDTrading {
namespace Persistence {

DbWriter::DbWriter(SqliteConnection& conn)
    : conn_(conn)
    , bar_repo_(conn)
    , trade_repo_(conn)
    , zone_repo_(conn) {
    LOG_INFO("[DbWriter] Initialized.");
}

// ============================================================
// subscribe_all — register all ASYNC EventBus subscriptions
// ============================================================

void DbWriter::subscribe_all(Events::EventBus& bus) {
    // All subscriptions are ASYNC — DB writes never block the engine

    bus.subscribe_async<Events::BarValidatedEvent>("db_bar",
        [this](const Events::BarValidatedEvent& e){ on_bar_validated(e); });

    bus.subscribe_async<Events::BarRejectedEvent>("db_reject",
        [this](const Events::BarRejectedEvent& e){ on_bar_rejected(e); });

    bus.subscribe_async<Events::SignalEvent>("db_signal",
        [this](const Events::SignalEvent& e){ on_signal(e); });

    bus.subscribe_async<Events::TradeOpenEvent>("db_trade_open",
        [this](const Events::TradeOpenEvent& e){ on_trade_open(e); });

    bus.subscribe_async<Events::TradeCloseEvent>("db_trade_close",
        [this](const Events::TradeCloseEvent& e){ on_trade_close(e); });

    bus.subscribe_async<Events::SystemAlertEvent>("db_alert",
        [this](const Events::SystemAlertEvent& e){ on_alert(e); });

    bus.subscribe_async<Events::ZoneSnapshotEvent>("db_zone_snapshot",
        [this](const Events::ZoneSnapshotEvent& e){ on_zone_snapshot(e); });

    bus.subscribe_async<Events::ZoneUpdateEvent>("db_zone_update",
        [this](const Events::ZoneUpdateEvent& e){ on_zone_update(e); });

    LOG_INFO("[DbWriter] Subscribed to 8 event types (all ASYNC).");
}

// ============================================================
// Handlers
// ============================================================

void DbWriter::on_bar_validated(const Events::BarValidatedEvent& evt) {
    if (bar_repo_.insert_bar(evt)) {
        ++bars_written_;
    }
}

void DbWriter::on_bar_rejected(const Events::BarRejectedEvent& evt) {
    if (bar_repo_.insert_rejection(evt)) {
        ++rejections_written_;
        if (rejections_written_ % 10 == 0) {
            LOG_WARN("[DbWriter] " << rejections_written_
                     << " total bar rejections logged to DB.");
        }
    }
}

void DbWriter::on_signal(const Events::SignalEvent& evt) {
    int64_t id = trade_repo_.insert_signal(evt);
    if (id > 0) ++signals_written_;
}

void DbWriter::on_trade_open(const Events::TradeOpenEvent& evt) {
    if (trade_repo_.insert_trade_open(evt)) {
        ++trades_opened_;
        LOG_INFO("[DbWriter] Trade open persisted: " << evt.order_tag);
    }
}

void DbWriter::on_trade_close(const Events::TradeCloseEvent& evt) {
    conn_.with_transaction([this, &evt]() {
        trade_repo_.update_trade_close(evt);
        ++trades_closed_;

        // Update running equity for this symbol
        // Get the trade_id for the equity curve FK
        int64_t trade_id = trade_repo_.get_signal_id_by_tag(evt.order_tag);
        update_equity(evt.symbol, evt.net_pnl, evt.exit_time, trade_id);
    });
}

void DbWriter::on_alert(const Events::SystemAlertEvent& evt) {
    // Alerts are always logged regardless of severity
    switch (evt.severity) {
        case Events::AlertSeverity::INFO:
            LOG_INFO("[ALERT:" << evt.source << "] " << evt.message);
            break;
        case Events::AlertSeverity::WARN:
            LOG_WARN("[ALERT:" << evt.source << "] " << evt.message);
            break;
        case Events::AlertSeverity::ERROR:
            LOG_ERROR("[ALERT:" << evt.source << "] " << evt.message);
            break;
        case Events::AlertSeverity::CRITICAL:
            LOG_ERROR("[CRITICAL ALERT:" << evt.source << "] " << evt.message);
            break;
    }
    // Future: persist to alerts table, send notification
}

// ============================================================
// Zone handlers
// ============================================================

void DbWriter::on_zone_snapshot(const Events::ZoneSnapshotEvent& evt) {
    if (zone_repo_.upsert_zone(evt)) {
        ++zones_written_;
        LOG_INFO("[DbWriter] ✅ Zone saved to DB: " << evt.symbol
                  << " Z-" << evt.zone_id
                  << " " << evt.zone_type
                  << " state=" << evt.state
                  << " (total=" << zones_written_ << ")");
    } else {
        LOG_WARN("[DbWriter] ⚠️ Zone upsert FAILED: " << evt.symbol
                  << " Z-" << evt.zone_id);
    }
}

void DbWriter::on_zone_update(const Events::ZoneUpdateEvent& evt) {
    if (zone_repo_.update_zone_state(evt)) {
        LOG_INFO("[DbWriter] Zone state updated in DB: " << evt.symbol
                  << " Z-" << evt.zone_id
                  << " → " << evt.new_state
                  << " touch=" << evt.touch_count);
    }
}

// ============================================================
// update_equity
// ============================================================

void DbWriter::update_equity(const std::string& symbol,
                               double net_pnl,
                               const std::string& timestamp,
                               int64_t trade_id)
{
    auto& state = equity_state_[symbol];

    // Initialize running equity from starting_capital on first trade
    // (We use a simple approach: track running total from 0, add PnL)
    state.running_equity += net_pnl;
    if (state.running_equity > state.peak_equity) {
        state.peak_equity = state.running_equity;
    }

    trade_repo_.append_equity_point(symbol, timestamp,
                                    state.running_equity,
                                    state.peak_equity,
                                    trade_id);

    LOG_INFO("[DbWriter] Equity updated: " << symbol
             << " running=" << state.running_equity
             << " peak=" << state.peak_equity
             << " pnl=" << net_pnl);
}

// ============================================================
// CSV export (backward compatibility)
// ============================================================

std::string DbWriter::export_trades_csv(const std::string& symbol) {
    return trade_repo_.export_trades_csv(symbol);
}

std::string DbWriter::export_trades_csv_all() {
    return trade_repo_.export_trades_csv_all();
}

} // namespace Persistence
} // namespace SDTrading