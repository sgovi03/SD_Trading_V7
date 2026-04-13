// ============================================================
// SD TRADING V8 - TRADE CLOSE LISTENER IMPLEMENTATION
// src/springboot/trade_close_listener.cpp
// Milestone M5
// ============================================================

#include "trade_close_listener.h"
#include <chrono>

// Undefine Windows macros that conflict with enums/log macros
#ifdef ERROR
#undef ERROR
#endif
#ifdef WARN
#undef WARN
#endif

namespace SDTrading {
namespace SpringBoot {

TradeCloseListener::TradeCloseListener(
    const TradeCloseListenerConfig& cfg,
    Persistence::SqliteConnection&  conn,
    Events::EventBus&               bus)
    : cfg_(cfg), conn_(conn), bus_(bus)
{
    LOG_INFO("[TradeCloseListener] Created."
             << " poll_interval=" << cfg_.poll_interval_seconds << "s");
}

TradeCloseListener::~TradeCloseListener() { stop(); }

void TradeCloseListener::start() {
    if (running_.exchange(true)) return;
    poll_thread_ = std::thread([this]() { poll_loop(); });
    LOG_INFO("[TradeCloseListener] Poll thread started.");
}

void TradeCloseListener::stop() {
    if (!running_.exchange(false)) return;
    if (poll_thread_.joinable()) poll_thread_.join();
    LOG_INFO("[TradeCloseListener] Stopped. "
             << "Closes detected: " << closes_detected_.load());
}

// ============================================================
// poll_loop
// ============================================================

void TradeCloseListener::poll_loop() {
    LOG_INFO("[TradeCloseListener] Polling SQLite every "
             << cfg_.poll_interval_seconds << "s for trade closes...");

    while (running_.load()) {
        try {
            check_for_closes();
        } catch (const std::exception& e) {
            LOG_ERROR("[TradeCloseListener] Poll error: " << e.what());
        }

        // Sleep in 500ms chunks so stop() responds quickly
        for (int i = 0; i < cfg_.poll_interval_seconds * 2 && running_.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }
}

// ============================================================
// check_for_closes
// Queries for CLOSED trades updated after last_processed_ts_.
// ============================================================

void TradeCloseListener::check_for_closes() {
    // Query: find trades closed by Spring Boot since last poll.
    // Spring Boot writes exit fields and sets trade_status='CLOSED'.
    // We track by updated_at to avoid reprocessing.
    static const char* SQL =
        "SELECT order_tag, symbol, exit_time, exit_price, exit_reason,"
        "       gross_pnl, brokerage, net_pnl, bars_in_trade,"
        "       exit_volume, exit_was_vol_climax, updated_at"
        " FROM trades"
        " WHERE trade_status = 'CLOSED'"
        "   AND exit_time IS NOT NULL"
        "   AND updated_at > ?"
        " ORDER BY updated_at ASC";

    // On first run use a far-past timestamp so we pick up anything
    // that closed before we started (e.g. after a restart).
    if (last_processed_ts_.empty()) {
        last_processed_ts_ = "1970-01-01 00:00:00";
    }

    auto stmt = conn_.prepare(SQL);
    stmt.bind_text(1, last_processed_ts_);

    std::string newest_ts = last_processed_ts_;

    while (stmt.step()) {
        Events::TradeCloseEvent evt;
        evt.order_tag          = stmt.column_text(0);
        evt.symbol             = stmt.column_text(1);
        evt.exit_time          = stmt.column_text(2);
        evt.exit_price         = stmt.column_double(3);
        evt.exit_reason        = stmt.column_text(4);
        evt.gross_pnl          = stmt.column_double(5);
        evt.brokerage          = stmt.column_double(6);
        evt.net_pnl            = stmt.column_double(7);
        evt.bars_in_trade      = stmt.column_int(8);
        evt.exit_volume        = stmt.column_double(9);
        evt.exit_was_vol_climax= stmt.column_bool(10);

        std::string row_ts = stmt.column_text(11);

        // Guard: skip ghost rows with empty order_tag.
        // These are left-over rows from sessions where publish_trade_events()
        // fired before order_tag was assigned. They loop forever because
        // update_trade_close(order_tag='') advances their updated_at to NOW()
        // on every poll, making them appear "newly closed" on every cycle.
        if (evt.order_tag.empty()) {
            LOG_WARN("[TradeCloseListener] Skipping ghost trade row "
                     "(empty order_tag, symbol=" << evt.symbol
                     << " reason=" << evt.exit_reason << ") — "
                     "clean up with: DELETE FROM trades WHERE order_tag=''");
            if (row_ts > newest_ts) newest_ts = row_ts;
            continue;
        }

        LOG_INFO("[TradeCloseListener] Trade closed detected:"
                 << " tag="    << evt.order_tag
                 << " symbol=" << evt.symbol
                 << " reason=" << evt.exit_reason
                 << " pnl="    << evt.net_pnl);

        bus_.publish(evt);
        ++closes_detected_;

        // Advance cursor
        if (row_ts > newest_ts) newest_ts = row_ts;
    }

    last_processed_ts_ = newest_ts;
}

} // namespace SpringBoot
} // namespace SDTrading