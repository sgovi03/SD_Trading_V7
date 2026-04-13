// ============================================================
// SD TRADING V8 - SIGNAL CONSUMER (Spring Boot Bridge)
// src/springboot/signal_consumer.h
// Milestone M5
//
// Subscribes to SignalEvent on the EventBus (ASYNC).
// For each signal: submits an order to Spring Boot via
// the existing OrderSubmitter HTTP client.
// Publishes TradeOpenEvent when Spring Boot confirms fill.
//
// Architecture:
//   EventBus::SignalEvent (ASYNC)
//        │
//        ▼
//   SignalConsumer::on_signal()
//        │
//        ├─► validate (portfolio guard, duplicate tag check)
//        │
//        ├─► OrderSubmitter::submit_order()  ← existing V7 component
//        │         POST /trade/multipleOrderSubmit
//        │         {quantity, strategyNumber, orderTag}
//        │
//        └─► on success: EventBus::publish(TradeOpenEvent)
//            on failure: EventBus::publish(SystemAlertEvent WARN)
//
// The Spring Boot service calls back into the C++ system via
// the shared SQLite DB (writing trade close events) — see M5.2.
// ============================================================

#ifndef SDTRADING_SIGNAL_CONSUMER_H
#define SDTRADING_SIGNAL_CONSUMER_H

#include <string>
#include <unordered_set>
#include <mutex>
#include <atomic>
#include "../events/event_bus.h"
#include "../events/event_types.h"
#include "../utils/logger.h"

// Reuse existing V7 order submitter
#include "../utils/http/order_submitter.h"

namespace SDTrading {
namespace SpringBoot {

// ============================================================
// SIGNAL CONSUMER CONFIG
// ============================================================
struct SignalConsumerConfig {
    bool    dry_run             = false;  // true = log but don't submit
    int     max_open_positions  = 3;      // Portfolio guard
    bool    enabled             = true;
};

// ============================================================
// SIGNAL CONSUMER
// ============================================================
class SignalConsumer {
public:
    SignalConsumer(const SignalConsumerConfig&     cfg,
                   const Live::OrderSubmitConfig& order_cfg,
                   Events::EventBus&              bus);

    // Register ASYNC subscriber on EventBus.
    // Call before bus.start().
    void subscribe(Events::EventBus& bus);

    // ── Runtime control ──────────────────────────────────────
    void enable()  { cfg_.enabled = true; }
    void disable() { cfg_.enabled = false; }

    // Called by ScannerOrchestrator on trade open/close
    // to keep the consumer's position count in sync.
    void on_trade_opened(const std::string& symbol);
    void on_trade_closed(const std::string& symbol);

    // ── Statistics ───────────────────────────────────────────
    int signals_received()  const { return signals_received_.load(); }
    int orders_submitted()  const { return orders_submitted_.load(); }
    int orders_failed()     const { return orders_failed_.load(); }
    int orders_skipped()    const { return orders_skipped_.load(); }

private:
    SignalConsumerConfig       cfg_;
    Live::OrderSubmitter       order_submitter_;
    Events::EventBus&          bus_;

    std::atomic<int> open_positions_   { 0 };
    std::atomic<int> signals_received_ { 0 };
    std::atomic<int> orders_submitted_ { 0 };
    std::atomic<int> orders_failed_    { 0 };
    std::atomic<int> orders_skipped_   { 0 };

    // Duplicate tag guard (within a session)
    std::unordered_set<std::string> submitted_tags_;
    mutable std::mutex              tags_mutex_;

    // ── Signal handler ───────────────────────────────────────
    void on_signal(const Events::SignalEvent& evt);

    // ── Publish events ───────────────────────────────────────
    void publish_trade_open(const Events::SignalEvent& signal,
                            const Live::OrderSubmitResult& result);
    void publish_alert(Events::AlertSeverity sev,
                       const std::string& msg,
                       const std::string& symbol);
};

} // namespace SpringBoot
} // namespace SDTrading

#endif // SDTRADING_SIGNAL_CONSUMER_H
