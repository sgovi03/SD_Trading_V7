// ============================================================
// SD TRADING V8 - SPRING BOOT BRIDGE IMPLEMENTATION
// src/springboot/springboot_bridge.cpp
// Milestone M5
// ============================================================

#include "springboot_bridge.h"

namespace SDTrading {
namespace SpringBoot {

SpringBootBridge::SpringBootBridge(
    const SpringBootBridgeConfig&   cfg,
    const Live::OrderSubmitConfig&  order_cfg,
    Persistence::SqliteConnection&  conn,
    Events::EventBus&               bus)
    : bus_(bus)
{
    consumer_       = std::make_unique<SignalConsumer>(cfg.signal, order_cfg, bus);

    // TradeCloseListener runs on its own polling thread. It must not share the
    // write connection used by DbWriter/EventBus worker when SQLite is opened
    // with NOMUTEX. Use a dedicated read-only connection for this thread.
    try {
        close_conn_ = Persistence::SqliteConnectionFactory::create_read_connection();
    } catch (const std::exception& e) {
        LOG_WARN("[SpringBootBridge] Failed to create dedicated read connection"
                 << " for TradeCloseListener: " << e.what()
                 << " -- falling back to shared connection");
    }

    close_listener_ = std::make_unique<TradeCloseListener>(
        cfg.close_listener,
        close_conn_ ? *close_conn_ : conn,
        bus);
    LOG_INFO("[SpringBootBridge] Created.");
}

void SpringBootBridge::start() {
    // Register ASYNC signal handler before bus starts
    consumer_->subscribe(bus_);

    // Start polling thread for trade closes
    close_listener_->start();

    LOG_INFO("[SpringBootBridge] Started."
             << " Signal consumer: ready"
             << " Close listener: polling every "
             << "5s");
}

void SpringBootBridge::stop() {
    close_listener_->stop();
    LOG_INFO("[SpringBootBridge] Stopped.");
}

int SpringBootBridge::signals_received()  const { return consumer_->signals_received(); }
int SpringBootBridge::orders_submitted()  const { return consumer_->orders_submitted(); }
int SpringBootBridge::orders_failed()     const { return consumer_->orders_failed(); }
int SpringBootBridge::closes_detected()   const { return close_listener_->closes_detected(); }

void SpringBootBridge::on_trade_opened(const std::string& symbol) {
    consumer_->on_trade_opened(symbol);
}
void SpringBootBridge::on_trade_closed(const std::string& symbol) {
    consumer_->on_trade_closed(symbol);
}

} // namespace SpringBoot
} // namespace SDTrading
