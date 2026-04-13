// ============================================================
// SD TRADING V8 - TRADE CLOSE LISTENER
// src/springboot/trade_close_listener.h
// Milestone M5
//
// Spring Boot executes trades via Fyers and writes exit data
// directly into the shared SQLite trades table when a position
// closes (TP hit, SL hit, manual square-off, EOD).
//
// This component polls the trades table every N seconds for
// rows that have flipped from OPEN → CLOSED, extracts the
// exit fields, and publishes TradeCloseEvent on the EventBus.
//
// That event is then consumed by:
//   - DbWriter     (async) → updates equity_curve, metrics
//   - SignalConsumer       → decrements open_positions_ count
//   - ScannerOrchestrator → on_trade_closed(), re-enables zone
//
// Design decision: polling rather than triggers because:
//   1. SQLite has no native push notifications to C++
//   2. Spring Boot is a separate process — no shared memory
//   3. 5-second poll lag is acceptable (trade is already closed)
// ============================================================

#ifndef SDTRADING_TRADE_CLOSE_LISTENER_H
#define SDTRADING_TRADE_CLOSE_LISTENER_H

#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include "../events/event_bus.h"
#include "../events/event_types.h"
#include "../persistence/sqlite_connection.h"
#include "../utils/logger.h"

namespace SDTrading {
namespace SpringBoot {

// ============================================================
// TRADE CLOSE LISTENER CONFIG
// ============================================================
struct TradeCloseListenerConfig {
    int    poll_interval_seconds = 5;    // How often to poll SQLite
    bool   enabled               = true;
};

// ============================================================
// TRADE CLOSE LISTENER
// Runs a single polling thread.
// Tracks the last seen updated_at timestamp to avoid
// re-publishing already-processed closes.
// ============================================================
class TradeCloseListener {
public:
    TradeCloseListener(const TradeCloseListenerConfig& cfg,
                       Persistence::SqliteConnection&  conn,
                       Events::EventBus&               bus);
    ~TradeCloseListener();

    // Non-copyable
    TradeCloseListener(const TradeCloseListener&) = delete;
    TradeCloseListener& operator=(const TradeCloseListener&) = delete;

    void start();
    void stop();

    bool is_running() const { return running_.load(); }
    int  closes_detected() const { return closes_detected_.load(); }

private:
    TradeCloseListenerConfig       cfg_;
    Persistence::SqliteConnection& conn_;
    Events::EventBus&              bus_;

    std::thread       poll_thread_;
    std::atomic<bool> running_ { false };
    std::atomic<int>  closes_detected_ { 0 };

    // Tracks the latest updated_at we have already processed
    std::string last_processed_ts_;

    void poll_loop();
    void check_for_closes();
};

} // namespace SpringBoot
} // namespace SDTrading

#endif // SDTRADING_TRADE_CLOSE_LISTENER_H
