// ============================================================
// SD TRADING V8 - SPRING BOOT BRIDGE
// src/springboot/springboot_bridge.h
// Milestone M5
//
// Top-level component that owns and wires together:
//   SignalConsumer      — EventBus → Spring Boot orders
//   TradeCloseListener — SQLite poll → EventBus closes
//
// Usage (in run_live.cpp or main entry point):
//
//   SpringBootBridge bridge(bridge_cfg, order_cfg, conn, bus);
//   bridge.start();
//   bus.start();
//   // ... run scanner ...
//   bridge.stop();
// ============================================================

#ifndef SDTRADING_SPRINGBOOT_BRIDGE_H
#define SDTRADING_SPRINGBOOT_BRIDGE_H

#include <memory>
#include "signal_consumer.h"
#include "trade_close_listener.h"
#include "../events/event_bus.h"
#include "../persistence/sqlite_connection.h"
#include "../utils/logger.h"

namespace SDTrading {
namespace SpringBoot {

// ============================================================
// BRIDGE CONFIG — aggregates all M5 config in one place
// ============================================================
struct SpringBootBridgeConfig {
    SignalConsumerConfig      signal;
    TradeCloseListenerConfig  close_listener;
};

// ============================================================
// SPRINGBOOT BRIDGE
// ============================================================
class SpringBootBridge {
public:
    SpringBootBridge(const SpringBootBridgeConfig&   cfg,
                     const Live::OrderSubmitConfig&  order_cfg,
                     Persistence::SqliteConnection&  conn,
                     Events::EventBus&               bus);

    // Register EventBus subscriptions and start poll thread.
    // Call BEFORE bus.start().
    void start();

    // Stop poll thread gracefully.
    void stop();

    // ── Status ───────────────────────────────────────────────
    int signals_received()  const;
    int orders_submitted()  const;
    int orders_failed()     const;
    int closes_detected()   const;

    // ── Runtime control ──────────────────────────────────────
    void enable_trading()  { consumer_->enable(); }
    void disable_trading() { consumer_->disable(); }

    // Called externally when a trade opens/closes to sync counts
    void on_trade_opened(const std::string& symbol);
    void on_trade_closed(const std::string& symbol);

private:
    std::unique_ptr<SignalConsumer>      consumer_;
    std::unique_ptr<TradeCloseListener>  close_listener_;
    std::unique_ptr<Persistence::SqliteConnection> close_conn_;
    Events::EventBus&                    bus_;
};

} // namespace SpringBoot
} // namespace SDTrading

#endif // SDTRADING_SPRINGBOOT_BRIDGE_H
