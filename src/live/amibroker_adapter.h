// ============================================================
// SD TRADING V8 - AMIBROKER DATA ADAPTER
// src/live/amibroker_adapter.h
// Milestone M2.5
//
// DESIGN: Replaces FyersAdapter's data path entirely.
//
// Role split after M2.5:
//   AmiBrokerAdapter  → data (bars come from EventBus/pipe)
//   FyersAdapter      → execution ONLY (order placement)
//
// The LiveEngine calls broker->get_latest_bar() once per tick
// (in update_bar_history()). In V7, FyersAdapter served both
// data and execution from the same CSV — the root cause of the
// bad-candle P&L loss.
//
// In V8, AmiBrokerAdapter acts as the broker interface for
// DATA ONLY. It receives validated bars from the EventBus
// and serves them to LiveEngine via the standard BrokerInterface.
//
// Order placement methods forward to the OrderSubmitter
// (Spring Boot HTTP), exactly as before.
//
// Architecture:
//
//   AmiBroker AFL ──pipe──► PipeListener ──► BarValidator
//                                               │
//                                        EventBus.publish(BarValidatedEvent)
//                                               │
//                                        AmiBrokerAdapter.on_bar_validated()
//                                               │
//                                        internal bar queue (thread-safe)
//                                               │
//                                LiveEngine.update_bar_history()
//                                    calls broker->get_latest_bar()
//                                               │
//                                        AmiBrokerAdapter.get_latest_bar()
//                                        returns queued bar (or last known)
//
// Thread safety:
//   on_bar_validated() called on EventBus sync thread (pipe reader).
//   get_latest_bar() called on engine's main thread.
//   Internal mutex protects the bar queue.
// ============================================================

#ifndef SDTRADING_AMIBROKER_ADAPTER_H
#define SDTRADING_AMIBROKER_ADAPTER_H

#include "broker_interface.h"
#include "../utils/logger.h"
#include <mutex>
#include <deque>
#include <atomic>
#include <string>

namespace SDTrading {
namespace Live {

class AmiBrokerAdapter : public BrokerInterface {
public:
    // --------------------------------------------------------
    // Constructor
    // symbol:     trading symbol (e.g. "NIFTY")
    // interval:   bar interval string (e.g. "5")
    // --------------------------------------------------------
    explicit AmiBrokerAdapter(const std::string& symbol,
                               const std::string& interval);
    ~AmiBrokerAdapter() override = default;

    // --------------------------------------------------------
    // Called by EventBus subscriber when a validated bar arrives.
    // Thread-safe: can be called from pipe listener thread.
    // --------------------------------------------------------
    void on_bar_validated(const Core::Bar& bar);

    // --------------------------------------------------------
    // BrokerInterface — DATA METHODS
    // Served from the internal bar queue.
    // --------------------------------------------------------
    bool connect() override;
    void disconnect() override;
    bool is_connected() const override;

    // Returns the most recently received validated bar.
    // If no new bar has arrived since last call, returns the
    // same bar (engine detects duplicate datetime → no re-process).
    Core::Bar get_latest_bar(const std::string& symbol,
                             const std::string& interval) override;

    // Returns the last known close price (from most recent bar).
    double get_ltp(const std::string& symbol) override;

    // Returns the bar history buffer (all bars received so far).
    std::vector<Core::Bar> get_historical_bars(const std::string& symbol,
                                               const std::string& interval,
                                               int count) override;

    // --------------------------------------------------------
    // BrokerInterface — EXECUTION METHODS
    // All order placement is delegated to OrderSubmitter
    // (Spring Boot HTTP endpoint). These methods are NOT
    // implemented here — use FyersAdapter or OrderSubmitter
    // directly for execution. Stub implementations raise a
    // clear error if accidentally called.
    // --------------------------------------------------------
    OrderResponse place_market_order(const std::string& symbol,
                                     const std::string& direction,
                                     int quantity) override;
    OrderResponse place_limit_order(const std::string& symbol,
                                    const std::string& direction,
                                    int quantity,
                                    double price) override;
    OrderResponse place_stop_order(const std::string& symbol,
                                   const std::string& direction,
                                   int quantity,
                                   double stop_price) override;
    bool cancel_order(const std::string& order_id) override;
    std::vector<Position> get_positions() override;
    double get_account_balance() override;
    bool has_position(const std::string& symbol) override;

    // ── Status accessors ────────────────────────────────────
    int  get_bars_received()     const { return bars_received_.load(); }
    bool has_received_any_bar()  const { return bars_received_.load() > 0; }

private:
    std::string          symbol_;
    std::string          interval_;
    std::atomic<bool>    connected_  { false };
    std::atomic<int>     bars_received_ { 0 };

    // Thread-safe bar buffer
    // Most recent bar is always at back().
    mutable std::mutex   bar_mutex_;
    std::deque<Core::Bar> bar_history_;   // all bars received this session
    Core::Bar            latest_bar_;     // current bar (may be same as last call)
    bool                 has_bar_ = false;

    // Internal: adds bar to history buffer
    void add_bar_internal(const Core::Bar& bar);
};

} // namespace Live
} // namespace SDTrading

#endif // SDTRADING_AMIBROKER_ADAPTER_H
