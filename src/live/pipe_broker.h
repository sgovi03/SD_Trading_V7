// ============================================================
// SD TRADING V8 - PIPE BROKER
// src/live/pipe_broker.h
//
// PURPOSE:
//   Adapter between V8's push-pipe architecture and V7's
//   pull-based LiveEngine / BrokerInterface.
//
// DESIGN:
//   V8 pushes bars via named pipe → PipeListener → EventBus
//   LiveEngine pulls bars via get_latest_bar() (blocking call)
//
//   PipeBroker bridges this gap:
//     push_bar()       ← called by BarRouter (pipe/EventBus thread)
//     get_latest_bar() ← called by LiveEngine thread (blocks until bar)
//
//   Order submission delegates to OrderSubmitter → Spring Boot.
//   Portfolio pre-filter delegates to PortfolioGuard.
//
// THREADING:
//   push_bar()       : pipe thread    (producer)
//   get_latest_bar() : engine thread  (consumer, blocks on CV)
//   Synchronised via std::mutex + std::condition_variable
//
// PATTERN: Adapter + Monitor Object
// ============================================================

#ifndef SDTRADING_PIPE_BROKER_H
#define SDTRADING_PIPE_BROKER_H

#include "broker_interface.h"
#include "utils/http/order_submitter.h"
#include "utils/logger.h"
#include "common_types.h"
#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <functional>

namespace SDTrading {
namespace Live {

// ============================================================
// PipeBroker
// ============================================================
class PipeBroker : public BrokerInterface {
public:
    // ----------------------------------------------------------
    // Construction
    // ----------------------------------------------------------
    // symbol       : canonical symbol name (e.g. "NIFTY")
    // order_cfg    : Spring Boot connection settings
    // get_balance  : callable returning current capital from
    //                PortfolioGuard (avoids circular dependency)
    // entry_allowed: callable returning PortfolioGuard pre-filter
    //                result for this symbol
    // on_order_ok  : callback after successful order — notifies
    //                PortfolioGuard of position opened
    // ----------------------------------------------------------
    PipeBroker(
        const std::string&              symbol,
        const OrderSubmitConfig&        order_cfg,
        std::function<double()>         get_balance,
        std::function<bool()>           entry_allowed,
        std::function<void(int, double)> on_order_ok   // lots, entry_price
    );

    ~PipeBroker() override = default;

    // ----------------------------------------------------------
    // Push interface  (called from BarRouter / pipe thread)
    // ----------------------------------------------------------

    // Enqueue a new bar. Wakes LiveEngine if it is blocking.
    void push_bar(const Core::Bar& bar);

    // Signal engine thread to stop (unblocks get_latest_bar).
    void shutdown();

    // ----------------------------------------------------------
    // BrokerInterface — data retrieval
    // ----------------------------------------------------------

    // Blocks until a new bar is pushed or shutdown() is called.
    // Returns an empty Bar (datetime == "") on shutdown.
    Core::Bar get_latest_bar(
        const std::string& symbol,
        const std::string& interval) override;

    // LTP = close of most recently delivered bar.
    double get_ltp(const std::string& symbol) override;

    // Returns last N delivered bars (from internal ring buffer).
    std::vector<Core::Bar> get_historical_bars(
        const std::string& symbol,
        const std::string& interval,
        int count) override;

    // ----------------------------------------------------------
    // BrokerInterface — portfolio pre-filter
    // ----------------------------------------------------------

    // Called by LiveEngine BEFORE check_for_entries().
    // Delegates to PortfolioGuard via entry_allowed_ callback.
    bool is_entry_allowed() override;

    // ----------------------------------------------------------
    // BrokerInterface — connection lifecycle
    // ----------------------------------------------------------
    bool connect()              override;
    void disconnect()           override;
    bool is_connected() const   override;

    // ----------------------------------------------------------
    // BrokerInterface — order placement
    // ----------------------------------------------------------
    OrderResponse place_market_order(
        const std::string& symbol,
        const std::string& direction,
        int                quantity) override;

    OrderResponse place_limit_order(
        const std::string& symbol,
        const std::string& direction,
        int                quantity,
        double             price) override;

    OrderResponse place_stop_order(
        const std::string& symbol,
        const std::string& direction,
        int                quantity,
        double             stop_price) override;

    bool cancel_order(const std::string& order_id) override;

    // ----------------------------------------------------------
    // BrokerInterface — account / position
    // ----------------------------------------------------------
    std::vector<Position> get_positions()              override;
    double                get_account_balance()        override;
    bool                  has_position(const std::string& symbol) override;

    // ----------------------------------------------------------
    // Diagnostics
    // ----------------------------------------------------------
    int  bars_pushed()    const { return bars_pushed_.load(); }
    int  bars_consumed()  const { return bars_consumed_.load(); }
    int  orders_sent()    const { return orders_sent_.load(); }
    int  orders_failed()  const { return orders_failed_.load(); }

private:
    // ── Identity ─────────────────────────────────────────────
    std::string         symbol_;

    // ── Bar queue (push → pull bridge) ───────────────────────
    std::queue<Core::Bar>       bar_queue_;
    mutable std::mutex          queue_mutex_;
    std::condition_variable     bar_cv_;
    std::atomic<bool>           shutdown_   { false };
    Core::Bar                   latest_bar_;          // last delivered bar

    // Ring buffer of recent bars for get_historical_bars()
    static constexpr int        HISTORY_CAPACITY = 500;
    std::vector<Core::Bar>      bar_history_;
    mutable std::mutex          history_mutex_;

    // ── Order submission ─────────────────────────────────────
    OrderSubmitter              order_submitter_;

    // ── PortfolioGuard callbacks ─────────────────────────────
    std::function<double()>          get_balance_;
    std::function<bool()>            entry_allowed_;
    std::function<void(int, double)> on_order_ok_;

    // ── State ────────────────────────────────────────────────
    std::atomic<bool>   connected_      { false };
    bool                has_position_   { false };
    mutable std::mutex  position_mutex_;

    // ── Counters ─────────────────────────────────────────────
    std::atomic<int>    bars_pushed_    { 0 };
    std::atomic<int>    bars_consumed_  { 0 };
    std::atomic<int>    orders_sent_    { 0 };
    std::atomic<int>    orders_failed_  { 0 };

    // ── Helpers ──────────────────────────────────────────────
    OrderResponse map_result_to_response(
        const OrderSubmitResult& result,
        const std::string&       direction,
        int                      quantity,
        double                   price);
};

} // namespace Live
} // namespace SDTrading

#endif // SDTRADING_PIPE_BROKER_H
